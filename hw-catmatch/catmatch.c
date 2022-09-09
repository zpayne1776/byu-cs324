//### THIS WAS MADE BY ZACH PAYNE ###
//### PART ONE ANSWERS ###
//
//ManHunt Questions
//1. Sections 1, 2, and 3, respectively
//2. Sections 1 and 2
//3. Sections 2 and 3
//4. Section 2
//5. #include <sys/types.h>
//       #include <sys/stat.h>
//       #include <fcntl.h>
//6. Sections 2 and 7
//7. SO_ACCEPTCONN
//8. 3 pages. The last commit message shows it was meant to be -k, which answered the uncertainty I had between it and -f.
//9. Null-terminated
//10. An integer greater than zero
//
//### PART TWO ###
//### I completed the TMUX exercise from Part 2 ###

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv) {
	fprintf( stderr, "%ld\n\n", (long)getpid() );

	char* environmentVar = getenv("CATMATCH_PATTERN");
	char stringBuffer[100];

	int isPresent = 0;

	FILE *fileHandle = fopen(argv[1], "r");

	if (fileHandle == NULL) {
		printf("File failed to open\n");
	} else {

	while (fgets(stringBuffer, 100, fileHandle)) {
		if (environmentVar == NULL) {
			fprintf(stdout, "0 %s", stringBuffer);
		} else if (strstr(stringBuffer, environmentVar) != NULL) {
			fprintf(stdout, "1 %s", stringBuffer);
		} else {
			fprintf(stdout, "0 %s", stringBuffer);
		}
	}

	}
}
