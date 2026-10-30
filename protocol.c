#define _POSIX_C_SOURCE 200809L
#include "protocol.h"
#include <cjson/cJSON.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// 将消息类型枚举转为对应的JSON字符串标识
static const char *type_to_string(MessageType type)
{
    switch (type) {
        case MSG_LOGIN:      return "login";
        case MSG_GROUP:      return "group";
        case MSG_PRIVATE:    return "private";
        case MSG_ONLINE_LIST:return "online_list";
        case MSG_LOGOUT:     return "logout";
        case MSG_NOTICE:     return "notice";
        case MSG_ERROR:      return "error";
        default:             return NULL;
    }
}

// 将JSON字符串标识解析为消息类型枚举，解析成功返回1，失败返回0
static int string_to_type(const char *s, MessageType *type)
{
    if (s == NULL || type == NULL) return 0;

    if (strcmp(s, "login") == 0)       *type = MSG_LOGIN;
    else if (strcmp(s, "group") == 0)  *type = MSG_GROUP;
    else if (strcmp(s, "private") == 0)*type = MSG_PRIVATE;
    else if (strcmp(s, "online_list") == 0) *type = MSG_ONLINE_LIST;
    else if (strcmp(s, "logout") == 0) *type = MSG_LOGOUT;
    else if (strcmp(s, "notice") == 0) *type = MSG_NOTICE;
    else if (strcmp(s, "error") == 0)  *type = MSG_ERROR;
    else return 0;

    return 1;
}

/* 严格的UTF-8合法性校验，用于JSON输入和字符串字段编码检查 */
static int valid_utf8(const unsigned char *s, size_t n)
{
    size_t i = 0;

    while (i < n) {
        unsigned char c = s[i];

        if (c <= 0x7f) {
            ++i;
        } else if (c >= 0xc2 && c <= 0xdf) {
            if (i + 1 >= n || (s[i + 1] & 0xc0) != 0x80) return 0;
            i += 2;
        } else if (c == 0xe0) {
            if (i + 2 >= n || s[i + 1] < 0xa0 || s[i + 1] > 0xbf ||
                (s[i + 2] & 0xc0) != 0x80) return 0;
            i += 3;
        } else if ((c >= 0xe1 && c <= 0xec) || (c >= 0xee && c <= 0xef)) {
            if (i + 2 >= n || (s[i + 1] & 0xc0) != 0x80 ||
                (s[i + 2] & 0xc0) != 0x80) return 0;
            i += 3;
        } else if (c == 0xed) {
            if (i + 2 >= n || s[i + 1] < 0x80 || s[i + 1] > 0x9f ||
                (s[i + 2] & 0xc0) != 0x80) return 0;
            i += 3;
        } else if (c == 0xf0) {
            if (i + 3 >= n || s[i + 1] < 0x90 || s[i + 1] > 0xbf ||
                (s[i + 2] & 0xc0) != 0x80 ||
                (s[i + 3] & 0xc0) != 0x80) return 0;
            i += 4;
        } else if (c >= 0xf1 && c <= 0xf3) {
            if (i + 3 >= n || (s[i + 1] & 0xc0) != 0x80 ||
                (s[i + 2] & 0xc0) != 0x80 ||
                (s[i + 3] & 0xc0) != 0x80) return 0;
            i += 4;
        } else if (c == 0xf4) {
            if (i + 3 >= n || s[i + 1] < 0x80 || s[i + 1] > 0x8f ||
                (s[i + 2] & 0xc0) != 0x80 ||
                (s[i + 3] & 0xc0) != 0x80) return 0;
            i += 4;
        } else {
            return 0;
        }
    }

    return 1;
}

