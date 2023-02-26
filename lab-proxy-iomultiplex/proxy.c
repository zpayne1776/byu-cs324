#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h>

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


int all_headers_received(char *);
int parse_request(char *, char *, char *, char *, char *, char *);
void test_parser();
void print_bytes(unsigned char *, int);
int open_sfd(char *);
int openRemoteSFD(char *, char *);
void handle_new_clients(request_info* newClient);
void handle_client(request_info* clientRequest);
void addToClientList(request_info* requestToAdd);
request_info* findClient(int fd);
int findClientIndex(int fd);
void removeFromClientList(int fd);
void removeClient(int fd);
void deregisterFD(int fd);
void wipeStrings(char *, char *, char *, char *, char *);
void wipeBuffer(char *);
void createRequest(char *, char *, char *, char *, char *);

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";
request_info *clientList;
int* capacity;
int* size;
int sfd;

int epollFD;
struct epoll_event ev, events[MAX_EVENTS];

int main(int argc, char *argv[])
{
    //request_info* myRequests;
    sfd = open_sfd(argv[1]);
    epollFD = epoll_create1(0);
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = sfd;
    *capacity = 10;
    *size = 0;
    clientList = calloc(*capacity, sizeof(request_info));

    if (epoll_ctl(epollFD, EPOLL_CTL_ADD, sfd, &ev) == -1) {
        perror("epoll_ctl - Adding listener FD");
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
                    addToClientList(newClient);
                    handle_new_clients(newClient);
                }
            }
        }
    }

//	test_parser();
//	printf("%s\n", user_agent_hdr);
	return 0;
}


void addToClientList(request_info* clientToAdd) {
    if (*size == *capacity) {
        *capacity *= 2;
        clientList = realloc(clientList, sizeof(request_info) * *capacity);
    }
    clientList[*size++] = *clientToAdd;
}

request_info* findClient(int fd) {
    for (int i = 0; i < *size; i++) {
        if (clientList[i].clientFD == fd) {
            printf("Successfully found Client!");
            fflush(stdout);
            return &clientList[i];
        }
    }
    printf("Failed to find Client.");
    fflush(stdout);
    return NULL;
}

int findClientIndex(int fd) {
    for (int i = 0; i < *size; i++) {
        if (clientList[i].clientFD == fd) {
            printf("Successfully found Client index!");
            fflush(stdout);
            return i;
        }
    }
    printf("Failed to find Client index.");
    fflush(stdout);
    return -1;
}

void removeClient(int fd) {
    deregisterFD(fd);
    removeFromClientList(fd);
}

void deregisterFD(int fd) {
    if (epoll_ctl(epollFD, EPOLL_CTL_DEL, sfd, &ev) == -1) {
        perror("epoll_ctl - Removing client FD");
        exit(EXIT_FAILURE);
    }
}

void removeFromClientList(int fd) {
    int index = findClientIndex(fd);

    if (index != -1) {
        printf("Successfully removed Client from Client List!");
        fflush(stdout);
        for (int i = index; i < *size - 1; i++) {
            clientList[i] = clientList[i + 1];
        }
        //memset(clientList[*size - 1], 0, sizeof(request_info));
        *size = *size - 1;
    }
}

void handle_client(request_info* clientRequest) {

    printf("########### ENTERED HANDLE_CLIENT ##############");
    printf("CLIENT FD: [%d]   REQUEST STATE: [%u]", clientRequest->clientFD, clientRequest->state);
    fflush(stdout);

    switch(clientRequest->state) {
        case READ_REQUEST:
            char method[100];
            char hostname[100];
            char port[100];
            char path[100];
            char headers[MAX_CACHE_SIZE];
            char buffer[MAX_BUFFER_SIZE];

            wipeStrings(method, hostname, port, path, headers);
            wipeBuffer(buffer);
            while(!all_headers_received(clientRequest->clientRequest)) {
                int stringLength = read(clientRequest->clientFD, buffer, MAX_BUFFER_SIZE);
                if (stringLength < 0) {
                    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                        return;
                    } else {
                        fprintf(stderr, "=======================================================\nepoll_wait failed with error: %s\n===============================================================\n",
                                strerror(errno));
                        fflush(stderr);
                        removeClient(clientRequest->clientFD);
                        return;
                    }
                }
                strncat(clientRequest->clientRequest, buffer, stringLength);
                wipeBuffer(buffer);
            }

            if (parse_request(clientRequest->clientRequest, method, hostname, port, path, headers)) {
                printf("SUCCESSFULLY PARSED REQUEST!");
                fflush(stdout);
                createRequest(clientRequest->serverRequest, method, hostname, port, path);
                clientRequest->serverFD = openRemoteSFD(hostname, port);

                struct epoll_event localEV;
                localEV.events = EPOLLIN | EPOLLET;
                localEV.data.fd = clientRequest->serverFD;
                if (epoll_ctl(epollFD, EPOLL_CTL_ADD, clientRequest->serverFD, &localEV) == -1) {
                    perror("epoll_ctl - Adding client FD");
                    exit(EXIT_FAILURE);
                }
            } else {
                printf("THIS IS WHERE THE SCRIPT FAILED TO PARSE THE REQUEST. NOT GOOD.");
                fflush(stdout);
            }
            clientRequest->state = SEND_REQUEST;
            break;
        case SEND_REQUEST:
            break;
        case READ_RESPONSE:
            break;
        case SEND_RESPONSE:
            break;
    }
}

void wipeStrings(char *method, char *hostname, char *port, char *path, char *headers) {
    memset(method, '\0', 100);
    memset(hostname, '\0', 100);
    memset(port, '\0', 100);
    memset(path, '\0', 100);
    memset(headers, '\0', MAX_CACHE_SIZE);
}

void wipeBuffer(char *buffer) {
    memset(buffer, '\0', MAX_BUFFER_SIZE);
}

void handle_new_clients(request_info* newClient) {

    while(1) {
        struct epoll_event localEV;
        struct sockaddr address;
        socklen_t addrLength = sizeof(address);

        newClient->clientFD = accept(sfd, &address, &addrLength);
        if (newClient->clientFD < 0) {
            break;
        }

        int flags = fcntl(sfd, F_GETFL, 0);
        fcntl(sfd, F_SETFL, flags | O_NONBLOCK);
        localEV.events = EPOLLIN | EPOLLET;
        localEV.data.fd = newClient->clientFD; 
        if (epoll_ctl(epollFD, EPOLL_CTL_ADD, newClient->clientFD, &localEV) == -1) {
            perror("epoll_ctl - Adding client FD");
            exit(EXIT_FAILURE);
        }
        newClient->state = READ_REQUEST;
        printf("============== NEW FD: %d", newClient->clientFD);
        fflush(stdout);
    }

    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        return;
    } else {
        exit(EXIT_FAILURE);
    }

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
