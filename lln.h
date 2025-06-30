#ifndef __LLN_H
#define __LLN_H

#ifndef LLN_DEF_CAP
#define LLN_DEF_CAP 16
#endif // LLN_DEF_CAP

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

// TODO: remove unused
#ifdef LLN_STRIP_PREFIX
#define StringBuilder lln_StringBuilder
#define sb_append lln_sb_append
#define sb_append_strn lln_sb_append_strn
#define sb_append_cstr lln_sb_append_cstr
#define sb_term lln_sb_term
#define da_append lln_da_append
#define read_whole_file lln_read_whole_file
#define sb_new_cstr lln_sb_new_cstr
#define sb_new_cstrn lln_sb_new_cstrn
#define run_lln_file lln_run_lln_file
#define Callable lln_Callable
#define Callables lln_Callables
#define ArgTypes lln_ArgTypes
#define ArgType lln_ArgType
#define Args lln_Args
#define Arg lln_Arg
#define ArgValue lln_ArgValue
#define CommandFnPtr lln_CommandFnPtr
#define ARGTYPE_STR LLN_ARGTYPE_STR
#define Args lln_Args
#define declare_command LLN_declare_command
#define register_command LLN_register_command
#define arg_str LLN_arg_str
#define arg_int LLN_arg_int
#define arg_flt LLN_arg_flt
#define arg_bool LLN_arg_bool
#define declare_command_custom_name LLN_declare_command_custom_name
#endif // LLN_STRIP_PREFIX

// Generic dynamic arrays for structs with such fields
// typedef struct {
//     ...
//     [type] *items;
//     size_t count;
//     size_t capacity;
//     ...
// } [_da_name];
#define lln_da_append(da, it) __lln_da_append_helper((void **)&(da)->items, &((da)->count), &((da)->capacity), &(it), sizeof((da)->items[0]))
static inline int __lln_da_append_helper(void **its, size_t *cnt, size_t *cap, void *it, size_t it_size) {
	if (*cnt >= *cap) {
		if (*cap == 0) *cap = LLN_DEF_CAP;
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
} lln_StringBuilder;

// returns 0 if success, -1 if failure
int lln_sb_append(lln_StringBuilder *sb, char c);

// NULL-terminate string
static inline int lln_sb_term(lln_StringBuilder *sb) {
	return lln_sb_append(sb, '\0');
}

// append n characters from s
int lln_sb_append_strn(lln_StringBuilder *sb, const char *s, size_t n);

int lln_sb_append_cstr(lln_StringBuilder *sb, const char *s);

// malloc cstr with contents of sb
char *lln_sb_new_cstr(lln_StringBuilder *sb);

// malloc cstr with contents of sb up to n chars
char *lln_sb_new_cstrn(lln_StringBuilder *sb, size_t n);

// This appends the file to the current sb if
// it isn't empty. To reset it do sb.len = 0 before
const char *lln_read_whole_file(lln_StringBuilder *sb, const char *filename);

typedef enum {
	ARG_INT,
	ARG_FLT,
	ARG_STR,
	ARG_BOOL,
	ARG_COUNT
} lln_ArgType;
#define ARG_INVALID ((lln_ArgType)-1)
_Static_assert(ARG_COUNT < 127, "Too many ArgTypes! -1 may conflict with a real enum value.");

// [!!!] should be modified in implementation
// if the enum above is
extern const char* LLN_ARGTYPE_STR[];

typedef struct {
	lln_ArgType *items;
	size_t count;
	size_t capacity;
} lln_ArgTypes;

typedef union {
	int i;
	float f;
	bool b;
	char *s;
} lln_ArgValue;

typedef struct {
	lln_ArgType type;
	lln_ArgValue value;
} lln_Arg;

typedef struct {
	lln_Arg *items;
	size_t count;
	size_t capacity;
} lln_Args;

typedef void *(*lln_CommandFnPtr)(lln_Args);

typedef struct {
	const char *name;
	lln_ArgTypes signature;

	lln_CommandFnPtr fnptr;
} lln_Callable;

typedef struct {
	lln_Callable *items;
	size_t count;
	size_t capacity;
	void (* pre)(void);
	void (* post)(void);
} lln_Callables;


void lln_run_lln_file(const char *filename, const lln_Callables *c);

#define LLN_declare_command(name, ...)                                     \
	LLN_declare_command_custom_name("!" #name, name, __VA_ARGS__)
#define LLN_declare_command_custom_name(cmdname, fnname, ...)              \
	static const lln_ArgType __LLN_##fnname##_sign[] = {__VA_ARGS__};      \
	void *fnname(lln_Args __LLN_args);                                     \
	static lln_Callable __LLN_##fnname##_call = (lln_Callable) {           \
        .name = cmdname,                                                   \
		.signature = (lln_ArgTypes) {                                      \
			.items = (lln_ArgType *) &__LLN_##fnname##_sign[0],            \
			.count = sizeof(__LLN_##fnname##_sign)/sizeof(lln_ArgType),    \
			.capacity = sizeof(__LLN_##fnname##_sign)/sizeof(lln_ArgType), \
		},                                                                 \
		.fnptr = fnname,                                                   \
	};                                                                     \
	void *fnname(lln_Args __LLN_args)

#define LLN_declare_pre  \
	void __LLN_pre(void)
	
#define LLN_declare_post \
	void __LLN_post(void)

#define LLN_register_pre(callables) \
	callables->pre = __LLN_pre;
#define LLN_register_post(callables) \
	callables->post = __LLN_post;
#define LLN_register_command(callables, fnname) \
	assert(__LLN_##fnname##_call.name[0] == '!' && "ERROR: command names must start with '!'"); \
	lln_da_append(callables, __LLN_##fnname##_call)
#define LLN_arg_str(i)           \
	(assert(__LLN_args.items[i].type == ARG_STR), __LLN_args.items[i].value.s)
#define LLN_arg_int(j)           \
	(assert(__LLN_args.items[j].type == ARG_INT), __LLN_args.items[j].value.i)
#define LLN_arg_flt(i)           \
	(assert(__LLN_args.items[i].type == ARG_FLT), __LLN_args.items[i].value.f)
#define LLN_arg_bool(i)          \
	(assert(__LLN_args.items[i].type == ARG_BOOL), __LLN_args.items[i].value.b)


void __lln_preproc_register_commands(void);
#ifndef __LLN_PREPROCESSED_FILE

static inline void __lln_noop_run(const char* filename) {
    (void)filename;
    fprintf(stderr, "Warning: LLinal run called from non-preprocessed file, ignoring...\n");
}
#define lln_run(filename) self_register_commands(); __lln_noop_run(filename)
#define self_register_commands() ((void) 0)

#else // __LLN_PREPROCESSED_FILE 
	  
lln_Callables __lln_preproc_callables;
#define lln_run(filename) self_register_commands();\
	lln_run_lln_file(filename, &__lln_preproc_callables)
#define self_register_commands() __lln_preproc_register_commands()

#endif // __LLN_PREPROCESSED_FILE

#endif // __LLN_H