// 从JSON对象读取指定key的字符串，拷贝到目标缓冲区，可选字段不存在时直接返回成功
static ChatStatus copy_json_string(cJSON *root, const char *key,
                                   char *dst, size_t dst_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    size_t len;

    dst[0] = '\0';
    if (item == NULL) return CHAT_OK; /* 可选字段 */
    if (!cJSON_IsString(item) || item->valuestring == NULL) return CHAT_INVALID;

    len = strlen(item->valuestring);
    if (len >= dst_size) return CHAT_INVALID;
    if (!valid_utf8((const unsigned char *)item->valuestring, len)) return CHAT_INVALID;

    memcpy(dst, item->valuestring, len + 1);
    return CHAT_OK;
}

// 将ChatMessage消息结构体编码为JSON字符串
ChatStatus encode_message(const ChatMessage *msg, char **json)
{
    cJSON *root;
    char *printed;
    const char *type;
    size_t sender_len, target_len, content_len, json_len;

    if (json != NULL) *json = NULL;
    if (msg == NULL || json == NULL) return CHAT_INVALID;

    type = type_to_string(msg->type);
    if (type == NULL) return CHAT_INVALID;

    sender_len = strnlen(msg->sender, sizeof(msg->sender));
    target_len = strnlen(msg->target, sizeof(msg->target));
    content_len = strnlen(msg->content, sizeof(msg->content));

    if (sender_len >= sizeof(msg->sender) ||
        target_len >= sizeof(msg->target) ||
        content_len >= sizeof(msg->content)) return CHAT_INVALID;

    if (!valid_utf8((const unsigned char *)msg->sender, sender_len) ||
        !valid_utf8((const unsigned char *)msg->target, target_len) ||
        !valid_utf8((const unsigned char *)msg->content, content_len)) {
        return CHAT_INVALID;
    }

    root = cJSON_CreateObject();
    if (root == NULL) return CHAT_ERR;

    if (!cJSON_AddStringToObject(root, "type", type) ||
        !cJSON_AddStringToObject(root, "sender", msg->sender) ||
        !cJSON_AddStringToObject(root, "target", msg->target) ||
        !cJSON_AddStringToObject(root, "content", msg->content)) {
        cJSON_Delete(root);
        return CHAT_ERR;
    }

    printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (printed == NULL) return CHAT_ERR;

    json_len = strlen(printed);
    if (json_len == 0 || json_len > CHAT_MAX_FRAME) {
        free(printed);
        return CHAT_INVALID;
    }

    *json = printed;
    return CHAT_OK;
}

// 将JSON字符串解码，填充到ChatMessage消息结构体
ChatStatus decode_message(const char *json, ChatMessage *msg)
{
    cJSON *root;
    cJSON *type_item;
    MessageType type;
    size_t len;
    ChatStatus status;

    if (json == NULL || msg == NULL) return CHAT_INVALID;

    len = strnlen(json, CHAT_MAX_FRAME + 1U);
    if (len == 0 || len > CHAT_MAX_FRAME) return CHAT_INVALID;
    if (!valid_utf8((const unsigned char *)json, len)) return CHAT_INVALID;

    root = cJSON_Parse(json);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return CHAT_INVALID;
    }

    type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type_item) || type_item->valuestring == NULL ||
        !string_to_type(type_item->valuestring, &type)) {
        cJSON_Delete(root);
        return CHAT_INVALID;
    }

    memset(msg, 0, sizeof(*msg));
    msg->type = type;

    status = copy_json_string(root, "sender",
                          msg->sender, sizeof(msg->sender));

if (status == CHAT_OK) {
    status = copy_json_string(root, "target",
                              msg->target, sizeof(msg->target));
}

if (status == CHAT_OK) {
    status = copy_json_string(root, "content",
                              msg->content, sizeof(msg->content));
}

cJSON_Delete(root);
    if (status != CHAT_OK) return status;

    /* 不同消息类型对应的必填字段：登录消息在content中携带请求的昵称 */
    if (msg->type == MSG_LOGIN && msg->content[0] == '\0') return CHAT_INVALID;
    if (msg->type == MSG_GROUP && msg->content[0] == '\0') return CHAT_INVALID;
    if (msg->type == MSG_PRIVATE &&
        (msg->target[0] == '\0' || msg->content[0] == '\0')) return CHAT_INVALID;

    return CHAT_OK;
}
