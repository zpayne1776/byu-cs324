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


int all_headers_received(char *request);
int parse_request(char *request, char *method, char *hostname, char *port, char *path, char *headers);
void test_parser();
void print_bytes(unsigned char *bytes, int byteslen);
int open_sfd(char *port);
int openRemoteSFD(char *hostname, char *port);
void handle_new_clients();
void handle_client(request_info* clientRequest);
void addToClientList(request_info* requestToAdd);
request_info* findClient(int fd);
int findClientIndex(int fd);
void removeFromClientList(int fd);
void removeClient(int clientFD, int serverFD);
void deregisterFD(int fd);
void wipeStrings(char *method, char *hostname, char *port, char *path, char *headers);
void wipeBuffer(char *buffer);
int createRequest(char *newRequest, char *method, char *hostname, char *port, char *path);

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";
request_info **clientList;
int capacity;
int size;
int sfd;

int epollFD;
struct epoll_event ev, events[MAX_EVENTS];

int main(int argc, char *argv[])
{
    sfd = open_sfd(argv[1]);
    epollFD = epoll_create1(0);
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = sfd;
    capacity = 5;
    size = 0;
    clientList = calloc(capacity, sizeof(request_info*));

    if (epoll_ctl(epollFD, EPOLL_CTL_ADD, sfd, &ev) == -1) {
        perror("epoll_ctl - Adding listener FD\n");
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
                    handle_new_clients();
                } else if (findClientIndex(events[i].data.fd) != -1) {
                    handle_client(findClient(events[i].data.fd));
                }
            }
        }
    }

//	test_parser();
//	printf("%s\n", user_agent_hdr);
	return 0;
}


void addToClientList(request_info* clientToAdd) {
    if (size == capacity) {
        capacity *= 2;
        clientList = realloc(clientList, (sizeof(request_info*) * capacity));
    }
    printf("+++ ADDING CLIENT WITH FD [%d] TO THE CLIENT LIST\n", clientToAdd->clientFD);
    fflush(stdout);
    clientList[size++] = clientToAdd;
}

request_info * findClient(int fd) {
    for (int i = 0; i < size; i++) {
        if (clientList[i]->clientFD == fd || clientList[i]->serverFD == fd) {
            printf("Successfully found Request!\n");
            fflush(stdout);
            return clientList[i];
        }
    }
    printf("Failed to find Request.\n");
    fflush(stdout);
    return NULL;
}

int findClientIndex(int fd) {
    printf("### SEARCHING FOR REQUEST WITH SOME FD [%d]\n", fd);
    fflush(stdout);
    for (int i = 0; i < size; i++) {
        printf("### CURRENTLY CHECKING CLIENT FD [%d] AND SERVER FD [%d]\n", clientList[i]->clientFD, clientList[i]->serverFD);
        fflush(stdout);
        if (clientList[i]->clientFD == fd || clientList[i]->serverFD == fd) {
            printf("Successfully found Client/Server index!\n");
            fflush(stdout);
            return i;
        }
    }
    printf("Failed to find Request index.\n");
    fflush(stdout);
    return -1;
}

void removeClient(int clientFD, int serverFD) {
    deregisterFD(clientFD);
    deregisterFD(serverFD);
    removeFromClientList(clientFD);
}

void deregisterFD(int fd) {
    struct epoll_event localEV;
    localEV.events = EPOLLIN | EPOLLOUT | EPOLLET;
    localEV.data.fd = fd;
    if (epoll_ctl(epollFD, EPOLL_CTL_DEL, fd, &localEV) == -1) {
        perror("epoll_ctl - Removing client FD\n");
        exit(EXIT_FAILURE);
    }
    close(fd);
}

void removeFromClientList(int fd) {
    int index = findClientIndex(fd);

    if (index != -1) {
        free(findClient(fd));
        if (index < size - 1) {
            for (int i = index; i < size - 1; i++) {
                clientList[i] = clientList[i + 1];
            }
            memset(&clientList[size-1], 0, sizeof(request_info *));
        } else {
            memset(&clientList[index], 0, sizeof(request_info *));
        }
        size--;
        printf("Successfully removed Client from Client List!\n");
        fflush(stdout);
    }
}

