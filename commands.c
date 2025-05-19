#include <stdio.h>

#define LLS_STRIP_PREFIX
#include "lls.h"

// @cmd printf
void *print(char *s, int i) {
	printf("%s\n\"%d\n", s, i);
	return NULL;
}

int main(int argc, char **argv) {
	const char* filename =  "./test-cases/errors.lls";
	Callables calls = {0};
	register_command(&calls, print);
	run_lls_file(filename, &calls);

	return 0;
}
