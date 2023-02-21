#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
//#define MAX_CACHE_SIZE 10000
#define MAX_OBJECT_SIZE 102400
#define MAX_BUFFER_SIZE 2048
#define NTHREADS  8
#define SBUFSIZE  5

/* $begin sbuft */
typedef struct {
    int *buf;          /* Buffer array */
    int n;             /* Maximum number of slots */
    int front;         /* buf[(front+1)%n] is first item */
    int rear;          /* buf[rear%n] is last item */
    sem_t mutex;       /* Protects accesses to buf */
    sem_t slots;       /* Counts available slots */
    sem_t items;       /* Counts available items */
} sbuf_t;
/* $end sbuft */

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

/* Create an empty, bounded, shared FIFO buffer with n slots */
/* $begin sbuf_init */
void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf = calloc(n, sizeof(int));
    sp->n = n;                       /* Buffer holds max of n items */
    sp->front = sp->rear = 0;        /* Empty buffer iff front == rear */
    sem_init(&sp->mutex, 0, 1);      /* Binary semaphore for locking */
    sem_init(&sp->slots, 0, n);      /* Initially, buf has n empty slots */
    sem_init(&sp->items, 0, 0);      /* Initially, buf has zero data items */
}
/* $end sbuf_init */

/* Clean up buffer sp */
/* $begin sbuf_deinit */
void sbuf_deinit(sbuf_t *sp)
{
    free(sp->buf);
}
/* $end sbuf_deinit */

/* Insert item onto the rear of shared buffer sp */
/* $begin sbuf_insert */
void sbuf_insert(sbuf_t *sp, int item)
{
    printf("before sem_wait(slots)\n"); fflush(stdout);
    sem_wait(&sp->slots);                          /* Wait for available slot */
    printf("after sem_wait(slots)\n"); fflush(stdout);

    sem_wait(&sp->mutex);                          /* Lock the buffer */
    sp->buf[(++sp->rear)%(sp->n)] = item;   /* Insert the item */
    sem_post(&sp->mutex);                          /* Unlock the buffer */

    printf("before sem_post(items)\n"); fflush(stdout);
    sem_post(&sp->items);                          /* Announce available item */
    printf("after sem_post(items)\n"); fflush(stdout);

}
/* $end sbuf_insert */

/* Remove and return the first item from buffer sp */
/* $begin sbuf_remove */
int sbuf_remove(sbuf_t *sp)
{
    int item;

    printf("before sem_wait(items)\n"); fflush(stdout);
    sem_wait(&sp->items);                          /* Wait for available item */
    printf("after sem_wait(items)\n"); fflush(stdout);

    sem_wait(&sp->mutex);                          /* Lock the buffer */
    item = sp->buf[(++sp->front)%(sp->n)];  /* Remove the item */
    sem_post(&sp->mutex);                          /* Unlock the buffer */

    printf("before sem_post(slots)\n"); fflush(stdout);
    sem_post(&sp->slots);                          /* Announce available slot */
    printf("after sem_post(slots)\n"); fflush(stdout);

    return item;
}
/* $end sbuf_remove */

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";
sbuf_t sbuf;

int all_headers_received(char *);
int parse_request(char *, char *, char *, char *, char *, char *);
void test_parser();
void print_bytes(unsigned char *, int);
int open_sfd(char *port);
void handle_client(int fd);
void *startClientThreads(void *vargp);
void wipeStrings(char *, char *, char *, char *, char *, char *, char *, char *);
void wipeBuffer(char *);
void wipeBuffer2(char *);
void createRequest(char *, char *, char *, char *, char *);


int main(int argc, char *argv[])
{
    pthread_t tid;
    int sfd = open_sfd(argv[1]);

    sbuf_init(&sbuf, SBUFSIZE); //line:conc:pre:initsbuf
    for (int i = 0; i < NTHREADS; i++)  /* Create worker threads */ //line:conc:pre:begincreate
        pthread_create(&tid, NULL, startClientThreads, NULL);

    while(1) {
        struct sockaddr address;
        socklen_t addrLength;
        int client;
        if ((client = accept(sfd, &address, &addrLength)) != -1) {
            //pthread_create(&tid, NULL, startClientThread, client);
            //handle_client(client);
            sbuf_insert(&sbuf, client);
        }
    }

	//test_parser();
	//printf("%s\n", user_agent_hdr);
	//return 0;
}

