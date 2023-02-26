#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_BUFFER_SIZE 2048
#define MAX_EVENTS 10

enum state {
    READ_REQUEST,
    SEND_REQUEST,
    READ_RESPONSE,
    SEND_RESPONSE

};

typedef struct {
    int clientFD;
    int serverFD;
    enum state state;
    char clientRequest[MAX_CACHE_SIZE];
    char serverRequest[MAX_CACHE_SIZE];
    char serverResponse[MAX_CACHE_SIZE];
    int bytesFromClient;
    int bytesForServer;
    int bytesToServer;
    int bytesFromServer;
    int bytesToClient;
} request_info;

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";
request_info *clientList;

int all_headers_received(char *);
int parse_request(char *, char *, char *, char *, char *, char *);
void test_parser();
void print_bytes(unsigned char *, int);
int open_sfd(char *);
void handle_new_clients(int sfd, int epollFD, request_info* newClient);
void handle_client(request_info* clientRequest);
void addToClientList(request_info* requestToAdd, int capacity, int size);
request_info* findClient(int fd, int size);

int main(int argc, char *argv[])
{
    //request_info* myRequests;
    int sfd = open_sfd(argv[1]);
    int epollFD;
    struct epoll_event ev, events[MAX_EVENTS];
    epollFD = epoll_create1(0);
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = sfd;
    int capacity = 10;
    int size = 0;
    clientList = calloc(capacity, sizeof(request_info));

    if (epoll_ctl(epollFD, EPOLL_CTL_ADD, sfd, &ev) == -1) {
        perror("epoll_ctl - Adding client FD");
        exit(EXIT_FAILURE);
    }

    while (1) {
        int check = epoll_wait(epollFD, events, MAX_EVENTS, 10);

        if (check == 0) {
            if (0) {
                // CHECK FOR GLOBAL SIGNAL FROM SOMETHING
            } else {
                continue;
            }
        } else if (check < 0) {
            fprintf(stderr, "=======================================================\nepoll_wait failed with error: %s\n===============================================================\n",
                    strerror(errno));
            exit(EXIT_FAILURE);
        } else {
            for (int i = 0; i < check; i++) {
                // Check if the event is for the listening socket
                if (events[i].data.fd == sfd) {
                    request_info* newClient = calloc(1, sizeof(request_info));
                    addToClientList(newClient, capacity, size);
                    handle_new_clients(sfd, epollFD, newClient);
                }
            }
        }
    }

//	test_parser();
//	printf("%s\n", user_agent_hdr);
	return 0;
}


void addToClientList(request_info* clientToAdd, int capacity, int size) {
    if (size == capacity) {
        capacity *= 2;
        clientList = realloc(clientList, sizeof(request_info) * capacity);
    }
    clientList[size++] = *clientToAdd;
}

request_info* findClient(int fd, int size) {
    for (int i = 0; i < size; i++) {
        if (clientList[i].clientFD == fd) {
            return &clientList[i];
        }
    }
    return NULL;
}

void handle_client(request_info* clientRequest) {

    printf("########### ENTERED HANDLE_CLIENT ##############");
    printf("CLIENT FD: [%d]   REQUEST STATE: [%u]", clientRequest->clientFD, clientRequest->state);
    fflush(stdout);

    switch(clientRequest->state) {
        case READ_REQUEST:
	    char buffer[MAX_BUFFER_SIZE];
	    
	    while(!all_headers_received(wholeArg)) {
     		int stringLength = read(fd, buffer, MAX_BUFFER_SIZE);
        	strncat(clientRequest->clientRequest, buffer, stringLength);
        	wipeBuffer(buffer);
    	    }


            break;
        case SEND_REQUEST:
            break;
        case READ_RESPONSE:
            break;
        case SEND_RESPONSE:
            break;
    }
}

void handle_new_clients(int sfd, int epollFD, request_info* newClient) {

    while(1) {
        struct epoll_event ev;
        struct sockaddr address;
        socklen_t addrLength = sizeof(address);

        newClient->clientFD = accept(sfd, &address, &addrLength);
        if (newClient->clientFD < 0) {
            break;
        }

        int flags = fcntl(sfd, F_GETFL, 0);
        fcntl(sfd, F_SETFL, flags | O_NONBLOCK);
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = newClient->clientFD;
        if (epoll_ctl(epollFD, EPOLL_CTL_ADD, newClient->clientFD, &ev) == -1) {
            perror("epoll_ctl - Adding client FD");
            exit(EXIT_FAILURE);
        }
        printf("============== NEW FD: %d", newClient->clientFD);
        fflush(stdout);
    }

    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        return;
    } else {
        exit(EXIT_FAILURE);
    }

}

int open_sfd(char *port) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);

    int optval = 1;
    setsockopt(sfd,SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    struct sockaddr_in ipv4_local;
    ipv4_local.sin_family = AF_INET;
    ipv4_local.sin_port = htons(atoi(port));
    ipv4_local.sin_addr.s_addr = 0;
    int flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    fcntl(sfd, F_SETFL, flags | O_NONBLOCK);

    if (bind(sfd, (struct sockaddr *)&ipv4_local, sizeof(struct sockaddr_in)) < 0) {
        perror("bind()");
    }

    int queueLength = 100; //Arbitrarily Big. No spec guidance, so whatever.
    if (listen(sfd, queueLength) < 0) {
        perror("listen()");
    }

    return sfd;
}

int all_headers_received(char *request) {
    char *eoh = "\r\n\r\n";
    if (strstr(request, eoh) != NULL) {
        return 1;
    } else {
        return 0;
    }
}

int parse_request(char *request, char *method,
		char *hostname, char *port, char *path, char *headers) {
    if (!all_headers_received(request)) {
        return 0;
    }

    char space[] = " ";
    char slash[] = "/";
    char colon[] = ":";
    char newLine[] = "\n";

    int length = strcspn(request, space);
    method = strncpy(method, request, length);

    char *temp2 = strstr(&strstr(request, slash)[1], slash);
    char temp3[72];
    memset(temp3, 0, 72);
    strncpy(temp3, &temp2[1], strcspn(&temp2[1], slash));
    hostname = strncpy(hostname, temp3, strcspn(temp3, colon));

    if (strstr(temp3, colon) != NULL) {
        port = strncpy(port, &strstr(temp3, colon)[1], strlen(&strstr(temp3, colon)[1]));
    } else {
        port = strcpy(port, "80");
    }

    char *temp4 = strstr(&temp2[1], slash);
    path = strncpy(path, &temp4[1], strcspn(temp4, space));

    char *temp5 = strstr(temp4, newLine);
    headers = strcpy(headers, &temp5[1]);

    return 1;
}

void test_parser() {
	int i;
	char method[16], hostname[64], port[8], path[64], headers[1024];

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
		if (parse_request(reqs[i], method, hostname, port, path, headers)) {
			printf("METHOD: %s\n", method);
			printf("HOSTNAME: %s\n", hostname);
			printf("PORT: %s\n", port);
			printf("HEADERS: %s\n", headers);
		} else {
			printf("REQUEST INCOMPLETE\n");
		}
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