void handle_client(request_info* clientRequest) {

    printf("########### ENTERED HANDLE_CLIENT ##############\n");
    printf("CLIENT FD: [%d]   REQUEST STATE: [%u]\n", clientRequest->clientFD, clientRequest->state);
    fflush(stdout);

    switch(clientRequest->state) {
        case READ_REQUEST: ; //#################################################### CASE 1
            char method[100];
            char hostname[100];
            char port[100];
            char path[100];
            char headers[MAX_CACHE_SIZE];
            char buffer[MAX_BUFFER_SIZE];

            wipeStrings(method, hostname, port, path, headers);
            wipeBuffer(buffer);
            while (!all_headers_received(clientRequest->clientRequest)) {
                int stringLength = read(clientRequest->clientFD, buffer, MAX_BUFFER_SIZE);
                if (stringLength < 0) {
                    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                        return;
                    } else {
                        fprintf(stderr,
                                "=======================================================\nFAILED AT CLIENT READ: %s\n===============================================================\n",
                                strerror(errno));
                        fflush(stderr);
                        removeClient(clientRequest->clientFD, clientRequest->serverFD);
                        return;
                    }
                }
                strncat(clientRequest->clientRequest, buffer, stringLength);
                clientRequest->bytesFromClient += stringLength;
                wipeBuffer(buffer);
            }

            if (parse_request(clientRequest->clientRequest, method, hostname, port, path, headers)) {
                printf("SUCCESSFULLY PARSED REQUEST!\n");
                fflush(stdout);
                clientRequest->bytesForServer = createRequest(clientRequest->serverRequest, method, hostname, port,
                                                              path);
                clientRequest->serverFD = openRemoteSFD(hostname, port);

                struct epoll_event localEV;
                //TODO MAKE SURE THIS TO BE WRITE
                localEV.events = EPOLLOUT | EPOLLET;
                localEV.data.fd = clientRequest->serverFD;
                printf("JUST MADE SERVER CONNECTION WITH SERVER FD [%d]\n", clientRequest->serverFD);
                fflush(stdout);
                if (epoll_ctl(epollFD, EPOLL_CTL_ADD, clientRequest->serverFD, &localEV) == -1) {
                    perror("epoll_ctl - ADDING SERVER FD\n");
                    exit(EXIT_FAILURE);
                }
            } else {
                printf("THIS IS WHERE THE SCRIPT FAILED TO PARSE THE REQUEST. NOT GOOD.\n");
                fflush(stdout);
            }
            clientRequest->state = SEND_REQUEST;
            break;

//#################################################### CASE 2
        case SEND_REQUEST:  ;
            int numSent = 0;

            while (numSent < clientRequest->bytesForServer) {
                int numToSend = clientRequest->bytesForServer - numSent;
                if (numToSend > MAX_BUFFER_SIZE) {
                    numToSend = MAX_BUFFER_SIZE;
                }
                int result = write(clientRequest->serverFD, clientRequest->serverRequest + numSent, numToSend);
                if (result < 0) {
                    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                        return;
                    } else {
                        fprintf(stderr,
                                "=======================================================\nFAILED AT SERVER WRITE: %s\n===============================================================\n",
                                strerror(errno));
                        fflush(stderr);
                        removeClient(clientRequest->clientFD, clientRequest->serverFD);
                        return;
                    }
                }
                numSent += result;
            }

            clientRequest->bytesToServer = numSent;

            //TODO MAKE SURE THIS IS RIGHT (READ) lol
            struct epoll_event localEV;
            localEV.events = EPOLLIN | EPOLLET;
            localEV.data.fd = clientRequest->serverFD;
            printf("CHANGING THE EPOLL TRACKING ON SERVER FD [%d]\n", clientRequest->serverFD);
            fflush(stdout);

            if (epoll_ctl(epollFD, EPOLL_CTL_MOD, clientRequest->serverFD, &localEV) == -1) {
                perror("epoll_ctl - MODIFYING SERVER FD\n");
                exit(EXIT_FAILURE);
            }
            clientRequest->state = READ_RESPONSE;
            break;

//################################################## CASE 3
        case READ_RESPONSE:
            wipeBuffer(buffer);
            int charsReceived = 0;

            while ((charsReceived = read(clientRequest->serverFD, buffer, MAX_BUFFER_SIZE)) > 0) {
                memcpy(clientRequest->serverResponse + clientRequest->bytesFromServer, buffer, charsReceived);
                wipeBuffer(buffer);
                clientRequest->bytesFromServer += charsReceived;
            }

           if (charsReceived < 0) {
                if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                    return;
                } else {
                    fprintf(stderr, "=======================================================\nFAILED AT SERVER READ: %s\n===============================================================\n",
                            strerror(errno));
                    fflush(stderr);
                    removeClient(clientRequest->clientFD, clientRequest->serverFD);
                    return;
                }
           }

            localEV.events = EPOLLOUT | EPOLLET;
            localEV.data.fd = clientRequest->clientFD;
            printf("CHANGING THE EPOLL TRACKING ON CLIENT FD [%d]\n", clientRequest->clientFD);
            fflush(stdout);

            if (epoll_ctl(epollFD, EPOLL_CTL_MOD, clientRequest->clientFD, &localEV) == -1) {
                perror("epoll_ctl - MODIFYING CLIENT FD\n");
                exit(EXIT_FAILURE);
            }
            
            clientRequest->state = SEND_RESPONSE;
            break;

