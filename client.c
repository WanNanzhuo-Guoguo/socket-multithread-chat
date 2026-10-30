#define _POSIX_C_SOURCE 200809L
#include "common.h"
#include "protocol.h"
#include "transport.h"

#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int g_fd = -1;
static int g_running = 1;
static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;

// 判断客户端是否正在运行
static int is_running(void)
{
    int value;
    pthread_mutex_lock(&g_state_lock);
    value = g_running;
    pthread_mutex_unlock(&g_state_lock);
    return value;
}

// 设置停止标记
static void set_stopped(void)
{
    pthread_mutex_lock(&g_state_lock);
    g_running = 0;
    pthread_mutex_unlock(&g_state_lock);
}

// 根据消息类型返回对应的中文名称
static const char *message_type_name(MessageType type)
{
    switch (type) {
        case MSG_GROUP: return "群聊";
        case MSG_PRIVATE: return "私聊";
        case MSG_ONLINE_LIST: return "在线用户";
        case MSG_NOTICE: return "系统通知";
        case MSG_ERROR: return "错误";
        default: return "消息";
    }
}

// 格式化打印收到的消息
static void print_message(const ChatMessage *msg)
{
    if (msg->type == MSG_PRIVATE) {
        printf("\n[私聊] %s -> %s: %s\n",
               msg->sender, msg->target, msg->content);
    } else if (msg->type == MSG_GROUP) {
        printf("\n[群聊] %s: %s\n", msg->sender, msg->content);
    } else {
        printf("\n[%s] %s\n", message_type_name(msg->type), msg->content);
    }
    printf("> ");
    fflush(stdout);
}

// 接收线程：持续接收服务端推送的消息
static void *receiver_thread(void *arg)
{
    (void)arg;

    while (is_running()) {
        char *payload = NULL;
        uint32_t length = 0;
        ChatMessage msg;
        ChatStatus status = recv_frame(g_fd, &payload, &length);

        if (status != CHAT_OK) {
            free(payload);
            if (is_running()) {
                fprintf(stderr, "\n与服务器的连接已经关闭。\n");
                set_stopped();
            }
            break;
        }

        if (payload == NULL || length > CHAT_MAX_FRAME) {
            free(payload);
            fprintf(stderr, "\n收到无效消息帧。\n");
            set_stopped();
            break;
        }
        payload[length] = '\0';

        status = decode_message(payload, &msg);
        free(payload);
        if (status != CHAT_OK) {
            fprintf(stderr, "\n收到无法解析的服务端消息。\n");
            continue;
        }

        print_message(&msg);
    }

    return NULL;
}

