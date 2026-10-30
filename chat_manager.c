#define _POSIX_C_SOURCE 200809L
#include "chat_manager.h"

#include "protocol.h"
#include "transport.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static ClientSession *g_clients[CHAT_MAX_CLIENTS];  // 在线客户端会话数组
static size_t g_client_count = 0;                    // 当前在线客户端数量
static pthread_mutex_t g_clients_lock;               // 保护客户端列表的互斥锁
static pthread_cond_t g_refs_changed;                // 会话引用计数变更条件变量
static int g_initialized = 0;                        // 管理器初始化标记

// 校验用户名：仅允许字母、数字、下划线
static int valid_username(const char *name)
{
    size_t i, len;

    if (name == NULL) return 0;
    len = strnlen(name, CHAT_NAME_SIZE);
    if (len == 0 || len >= CHAT_NAME_SIZE) return 0;

    /* 保证用户名格式可控，避免显示与控制字符带来的问题 */
    for (i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '_')) return 0;
    }
    return 1;
}

/* 以下辅助函数必须在持有 g_clients_lock 锁时调用 */
// 增加会话引用计数
static void retain_locked(ClientSession *session)
{
    if (session != NULL) ++session->refs;
}

// 减少会话额外引用计数，引用大于1才递减并发送条件广播
static void release_extra_locked(ClientSession *session)
{
    if (session != NULL && session->refs > 1) {
        --session->refs;
        pthread_cond_broadcast(&g_refs_changed);
    }
}

// 在客户端数组查找指定会话，返回下标，找不到返回-1
static int find_session_locked(ClientSession *session)
{
    size_t i;
    for (i = 0; i < g_client_count; ++i) {
        if (g_clients[i] == session) return (int)i;
    }
    return -1;
}

// 根据用户名查找客户端，返回下标，找不到返回-1
static int find_name_locked(const char *name)
{
    size_t i;
    for (i = 0; i < g_client_count; ++i) {
        if (strcmp(g_clients[i]->username, name) == 0) return (int)i;
    }
    return -1;
}

// 快速发送一条服务端简单消息（通知、错误提示）
static ChatStatus send_simple(ClientSession *session, MessageType type,
                              const char *content)
{
    ChatMessage out;
    memset(&out, 0, sizeof(out));
    out.type = type;
    if (content != NULL) {
        (void)snprintf(out.content, sizeof(out.content), "%s", content);
    }
    (void)snprintf(out.sender, sizeof(out.sender), "SERVER");
    return chat_send(session, &out);
}

// 释放客户端快照里所有会话的额外引用
static void release_snapshot(ClientSession **snapshot, size_t count)
{
    size_t i;

    if (!g_initialized) return;
    pthread_mutex_lock(&g_clients_lock);
    for (i = 0; i < count; ++i) release_extra_locked(snapshot[i]);
    pthread_mutex_unlock(&g_clients_lock);
}

// 拷贝客户端快照（排除指定会话），给每个会话增加引用
static size_t snapshot_clients(ClientSession **snapshot, size_t capacity,
                               ClientSession *exclude)
{
    size_t i, n = 0;

    pthread_mutex_lock(&g_clients_lock);
    for (i = 0; i < g_client_count && n < capacity; ++i) {
        if (g_clients[i] == exclude) continue;
        retain_locked(g_clients[i]);
        snapshot[n++] = g_clients[i];
    }
    pthread_mutex_unlock(&g_clients_lock);
    return n;
}

// 向快照内所有客户端转发消息，之后释放快照引用
static void notify_snapshot(ClientSession **snapshot, size_t count,
                            const ChatMessage *msg)
{
    size_t i;
    for (i = 0; i < count; ++i) (void)chat_send(snapshot[i], msg);
    release_snapshot(snapshot, count);
}

// 构造服务端通知消息
static void make_notice(ChatMessage *msg, const char *content)
{
    memset(msg, 0, sizeof(*msg));
    msg->type = MSG_NOTICE;
    (void)snprintf(msg->sender, sizeof(msg->sender), "SERVER");
    (void)snprintf(msg->content, sizeof(msg->content), "%s", content);
}

