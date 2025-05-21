#include <stdio.h>

#include "lls.h"

// @cmd !printf
void *print(const char *s, int i) {
	printf("%s\n%d\n", s, i);
}

int main(int argc, char **argv) {
	lls_run("./test-cases/hello.lls");
	return 0;
}
