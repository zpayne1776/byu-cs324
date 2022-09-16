#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<strings.h>

int main(int argc, char *argv[]) {
	int pid;

	printf("Starting program; process has pid %d\n", getpid());

	int rw[2];
	pipe(rw);

	FILE *fileHandle = fopen("fork-output.txt", "w");
	fprintf(fileHandle, "BEFORE FORK\n");
	fflush(fileHandle);

	if ((pid = fork()) < 0) {
		fprintf(stderr, "Could not fork()");
		exit(1);
	}

	/* BEGIN SECTION A */

	fprintf(fileHandle, "SECTION A\n");
	fflush(fileHandle);
	printf("Section A;  pid %d\n", getpid());
	//sleep(30);

	/* END SECTION A */
	if (pid == 0) {
		/* BEGIN SECTION B */

		close(rw[0]);
		char message[] = "hello from Section B\n";
		write(rw[1], message, 22);

		fprintf(fileHandle, "SECTION B\n");
		fflush(fileHandle);
		printf("Section B\n");
		//sleep(30);
		//sleep(30);
		//printf("Section B done sleeping\n");


	        char *newenviron[] = { NULL };

	        printf("Program \"%s\" has pid %d. Sleeping.\n", argv[0], getpid());
	        //sleep(30);

	        if (argc <= 1) {
	                printf("No program to exec.  Exiting...\n");
	                exit(0);
	        }

	        printf("Running exec of \"%s\"\n", argv[1]);
		dup2(1,rw[1]);
		execve(argv[1], &argv[1], newenviron);
	        printf("End of program \"%s\".\n", argv[0]);


		exit(0);

		/* END SECTION B */
	} else {
		/* BEGIN SECTION C */

		close(rw[1]);
		char recievedMessage[22];
		read(rw[0], recievedMessage, 22);
		printf("%s", recievedMessage);

		fprintf(fileHandle, "SECTION C\n");
		fflush(fileHandle);
		printf("Section C\n");
		//sleep(30);
		wait(NULL);
		//sleep(30);
		//printf("Section C done sleeping\n");

		exit(0);

		/* END SECTION C */
	}
	/* BEGIN SECTION D */

	fprintf(fileHandle, "SECTION D\n");
	fflush(fileHandle);
	printf("Section D\n");
	sleep(30);

	/* END SECTION D */
}