// 初始化聊天管理器，创建互斥锁和条件变量
ChatStatus chat_manager_init(void)
{
    int rc;

    if (g_initialized) return CHAT_OK;

    memset(g_clients, 0, sizeof(g_clients));
    g_client_count = 0;

    rc = pthread_mutex_init(&g_clients_lock, NULL);
    if (rc != 0) return CHAT_ERR;

    rc = pthread_cond_init(&g_refs_changed, NULL);
    if (rc != 0) {
        pthread_mutex_destroy(&g_clients_lock);
        return CHAT_ERR;
    }

    g_initialized = 1;
    return CHAT_OK;
}

// 增加会话引用计数，外部接口
void chat_session_retain(ClientSession *session)
{
    if (session == NULL || !g_initialized) return;
    pthread_mutex_lock(&g_clients_lock);
    ++session->refs;
    pthread_mutex_unlock(&g_clients_lock);
}

// 释放会话引用，引用归0时关闭fd、销毁会话
void chat_session_release(ClientSession *session)
{
    int should_free = 0;

    if (session == NULL) return;

    if (g_initialized) {
        pthread_mutex_lock(&g_clients_lock);
        if (session->refs > 0) --session->refs;
        if (session->refs == 0) {
            session->closing = 1;
            should_free = 1;
        }
        pthread_cond_broadcast(&g_refs_changed);
        pthread_mutex_unlock(&g_clients_lock);
    } else {
        /* 防御性兜底；正常流程会先初始化管理器 */
        should_free = 1;
    }

    if (should_free) {
        /* 没有其他引用，不会再有发送方使用该文件描述符 */
        pthread_mutex_lock(&session->send_lock);
        if (session->fd >= 0) {
            (void)shutdown(session->fd, SHUT_RDWR);
            (void)close(session->fd);
            session->fd = -1;
        }
        pthread_mutex_unlock(&session->send_lock);
        pthread_mutex_destroy(&session->send_lock);
        free(session);
    }
}

// 编码并发送一条聊天消息
ChatStatus chat_send(ClientSession *session, const ChatMessage *msg)
{
    char *json = NULL;
    size_t len;
    ChatStatus status;
    int closing;

    if (session == NULL || msg == NULL || !g_initialized) return CHAT_INVALID;

    status = encode_message(msg, &json);
    if (status != CHAT_OK) return status;

    len = strlen(json);
    if (len == 0 || len > CHAT_MAX_FRAME) {
        free(json);
        return CHAT_INVALID;
    }

    /* 加锁顺序：先 send_lock，后 clients_lock。不存在持有 clients_lock
       等待 send_lock 的代码，因此不会形成死锁环路 */
    pthread_mutex_lock(&session->send_lock);
    pthread_mutex_lock(&g_clients_lock);
    closing = session->closing;
    pthread_mutex_unlock(&g_clients_lock);

    if (closing || session->fd < 0) {
        status = CHAT_CLOSED;
    } else {
        status = send_frame(session->fd, json, (uint32_t)len);
    }

    pthread_mutex_unlock(&session->send_lock);
    free(json);
    return status;
}

// 客户端注册登录，加入聊天室，广播上线通知
ChatStatus chat_register(ClientSession *session, const char *username)
{
    size_t i, n = 0;
    ClientSession *snapshot[CHAT_MAX_CLIENTS];
    ChatMessage notice;
    char text[CHAT_CONTENT_SIZE];

    if (!g_initialized || session == NULL || !valid_username(username)) {
        if (session != NULL && g_initialized)
            (void)send_simple(session, MSG_ERROR,
                              "Invalid nickname: use 1-31 letters, digits or underscores.");
        return CHAT_INVALID;
    }

    pthread_mutex_lock(&g_clients_lock);

    /* 支持幂等注册，适配通过 chat_process 分发登录的处理逻辑 */
    if (find_session_locked(session) >= 0) {
        int same = strcmp(session->username, username) == 0;
        pthread_mutex_unlock(&g_clients_lock);
        return same ? CHAT_OK : CHAT_INVALID;
    }

    if (session->closing) {
        pthread_mutex_unlock(&g_clients_lock);
        return CHAT_CLOSED;
    }

    if (find_name_locked(username) >= 0) {
        pthread_mutex_unlock(&g_clients_lock);
        (void)send_simple(session, MSG_ERROR, "Nickname is already in use.");
        return CHAT_DUPLICATE;
    }

    if (g_client_count >= CHAT_MAX_CLIENTS) {
        pthread_mutex_unlock(&g_clients_lock);
        (void)send_simple(session, MSG_ERROR, "Chat room is full.");
        return CHAT_FULL;
    }

    (void)snprintf(session->username, sizeof(session->username), "%s", username);
    g_clients[g_client_count++] = session;
    retain_locked(session); /* 注册表持有的引用 */

    for (i = 0; i < g_client_count && n < CHAT_MAX_CLIENTS; ++i) {
        retain_locked(g_clients[i]);
        snapshot[n++] = g_clients[i];
    }
    pthread_mutex_unlock(&g_clients_lock);

    (void)snprintf(text, sizeof(text), "%s joined the chat.", username);
    make_notice(&notice, text);
    notify_snapshot(snapshot, n, &notice);
    return CHAT_OK;
}

