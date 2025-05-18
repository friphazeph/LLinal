#ifndef __LLS_H
#define __LLS_H

#ifndef LLS_DEF_CAP
#define LLS_DEF_CAP 1024
#endif // LLS_DEF_CAP

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	char *content;
	size_t len;
	size_t cap;
} lls_StringBuilder;

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

#endif // __LLS_H
