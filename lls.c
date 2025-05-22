#define LLS_STRIP_PREFIX
#include "lls.h"

// ===== UTILS =====

// ----- StringBuilder -----

int sb_append(StringBuilder *sb, char c) {
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

int sb_append_strn(StringBuilder *sb, const char *s, size_t n) {
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

int sb_append_cstr(StringBuilder *sb, const char *s) {
	return sb_append_strn(sb, s, strlen(s));
}

char *sb_new_cstr(StringBuilder *sb) {
	char *cstr = malloc(sb->len+1);
	if (cstr == NULL) return NULL;
	strcpy(cstr, sb->content);
	return cstr;
}

char *sb_new_cstrn(StringBuilder *sb, size_t n) {
	char *cstr = malloc(n+1);
	if (cstr == NULL) return NULL;
	strcpy(cstr, sb->content);
	cstr[n] = '\0';
	return cstr;
}

// ----- File -----

// This appends the file to the current sb, if
// it isn't empty. To reset it do sb.len = 0 before
const char *read_whole_file(StringBuilder *sb, const char *filename) {
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

// ===== LLS =====

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

const char *TOKKIND_STR[] = {
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

// ----- Keyword -----

typedef enum {
	KW_FALSE = 0,
	KW_TRUE,
	KW_COUNT,
} Keyword;
#define KW_STRN_TO_KEYWORD_FAILED ((Keyword)-1)
_Static_assert(KW_COUNT < 127, "Too many keywords! -1 may conflict with a real enum value.");

typedef struct {
	const char *str;
	const Keyword kw;
} StrKwMap;

static const StrKwMap STR_TO_KW_MAP[] = {
	{"true", KW_TRUE},
	{"True", KW_TRUE}, // be permissive this is for a LLM
	{"false", KW_FALSE},
	{"False", KW_FALSE},
	{0}
};

Keyword strn_to_keyword(char *start, size_t len) {
    for (size_t i = 0; STR_TO_KW_MAP[i].str != NULL; i++) {
        size_t key_len = strlen(STR_TO_KW_MAP[i].str);
        if (key_len == len && strncmp(start, STR_TO_KW_MAP[i].str, len) == 0)
            return STR_TO_KW_MAP[i].kw;
    }
    return KW_STRN_TO_KEYWORD_FAILED;
}

static inline TokKind kw_to_tokkind(Keyword kw) {
	switch (kw) {
		case KW_FALSE: return TOK_KW_FALSE;
		case KW_TRUE: return TOK_KW_TRUE;
		case KW_COUNT:
			assert(false && "UNREACHABLE");
	}	
}

// ----- Loc -----

typedef struct {
	const char *filename;

	size_t row;
	size_t col;

	const char *prev_line_start;
	const char *line_start;
} Loc;

void print_line(FILE *fptr, const char* str) {
    while (*str && *str != '\n') {
        fputc(*str++, fptr);
    }
	fputc('\n', fptr);
}

void fprint_context(FILE *fptr, Loc loc, const char *format, ...) {
	const char* tab = "    ";
	fprintf(fptr, "%s:%zu:%zu:", loc.filename, loc.row, loc.col);
    va_list args;
    va_start(args, format);
	vfprintf(fptr, format, args);
	if (loc.prev_line_start) {
		if (loc.row > 2) fprintf(fptr, "%4zu | ...\n", loc.row - 2 % 10000);
		fprintf(fptr, "%4zu | ", loc.row - 1 % 10000);
		print_line(fptr, loc.prev_line_start);
	}
	fprintf(fptr, "%4zu | ", loc.row % 10000);
	print_line(fptr, loc.line_start);
	for (size_t i = 1; i < loc.col; i++) fputc(' ', fptr);
	fprintf(fptr, "   %s^\n", tab);
}

typedef struct {
	Loc loc;

	char *start;
	size_t len;
	TokKind kind;
	StringBuilder sb_cstr;
} Token;

char *tok_to_cstr(Token *t, StringBuilder *sb) {
	sb->len = 0;
	sb_append_strn(sb, t->start, t->len);
	sb_term(sb);
	return sb->content;
}

// ----- Lexer -----

typedef struct {
	const char *content;

	char *cur;
	Loc loc;

	Token tok;
} Lexer;

char *lexer_chop_char(Lexer *l) {
	if (l->cur[0] == '\0') return NULL;
	if (l->cur[0] == '\n') {
		l->loc.prev_line_start = l->loc.line_start;
		l->loc.line_start = l->cur + 1;
		l->loc.col = 0;
		l->loc.row++;
	}
	l->cur++;
	l->loc.col++;
	return l->cur;
}

char *lexer_chop_leading_space(Lexer *l) {
	while (isspace(l->cur[0])) lexer_chop_char(l);
	return l->cur;
}

char *lexer_chop_while_predicate(Lexer *l, bool (*p)(Lexer *)) {
	while (p(l)) if(!lexer_chop_char(l)) return NULL;
	return l->cur;
}

static inline bool is_symbol_char(Lexer *l) {
	return isalnum(l->cur[0]) || l->cur[0] == '_';
}

static inline bool is_digit(Lexer *l) {
	return isdigit(l->cur[0]);
}

static inline bool is_not_space(Lexer *l) {
	return !isspace(l->cur[0]);
}

static inline bool is_in_str(Lexer *l) {
	if (l->cur[0] == '"') return false;
	if (l->cur[0] == '\\') {
		char *c;
		c = lexer_chop_char(l);
		if (c == NULL) return false;
	}
	return true;
}

Token *lexer_next_token(Lexer *l) {
	lexer_chop_leading_space(l);

	if (l->cur[0] == '\0') {
		l->tok.kind = TOK_END;
		l->tok.len = 0;
		return NULL;
	}

	Token t = {0};
	t.loc = l->loc;
	t.start = l->cur;
	t.sb_cstr = l->tok.sb_cstr;
	if (l->cur[0] == '!') {
		t.kind = TOK_COMMAND;
		lexer_chop_char(l); // chop leading '!'
		lexer_chop_while_predicate(l, is_symbol_char);
	} else if (l->cur[0] == '"') {
		t.kind = TOK_STR;
		lexer_chop_char(l); // chop leading quotes
		lexer_chop_while_predicate(l, is_in_str);
		lexer_chop_char(l);
	} else if (l->cur[0] == '(') {
		t.kind = TOK_OPAREN;
		lexer_chop_char(l);
	} else if (l->cur[0] == ')') {
		t.kind = TOK_CPAREN;
		lexer_chop_char(l);
	} else if (l->cur[0] == ',') {
		t.kind = TOK_COMMA;
		lexer_chop_char(l);
	} else if (isdigit(l->cur[0]) || l->cur[0] == '.') {
		t.kind = TOK_INT;
		lexer_chop_while_predicate(l, is_digit);
		if (l->cur[0] == '.') {
			t.kind = TOK_FLT;
		    lexer_chop_char(l); // chop leading dot
			lexer_chop_while_predicate(l, is_digit);
		}
	} else {
		lexer_chop_while_predicate(l, is_symbol_char);	
		Keyword k = strn_to_keyword(t.start, (size_t) (l->cur - t.start));
		if (k != KW_STRN_TO_KEYWORD_FAILED) {
			t.kind = kw_to_tokkind(k);
		} else {
			t.kind = TOK_COMMENT;
			lexer_chop_while_predicate(l, is_not_space);
		}
	}

	t.len = l->cur - t.start;
	l->tok = t;
	tok_to_cstr(&t, &l->tok.sb_cstr);
	if (t.len == 0) {
		l->tok.kind = TOK_END;
		return NULL;
	}
	return &l->tok;	
}

// takes in 0-initialized Lexer
void lexer_init(Lexer *l, const char *c, const char *f) {
	l->content = c;
	l->cur = (char *) c;
	l->loc = (Loc) {
		.row = 1,
		.col = 1,
		.line_start = l->cur,
		.prev_line_start = NULL,
		.filename = f
	};
}

// ----- Arguments -----

const char* ARGTYPE_STR[] = {
	"INT",
	"FLT",
	"STR",
	"BOOL",
};

void args_free(Args *args) {
	for (size_t i = 0; i < args->count; i++) {
		Arg a = args->items[i];
		if(a.type == ARG_STR) free(a.value.s);
		}
	free(args->items);
}

// ----- Commands -----

typedef struct {
	char *name;
	Args args;
	bool malformed;

	Loc loc;
	CommandFnPtr f;
} Comm;

void comm_free(Comm *c) {
	args_free(&c->args);
	free(c->name);
}

typedef struct {
	Comm *items;
	size_t count;
	size_t capacity;
} Comms;

void comms_free(Comms *comms) {
	for (size_t i = 0; i < comms->count; i++) {
		Comm c = comms->items[i];
		comm_free(&c);
	}
	free(comms->items);
}

// ----- parsing -----

Token *lexer_next_non_comment(Lexer *l) {
	while (lexer_next_token(l) && l->tok.kind == TOK_COMMENT);
	if (l->tok.kind == TOK_END) return NULL;
	return &l->tok;
}

Arg parse_arg(Token t) {
	Arg a = {0};
	bool arg_bool_value = false;

	switch (t.kind) {
		case TOK_STR:
			// TODO: parse strings correctly
			// probably use separate function
			a.type = ARG_STR;
			char *val = malloc(t.len - 1);
			memcpy(val, t.start + 1, t.len - 2); // To cut out quote characters
			((char *)val)[t.len - 2] = '\0';
			a.value.s = val;
			break;
		case TOK_INT:
			a.type = ARG_INT;
			a.value.i = atoi(t.sb_cstr.content);
			break;
		case TOK_FLT:
			a.type = ARG_FLT;
			a.value.f = atof(t.sb_cstr.content);
			break;
		case TOK_KW_TRUE:
			arg_bool_value = true;
		case TOK_KW_FALSE:
			a.type = ARG_BOOL;
			a.value.b = arg_bool_value;
			break;

		default:
			a.type = ARG_INVALID;
	}
	return a;
}

Comm parse_command(Lexer *l) {
	Comm c = {0};
	Args args = {0};
	assert(l->tok.kind == TOK_COMMAND);
	c.name = sb_new_cstr(&l->tok.sb_cstr);
	c.loc = l->tok.loc;
	lexer_next_non_comment(l);
	if (l->tok.kind != TOK_OPAREN) goto return_malformed;
	while(1) {
		lexer_next_non_comment(l);
		if (l->tok.kind == TOK_CPAREN) break;
		Arg arg = parse_arg(l->tok);
		da_append(&args, arg);
		if (arg.type == ARG_INVALID) goto return_malformed;

		lexer_next_non_comment(l);
		if (l->tok.kind == TOK_COMMA) continue;
		else if (l->tok.kind == TOK_CPAREN) break;
		else goto return_malformed;
	}
	c.args = args;
	return c;
return_malformed:
	args_free(&args);
	c.malformed = true;
	return c;
}

Comms parse(Lexer *l) {
	Comms comms = {0};
	while(lexer_next_non_comment(l)) {
		while (l->tok.kind == TOK_COMMAND) {
			Comm comm = parse_command(l);
			da_append(&comms, comm);
		}
	}
	return comms;
}

// ----- validation -----

Callable *name_to_callable(const char *name, const Callables *cs) {
	size_t len = strlen(name);
    for (size_t i = 0; cs->items[i].name != NULL; i++) {
        size_t key_len = strlen(cs->items[i].name);
        if (key_len == len && strncmp(name, cs->items[i].name, len) == 0)
            return &cs->items[i];
    }
    return NULL;
}

static inline Arg *try_cast_to_int(Arg *a) {
	switch(a->type) {
		case ARG_BOOL: 
		case ARG_INT:	
			break;
		case ARG_FLT:	
		case ARG_STR:	
			return NULL;
		case ARG_COUNT:
			assert(false && "UNREACHABLE");
	}
	a->type = ARG_INT;
	return a;
}

static inline Arg *try_cast_to_flt(Arg *a) {
	switch(a->type) {
		case ARG_INT: {
			a->value.f = (float) a->value.i;
			break;
		}
		case ARG_FLT:
			break;
		case ARG_BOOL: 
		case ARG_STR:	
			return NULL;
		case ARG_COUNT:
			assert(false && "UNREACHABLE");
	}
	a->type = ARG_FLT;
	return a;
}

static inline Arg *try_cast_to_str(Arg *a) {
	switch(a->type) {
		case ARG_STR:	
			break;
		case ARG_INT:
		case ARG_FLT:
		case ARG_BOOL: 
			return NULL;
		case ARG_COUNT:
			assert(false && "UNREACHABLE");
	}
	a->type = ARG_STR;
	return a;
}

static inline Arg *try_cast_to_bool(Arg *a) {
	switch(a->type) {
		case ARG_BOOL: 
			break;
		case ARG_INT: {
			a->value.b = a->value.i != 0;
			break;
		}
		case ARG_FLT:
		case ARG_STR:	
			return NULL;
		case ARG_COUNT:
			assert(false && "UNREACHABLE");
	}
	a->type = ARG_BOOL;
	return a;
}

Arg *try_cast(Arg *a, ArgType t) {
	switch (t) {
		case ARG_INT:	
			return try_cast_to_int(a);
		case ARG_FLT:	
			return try_cast_to_flt(a);
		case ARG_STR:	
			return try_cast_to_str(a);
		case ARG_BOOL:	
			return try_cast_to_bool(a);
		case ARG_COUNT:
		assert(false && "UNREACHABLE");
	}
	return a;
}

char *nth(size_t i) {
	switch (i % 10) {
		case 0: return "th";
		case 1: return "st";
		case 2: return "nd";
		case 3: return "rd";
		default: return "st";
	}
}

Comms validate(Comms *comms, const Callables *cs) {
	Comms valid_comms = {0};
	for (int i = 0; i < comms->count; i++) {
		Comm *comm = &comms->items[i];
		Callable *c = name_to_callable(comm->name, cs);
		if (!c) {
			fprint_context(stderr, comm->loc, "Command '%s' doesn't exist.\n", comm->name);
			continue;
		}
		if (comm->malformed) {
			// TODO: Elaborate ? Maybe a malformation struct or enum idk
			fprint_context(stderr, comm->loc, "Command '%s' is malformed.\n", comm->name);
			continue;
		}
		Args args = comm->args;
		if (args.count < c->signature.count) {
			fprint_context(stderr, comm->loc, "Command '%s' needs %zu arguments, only %zu were passed.\n", comm->name, c->signature.count, args.count);
			continue;
		}
		if (args.count > c->signature.count) {
			fprint_context(stderr, comm->loc, "Command '%s' needs %zu arguments, but %zu were passed.\n", comm->name, c->signature.count, args.count);
			continue;
		}
		for (int i = 0; i < args.count; i++) {
			Arg *a = &args.items[i];
			ArgType t = c->signature.items[i];
			Arg *cast = try_cast(a, t);
			if (!cast) {
				fprint_context(
						stderr, 
						comm->loc, 
						"Command '%s' expects %s as %zu%s argument, but %s was passed.\n", 
						comm->name,
						ARGTYPE_STR[t],
						i+1, nth(i+1),
						ARGTYPE_STR[a->type]);
				continue;
			}
		}
		comm->f = c->fnptr; 
		da_append(&valid_comms, *comm);
	}
	return valid_comms;
}

// ----- logging -----

// void print_ast(Comms comms) {
// 	for (size_t i = 0; i < comms.count; i++) {
// 		Comm c = comms.items[i];
// 		printf("%s(\n", c.name);
// 		if (c.malformed) {
// 			printf("    malformed\n)\n");
// 			continue;
// 		} 
// 		for (size_t j = 0; j < c.args.count; j++) {
// 			Arg a = c.args.items[j];	
// 			printf("    %s,\n", arg_to_cstr(a));
// 		}
// 		printf(")\n");
// 	}
// }

// static char buf[16];
// char *arg_to_cstr(Arg a) {
// 	switch (a.type) {
// 		case ARG_COUNT: return "Invalid";
// 		case ARG_INT: 
// 			sprintf(buf, "%d", a.value.i); 
// 			return buf;
// 		case ARG_FLT: 
// 			sprintf(buf, "%f", a.value.f); 
// 			return buf;
// 		case ARG_STR: 
// 			return a.value.s;
// 		case ARG_BOOL:
// 			return a.value.b == true ? "true" : "false";
// 	}
// }

// ----- running -----

void execute(Comms *comms) {
	for (size_t i = 0; i < comms->count; i++)
		comms->items[i].f(comms->items[i].args);
}

void run_lls_file(const char *filename, const Callables *c) {
	StringBuilder file = {0};
	Lexer l = {0};
	read_whole_file(&file, filename);
	lexer_init(&l, file.content, filename);
	Comms comms = parse(&l);
	Comms valid = validate(&comms, c);
	execute(&valid);
	comms_free(&comms);
	free(valid.items);
	free(file.content);
	free(l.tok.sb_cstr.content);
}

// ----- LLSpreproc -----

typedef enum {
	CLEXTOK_END = 0,
	CLEXTOK_SYMBOL,
	CLEXTOK_SEPARATOR,
	CLEXTOK_COMMENT,
	CLEXTOK_MARKER_COMMENT,
	CLEXTOK_KEYWORD,
	CLEXTOK_CPREPROC,
	CLEXTOK_STRLIT,
	CLEXTOK_CHARLIT,
	CLEXTOK_NUMLIT,
	CLEXTOK_UNKNOWN,
	CLEXTOK_COUNT
} ClexTokKind;

const char *CLEXTOKKIND_STR[] = {
	"TOK_END",
	"TOK_SYMBOL",
	"TOK_SEPARATOR",
	"TOK_COMMENT",
	"TOK_KEYWORD",
	"TOK_CPREPROC",
	"TOK_STRLIT",
	"TOK_CHARLIT",
	"TOK_NUMLIT",
	"TOK_UNKNOWN",
};

typedef enum {
	CLEXKW_NO_KW,
	CLEXKW_CHAR,
	CLEXKW_INT,
	CLEXKW_FLT,
	CLEXKW_VOID,
	CLEXKW_CONST,
	CLEXKW_STATIC,
	CLEXKW_INLINE,
	CLEXKW_KIND
} ClexKeyword;

const char *CLEX_KEYWORDS[] = {
	"",
	"char", 
	"int",
	"float",
	"void",
	"const",
	"static",
	"inline",
	NULL
};

ClexKeyword clex_strn_to_keyword(char *start, size_t len) {
    for (size_t i = 0; CLEX_KEYWORDS[i] != NULL; i++) {
        size_t key_len = strlen(CLEX_KEYWORDS[i]);
        if (key_len == len && strncmp(start, CLEX_KEYWORDS[i], len) == 0)
            return i;
    }
    return -1;
}

typedef struct {
	Loc loc;

	char *start;
	size_t len;
	ClexTokKind kind;
	StringBuilder sb_cstr;
	ClexKeyword kw;
} ClexToken;

char *clextok_to_cstr(ClexToken *t, StringBuilder *sb) {
	sb->len = 0;
	sb_append_strn(sb, t->start, t->len);
	sb_term(sb);
	return sb->content;
}

typedef struct {
	const char *content;

	char *cur;
	Loc loc;

	ClexToken tok;
} Clex;

char *clex_chop_char(Clex *l) {
	if (l->cur[0] == '\0') return NULL;
	if (l->cur[0] == '\n') {
		l->loc.prev_line_start = l->loc.line_start;
		l->loc.line_start = l->cur + 1;
		l->loc.col = 0;
		l->loc.row++;
	}
	l->cur++;
	l->loc.col++;
	return l->cur;
}

char *clex_chop_while_predicate(Clex *l, bool (*p)(Clex *)) {
	while (p(l)) if(!clex_chop_char(l)) return NULL;
	return l->cur;
}

static inline bool clex_is_space(Clex *l) {
	return isspace(l->cur[0]);
}

static inline bool clex_whole_line(Clex *l) {
	if (l->cur[0] == '\n') return false;
	if (l->cur[0] == '\\') {
		char *c;
		c = clex_chop_char(l); // try remove escaped
		if (c == NULL) return false; // check if EOF
	}
	return true;
}

static inline bool clex_multil_comm(Clex *l) {
	char *c;
	if (l->cur[0] == '\\') {
		c = clex_chop_char(l); // try remove escaped
		if (c == NULL) return false; // check if EOF
	} else if (l->cur[0] == '*') {
		c = clex_chop_char(l);
		if (c == NULL) return false; // check if EOF
		if (*c == '/') {
			clex_chop_char(l); // consume '/'
			return false;
		}
	}
	return true;
}

static inline bool clex_in_str(Clex *l) {
	if (l->cur[0] == '"') return false;
	if (l->cur[0] == '\\') {
		char *c;
		c = clex_chop_char(l);
		if (c == NULL) return false;
	}
	return true;
}

static inline bool clex_is_sep(Clex *l) {
	char separators[] = ".,;()[]{}*=";
	for (size_t i = 0; separators[i] != '\0'; i++) {
		if (l->cur[0] == separators[i]) return true;
	}
	return isspace(l->cur[0]);
}

static inline bool clex_is_not_sep(Clex *l) {
	return !clex_is_sep(l);
}

static inline bool clex_is_digit(Clex *l) {
	return isdigit(l->cur[0]);
}

// (!!) Assumes valid C, pass the file through
// `gcc -fsyntax-only` before
ClexToken *clex_next_token(Clex *l) {
	// chop leading space
	clex_chop_while_predicate(l, clex_is_space);

	if (l->cur[0] == '\0') {
		l->tok.kind = TOK_END;
		l->tok.len = 0;
		return NULL;
	} 

	ClexToken t = {0};
	t.loc = l->loc;
	t.start = l->cur;
	t.sb_cstr = l->tok.sb_cstr;
	if (l->cur[0] == '#') {
		t.kind = CLEXTOK_CPREPROC;
		clex_chop_while_predicate(l, clex_whole_line);
	} else if (clex_is_sep(l)) {
		t.kind = CLEXTOK_SEPARATOR;
		clex_chop_char(l); // consume separator
	} else if (l->cur[0] == '/') {
		while(l->cur[0] == '/') {
			clex_chop_char(l);
			t.kind = CLEXTOK_COMMENT;
			if (l->cur[0] == '/') {
				clex_chop_while_predicate(l, clex_whole_line);
			} else if (l->cur[0] == '*') {
				clex_chop_while_predicate(l, clex_multil_comm);
			}
			clex_chop_while_predicate(l, clex_is_space); // handle multiple comments in a row as one token
		}
	} else if (l->cur[0] == '"') {
		t.kind = CLEXTOK_STRLIT;
		clex_chop_char(l); // chop leading quotes
		clex_chop_while_predicate(l, clex_in_str);
		clex_chop_char(l);
	} else if (l->cur[0] == '\'') {
		t.kind = CLEXTOK_CHARLIT;
		clex_chop_char(l); // chop leading single quote
		if (l->cur[0] == '\\') clex_chop_char(l);
		clex_chop_char(l);
		clex_chop_char(l);
	} else if (isdigit(l->cur[0]) || l->cur[0] == '.') {
		t.kind = CLEXTOK_NUMLIT;
		clex_chop_while_predicate(l, clex_is_digit);
		if (l->cur[0] == '.') {
		    clex_chop_char(l); // chop leading dot
			clex_chop_while_predicate(l, clex_is_digit);
		}
	} else {
		clex_chop_while_predicate(l, clex_is_not_sep);	
		ClexKeyword k = clex_strn_to_keyword(t.start, (size_t) (l->cur - t.start));
		if (k != -1) {
			t.kw = k;
			t.kind = CLEXTOK_KEYWORD;
		} else {
			t.kind = CLEXTOK_SYMBOL;
		}
	}


	t.len = l->cur - t.start;
	l->tok = t;
	clextok_to_cstr(&t, &l->tok.sb_cstr);
	if (t.len == 0) {
		l->tok.kind = TOK_END;
		return NULL;
	}
	return &l->tok;
}

// takes in 0-initialized Lexer
void clex_init(Clex *l, const char *c, const char *f) {
	l->content = c;
	l->cur = (char *) c;
	l->loc = (Loc) {
		.row = 1,
		.col = 1,
		.line_start = l->cur,
		.prev_line_start = NULL,
		.filename = f
	};
}

typedef enum {
	CMTKW_CMD,
	CMTKW_COUNT
} CommentKeyword;

const char *COMMENT_KEYWORDS[] = {
	"@cmd", 
	NULL
};

CommentKeyword comlex_strn_to_keyword(char *start, size_t len) {
    for (size_t i = 0; COMMENT_KEYWORDS[i] != NULL; i++) {
        size_t key_len = strlen(COMMENT_KEYWORDS[i]);
        if (key_len == len && strncmp(start, COMMENT_KEYWORDS[i], len) == 0)
            return i;
    }
    return -1;
}

typedef enum {
	COMTOK_SYMBOL,
	COMTOK_KEYWORD,
	COMTOK_END,
	COMTOK_COUNT,
} ComTokKind;

typedef struct {
	char *start;
	size_t len;
	ComTokKind kind;
	StringBuilder sb_cstr;
	ClexKeyword kw;
} ComToken;

typedef struct {
	const char *content;

	char *cur;
	ComToken tok;
} Comlex;

char *comtok_to_cstr(ComToken *t, StringBuilder *sb) {
	sb->len = 0;
	sb_append_strn(sb, t->start, t->len);
	sb_term(sb);
	return sb->content;
}

char *comlex_chop_char(Comlex *l) {
	if (l->cur[0] == '\0') return NULL;
	l->cur++;
	return l->cur;
}

char *comlex_chop_while_predicate(Comlex *l, bool (*p)(Comlex *)) {
	while (p(l)) if(!comlex_chop_char(l)) return NULL;
	return l->cur;
}

static inline bool comlex_is_space(Comlex *l) {
	return isspace(l->cur[0]) || l->cur[0] == '\\' || l->cur[0] == '*';
}

static inline bool comlex_is_not_space(Comlex *l) {
	return !comlex_is_space(l);
}

ComToken *comlex_next_token(Comlex *l) {
	// chop leading space
	comlex_chop_while_predicate(l, comlex_is_space);

	if (l->cur[0] == '\0') {
		l->tok.kind = TOK_END;
		l->tok.len = 0;
		return NULL;
	} 

	ComToken t = {0};
	t.start = l->cur;
	t.sb_cstr = l->tok.sb_cstr;
	comlex_chop_while_predicate(l, comlex_is_not_space);	
	CommentKeyword k = comlex_strn_to_keyword(t.start, (size_t) (l->cur - t.start));
	if (k != -1) {
		t.kw = k;
		t.kind = COMTOK_KEYWORD;
	} else {
		t.kind = COMTOK_SYMBOL;
	}

	t.len = l->cur - t.start;
	l->tok = t;
	comtok_to_cstr(&t, &l->tok.sb_cstr);
	if (t.len == 0) {
		l->tok.kind = COMTOK_END;
		return NULL;
	}
	return &l->tok;
}

// takes in 0-initialized Lexer
void comlex_init(Comlex *l, const char *c) {
	l->content = c;
	l->cur = (char *) c;
}

typedef struct {
	char *name;
	bool is_cmd;
} CmtMeta;

CmtMeta get_comment_metadata(char *text) {
	CmtMeta cmt = {0};
	Comlex l = {0};
	comlex_init(&l, text);
	while (comlex_next_token(&l)) {
		if (l.tok.kind == COMTOK_KEYWORD) {
			if (l.tok.kw == CMTKW_CMD) {
				cmt.is_cmd = true;
				if (comlex_next_token(&l)) cmt.name = l.tok.sb_cstr.content;
				return cmt;
			}
		}
	}
	return cmt;
}

typedef struct {
	ArgType type;
	char *name;
} FnArg;

typedef struct {
	FnArg *items;
	size_t count;
	size_t capacity;
} FnArgs;

FnArgs parse_fnargs(Clex *l) {
	FnArgs args = {0};
	while (l->tok.sb_cstr.content[0] != ')') {
		FnArg a = {0};
		clex_next_token(l);
		switch (l->tok.kw) {
			case CLEXKW_CONST:
				continue;
			case CLEXKW_CHAR:
				clex_next_token(l);
				if (l->tok.sb_cstr.content[0] != '*') {
					fprint_context(stderr, l->tok.loc, "ERROR: command functions only accept argument types 'char *' 'int' and 'float'.\n");
					exit(1);
				}
				a.type = ARG_STR;
				break;
			case CLEXKW_INT: a.type = ARG_INT; break;
			case CLEXKW_FLT: a.type = ARG_FLT; break;
			default:
				fprint_context(stderr, l->tok.loc, "ERROR: command functions only accept argument types 'char *' 'int' and 'float'.\n");
				exit(1);
		}
		clex_next_token(l);
		assert(l->tok.kind == CLEXTOK_SYMBOL);
		a.name = sb_new_cstr(&l->tok.sb_cstr);
		clex_next_token(l);
		da_append(&args, a);
	}
	return args;
}

typedef struct {
	char **items;
	size_t count;
	size_t capacity;
} FnNames;

static char buf[16];
StringBuilder *build_new_file(Clex *l, StringBuilder *sb) {
	sb_append_cstr(sb, "#define __LLS_PREPROCESSED_FILE\n");
	FnNames fnnames = {0};
	size_t level = 0;
	while(clex_next_token(l)) {
		ClexToken tok = l->tok;
		if (tok.sb_cstr.content[0] == '{') level++;
		if (tok.sb_cstr.content[0] == '}') level--;
		if (tok.kind == CLEXTOK_COMMENT && level == 0) {
			sb_append_strn(sb, tok.sb_cstr.content, tok.len);
			CmtMeta cm = get_comment_metadata(tok.sb_cstr.content);
			if (cm.is_cmd) {
				clex_next_token(l);
				if (l->tok.kind == CLEXTOK_END || l->tok.kw != CLEXKW_VOID) {
					fprint_context(stderr, l->tok.loc, "ERROR: '@cmd' tags can only come before 'void *' function declarations.\n");
					exit(1);
				}
				clex_next_token(l);
				if (l->tok.kind != CLEXTOK_SEPARATOR || l->tok.sb_cstr.content[0] != '*') {
					fprint_context(stderr, l->tok.loc, "ERROR: '@cmd' tags can only come before 'void *' function declarations.\n");
					exit(1);
				}
				clex_next_token(l);
				if (l->tok.kind != CLEXTOK_SYMBOL) {
					fprint_context(stderr, l->tok.loc, "ERROR: '@cmd' tags can only come before 'void *' function declarations.\n");
					exit(1);
				}
				char *fn_name = sb_new_cstr(&l->tok.sb_cstr);
				da_append(&fnnames, fn_name);
				clex_next_token(l);
				if (l->tok.kind != CLEXTOK_SEPARATOR || l->tok.sb_cstr.content[0] != '(') {
					fprint_context(stderr, l->tok.loc, "ERROR: '@cmd' tags can only come before 'void *' function declarations.\n");
					exit(1);
				}
				FnArgs args = parse_fnargs(l);
				clex_next_token(l);
				if (l->tok.sb_cstr.content[0] != '{') {
					fprint_context(stderr, l->tok.loc, "ERROR: command functions must have a body.\n");
					exit(1);
				}
				level++;

				if (cm.name) {
					sb_append_cstr(sb, "LLS_declare_command_custom_name(\"");
					sb_append_cstr(sb, cm.name);
					sb_append_cstr(sb, "\", ");
				} else {
					sb_append_cstr(sb, "LLS_declare_command(\"");
				}
				sb_append_cstr(sb, fn_name);
				sb_append_cstr(sb, ", ");
				for (size_t i = 0; i < args.count; i++) {
					sb_append_cstr(sb, "ARG_");
					sb_append_cstr(sb, ARGTYPE_STR[args.items[i].type]);
					if (i < args.count - 1) sb_append_cstr(sb, ", ");
				}
				sb_append_cstr(sb, ") {\n\t");
				for (size_t i = 0; i < args.count; i++) {
					switch (args.items[i].type) {
						case ARG_STR: 
							sb_append_cstr(sb, "char *");
							sb_append_cstr(sb, args.items[i].name);
							sb_append_cstr(sb, " = LLS_arg_str(");
							break;
						case ARG_INT: 
							sb_append_cstr(sb, "int ");
							sb_append_cstr(sb, args.items[i].name);
							sb_append_cstr(sb, " = LLS_arg_int(");
							break;
						case ARG_FLT: 
							sb_append_cstr(sb, "float ");
							sb_append_cstr(sb, args.items[i].name);
							sb_append_cstr(sb, " = LLS_arg_flt(");
							break;
						case ARG_BOOL:
							sb_append_cstr(sb, "bool ");
							sb_append_cstr(sb, args.items[i].name);
							sb_append_cstr(sb, " = LLS_arg_bool(");
							break;
					}
					sprintf(buf, "%zu", i);
					sb_append_cstr(sb, buf);
					sb_append_cstr(sb, ");\n\t");
				}
			}
			free(cm.name);
		} else {
			sb_append_cstr(sb, tok.sb_cstr.content);
			while (clex_is_space(l)) {
				sprintf(buf, "%c", l->cur[0]);
				sb_append_cstr(sb, buf);
					clex_chop_char(l);
			}
		}
	}
	sb_append_cstr(sb, "\nvoid __lls_preproc_register_commands(void) {\n");
	for (size_t i = 0; i < fnnames.count; i++) {
		sb_append_cstr(sb, "\tLLS_register_command(&__lls_preproc_callables, ");
		sb_append_cstr(sb, fnnames.items[i]);
		sb_append_cstr(sb, ");\n");

	}
	sb_append_cstr(sb, "}\n");

	return sb;
}

void lls_preproc_and_rerun_file(const char *filename) {
	StringBuilder file = {0};
	StringBuilder out = {0};
	Clex l = {0};
	char trimmed[1024];
	strcpy(trimmed, filename);
	trimmed[strlen(trimmed) - 2] = '\0';

	read_whole_file(&file, filename);
	clex_init(&l, file.content, filename);
	build_new_file(&l, &out);

	char filename_out[1024];
	snprintf(filename_out, sizeof(filename_out), "%s_lls_preproc.c", trimmed);
	FILE *f = fopen(filename_out, "w");
	if (!f) {
		fprintf(stderr, "Could create new file %s_lls_preproc.c\n", filename_out);
		exit(1);
	}
	fputs(out.content, f);
	fclose(f);

	char command[4096];
	snprintf(command, sizeof(command), "cc -o %s %s lls.o", trimmed, filename_out);
	printf("%s\n", command);
	system(command);
	snprintf(command, sizeof(command), "chmod +x ./%s && ./%s", trimmed, trimmed);
	printf("%s\n", command);
	system(command);
	snprintf(command, sizeof(command), "rm %s", filename_out);
	printf("%s\n", command);
	system(command);
	free(file.content);
	free(out.content);
	free(l.tok.sb_cstr.content);
	exit(0);
}
