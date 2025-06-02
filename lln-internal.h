#ifndef _LLS_INTERNAL_H
#define _LLS_INTERNAL_H

// ----- TokKind -----

typedef enum {
	TOK_END = 0,
	TOK_COMMAND,
	TOK_STR,
	TOK_INT,
	TOK_FLT,
	TOK_OPAREN,
	TOK_CPAREN,
	TOK_COMMA,
	TOK_KW_TRUE,
	TOK_KW_FALSE,
	TOK_COMMENT,
	TOK_COUNT
} TokKind;

static const char *TOKKIND_STR[] = {
	"TOK_END",
	"TOK_COMMAND",
	"TOK_STR",
	"TOK_INT",
	"TOK_FLT",
	"TOK_OPAREN",
	"TOK_CPAREN",
	"TOK_COMMA",
	"TOK_KW_TRUE",
	"TOK_KW_FALSE",
	"TOK_COMMENT",
	"TOK_COUNT"
};

// ----- Loc -----

typedef struct {
	const char *filename;

	size_t row;
	size_t col;

	const char *prev_line_start;
	const char *line_start;
} Loc;

void fprint_context(FILE *fptr, Loc loc, const char *format, ...);

int sb_appendf(StringBuilder *sb, const char *fmt, ...);
int sb_vappendf(StringBuilder *sb, const char *fmt, va_list args);

#endif // _LLS_INTERNAL_H
