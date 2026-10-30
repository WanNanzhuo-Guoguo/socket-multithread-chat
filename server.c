#include "handler.h"
#include "common.h"
#include "chat_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BACKLOG 10

int main(void)
{
    int server_fd;
    struct sockaddr_in server_addr;

    // 初始化聊天管理器
    if (chat_manager_init() != CHAT_OK) {
        fprintf(stderr, "聊天管理器初始化失败\n");
        return 1;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {
        perror("socket");
        chat_manager_destroy();
        return 1;
    }

    int opt = 1;

    if (setsockopt(server_fd, SOL_SOCKET,
                   SO_REUSEADDR, &opt,
                   sizeof(opt)) == -1) {
        perror("setsockopt");
        close(server_fd);
        chat_manager_destroy();
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(CHAT_PORT);

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        chat_manager_destroy();
        return 1;
    }

    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        chat_manager_destroy();
        return 1;
    }

    printf("====================================\n");
    printf(" 多线程 Socket 聊天服务器\n");
    printf(" 监听地址：127.0.0.1:%d\n", CHAT_PORT);
    printf(" 最大在线用户数：%d\n", CHAT_MAX_CLIENTS);
    printf("====================================\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(
            server_fd,
            (struct sockaddr *)&client_addr,
            &client_len
        );

        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("accept");
            continue;
        }

        char ip[INET_ADDRSTRLEN];

        inet_ntop(AF_INET,
                  &client_addr.sin_addr,
                  ip,
                  sizeof(ip));

        printf("[主线程] 新客户端连接：%s:%d\n",
               ip, ntohs(client_addr.sin_port));

        // 创建客户端会话对象
        ClientSession *session = calloc(
            1, sizeof(ClientSession)
        );

        if (session == NULL) {
            perror("calloc");
            close(client_fd);
            continue;
        }

        session->fd = client_fd;
        session->refs = 1;
        session->closing = 0;

        int ret = pthread_mutex_init(
            &session->send_lock, NULL
        );

        if (ret != 0) {
            fprintf(stderr,
                    "pthread_mutex_init: %s\n",
                    strerror(ret));

            close(client_fd);
            free(session);
            continue;
        }

        pthread_t tid;

        ret = pthread_create(
            &tid,
            NULL,
            handle_client,
            session
        );

        if (ret != 0) {
            fprintf(stderr,
                    "pthread_create: %s\n",
                    strerror(ret));

            // 线程创建失败，由主线程释放会话
            chat_session_release(session);
            continue;
        }

        // 分离线程，结束后自动回收线程资源
        ret = pthread_detach(tid);

        if (ret != 0) {
            fprintf(stderr,
                    "pthread_detach: %s\n",
                    strerror(ret));
        }
    }

    close(server_fd);
    return 0;
}

