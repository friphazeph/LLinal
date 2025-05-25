#define LLS_STRIP_PREFIX
#include "lls.h"

// ===== UTILS =====

// ----- StringBuilder -----

int sb_reserve(StringBuilder *sb, size_t new_cap) {
	if (sb->cap < new_cap) {
		if (sb->cap == 0) sb->cap = LLS_DEF_CAP;
		while (sb->cap < new_cap) sb->cap *= 2;
		void *temp = realloc(sb->content, sb->cap * sizeof(char));
		if (!temp) return -1;
		sb->content = temp;
	}
	return 0;
}

int sb_append(StringBuilder *sb, char c) {
	if (sb_reserve(sb, sb->len + 1) != 0) return -1;
	sb->content[sb->len++] = c;
	return 0;
}

int sb_append_strn(StringBuilder *sb, const char *s, size_t n) {
	if (sb_reserve(sb, sb->len + n)) return -1;
	strncpy(sb->content + sb->len, s, n);
	sb->len += n;
	return 0;
}

int sb_append_cstr(StringBuilder *sb, const char *s) {
	return sb_append_strn(sb, s, strlen(s));
}

int sb_vappendf(StringBuilder *sb, const char *fmt, va_list args) {
	va_list args_cp;
	va_copy(args_cp, args);
	int needed = vsnprintf(NULL, 0, fmt, args_cp);
	va_end(args_cp);
	if (needed < 0) return -1;

	if (sb_reserve(sb, sb->len + (size_t) needed) != 0)
		return -1;

	vsnprintf(sb->content + sb->len, (size_t) needed + 1, fmt, args);
	sb->len += (size_t) needed;

	return 0;
}


int sb_appendf(StringBuilder *sb, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int res = sb_vappendf(sb, fmt, args);
	va_end(args);
	return res;
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
		bool valid_args = true;
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
				valid_args = false;
			}
		}
		if (!valid_args) continue;
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
	if (c->count == 0) exit(0);
	StringBuilder file = {0};
	Lexer l = {0};
	read_whole_file(&file, filename);
	lexer_init(&l, file.content, filename);
	Comms comms = parse(&l);
	Comms valid = validate(&comms, c);
	if (c->pre) c->pre();
	execute(&valid);
	if (c->post) c->post();
	comms_free(&comms);
	free(valid.items);
	free(file.content);
	free(l.tok.sb_cstr.content);
}