//############################################## CASE 4
        case SEND_RESPONSE:
            printf("WRITING BACK TO THE CLIENT! NEARLY DONE.\n");
            fflush(stdout);

            printf("==========\nTHIS IS THE SERVER RESPONSE TO THE CLIENT\n%s\n===========\n", clientRequest->serverResponse);
            fflush(stdout);

            numSent = 0;

            while (numSent < clientRequest->bytesFromServer) {
                int numToSend = clientRequest->bytesFromServer - numSent;
                if (numToSend > MAX_BUFFER_SIZE) {
                    numToSend = MAX_BUFFER_SIZE;
                }
                int result = write(clientRequest->clientFD, clientRequest->serverResponse + numSent, numToSend);
                if (result < 0) {
                    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                        return;
                    } else {
                        fprintf(stdout,
                                "=======================================================\nFAILED AT CLIENT WRITE: %s\n===============================================================\n",
                                strerror(errno));
                        fflush(stdout);
                        removeClient(clientRequest->clientFD, clientRequest->serverFD);
                        return;
                    }
                }
                numSent += result;
            }

            clientRequest->bytesToClient = numSent;
            printf("DONE!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
            fflush(stdout);
            removeClient(clientRequest->clientFD, clientRequest->serverFD);
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

void handle_new_clients() {

    while(1) {
        request_info *newClient = calloc(1, sizeof(request_info));
        struct epoll_event localEV;
        struct sockaddr address;
        socklen_t addrLength = sizeof(address);

        int newFD = accept(sfd, &address, &addrLength);

        if (newFD < 0) {
            free(newClient);
            break;
        }

        newClient->clientFD = newFD;

        int flags = fcntl(newClient->clientFD, F_GETFL, 0);
        fcntl(newClient->clientFD, F_SETFL, flags | O_NONBLOCK);
        localEV.events = EPOLLIN | EPOLLET;
        localEV.data.fd = newClient->clientFD;
        if (epoll_ctl(epollFD, EPOLL_CTL_ADD, newClient->clientFD, &localEV) == -1) {
            perror("epoll_ctl - Adding client FD\n");
            exit(EXIT_FAILURE);
        }
        newClient->state = READ_REQUEST;
        printf("============== NEW FD: %d\n", newClient->clientFD);
        fflush(stdout);
        addToClientList(newClient);
    }

    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        return;
    } else {
        exit(EXIT_FAILURE);
    }

}

int createRequest(char *newRequest, char *method, char *hostname, char *port, char *path) {
    int length = 0;
    strcat(newRequest, method);
    length += strlen(method);
    strcat(newRequest, " /");
    length += 2;
    strcat(newRequest, path);
    length += strlen(path);
    strcat(newRequest, "HTTP/1.0\r\n");
    length += strlen("HTTP/1.0\r\n");
    strcat(newRequest, "Host: ");
    length += strlen("Host: ");
    strcat(newRequest, hostname);
    length += strlen(hostname);
    if (strcmp(port,"80")) {
        strcat(newRequest, ":");
        strcat(newRequest, port);
        length += strlen(port) + 1;
    }
    strcat(newRequest, "\r\n");
    length += strlen("\r\n");
    strcat(newRequest, user_agent_hdr);
    length += strlen(user_agent_hdr);
    strcat(newRequest, "\r\n");
    length += strlen("\r\n");
    strcat(newRequest, "Connection: close\r\n");
    length += strlen("Connection: close\r\n");
    strcat(newRequest, "Proxy-Connection: close\r\n\r\n\0");
    length += strlen("Proxy-Connection: close\r\n\r\n\0");
    return length;
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

            int flags = fcntl(serverFD, F_GETFL, 0);
            if (flags == -1) {
                return -1;
            }
            fcntl(serverFD, F_SETFL, flags | O_NONBLOCK);
            
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

int parse_request(char *request, char *method, char *hostname, char *port, char *path, char *headers) {
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
