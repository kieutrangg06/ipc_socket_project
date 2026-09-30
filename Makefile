CC = gcc
CFLAGS = -Wall -Wextra -O2

all: server client

server: server.c protocol.h logger.h
	$(CC) $(CFLAGS) server.c -o server

client: client.c protocol.h
	$(CC) $(CFLAGS) client.c -o client

clean:
	rm -f server client /tmp/ipc_chat_system.sock *.bin nhan_* *.log *.csv *.in

.PHONY: all clean
