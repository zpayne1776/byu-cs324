#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#define BUFSIZE 8

int main() {
	unsigned char buf[BUFSIZE];
	unsigned short val = htons(0xabcd);
	int i = 0;
	bzero(buf, BUFSIZE);

	memcpy(&buf[6], &val, 2);
	for (i = 0; i < BUFSIZE; i++) {
		printf("%x ", buf[i]);
	}
	printf("\n");
}