// 连接聊天服务器，返回socket文件描述符
static int connect_server(const char *host, const char *port)
{
    struct addrinfo hints, *result = NULL, *it;
    int fd = -1;
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rc = getaddrinfo(host, port, &hints, &result);
    if (rc != 0) {
        fprintf(stderr, "地址解析失败：%s\n", gai_strerror(rc));
        return -1;
    }

    for (it = result; it != NULL; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;

        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    if (fd < 0) perror("connect");
    return fd;
}

// 编码并发送一条聊天消息
static ChatStatus send_chat_message(const ChatMessage *msg)
{
    char *json = NULL;
    ChatStatus status;
    size_t len;

    status = encode_message(msg, &json);
    if (status != CHAT_OK) return status;

    len = strlen(json);
    status = send_frame(g_fd, json, (uint32_t)len);
    free(json);
    return status;
}

// 将内容拷贝到消息结构体，检查长度是否超限
static int set_content(ChatMessage *msg, const char *content)
{
    size_t len;

    if (content == NULL) content = "";
    len = strlen(content);
    if (len >= sizeof(msg->content)) {
        fprintf(stderr, "消息过长，正文最大 %d 字节。\n", CHAT_CONTENT_SIZE - 1);
        return 0;
    }
    memcpy(msg->content, content, len + 1);
    return 1;
}

// 打印帮助命令提示
static void print_help(void)
{
    puts("命令：");
    puts("  直接输入文字       发送群聊消息");
    puts("  /w 用户名 内容     向指定用户发送私聊");
    puts("  /list              查询在线用户");
    puts("  /help              显示帮助");
    puts("  /quit              退出聊天室");
}

int main(int argc, char **argv)
{
    const char *host = (argc >= 2) ? argv[1] : "127.0.0.1";
    const char *port = (argc >= 3) ? argv[2] : "8888";
    char username[CHAT_NAME_SIZE];
    pthread_t receiver;
    ChatMessage login;
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    int receiver_started = 0;

    if (argc > 3) {
        fprintf(stderr, "用法：%s [host] [port]\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("服务器地址 %s:%s\n请输入昵称（1-31 位英文、数字或下划线）：", host, port);
    fflush(stdout);
    if (fgets(username, sizeof(username), stdin) == NULL) return EXIT_FAILURE;
    username[strcspn(username, "\r\n")] = '\0';

    if (username[0] == '\0' || strnlen(username, sizeof(username)) >= sizeof(username)) {
        fprintf(stderr, "昵称无效。\n");
        return EXIT_FAILURE;
    }

    g_fd = connect_server(host, port);
    if (g_fd < 0) return EXIT_FAILURE;

    memset(&login, 0, sizeof(login));
    login.type = MSG_LOGIN;
    if (!set_content(&login, username) || send_chat_message(&login) != CHAT_OK) {
        fprintf(stderr, "发送登录请求失败。\n");
        close(g_fd);
        return EXIT_FAILURE;
    }

    if (pthread_create(&receiver, NULL, receiver_thread, NULL) != 0) {
        perror("pthread_create");
        close(g_fd);
        return EXIT_FAILURE;
    }
    receiver_started = 1;

    print_help();
    puts("已连接。输入消息后按回车发送。");
    printf("> ");
    fflush(stdout);

    while (is_running() && (line_len = getline(&line, &line_cap, stdin)) >= 0) {
        ChatMessage msg;
        char *content;

        while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
            line[--line_len] = '\0';
        }
        if (line_len == 0) {
            printf("> ");
            fflush(stdout);
            continue;
        }

        memset(&msg, 0, sizeof(msg));

        if (strcmp(line, "/quit") == 0) {
            msg.type = MSG_LOGOUT;
            (void)send_chat_message(&msg);
            break;
        } else if (strcmp(line, "/list") == 0) {
            msg.type = MSG_ONLINE_LIST;
        } else if (strcmp(line, "/help") == 0) {
            print_help();
            printf("> ");
            fflush(stdout);
            continue;
        } else if (strncmp(line, "/w ", 3) == 0) {
            char *target = line + 3;
            char *space = strchr(target, ' ');
            if (space == NULL || space == target || space[1] == '\0') {
                puts("格式：/w 用户名 内容");
                printf("> ");
                fflush(stdout);
                continue;
            }
            *space = '\0';
            if (strlen(target) >= sizeof(msg.target)) {
                puts("目标昵称过长。");
                printf("> ");
                fflush(stdout);
                continue;
            }
            (void)snprintf(msg.target, sizeof(msg.target), "%s", target);
            content = space + 1;
            msg.type = MSG_PRIVATE;
            if (!set_content(&msg, content)) {
                printf("> ");
                fflush(stdout);
                continue;
            }
        } else {
            msg.type = MSG_GROUP;
            if (!set_content(&msg, line)) {
                printf("> ");
                fflush(stdout);
                continue;
            }
        }

        if (send_chat_message(&msg) != CHAT_OK) {
            fprintf(stderr, "发送失败，连接可能已经断开。\n");
            set_stopped();
            break;
        }

        printf("> ");
        fflush(stdout);
    }

    free(line);
    set_stopped();
    if (g_fd >= 0) (void)shutdown(g_fd, SHUT_RDWR);
    if (receiver_started) (void)pthread_join(receiver, NULL);
    if (g_fd >= 0) close(g_fd);
    pthread_mutex_destroy(&g_state_lock);
    return EXIT_SUCCESS;
}
