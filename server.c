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
#include <pthread.h>		// threads

// global constants
#define USHRT_MAX		65535
#define	BUF_LEN			1024
#define PORT			13000
#define LST_LEN			500

// message queue
typedef struct {
	char lst[LST_LEN][BUF_LEN];
	int head, tail, size;
} MsgQueue; 

// connection parameters
typedef struct {
	int sockfd;
	int clientfd;
} Connection;

// static globals - accessed by multiple threads
static pthread_cond_t enqueueCond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t dequeueCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t msgMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t connMutex = PTHREAD_MUTEX_INITIALIZER;
static MsgQueue msgLst;
static int clientCount = 0;
// There can be at most LST_LEN clients
static Connection clientLst[LST_LEN]; 


// function prototypes
void queueInit(MsgQueue *);
void enqueue(char *);
char* dequeue(void);
int findIndex(int);

void argParser(int, char **, in_port_t*);
void chatServer(int);
void* receMsg(void*);
void* sendMsg(void*);
void* acceptClient(void*);

int main(int argc, char **argv) {
	// set the chat setting
	in_port_t port = PORT;
	argParser(argc, argv, &port);
	// create the socket
	int sockfd;
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket error");
		exit(1);
	}
	// set up the sockaddr_in and zero it
	struct sockaddr_in	servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;					// specify the family
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);	// use any available
	servaddr.sin_port = htons(port);				// port to listen on
	// bind that address object to our listening file descriptor
	if (bind(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		perror("socket bind error");
		exit(1);
	}
	// listen on the socket file descriptor
	if (listen(sockfd, BUF_LEN) < 0) {
		perror("listen error");
		exit(1);
	}
	// function for chat 
	chatServer(sockfd);
	// close the socket 
	close(sockfd);
}

// check argv - cmd, port_number
void argParser(int argc, char **argv, in_port_t* port) {
	if (argc > 2) { // too many args
		fprintf(stderr,
			"Too many arguments: USAGE %s port_number\n", argv[0]);
		exit(1);
	}
	if (argc == 2) { // check for valid port number
		char* ptr;
		long portl = strtol(argv[1], &ptr, 10);
		if (portl < 0 || portl > USHRT_MAX) {
			fprintf(stderr, "invalid port number\n");
			exit(1);
		}
		*port = (in_port_t)portl; // casting
	}
}

// send and receive info from socket
void chatServer(int sockfd) {
	// set up connection and queue
	Connection conn;
	conn.sockfd = sockfd;
	queueInit(&msgLst);
	// accept thread
	pthread_t acceptThread;
	if ((pthread_create(&acceptThread, NULL, acceptClient, (void *)&conn)) > 0) {
		perror("thread create error");
		exit(1);
	}
	// message thread
	pthread_t msgThread;
	if ((pthread_create(&msgThread, NULL, sendMsg, NULL)) > 0) {
		perror("thread create error");
		exit(1);
	}
	// main() should not exit before these threads
	if ((pthread_join(acceptThread, NULL)) > 0) {
		perror("thread create error");
		exit(1);
	}
	if ((pthread_detach(msgThread)) > 0 ) {
		perror("thread create error");
		exit(1);
	}
}

// compose welcome message
char* composeMsg(void) {
	char* msg = (char *)malloc(BUF_LEN * sizeof(char*));
	if (sprintf(msg, "A client on fd %d connected!\nCurrent avalible client fd(s):", 
		clientLst[clientCount - 1].clientfd) < 0) {
		puts("sprintf error"); 
		exit(1);
	}
	for (int i = 0; i < clientCount; i++) {
		if (sprintf(msg + strlen(msg), " %d", clientLst[i].clientfd) < 0) {
			puts("sprintf error"); 
			exit(1);
		}
	}
	if (sprintf(msg + strlen(msg), "\n") < 0) {
		puts("sprintf error"); 
		exit(1);
	}
	return msg;
}

