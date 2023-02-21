// Replace PUT_USERID_HERE with your actual BYU CS user id, which you can find
// by running `id -u` on a CS lab machine.
#define USERID 1823603856
#define BUFFERSIZE 64

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>


int verbose = 0;

struct addrinfo hints;
struct addrinfo *result, *rp;

struct sockaddr_in ipv4_local;
struct sockaddr_in ipv4_remote;
struct sockaddr_in6 ipv6_local;
struct sockaddr_in6 ipv6_remote;

socklen_t addrlen;

int af;
int sfd, s, j;

char *server;
char *charPort;
int port;
int level;
int seed;
int treasureIndex = 0;

void print_bytes(unsigned char *bytes, int byteslen);
void connectNewPort(unsigned short remotePort);
void connectNewPortAndFamily(unsigned short remotePort);
void bindNewPort(unsigned short localPort);
void printJunk();
void reconnectPort();
void rebindPort();
unsigned int getRemotePort();

int main(int argc, char *argv[]) {
	//if (argc != 5) {
	//	printf("Incorrect number of Arguments. Expected usage is ./treasure_hunter server port level seed");
	//	return 1;
	//}

	int z = 0;
	
	if (strcmp(argv[1], "-v") == 0) {
		z = 1;
		verbose = 1;
	}

	server = argv[1+z];
	charPort = argv[2+z];
	port = atoi(charPort);
	level = atoi(argv[3+z]);
	seed = atoi(argv[4+z]);

	unsigned char buffer[BUFFERSIZE];
	bzero(buffer, BUFFERSIZE);

	unsigned char request[BUFFERSIZE];
	bzero(request, BUFFERSIZE);

	request[0] = 0;
	request[1] = level;
	unsigned long userID = htonl(0x6cb1fc90);
	memcpy(&request[2], &userID, 4);
	unsigned short hexSeed = htons(seed);
	memcpy(&request[6], &hexSeed, 2);

	af = AF_INET;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = af;
	hints.ai_socktype = SOCK_DGRAM;

	s = getaddrinfo(server, charPort, &hints, &result);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

		if (sfd == -1) {
			continue;
		}
		if (connect (sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
			ipv4_remote = *(struct sockaddr_in *)rp->ai_addr;
			addrlen = sizeof(struct sockaddr_in);
			getsockname(sfd, (struct sockaddr *)&ipv4_local, &addrlen);
			break;
		}

		close(sfd);
	}

	if (rp == NULL) {
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(result);

	if (send(sfd, request,  8, 0) != 8) {
		fprintf(stderr, "partial/failed write\n");
		exit(EXIT_FAILURE);
	}

	printJunk();

	j = recv(sfd, buffer, 64, 0);

	int messageSize = buffer[0];
	unsigned char treasureBuffer[5000];
	bzero(treasureBuffer, 5000);

	while (messageSize != 0) {

		int opCode = buffer[messageSize+1];
		unsigned char opParam[2];
		bzero(opParam, 2);
		opParam[0] = buffer[messageSize+2];
		opParam[1] = buffer[messageSize+3];

		if (verbose) {
			printf("opCode: %d\n", opCode);

			for (int i = 0; i < 2; i++) {
				printf("\topParam [%02X]\n", opParam[i]);
			}
		}

		unsigned char nonce[4];
		bzero(nonce, 4);
		for(int i = 0; i < 4; i++) {
			nonce[i] = buffer[messageSize+i+4];
		}

		if (verbose) {
			for (int i = 0; i < 4; i++) {
				printf("\t\tnonce [%02X]\n", nonce[i]);
			}
		}
		
		unsigned int nonceNum = (uint32_t)nonce[0] << 24 |
			(uint32_t)nonce[1] << 16 |
			(uint32_t)nonce[2] << 8  |
			(uint32_t)nonce[3];


		for (int k = 1; k <= messageSize; k++) {
			treasureBuffer[treasureIndex] = buffer[k];
			treasureIndex++;	
		}

		//=======================================================================================//
		//------------------------------------- OP CODE STUFF -----------------------------------//
		//=======================================================================================/
		
		if (opCode == 1) {
			unsigned int newRemotePort = (uint16_t)opParam[0] << 8 |
				(uint16_t)opParam[1];
			connectNewPort(newRemotePort);
		} else if (opCode == 2) {
			unsigned int newLocalPort = (uint16_t)opParam[0] << 8 |
				(uint16_t)opParam[1];
			bindNewPort(newLocalPort);

			reconnectPort();


		} else if (opCode == 3) {
			unsigned int iterationsToRead = (uint16_t)opParam[0] << 8 |
				(uint16_t)opParam[1];

			unsigned int oldPort = getRemotePort();

			if (verbose) {
				if (af == AF_INET) {
					printf("Rebinding to %d\n", ipv4_local.sin_port);
				} else {
					printf("Rebinding to %d\n", ipv6_local.sin6_port);
				}
				fflush(stdout);
			}
			
			close(sfd);
			rebindPort();

			int result = 0;
			struct sockaddr infoCollector;
			socklen_t infoLength = sizeof(infoCollector);

			for (int i = 0; i < iterationsToRead; i++) {
				bzero(buffer, BUFFERSIZE);
				if (verbose) {
					printf("Starting #%d read in Level 3 loop\n Here's the junk: ", i+1);
					fflush(stdout);

					printJunk();

					char currentHost[64];
					char currentPort[32];
					getnameinfo((struct sockaddr *)&ipv4_local, sizeof(struct sockaddr_in), currentHost, 64, currentPort, 32, 0);

					printf("Current local host is %s and current local port is %s\n", currentHost, currentPort);
					fflush(stdout);

					bzero(currentHost, 64);
					bzero(currentPort, 32);

					getnameinfo((struct sockaddr *)&ipv4_remote, sizeof(struct sockaddr_in), currentHost, 64, currentPort, 32, 0);

					printf("Current remote host is %s and current remote port is %s\n", currentHost, currentPort);
					fflush(stdout);
				}

				if (verbose) {
					printf("My current socket is :%d\n", sfd);
					fflush(stdout);
				}

				int check = recvfrom(sfd, buffer, BUFFERSIZE, 0, &infoCollector, &infoLength);

				if (verbose) {
					printf("%d of %d total recvfrom iterations required\n", i+1, iterationsToRead);
					printf("\t Recvfrom Sanity Check Value: %d\n", check);
					fflush(stdout);
				}

				if (infoCollector.sa_family == AF_INET) {
					
					if (verbose) {
						printf("\tIN AF_INET - casting sockaddr_storage as sockaddr_in\n");
						fflush(stdout);
					}

					struct sockaddr_in *sin = (struct sockaddr_in *)&infoCollector;
					result += ntohs(sin->sin_port);
					
					if (verbose) {
						printf("\t\tThe Port to be added is %d\n", ntohs(sin->sin_port));
						printf("\t\tResult of summing ports is currently: %d\n", result);
						fflush(stdout);
					}

				} else {
					if (verbose) {
						printf("\tIN AF_INET6 - casting sockaddr_storage as sockaddr_in6\n");
						fflush(stdout);
					}

					struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&infoCollector;
					result += ntohs(sin6->sin6_port);
					
					if (verbose) {
						printf("\t\tThe Port to be added is %d\n", ntohs(sin6->sin6_port));
						printf("\t\tResult of summing ports is currently: %d\n", result);
						fflush(stdout);
					}
				}
			}
			if (verbose) {
				printf("Out of recvfrom loop\n");
				fflush(stdout);
			}

			connectNewPort(oldPort);
			nonceNum = result;
		} else if (opCode == 4) {
			unsigned int newRemotePort = (uint16_t)opParam[0] << 8 | 
				(uint16_t)opParam[1];
			close(sfd);
			connectNewPortAndFamily(newRemotePort);
		}
	
		if (verbose) {
			printf("Nonce: %d\n", nonceNum);
			fflush(stdout);
		}

		nonceNum++;

		if (verbose) {
			printf("Nonce plus one: %d\n", nonceNum);
			fflush(stdout);
		}

		unsigned int hexNonce = htonl(nonceNum);
	
		bzero(request, BUFFERSIZE);
		memcpy(&request[0], &hexNonce, 4);

		if (verbose) {
			print_bytes(request,4);
		}

		if (send(sfd, request, 4, 0) != 4) {
			fprintf(stderr, "partial/failed write\n");
			exit(EXIT_FAILURE);
		}
		
		printJunk();

		bzero(buffer, BUFFERSIZE);
		j=recv(sfd, buffer, BUFFERSIZE, 0);
		
		if (verbose) {
			printf("newBuffer [%s]\n",buffer);
			print_bytes(buffer, j);
			printf("Bottom of Treasure loop\n");
		}

		messageSize = buffer[0];

	}
	if (verbose) {
		printf("Out of Treasure loop\n");
		fflush(stdout);
	}

	printf("%s\n", treasureBuffer);
	fflush(stdout);
	return 0;
}

void reconnectPort() {
	if (af == AF_INET) {
		connectNewPort(ntohs(ipv4_remote.sin_port));
	} else {
		connectNewPort(ntohs(ipv6_remote.sin6_port));
	}
}

void rebindPort() {
	if (af == AF_INET) {
		bindNewPort(ntohs(ipv4_local.sin_port));
	} else {
		bindNewPort(ntohs(ipv6_local.sin6_port));
	}
}

unsigned int getRemotePort() {
	unsigned int toReturn;

	if (af == AF_INET) {
		toReturn = ntohs(ipv4_remote.sin_port);
		return toReturn;
	} else {
		toReturn = ntohs(ipv6_remote.sin6_port);
		return toReturn;
	}
}

void connectNewPort(unsigned short remotePort) {
	if (verbose) {
		printf("entered connectnewport\n");
		fflush(stdout);
	}


	if (verbose) {
		printf("Port num: %d\n", htons(remotePort));
		fflush(stdout);
	}

	if (af == AF_INET) {
		ipv4_remote.sin_port = htons(remotePort);
		
		if (connect(sfd, (struct sockaddr *)&ipv4_remote, sizeof(struct sockaddr_in)) < 0) {
			perror("connect()");
		}
		addrlen = sizeof(struct sockaddr_in);
		getsockname(sfd, (struct sockaddr *)&ipv4_local, &addrlen);
	} else {
		ipv6_remote.sin6_port = htons(remotePort);

		if (connect(sfd, (struct sockaddr *)&ipv6_remote, sizeof(struct sockaddr_in6)) < 0) {
			perror("connect()");
		}
		addrlen = sizeof(struct sockaddr_in6);
		getsockname(sfd, (struct sockaddr *)&ipv6_local, &addrlen);
	}

	if (verbose) {
		printf("Leaving connectnewport\n");
		fflush(stdout);
	}
}

void connectNewPortAndFamily(unsigned short remotePort) {

	if (verbose) {
		printf("Port num: %d\n", htons(remotePort));
		fflush(stdout);
	}

	memset(&hints, 0, sizeof(struct addrinfo));
	if (af == AF_INET) {
		hints.ai_family = AF_INET6;
		af = AF_INET6;
	} else {
		hints.ai_family = AF_INET;
		af = AF_INET;
	}
	hints.ai_socktype = SOCK_DGRAM;

	char newCharPort[10];
	sprintf(newCharPort, "%u", remotePort);

	if (verbose) {
		printf("Char Port num: %s\n", newCharPort);
		fflush(stdout);
	}

	s = getaddrinfo(server, newCharPort, &hints, &result);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

		if (sfd == -1) {
		      	continue;
		}
		
		if (connect (sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
			if (af == AF_INET) {
				ipv4_remote = *(struct sockaddr_in *)rp->ai_addr;
				addrlen = sizeof(struct sockaddr_in);
				getsockname(sfd, (struct sockaddr *)&ipv4_local, &addrlen);
				break;
			} else {
				ipv6_remote = *(struct sockaddr_in6 *)rp->ai_addr;
				addrlen = sizeof(struct sockaddr_in6);
				getsockname(sfd, (struct sockaddr *)&ipv6_local, &addrlen);
				break;
			}
	        }

                close(sfd);
        }

	if (rp == NULL) {
                fprintf(stderr, "Could not connect\n");
                exit(EXIT_FAILURE);
        }

        freeaddrinfo(result);		
}

void printJunk() {

	if (af == AF_INET) {
	       	char localAddress[INET_ADDRSTRLEN];
		char remoteAddress[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &ipv4_local.sin_addr, localAddress, INET_ADDRSTRLEN);
		inet_ntop(AF_INET, &ipv4_remote.sin_addr, remoteAddress, INET_ADDRSTRLEN);
		fprintf(stderr, "%s:%d -> %s:%d\n", localAddress,
				ipv4_local.sin_port,
				remoteAddress,
				ipv4_remote.sin_port);
	} else {
		char localAddress[INET6_ADDRSTRLEN];
		char remoteAddress[INET6_ADDRSTRLEN];
		inet_ntop(AF_INET6, &ipv6_local.sin6_addr, localAddress, INET6_ADDRSTRLEN);
		inet_ntop(AF_INET6, &ipv6_remote.sin6_addr, remoteAddress, INET6_ADDRSTRLEN);
		fprintf(stderr, "%s::%d -> %s::%d\n", localAddress,
				ipv6_local.sin6_port,
		   		remoteAddress,
				ipv6_remote.sin6_port);
	}

}

void bindNewPort(unsigned short newLocalPort) {
	// CLOSE OLD SOCKET
	
	if (verbose) {
		printf("\tClosing socket\n");
		fflush(stdout);
	}

	//close(sfd);


	// MAKE SOCKET	
	
	if (verbose) {
		printf("\tRemaking the socket\n");
		fflush(stdout);
	}
	
	if (af == AF_INET) {
		if ((sfd = socket(AF_INET, SOCK_DGRAM, 0)) < -1) {
			perror("socket()");
		}
	} else {
		if ((sfd = socket(AF_INET6, SOCK_DGRAM, 0)) < -1) {
			perror("socket()");
		}
	}

	// BIND SOCKET

	if (verbose) {
		printf("\tBinding the socket to newLocalPort: %d\n", htons(newLocalPort));
		fflush(stdout);
	}

	if (af == AF_INET) {
		ipv4_local.sin_family = AF_INET;
		ipv4_local.sin_port = htons(newLocalPort);
		ipv4_local.sin_addr.s_addr = 0;
		if (bind(sfd, (struct sockaddr *)&ipv4_local, sizeof(struct sockaddr_in)) < 0) {
			perror("bind()");
		}
	} else {
		ipv6_local.sin6_family = AF_INET6;
		ipv6_local.sin6_port = htons(newLocalPort);
		bzero(ipv6_local.sin6_addr.s6_addr, 16);
		if (bind(sfd, (struct sockaddr *)&ipv6_local, sizeof(struct sockaddr_in6)) < 0) {
			perror("bind()");
		}
	}

	if (verbose) {
		printf("My current socket (within Bind) is :%d\n", sfd);
		fflush(stdout);
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
