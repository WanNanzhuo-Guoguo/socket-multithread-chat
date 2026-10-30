#ifndef CHAT_TRANSPORT_H
#define CHAT_TRANSPORT_H

#include "common.h"

/* Debug-compatible transport implementation; production version belongs to student 1. */
ChatStatus send_frame(int fd, const void *data, uint32_t length);
ChatStatus recv_frame(int fd, char **data, uint32_t *length);

#endif
