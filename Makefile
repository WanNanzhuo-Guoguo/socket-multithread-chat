CC = gcc
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -pthread
LDLIBS = -lcjson -pthread
COMMON = transport.c protocol.c
SERVER_SRC = server.c handler.c chat_manager.c $(COMMON)
CLIENT_SRC = client.c $(COMMON)

.PHONY: all clean
all: server client
server: $(SERVER_SRC) common.h handler.h chat_manager.h transport.h protocol.h
	$(CC) $(CFLAGS) $(SERVER_SRC) -o $@ $(LDLIBS)
client: $(CLIENT_SRC) common.h transport.h protocol.h
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o $@ $(LDLIBS)
clean:
	rm -f server client *.o basic_demo/basic_server basic_demo/basic_client tests/test_client tests/test_transport