int open_sfd(char *port) {
	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	
	int optval = 1;
	setsockopt(sfd,SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

	struct sockaddr_in ipv4_local;
	ipv4_local.sin_family = AF_INET;
        ipv4_local.sin_port = htons(atoi(port));
        ipv4_local.sin_addr.s_addr = 0;

	if (bind(sfd, (struct sockaddr *)&ipv4_local, sizeof(struct sockaddr_in)) < 0) {
                        perror("bind()");
        }

	int queueLength = 100; //Arbitrarily Big. No spec guidance, so whatever.
	if (listen(sfd, queueLength) < 0) {
		perror("listen()");
	}

	return sfd;
}

int openRemoteSFD(char *hostname, char *port) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int serverFD;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int check = getaddrinfo(hostname, port, &hints, &result);
    if (check != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(check));
        exit(EXIT_FAILURE);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        serverFD = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (serverFD == -1) {
            continue;
        }

        if (connect (serverFD, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;
        }

        close(serverFD);
    }

    if (rp == NULL) {
        fprintf(stderr, "Could not connect\n");
        exit(EXIT_FAILURE);
    }

    //freeaddrinfo(result);
    //freeaddrinfo(rp);
    return serverFD;
}

void *startClientThreads(void *vargp) {
    pthread_detach(pthread_self());
    while (1) {
        int connfd = sbuf_remove(&sbuf); /* Remove connfd from buffer */ //line:conc:pre:removeconnfd
        printf("STARTING THREAD WITH FD: %d", connfd);
        fflush(stdout);
        handle_client(connfd);                /* Service client */
    }
}


void handle_client(int fd) {
    char method[100];
    char hostname[100];
    char port[100];
    char path[100];
    char headers[MAX_CACHE_SIZE];
    char buffer[MAX_BUFFER_SIZE];
    char buffer2[MAX_BUFFER_SIZE];
    char wholeArg[MAX_CACHE_SIZE];
    char newRequest[MAX_CACHE_SIZE];
    char serverResponse[MAX_CACHE_SIZE];

    //printf("Starting first Read Loop!");
    //fflush(stdout);

    wipeStrings(method, hostname, port, path, headers, wholeArg, newRequest, serverResponse);
    wipeBuffer(buffer);
    while(!all_headers_received(wholeArg)) {
        int stringLength = read(fd, buffer, MAX_BUFFER_SIZE);
        strncat(wholeArg, buffer, stringLength);
        wipeBuffer(buffer);
    }

    //printf("Just left Read Loop!");
    //fflush(stdout);

    if (parse_request(wholeArg, method, hostname, port, path, headers)) {
        printf("METHOD: %s\n", method);
        printf("HOSTNAME: %s\n", hostname);
        printf("PORT: %s\n", port);
        printf("HEADERS: %s\n", headers);
        fflush(stdout);

        createRequest(newRequest, method, hostname, port, path);
        printf("%s", newRequest);
        fflush(stdout);

        int serverFD = openRemoteSFD(hostname, port);
        write(serverFD, newRequest, strlen(newRequest));

        wipeBuffer2(buffer2);
        //int i = 0;

//        printf("PrintBytes:\n");
//        fflush(stdout);
//        print_bytes((unsigned char *) serverResponse, MAX_CACHE_SIZE);
//        fflush(stdout);
//        printf("EndPrintBytes");
//        fflush(stdout);


        int charsReceived = 0;
        int totalChars = 0;
        while((charsReceived = read(serverFD, buffer2, MAX_BUFFER_SIZE)) != 0) {
//            printf("SECTION %d:\n[[[[  %s  ]]]]", i, buffer2);
//            fflush(stdout);

            print_bytes((unsigned char *) buffer2, charsReceived);
            fflush(stdout);

            //strncat(serverResponse, buffer2, charsReceived);
            memcpy(serverResponse + totalChars, buffer2, charsReceived);

            wipeBuffer2(buffer2);
            totalChars += charsReceived;
            printf("SECTION CHARS RECEIVED: %d\n", charsReceived);
        }
        printf("TOTAL CHARACTERS RECEIVED: %d\n", totalChars);

        close(serverFD);
//        printf("%s", serverResponse);
//        fflush(stdout);

        write(fd, serverResponse, totalChars);
        close(fd);

    } else {
        printf("REQUEST INCOMPLETE\n");
        fflush(stdout);
    }

    wipeStrings(method, hostname, port, path, headers, wholeArg, newRequest,serverResponse);
    close(fd);
}

int all_headers_received(char *request) {
	char *eoh = "\r\n\r\n";
	if (strstr(request, eoh) != NULL) {
		return 1;
	} else {
		return 0;
	}
}

void wipeStrings(char *method, char *hostname, char *port, char *path, char *headers, char *wholeArg, char *newRequest, char *serverResponse) {
	memset(method, '\0', 100);
	memset(hostname, '\0', 100);
	memset(port, '\0', 100);
	memset(path, '\0', 100);
	memset(headers, '\0', MAX_CACHE_SIZE);
    memset(wholeArg, '\0', MAX_CACHE_SIZE);
    memset(newRequest, '\0', MAX_CACHE_SIZE);
    memset(serverResponse, '\0', MAX_CACHE_SIZE);
}

void wipeBuffer(char *buffer) {
    memset(buffer, '\0', MAX_BUFFER_SIZE);
}

void wipeBuffer2(char *buffer2) {
    memset(buffer2, '\0', MAX_BUFFER_SIZE);
}

