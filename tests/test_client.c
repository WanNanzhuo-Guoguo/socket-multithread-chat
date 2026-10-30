#include "../transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);

    if (inet_pton(AF_INET, "127.0.0.1",
                  &addr.sin_addr) != 1) {
        fprintf(stderr, "IP 地址错误\n");
        close(fd);
        return 1;
    }

    if (connect(fd, (struct sockaddr *)&addr,
                sizeof(addr)) == -1) {
        perror("connect");
        close(fd);
        return 1;
    }

    printf("已连接服务器，输入消息，quit 退出\n");

    char input[1024];

    while (fgets(input, sizeof(input), stdin)) {
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "quit") == 0) {
            break;
        }

        ChatStatus status = send_frame(
            fd, input, (uint32_t)strlen(input)
        );

        if (status != CHAT_OK) {
            printf("发送失败\n");
            break;
        }

        char *reply = NULL;
        uint32_t length = 0;

        status = recv_frame(fd, &reply, &length);

        if (status != CHAT_OK) {
            printf("服务器已断开或接收失败\n");
            free(reply);
            break;
        }

        printf("服务器回复：%s\n", reply);
        free(reply);
    }

    close(fd);
    return 0;
}
