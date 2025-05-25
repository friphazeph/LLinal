#include <stdio.h>

#include "lls.h"

// @cmd !print
void *print(char *str, int i) {
	printf("%s %d\n", str, i);
}

// @pre
void test() {
	printf("AAAAAAAH\n");
}

// @post
void test2() {
	printf("OOOH\n");
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "ERROR: No filename was provided.\n");
		fprintf(stderr, "INFO: Usage: `%s [file.lls]`\n", argv[0]);
		exit(1);
	}
	char *lls_file = argv[1];
	lls_run(lls_file);
	return 0;
}
