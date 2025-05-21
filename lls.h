#ifndef __LLS_H
#define __LLS_H

#ifndef LLS_DEF_CAP
#define LLS_DEF_CAP 1024
#endif // LLS_DEF_CAP

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef LLS_STRIP_PREFIX
#define StringBuilder lls_StringBuilder
#define sb_append lls_sb_append
#define sb_append_strn lls_sb_append_strn
#define sb_append_cstr lls_sb_append_cstr
#define sb_term lls_sb_term
#define da_append lls_da_append
#define read_whole_file lls_read_whole_file
#define sb_new_cstr lls_sb_new_cstr
#define sb_new_cstrn lls_sb_new_cstrn
#define run_lls_file lls_run_lls_file
#define Callable lls_Callable
#define Callables lls_Callables
#define ArgTypes lls_ArgTypes
#define ArgType lls_ArgType
#define Args lls_Args
#define Arg lls_Arg
#define ArgValue lls_ArgValue
#define CommandFnPtr lls_CommandFnPtr
#define ARGTYPE_STR LLS_ARGTYPE_STR
#define Args lls_Args
#define declare_command LLS_declare_command
#define register_command LLS_register_command
#define arg_str LLS_arg_str
#define arg_int LLS_arg_int
#define arg_flt LLS_arg_flt
#define arg_bool LLS_arg_bool
#define declare_command_custom_name LLS_declare_command_custom_name
#endif // LLS_STRIP_PREFIX

// Generic dynamic arrays for structs with such fields
// typedef struct {
//     ...
//     [type] *items;
//     size_t count;
//     size_t capacity;
//     ...
// } [_da_name];
#define lls_da_append(da, it) __lls_da_append_helper((void **)&(da)->items, &((da)->count), &((da)->capacity), &(it), sizeof((da)->items[0]))
static inline int __lls_da_append_helper(void **its, size_t *cnt, size_t *cap, void *it, size_t it_size) {
	if (*cnt >= *cap) {
		if (*cap == 0) *cap = LLS_DEF_CAP;
		else *cap *= 2;
		void *temp = realloc(*its, *cap * it_size);
		if (!temp) return -1;
		*its = temp;
	}
	memcpy((char *)(*its) + (*cnt * it_size), it, it_size);
	(*cnt)++;
	return 0;
}

typedef struct {
	char *content;
	size_t len;
	size_t cap;
} lls_StringBuilder;

// returns 0 if success, -1 if failure
int lls_sb_append(lls_StringBuilder *sb, char c);

// NULL-terminate string
static inline int lls_sb_term(lls_StringBuilder *sb) {
	return lls_sb_append(sb, '\0');
}

// append n characters from s
int lls_sb_append_strn(lls_StringBuilder *sb, const char *s, size_t n);

int lls_sb_append_cstr(lls_StringBuilder *sb, const char *s);

// malloc cstr with contents of sb
char *lls_sb_new_cstr(lls_StringBuilder *sb);

// malloc cstr with contents of sb up to n chars
char *lls_sb_new_cstrn(lls_StringBuilder *sb, size_t n);

// This appends the file to the current sb if
// it isn't empty. To reset it do sb.len = 0 before
const char *lls_read_whole_file(lls_StringBuilder *sb, const char *filename);

typedef enum {
	ARG_INT,
	ARG_FLT,
	ARG_STR,
	ARG_BOOL,
	ARG_COUNT
} lls_ArgType;
#define ARG_INVALID ((lls_ArgType)-1)
_Static_assert(ARG_COUNT < 127, "Too many ArgTypes! -1 may conflict with a real enum value.");

// [!!!] should be modified in implementation
// if the enum above is
extern const char* LLS_ARGTYPE_STR[];

typedef struct {
	lls_ArgType *items;
	size_t count;
	size_t capacity;
} lls_ArgTypes;

typedef union {
	int i;
	float f;
	bool b;
	char *s;
} lls_ArgValue;

typedef struct {
	lls_ArgType type;
	lls_ArgValue value;
} lls_Arg;

typedef struct {
	lls_Arg *items;
	size_t count;
	size_t capacity;
} lls_Args;

typedef void *(*lls_CommandFnPtr)(lls_Args);

typedef struct {
	const char *name;
	lls_ArgTypes signature;

	lls_CommandFnPtr fnptr;
} lls_Callable;

typedef struct {
	lls_Callable *items;
	size_t count;
	size_t capacity;
} lls_Callables;


void lls_run_lls_file(const char *filename, const lls_Callables *c);
void lls_preproc_and_rerun_file(const char *filename);

#define LLS_declare_command(name, ...)                                     \
	LLS_declare_command_custom_name("!" #name, name, __VA_ARGS__)
#define LLS_declare_command_custom_name(cmdname, fnname, ...)              \
	static const lls_ArgType __LLS_##fnname##_sign[] = {__VA_ARGS__};      \
	void *fnname(lls_Args __LLS_args);                                     \
	static lls_Callable __LLS_##fnname##_call = (lls_Callable) {           \
        .name = cmdname,                                                   \
		.signature = (lls_ArgTypes) {                                      \
			.items = (lls_ArgType *) &__LLS_##fnname##_sign[0],            \
			.count = sizeof(__LLS_##fnname##_sign)/sizeof(lls_ArgType),    \
			.capacity = sizeof(__LLS_##fnname##_sign)/sizeof(lls_ArgType), \
		},                                                                 \
		.fnptr = fnname,                                                   \
	};                                                                     \
	void *fnname(lls_Args __LLS_args)
#define LLS_register_command(callables, fnname) \
	assert(__LLS_##fnname##_call.name[0] == '!' && "command names must start with '!'"); \
	lls_da_append(callables, __LLS_##fnname##_call)
#define LLS_arg_str(i)           \
	__LLS_args.items[i].value.s; \
	assert(__LLS_args.items[i].type == ARG_STR) 
#define LLS_arg_int(j)           \
	__LLS_args.items[j].value.i; \
	assert(__LLS_args.items[j].type == ARG_INT)
#define LLS_arg_flt(i)           \
	__LLS_args.items[i].value.f; \
	assert(__LLS_args.items[i].type == ARG_FLT)
#define LLS_arg_bool(i)          \
	__LLS_args.items[i].value.b; \
	assert(__LLS_args.items[i].type == ARG_BOOL)


#ifndef __LLS_PREPROCESSED_FILE
#define self_register_commands() lls_preproc_and_rerun_file(__FILE__)
#define lls_run(filename) self_register_commands(); exit(0)
#else // __LLS_PREPROCESSED_FILE 
lls_Callables __lls_preproc_callables;
void __lls_preproc_register_commands(void);
#define lls_run(filename) self_register_commands();\
	lls_run_lls_file(filename, &__lls_preproc_callables)
#define self_register_commands() __lls_preproc_register_commands()
#endif // __LLS_PREPROCESSED_FILE

#endif // __LLS_H
