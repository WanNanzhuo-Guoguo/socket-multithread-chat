#include "handler.h"
#include "common.h"
#include "transport.h"
#include "protocol.h"
#include "chat_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <string.h>

void *handle_client(void *arg)
{
    ClientSession *session = (ClientSession *)arg;
    pthread_t tid = pthread_self();

    printf("[线程 %lu] 客户端已连接，fd=%d\n",
           (unsigned long)tid, session->fd);

    while (1) {
        char *json = NULL;
        uint32_t length = 0;
        ChatMessage msg;

        // 接收完整的 TCP 消息帧
        ChatStatus status = recv_frame(
            session->fd, &json, &length
        );

        if (status == CHAT_CLOSED) {
            printf("[线程 %lu] 客户端断开连接\n",
                   (unsigned long)tid);
            free(json);
            break;
        }

        if (status != CHAT_OK) {
            printf("[线程 %lu] 接收失败，状态=%d\n",
                   (unsigned long)tid, status);
            free(json);
            break;
        }

        // 将 JSON 字符串解析为 ChatMessage
        status = decode_message(json, &msg);
        free(json);

        if (status != CHAT_OK) {
            printf("[线程 %lu] 收到非法 JSON 消息\n",
                   (unsigned long)tid);

            ChatMessage error_msg = {0};
            error_msg.type = MSG_ERROR;

            snprintf(error_msg.content,
                     sizeof(error_msg.content),
                     "Invalid JSON message.");

            if (chat_send(session, &error_msg) != CHAT_OK) {
                break;
            }

            continue;
        }

        printf("[线程 %lu] 消息类型=%d，发送者=%s\n",
               (unsigned long)tid,
               msg.type,
               msg.sender);

        // 调用第二位同学实现的聊天业务模块
        status = chat_process(session, &msg);

        if (status == CHAT_CLOSED) {
            printf("[线程 %lu] 客户端主动退出\n",
                   (unsigned long)tid);
            break;
        }

        if (status != CHAT_OK) {
            printf("[线程 %lu] 业务处理状态=%d\n",
                   (unsigned long)tid, status);

            // 非致命业务错误不一定需要断开连接
            if (status == CHAT_INVALID ||
                status == CHAT_DUPLICATE ||
                status == CHAT_FULL ||
                status == CHAT_NOT_FOUND) {
                continue;
            }

            break;
        }
    }

    // 注销客户端，等待其他发送引用释放
    chat_unregister(session);

    printf("[线程 %lu] 正在释放客户端会话\n",
           (unsigned long)tid);

    // 统一释放会话，最终关闭 Socket
    chat_session_release(session);

    printf("[线程 %lu] 工作线程结束\n",
           (unsigned long)tid);

    return NULL;
}