// 向请求方发送在线用户列表，超长自动分片发送
static ChatStatus send_online_list(ClientSession *session)
{
    char names[CHAT_MAX_CLIENTS][CHAT_NAME_SIZE];
    size_t count, i;
    char chunk[CHAT_CONTENT_SIZE];
    size_t used = 0;

    pthread_mutex_lock(&g_clients_lock);
    if (find_session_locked(session) < 0 || session->closing) {
        pthread_mutex_unlock(&g_clients_lock);
        return send_simple(session, MSG_ERROR, "Please log in first.");
    }

    count = g_client_count;
    for (i = 0; i < count; ++i) {
        (void)snprintf(names[i], sizeof(names[i]), "%s", g_clients[i]->username);
    }
    pthread_mutex_unlock(&g_clients_lock);

    if (count == 0) {
        return send_simple(session, MSG_ONLINE_LIST, "(no users online)");
    }

    chunk[0] = '\0';
    for (i = 0; i < count; ++i) {
        size_t need = strlen(names[i]) + (used ? 2U : 0U);

        if (used > 0 && used + need >= sizeof(chunk)) {
            ChatMessage out;
            memset(&out, 0, sizeof(out));
            out.type = MSG_ONLINE_LIST;
            (void)snprintf(out.sender, sizeof(out.sender), "SERVER");
            (void)snprintf(out.content, sizeof(out.content), "%s", chunk);
            if (chat_send(session, &out) != CHAT_OK) return CHAT_ERR;
            used = 0;
            chunk[0] = '\0';
            need = strlen(names[i]);
        }

        if (used > 0) {
            chunk[used++] = ',';
            chunk[used++] = ' ';
            chunk[used] = '\0';
        }
        (void)snprintf(chunk + used, sizeof(chunk) - used, "%s", names[i]);
        used += strlen(names[i]);
    }

    {
        ChatMessage out;
        memset(&out, 0, sizeof(out));
        out.type = MSG_ONLINE_LIST;
        (void)snprintf(out.sender, sizeof(out.sender), "SERVER");
        (void)snprintf(out.content, sizeof(out.content), "%s", chunk);
        return chat_send(session, &out);
    }
}

// 处理群聊消息，广播给所有在线用户
static ChatStatus group_message(ClientSession *sender, const ChatMessage *msg)
{
    ClientSession *snapshot[CHAT_MAX_CLIENTS];
    size_t n, i;
    ChatMessage out;

    memset(&out, 0, sizeof(out));
    out.type = MSG_GROUP;
    (void)snprintf(out.sender, sizeof(out.sender), "%s", sender->username);
    (void)snprintf(out.content, sizeof(out.content), "%s", msg->content);

    n = snapshot_clients(snapshot, CHAT_MAX_CLIENTS, NULL);
    for (i = 0; i < n; ++i) (void)chat_send(snapshot[i], &out);
    release_snapshot(snapshot, n);
    return CHAT_OK;
}

// 处理私聊消息，发给目标并回显给发送者
static ChatStatus private_message(ClientSession *sender, const ChatMessage *msg)
{
    ClientSession *target = NULL;
    int idx;
    ChatMessage out;
    ChatStatus status;

    if (msg->target[0] == '\0') {
        return send_simple(sender, MSG_ERROR, "Private message requires a target nickname.");
    }

    pthread_mutex_lock(&g_clients_lock);
    if (find_session_locked(sender) < 0 || sender->closing) {
        pthread_mutex_unlock(&g_clients_lock);
        return send_simple(sender, MSG_ERROR, "Please log in first.");
    }

    idx = find_name_locked(msg->target);
    if (idx >= 0) {
        target = g_clients[idx];
        if (target != sender) retain_locked(target);
    }
    pthread_mutex_unlock(&g_clients_lock);

    if (target == NULL) {
        return send_simple(sender, MSG_ERROR, "Private-message target is not online.");
    }

    memset(&out, 0, sizeof(out));
    out.type = MSG_PRIVATE;
    (void)snprintf(out.sender, sizeof(out.sender), "%s", sender->username);
    (void)snprintf(out.target, sizeof(out.target), "%s", target->username);
    (void)snprintf(out.content, sizeof(out.content), "%s", msg->content);

    status = chat_send(target, &out);
    if (target != sender) {
        (void)chat_send(sender, &out); /* 回显给发送方 */
        pthread_mutex_lock(&g_clients_lock);
        release_extra_locked(target);
        pthread_mutex_unlock(&g_clients_lock);
    }
    return status;
}

