CC := gcc
CFLAGS := -Wall -Wextra
ccflags-y := -std=gnu11
SERVER_SRC := server.c src/queue.c src/command_parser.c src/logger.c
CLIENT_SRC := client.c src/command_parser.c src/logger.c
SERVER_BIN := server
CLIENT_BIN := client
LOGS_DIR := logs

.PHONY: all clean

all: server client

server:
	$(CC) $(CFLAGS) $(SERVER_SRC) -o $(SERVER_BIN) -lpthread -lrt -std=gnu99 -D_DEFAULT_SOURCE

client:
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o $(CLIENT_BIN) -lpthread -lrt -std=gnu99 -D_DEFAULT_SOURCE

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
	rm -rf $(LOGS_DIR)