int parse_request(char *request, char *method,
		char *hostname, char *port, char *path, char *headers) {

	if (!all_headers_received(request)) {
		return 0;
	}

	//printf("FULL REQUEST:\n%s", request);	

	char space[] = " ";
	char slash[] = "/";
	char colon[] = ":";
	char newLine[] = "\n";

	int length = strcspn(request, space);
	//char temp1[length];
	method = strncpy(method, request, length);

	//int hostStart = length+8;

	char *temp2 = strstr(&strstr(request, slash)[1], slash);
	//printf("\tTemp 2: %s\n", temp2);
	//fflush(stdout);
	char temp3[72];
	memset(temp3, 0, 72);
	//printf("\tSetting temp3\n");
	//fflush(stdout);
	strncpy(temp3, &temp2[1], strcspn(&temp2[1], slash));
	//printf("\tSetting hostname\n)");
	//fflush(stdout);
	hostname = strncpy(hostname, temp3, strcspn(temp3, colon));
	//printf("\tHostname: [%s]\n", hostname);
	//fflush(stdout);
	if (strstr(temp3, colon) != NULL) {
		//printf("\tIn port check\n");
		//fflush(stdout);
		port = strncpy(port, &strstr(temp3, colon)[1], strlen(&strstr(temp3, colon)[1]));
		//printf("\tReturning from port check\n");
		//fflush(stdout);
	} else {
		//printf("\tSetting default port\n");
		//fflush(stdout);
		port = strcpy(port, "80");
	}

	char *temp4 = strstr(&temp2[1], slash);
	path = strncpy(path, &temp4[1], strcspn(temp4, space));

 	char *temp5 = strstr(temp4, newLine);
	//printf("\tTemp5: [%s]", &temp5[1]);
	//fflush(stdout);
	headers = strcpy(headers, &temp5[1]);

	return 1;
}

void createRequest(char *newRequest, char *method,
                   char *hostname, char *port, char *path) {
    strcat(newRequest, method);
    strcat(newRequest, " /");
    strcat(newRequest, path);
    strcat(newRequest, "HTTP/1.0\r\n");
    strcat(newRequest, "Host: ");
    strcat(newRequest, hostname);
    if (strcmp(port,"80")) {
        strcat(newRequest, ":");
        strcat(newRequest, port);
    }
    strcat(newRequest, "\r\n");
    strcat(newRequest, user_agent_hdr);
    strcat(newRequest, "\r\n");
    strcat(newRequest, "Connection: close\r\n");
    strcat(newRequest, "Proxy-Connection: close\r\n\r\n\0");
}

void test_parser() {
	int i;
	char method[16], hostname[64], port[8], path[64], headers[1024];

	memset(method, 0, sizeof(method));
	memset(hostname, 0, sizeof(hostname));
	memset(port, 0, sizeof(port));
	memset(path, 0, sizeof(path));
	memset(headers, 0, sizeof(headers));

       	char *reqs[] = {
		"GET http://www.example.com/index.html HTTP/1.0\r\n"
		"Host: www.example.com\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://www.example.com:8080/index.html?foo=1&bar=2 HTTP/1.0\r\n"
		"Host: www.example.com:8080\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://localhost:1234/home.html HTTP/1.0\r\n"
		"Host: localhost:1234\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://www.example.com:8080/index.html HTTP/1.0\r\n",

		NULL
	};
	
	for (i = 0; reqs[i] != NULL; i++) {
		printf("Testing %s\n", reqs[i]);
		fflush(stdout);
		if (parse_request(reqs[i], method, hostname, port, path, headers)) {
			printf("METHOD: %s\n", method);
			printf("HOSTNAME: %s\n", hostname);
			printf("PORT: %s\n", port);
			printf("HEADERS: %s\n", headers);
			fflush(stdout);
		} else {
			printf("REQUEST INCOMPLETE\n");
			fflush(stdout);
		}
		//wipeStrings(method, hostname, port, path, headers);
	}
}

void print_bytes(unsigned char *bytes, int byteslen) {
	int i, j, byteslen_adjusted;

	if (byteslen % 8) {
		byteslen_adjusted = ((byteslen / 8) + 1) * 8;
	} else {
		byteslen_adjusted = byteslen;
	}
	for (i = 0; i < byteslen_adjusted + 1; i++) {
		if (!(i % 8)) {
			if (i > 0) {
				for (j = i - 8; j < i; j++) {
					if (j >= byteslen_adjusted) {
						printf("  ");
					} else if (j >= byteslen) {
						printf("  ");
					} else if (bytes[j] >= '!' && bytes[j] <= '~') {
						printf(" %c", bytes[j]);
					} else {
						printf(" .");
					}
				}
			}
			if (i < byteslen_adjusted) {
				printf("\n%02X: ", i);
			}
		} else if (!(i % 4)) {
			printf(" ");
		}
		if (i >= byteslen_adjusted) {
			continue;
		} else if (i >= byteslen) {
			printf("   ");
		} else {
			printf("%02X ", bytes[i]);
		}
	}
	printf("\n");
}