// 统一消息分发入口，根据消息类型路由处理
ChatStatus chat_process(ClientSession *session, const ChatMessage *msg)
{
    int logged_in;

    if (!g_initialized || session == NULL || msg == NULL) return CHAT_INVALID;

    if (msg->type == MSG_LOGIN) {
        /* 登录用户名放在content字段，sender字段不可信 */
        return chat_register(session, msg->content);
    }

    pthread_mutex_lock(&g_clients_lock);
    logged_in = (find_session_locked(session) >= 0 && !session->closing);
    pthread_mutex_unlock(&g_clients_lock);

    if (!logged_in) return send_simple(session, MSG_ERROR, "Please log in first.");

    switch (msg->type) {
        case MSG_GROUP:
            if (msg->content[0] == '\0') {
                return send_simple(session, MSG_ERROR, "Group message cannot be empty.");
            }
            return group_message(session, msg);

        case MSG_PRIVATE:
            if (msg->content[0] == '\0') {
                return send_simple(session, MSG_ERROR, "Private message cannot be empty.");
            }
            return private_message(session, msg);

        case MSG_ONLINE_LIST:
            return send_online_list(session);

        case MSG_LOGOUT:
            chat_unregister(session);
            return CHAT_CLOSED;

        case MSG_NOTICE:
        case MSG_ERROR:
        default:
            return send_simple(session, MSG_ERROR, "Unsupported client message type.");
    }
}

// 客户端注销下线，从列表移除，广播下线通知，等待引用释放
void chat_unregister(ClientSession *session)
{
    ClientSession *snapshot[CHAT_MAX_CLIENTS];
    size_t n = 0, i;
    int idx;
    char username[CHAT_NAME_SIZE] = "";
    ChatMessage notice;
    char text[CHAT_CONTENT_SIZE];

    if (session == NULL || !g_initialized) return;

    pthread_mutex_lock(&g_clients_lock);
    idx = find_session_locked(session);
    session->closing = 1;

    if (idx >= 0) {
        (void)snprintf(username, sizeof(username), "%s", session->username);

        for (i = (size_t)idx; i + 1 < g_client_count; ++i) {
            g_clients[i] = g_clients[i + 1];
        }
        --g_client_count;
        g_clients[g_client_count] = NULL;
        release_extra_locked(session); /* 移除注册表的引用 */

        for (i = 0; i < g_client_count && n < CHAT_MAX_CLIENTS; ++i) {
            retain_locked(g_clients[i]);
            snapshot[n++] = g_clients[i];
        }
    }
    pthread_mutex_unlock(&g_clients_lock);

    if (idx >= 0) {
        (void)snprintf(text, sizeof(text), "%s left the chat.", username);
        make_notice(&notice, text);
        notify_snapshot(snapshot, n, &notice);
    }

    /* 等待所有广播、私聊快照释放该会话。
       工作线程自身仍持有一份引用（refs == 1） */
    pthread_mutex_lock(&g_clients_lock);
    while (session->refs > 1) {
        pthread_cond_wait(&g_refs_changed, &g_clients_lock);
    }
    pthread_mutex_unlock(&g_clients_lock);
}

// 销毁管理器，释放锁与条件变量（无在线客户端时才销毁）
void chat_manager_destroy(void)
{
    if (!g_initialized) return;

    pthread_mutex_lock(&g_clients_lock);
    if (g_client_count != 0) {
        /* 仍存在会话时，不能销毁同步原语 */
        pthread_mutex_unlock(&g_clients_lock);
        return;
    }
    pthread_mutex_unlock(&g_clients_lock);

    pthread_cond_destroy(&g_refs_changed);
    pthread_mutex_destroy(&g_clients_lock);
    g_initialized = 0;
}
