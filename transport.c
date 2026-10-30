#include "transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// 循环发送，直到所有字节发送完成
static ChatStatus send_all(
    int fd,
    const void *buffer,
    size_t length
) {
    const char *ptr = buffer;
    size_t total = 0;

    while (total < length) {
        ssize_t n = send(
            fd,
            ptr + total,
            length - total,
            MSG_NOSIGNAL
        );

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return CHAT_ERR;
        }

        if (n == 0) {
            return CHAT_ERR;
        }

        total += (size_t)n;
    }

    return CHAT_OK;
}

// 循环接收，直到读取指定字节数
static ChatStatus recv_all(
    int fd,
    void *buffer,
    size_t length
) {
    char *ptr = buffer;
    size_t total = 0;

    while (total < length) {
        ssize_t n = recv(
            fd,
            ptr + total,
            length - total,
            0
        );

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return CHAT_ERR;
        }

        if (n == 0) {
            return CHAT_CLOSED;
        }

        total += (size_t)n;
    }

    return CHAT_OK;
}

// 发送消息帧：4字节长度 + 消息内容
ChatStatus send_frame(
    int fd,
    const void *data,
    uint32_t length
) {
    if (length > CHAT_MAX_FRAME) {
        return CHAT_INVALID;
    }

    if (length > 0 && data == NULL) {
        return CHAT_ERR;
    }

    uint32_t network_length = htonl(length);

    ChatStatus status = send_all(
        fd,
        &network_length,
        sizeof(network_length)
    );

    if (status != CHAT_OK) {
        return status;
    }

    return send_all(fd, data, length);
}

// 接收消息帧
ChatStatus recv_frame(
    int fd,
    char **data,
    uint32_t *length
) {
    if (data == NULL || length == NULL) {
        return CHAT_ERR;
    }

    *data = NULL;
    *length = 0;

    uint32_t network_length;

    ChatStatus status = recv_all(
        fd,
        &network_length,
        sizeof(network_length)
    );

    if (status != CHAT_OK) {
        return status;
    }

    uint32_t message_length = ntohl(network_length);

    if (message_length > CHAT_MAX_FRAME) {
        return CHAT_INVALID;
    }

    char *buffer = malloc((size_t)message_length + 1);

    if (buffer == NULL) {
        return CHAT_ERR;
    }

    status = recv_all(fd, buffer, message_length);

    if (status != CHAT_OK) {
        free(buffer);
        return status;
    }

    buffer[message_length] = '\0';

    *data = buffer;
    *length = message_length;

    return CHAT_OK;
}
