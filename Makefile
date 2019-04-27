# Makefile

all: server.c client.c
	gcc -o server -std=c99 -Wall server.c -pthread
	gcc -o client -std=c99 -Wall client.c

clean:
	rm -f shell