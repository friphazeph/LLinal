#include <stdio.h>

#define LLS_STRIP_PREFIX
#include "lls.h"

declare_command(print, ARG_STR, ARG_INT) {
	char *s = arg_str(0);
	int i = arg_int(1);
	printf("%s\n%d\n", s, i);
	return NULL;
}

declare_command_custom_name("!printf", print2, ARG_STR, ARG_FLT) {
	char *s = arg_str(0);
	float i = arg_flt(1);
	printf("%s\n%f\n", s, i);
	return NULL;
}

int main(int argc, char **argv) {
	// const char* filename =  argv[1];
	const char* filename =  "./test-cases/errors.lls";
	Callables calls = {0};
	register_command(&calls, print);
	register_command(&calls, print2);
	run_lls_file(filename, &calls);

	return 0;
}
