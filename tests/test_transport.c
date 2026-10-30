#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>

#include "../transport.h"

// 测试1：发送并接收一条消息
static int test_single_message(void) {
    int fds[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        perror("socketpair");
        return 0;
    }

    const char *message = "Hello Transport!";

    ChatStatus status = send_frame(
        fds[0], message, (uint32_t)strlen(message)
    );

    if (status != CHAT_OK) {
        printf("[FAIL] 单条消息发送失败\n");
        close(fds[0]);
        close(fds[1]);
        return 0;
    }

    char *received = NULL;
    uint32_t length = 0;

    status = recv_frame(fds[1], &received, &length);

    int passed = status == CHAT_OK &&
                 length == strlen(message) &&
                 strcmp(received, message) == 0;

    printf("[%s] 单条消息收发\n",
           passed ? "PASS" : "FAIL");

    free(received);
    close(fds[0]);
    close(fds[1]);

    return passed;
}

// 测试2：连续发送多条消息
static int test_multiple_messages(void) {
    int fds[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        perror("socketpair");
        return 0;
    }

    const char *messages[] = {
        "First message",
        "Second message",
        "Third message"
    };

    int passed = 1;

    for (int i = 0; i < 3; i++) {
        if (send_frame(
                fds[0],
                messages[i],
                (uint32_t)strlen(messages[i])
            ) != CHAT_OK) {
            passed = 0;
            break;
        }
    }

    if (passed) {
        for (int i = 0; i < 3; i++) {
            char *received = NULL;
            uint32_t length = 0;

            ChatStatus status = recv_frame(
                fds[1], &received, &length
            );

            if (status != CHAT_OK ||
                length != strlen(messages[i]) ||
                received == NULL ||
                strcmp(received, messages[i]) != 0) {
                passed = 0;
            }

            free(received);

            if (!passed) {
                break;
            }
        }
    }

    printf("[%s] 连续多条消息收发\n",
           passed ? "PASS" : "FAIL");

    close(fds[0]);
    close(fds[1]);

    return passed;
}

// 测试3：空消息
static int test_empty_message(void) {
    int fds[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        perror("socketpair");
        return 0;
    }

    ChatStatus status = send_frame(fds[0], NULL, 0);

    char *received = NULL;
    uint32_t length = 0;

    if (status == CHAT_OK) {
        status = recv_frame(fds[1], &received, &length);
    }

    int passed = status == CHAT_OK &&
                 length == 0 &&
                 received != NULL &&
                 received[0] == '\0';

    printf("[%s] 空消息收发\n",
           passed ? "PASS" : "FAIL");

    free(received);
    close(fds[0]);
    close(fds[1]);

    return passed;
}

// 测试4：超长消息
static int test_oversized_message(void) {
    int fds[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        perror("socketpair");
        return 0;
    }

    char data[CHAT_MAX_FRAME + 1];
    memset(data, 'A', sizeof(data));

    ChatStatus status = send_frame(
        fds[0], data, sizeof(data)
    );

    int passed = status == CHAT_INVALID;

    printf("[%s] 超长消息拒绝\n",
           passed ? "PASS" : "FAIL");

    close(fds[0]);
    close(fds[1]);

    return passed;
}

// 测试5：连接关闭
static int test_disconnection(void) {
    int fds[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        perror("socketpair");
        return 0;
    }

    close(fds[0]);

    char *received = NULL;
    uint32_t length = 0;

    ChatStatus status = recv_frame(
        fds[1], &received, &length
    );

    int passed = status == CHAT_CLOSED;

    printf("[%s] 连接关闭检测\n",
           passed ? "PASS" : "FAIL");

    free(received);
    close(fds[1]);

    return passed;
}

int main(void) {
    printf("================================\n");
    printf(" TCP 消息传输模块测试\n");
    printf("================================\n");

    int passed = 0;
    int total = 5;

    passed += test_single_message();
    passed += test_multiple_messages();
    passed += test_empty_message();
    passed += test_oversized_message();
    passed += test_disconnection();

    printf("================================\n");
    printf("测试结果：%d/%d 通过\n", passed, total);
    printf("================================\n");

    return passed == total ? 0 : 1;
}
