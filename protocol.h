#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H

#include "common.h"

/* 输出参数 json 会分配内存，调用方需要使用 free() 释放 */
ChatStatus encode_message(const ChatMessage *msg, char **json);

/* 解析一条以 '\0' 结尾的 JSON 帧，填充到有长度限制的 ChatMessage 结构体 */
ChatStatus decode_message(const char *json, ChatMessage *msg);

#endif