// accept new client
void* acceptClient(void* connection) {
	puts("Waiting to accept clients...");
	Connection* conn = (Connection*)connection;
	int clientfd;
	while (1) {
		if ((clientfd = accept(conn->sockfd, NULL, NULL)) < 0) {
			perror("accept error");  continue; }
		if (clientCount == LST_LEN) {
			puts("Max client number reached, failed to add client.\n");
			close(clientfd); continue; }
		
		pthread_mutex_lock(&connMutex);
		// lock for shared resources
		pthread_t clientThread; // new client thread
		conn->clientfd = clientfd; // update the connection variables
		clientLst[clientCount] = *conn;
		if ((pthread_create(&clientThread, NULL, receMsg, (void *)&clientLst[clientCount])) > 0) {
			perror("thread create error");
			close(clientfd); exit(1);
		}
		if ((pthread_detach(clientThread)) > 0) {
			perror("thread create error"); exit(1);
		}
		++clientCount;
		// boardcast the new client message to all clients
		char* newClientMsg = composeMsg();
		enqueue(newClientMsg);
		free(newClientMsg);
		pthread_mutex_unlock(&connMutex);
	}
}

// remove the client from the clientLst
void removeClient(int fd) {
	pthread_mutex_lock(&connMutex); 
	// lock for shared resources
	int index = findIndex(fd);
	if (index == -1) { // not found, no remove necessary
		pthread_mutex_unlock(&connMutex);
		return;
	}
	// move the last client to this client's position on list
	clientLst[index] = clientLst[clientCount - 1];
	clientCount -= 1;
	close(fd); // close unused fd
	pthread_mutex_unlock(&connMutex);
}

// server receives msg and enqueue
void* receMsg(void* connection) {
	Connection conn = *(Connection*)connection;
	char buff[BUF_LEN];

	while (1) {
		memset(buff, 0, sizeof(buff));
		// read in the input
		int n;
		if ((n = read(conn.clientfd, buff, BUF_LEN - 1)) < 0) {
			perror("read error");
			close(conn.clientfd);
			exit(1);
		}
		buff[n] = '\0';
		// client exit
		if (n == 0 || strncmp(buff, "exit", 4) == 0) { 
			// boardcast the client exit message to all clients
			char exitMsg[BUF_LEN];
			snprintf(exitMsg, BUF_LEN, "A client on fd %d exited.\n", conn.clientfd);
			removeClient(conn.clientfd);
			enqueue(exitMsg);
			return NULL;
		}
		else // enqueue
			enqueue(buff);
	}
}

// server sends msg and dequeue
void* sendMsg(void* conn) {
	while (1) {
		char* buff = dequeue();
		for (int i = 0; i < clientCount; i++) {
			if (write(clientLst[i].clientfd, buff, BUF_LEN - 1) == -1) {
				perror("write error");
				exit(1);
			}
		}
	}
}

// initalize a queue
void queueInit(MsgQueue *queue) {
	queue->head = 0;
	queue->tail = 0;
	queue->size = 0;
}

// enqueue msg
void enqueue(char* msg) {
	pthread_mutex_lock(&msgMutex);
	// lock for shared resources
	if (msgLst.size >= LST_LEN) { // enqueue when full -> wait
		pthread_cond_wait(&enqueueCond, &msgMutex);
	}
	strcpy(msgLst.lst[msgLst.tail], msg);
	printf("Message Enqueued -> %s", msgLst.lst[msgLst.tail]);
	msgLst.tail = (msgLst.tail + 1) % LST_LEN;
	msgLst.size += 1;
	pthread_cond_signal(&dequeueCond); // wake all sleeping consumer
	pthread_mutex_unlock(&msgMutex);
}

// dequeue msg
char* dequeue(void) {
	pthread_mutex_lock(&msgMutex);
	// lock for shared resources
	while (msgLst.size == 0) { // dequeue when empty -> wait
		pthread_cond_wait(&dequeueCond, &msgMutex);
	}
	char* msg = msgLst.lst[msgLst.head];
	printf("Message Dequeued -> %s", msg);
	msgLst.head = (msgLst.head + 1) % LST_LEN;
	msgLst.size -= 1;
	pthread_cond_signal(&enqueueCond); // wake sleeping consumer
	pthread_mutex_unlock(&msgMutex);
	return msg;
}

// find the index of fd from client list
int findIndex(int num) {
	for (int i = 0; i < clientCount; i++) {
		if (clientLst[i].clientfd == num) return i;
	}
	return -1;
}