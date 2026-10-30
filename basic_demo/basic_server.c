#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8888
#define BUFFER_SIZE 1024

int main(void) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // 1. 创建 TCP Socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    // 2. 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(PORT);

    // 3. 绑定 IP 地址和端口
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    // 4. 开始监听
    if (listen(server_fd, 5) == -1) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("服务器启动，监听端口 %d...\n", PORT);

    // 5. 等待客户端连接
    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd == -1) {
        perror("accept");
        close(server_fd);
        return 1;
    }

    printf("客户端已连接！\n");

    // 6. 接收客户端消息
    ssize_t n = recv(client_fd, buffer,
                     BUFFER_SIZE - 1, 0);

    if (n > 0) {
        buffer[n] = '\0';
        printf("收到客户端消息：%s\n", buffer);

        // 7. 回复客户端
        const char *reply = "Hello Client!";
        if (send(client_fd, reply,
                 strlen(reply), 0) == -1) {
            perror("send");
        }
    } else if (n == 0) {
        printf("客户端已断开连接\n");
    } else {
        perror("recv");
    }

    // 8. 关闭连接
    close(client_fd);
    close(server_fd);

    return 0;
}
