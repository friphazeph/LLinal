#include "../lln.h"
#include <stdio.h>

// @cmd !printf <- you can use a reserved name that way
void *print(char *s, int i) {
	printf("%s, %d\n", s, i);
	return NULL; // functionally optional, avoids warnings
}
