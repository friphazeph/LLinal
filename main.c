#include <stdio.h>

#define LLS_STRIP_PREFIX
#include "lls.h"

// @cmd !printf
void *print(const char *s, int i) {
	printf("%s\n%d\n", s, i);
}

int main(int argc, char **argv) {
	const char* filename =  "./test-cases/hello.lls";
	self_register_commands();
	lls_run(filename);

	return 0;
}
