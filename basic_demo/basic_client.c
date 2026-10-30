#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8888
#define BUFFER_SIZE 1024

int main(void) {
    int sock_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // 1. 创建 TCP Socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        return 1;
    }

    // 2. 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1",
                  &server_addr.sin_addr) != 1) {
        fprintf(stderr, "IP 地址无效\n");
        close(sock_fd);
        return 1;
    }

    // 3. 连接服务器
    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == -1) {
        perror("connect");
        close(sock_fd);
        return 1;
    }

    printf("成功连接服务器！\n");

    // 4. 向服务器发送消息
    const char *message = "Hello Server!";
    if (send(sock_fd, message,
             strlen(message), 0) == -1) {
        perror("send");
        close(sock_fd);
        return 1;
    }

    printf("发送消息：%s\n", message);

    // 5. 接收服务器回复
    ssize_t n = recv(sock_fd, buffer,
                     BUFFER_SIZE - 1, 0);

    if (n > 0) {
        buffer[n] = '\0';
        printf("服务器回复：%s\n", buffer);
    } else if (n == 0) {
        printf("服务器已关闭连接\n");
    } else {
        perror("recv");
    }

    // 6. 关闭连接
    close(sock_fd);

    return 0;
}
