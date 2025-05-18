#include "lls.h"

int lls_sb_append(lls_StringBuilder *sb, char c) {
	if (sb->len >= sb->cap) {
		if (sb->cap == 0) sb->cap = LLS_DEF_CAP;
		else sb->cap *= 2;
		void *temp = realloc(sb->content, sb->cap * sizeof(char));
		if (!temp) return -1;
		sb->content = temp;
	}
	sb->content[sb->len++] = c;
	return 0;
}

int lls_sb_append_strn(lls_StringBuilder *sb, const char *s, size_t n) {
	if (sb->len + n > sb->cap) {
		if (sb->cap == 0) sb->cap = LLS_DEF_CAP;
		while (sb->len + n > sb->cap) sb->cap *= 2;
		void *temp = realloc(sb->content, sb->cap * sizeof(char));
		if (!temp) return -1;
		sb->content = temp;
	}
	strncpy(sb->content + sb->len, s, n);
	sb->len += n;
	return 0;
}

int lls_sb_append_cstr(lls_StringBuilder *sb, const char *s) {
	return lls_sb_append_strn(sb, s, strlen(s));
}

char *lls_sb_new_cstr(lls_StringBuilder *sb) {
	char *cstr = malloc(sb->len+1);
	if (cstr == NULL) return NULL;
	strcpy(cstr, sb->content);
	return cstr;
}

char *lls_sb_new_cstrn(lls_StringBuilder *sb, size_t n) {
	char *cstr = malloc(n+1);
	if (cstr == NULL) return NULL;
	strcpy(cstr, sb->content);
	cstr[n] = '\0';
	return cstr;
}

// This appends the file to the current sb, if
// it isn't empty. To reset it do sb.len = 0 before
const char *lls_read_whole_file(lls_StringBuilder *sb, const char *filename) {
	FILE *fd = fopen(filename, "r");
	if (!fd) {
		fprintf(stderr, "Could not open file '%s'", filename);
		return NULL;
	}

	fseek(fd, 0, SEEK_END);
	size_t len = ftell(fd);
	fseek(fd, 0, SEEK_SET);

	if (sb->cap == 0) sb->cap = LLS_DEF_CAP;
	while (sb->len + len >= sb->cap) sb->cap *= 2;
	void *temp = realloc(sb->content, sb->cap * sizeof(char));
	if (!temp) {
		fprintf(stderr, "Could not open file '%s' (insufficient memory)", filename);
		return NULL;
	}
	sb->content = temp;

	size_t read_len = fread(sb->content + sb->len, 1, len, fd);
	if (read_len != len) {
		fprintf(stderr, "Could not read whole file '%s' (file too big)", filename);
		return NULL;
	}
	
	sb->len += len;
	sb->content[sb->len] = '\0';

	fclose(fd);
	return sb->content;
}
