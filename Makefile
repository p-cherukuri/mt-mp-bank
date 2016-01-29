CC = gcc
CFLAGS = -Wall -g -std=c99 -pedantic -D_XOPEN_SOURCE=600 -pthread

all: server client

server: server.c
	$(CC) $(CFLAGS) -o server server.c

client: client.c
	$(CC) $(CFLAGS) -o client client.c

clean:
	-rm -rf client.dSYM
	-rm -rf server.dSYM
	-rm client
	-rm server
