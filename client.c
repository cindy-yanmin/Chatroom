/*
HW7 Cindy Dai - Networking Client
Implement a chatroom with threads
*/

#include <stdlib.h>			// exit
#include <stdio.h>			// printf, perror
#include <unistd.h>			// close, write, read
#include <string.h>			// string
#include <netdb.h>
#include <arpa/inet.h>		// inet_ntop
#include <netinet/in.h>		// servaddr, INADDR_ANY, htons
#include <sys/socket.h>		// socket, bind, listen, accept
#include <sys/select.h>		// select

// global constants
#define USHRT_MAX		65535
#define	BUF_LEN			1024
#define PORT			13000

// function prototypes
void argParser(int, char **, char **, char **, in_port_t*);
void chatClient(int, char *);

int main(int argc, char *argv[]) {
	// set up the chat setting
	char *username = "CLIENT";
	char *address = "127.0.0.1";
	in_port_t port = PORT;
	argParser(argc, argv, &username, &address, &port);
	// create socket file descriptor
	int sockfd;
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket error");
		exit(1);
	}
	// set up the sockaddr_in and zero it
	struct sockaddr_in	servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;					// specify the family
	servaddr.sin_port = htons(port);				// port to listen on
	if (inet_pton(AF_INET, address, &servaddr.sin_addr) <= 0) { //ip address
		fprintf(stderr, "inet_pton error for: %s\n", argv[1]);
		exit(1);
	}
	// connect socket to server
	if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		perror("connect error");
		exit(1);
	}
    // function for chat 
	chatClient(sockfd, username);
	// close the socket 
	close(sockfd);
}

// check argv - cmd, username, IP address, port number
void argParser(int argc, char **argv, char **username, char **address, in_port_t* port) {
	if (argc > 4) {
		fprintf(stderr,
			"Too many arguments: USAGE %s username IP_address port_number\n", argv[0]);
		exit(1);
	}
	if (argc >= 2)	*username = argv[1];
	if (argc >= 3)	*address = argv[2];
	if (argc == 4) { // check for valid port number
		char* ptr;
		long portl = strtol(argv[3], &ptr, 10);
		if (portl < 0 || portl > USHRT_MAX) {
			fprintf(stderr, "invalid port number\n");
			exit(1);
		}
		*port = (in_port_t)portl; // casting
	}
}

// receive message from server
void receMsg(int fd) {
	// buffer
	char buff[BUF_LEN];
	memset(&buff, 0, sizeof(buff));
	// read from server
	int n;
	if ((n = read(fd, buff, BUF_LEN - 1)) <= 0) {
		if (n < 0) perror("read error");
		else puts("Server exited.");
		exit(1);
	}
	buff[n] = '\0';
	// print to stdout
	printf("%s", buff);
}

// send message to server
void sendMsg(int sockfd, char* name) {
	// buffer
	char buff[BUF_LEN], chatMsg[BUF_LEN];
	memset(&buff, 0, sizeof(buff));
	// read from stdin
	int n;
	if ((n = read(0, buff, BUF_LEN - 1)) < 0) {
		perror("read error");
		exit(1);
	}
	buff[n] = '\0';
	// exit message
	if (strncmp(buff, "exit", 4) == 0) {
		close(sockfd);
		exit(0);
	}
	// go back to prev line so server boardcast will overwrite this line
	char *message = "\033[F";
	if (write(1, message, strlen(message)) < 0) { 
		perror("write error"); exit(1);
	}
	// construct the message
	memset(chatMsg, 0, sizeof(chatMsg));
	strcpy(chatMsg, name);
	strcat(chatMsg, ": ");
	strcat(chatMsg, buff);
	// write to server
	if (write(sockfd, chatMsg, BUF_LEN - 1) < 0) {
		perror("write error"); exit(1);
	}
}

// send and receive info from socket
void chatClient(int sockfd, char* username) {
	// set up for select()
	fd_set set;
	fd_set oirginalSet;
	FD_ZERO(&oirginalSet);
	FD_SET(0, &oirginalSet); // stdin
	FD_SET(sockfd, &oirginalSet); // socket fd
	int maxfd = sockfd + 1;
	while (1) {
		// reset the select setting
		set = oirginalSet;
		// blocks until theres available input
		if (select(maxfd, &set, NULL, NULL, NULL) < 0) {
			perror("select error");
			exit(1);
		}
		if (FD_ISSET(0, &set)) // read from stdin
			sendMsg(sockfd, username);
		else if (FD_ISSET(sockfd, &set)) // read from server
			receMsg(sockfd);
	}
}