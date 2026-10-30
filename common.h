#ifndef CHAT_COMMON_H
#define CHAT_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#define CHAT_PORT 8888
#define CHAT_MAX_CLIENTS 50
#define CHAT_NAME_SIZE 32
#define CHAT_CONTENT_SIZE 1025
#define CHAT_MAX_FRAME 4096

typedef enum {
    CHAT_OK = 0,
    CHAT_ERR = -1,
    CHAT_CLOSED = -2,
    CHAT_INVALID = -3,
    CHAT_DUPLICATE = -4,
    CHAT_FULL = -5,
    CHAT_NOT_FOUND = -6
} ChatStatus;

typedef enum {
    MSG_LOGIN,
    MSG_GROUP,
    MSG_PRIVATE,
    MSG_ONLINE_LIST,
    MSG_LOGOUT,
    MSG_NOTICE,
    MSG_ERROR
} MessageType;

typedef struct {
    MessageType type;
    char sender[CHAT_NAME_SIZE];
    char target[CHAT_NAME_SIZE];
    char content[CHAT_CONTENT_SIZE];
} ChatMessage;

typedef struct ClientSession {
    int fd;
    char username[CHAT_NAME_SIZE];
    pthread_mutex_t send_lock;
    unsigned int refs;
    int closing;
} ClientSession;

#endif
