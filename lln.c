#define LLN_STRIP_PREFIX
#include "lln.h"
#include "lln-internal.h"
#include <dlfcn.h>

// ===== UTILS =====

// ----- StringBuilder -----

int sb_reserve(StringBuilder *sb, size_t new_cap) {
	if (sb->cap < new_cap) {
		if (sb->cap == 0) sb->cap = LLN_DEF_CAP;
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

	if (sb->cap == 0) sb->cap = LLN_DEF_CAP;
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

// ===== LLN =====

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
	return -1;
}

// ----- Loc -----

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

	char *text_view;
} Token;

char *tok_to_cstr(Token *t, StringBuilder *sb) {
	sb->len = 0;
	sb_append_strn(sb, t->start, t->len);
	sb_term(sb);
	return sb->content;
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

// ----- Lexer -----

typedef struct {
	const char *content;

	char *cur;
	Loc loc;

	Token tok;
	StringBuilder sb_tok_text;

	Comm comm;
} Lexer;

void lexer_free(Lexer *l) {
	comm_free(&l->comm);
	free(l->sb_tok_text.content);
}

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
	t.text_view = l->tok.text_view;
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
	t.text_view = tok_to_cstr(&t, &l->sb_tok_text);
	l->tok = t;
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
			a.value.i = atoi(t.text_view);
			break;
		case TOK_FLT:
			a.type = ARG_FLT;
			a.value.f = atof(t.text_view);
			break;
		case TOK_KW_TRUE:
			arg_bool_value = true;
			a.type = ARG_BOOL;
			a.value.b = arg_bool_value;
			break;
		case TOK_KW_FALSE:
			a.type = ARG_BOOL;
			a.value.b = arg_bool_value;
			break;

		default:
			a.type = ARG_INVALID;
	}
	return a;
}

Comm *parse_command(Lexer *l) {
	args_free(&l->comm.args);
	l->comm.args = (Args) {0};
	assert(l->tok.kind == TOK_COMMAND);
	free(l->comm.name);
	l->comm.name = sb_new_cstr(&l->sb_tok_text);
	l->comm.loc = l->tok.loc;
	lexer_next_non_comment(l);
	if (l->tok.kind != TOK_OPAREN) goto return_malformed;
	while(1) {
		lexer_next_non_comment(l);
		if (l->tok.kind == TOK_CPAREN) break;
		Arg arg = parse_arg(l->tok);
		da_append(&l->comm.args, arg);
		if (arg.type == ARG_INVALID) goto return_malformed;

		lexer_next_non_comment(l);
		if (l->tok.kind == TOK_COMMA) continue;
		else if (l->tok.kind == TOK_CPAREN) break;
		else goto return_malformed;
	}
	return &l->comm;
return_malformed:
	l->comm.malformed = true;
	return &l->comm;
}

Comm *lexer_next_command(Lexer *l) {
	while(lexer_next_non_comment(l)) {
		if (l->tok.kind == TOK_COMMAND) {
			parse_command(l);
			return &l->comm;
		}
	}
	return NULL;
}

// ----- validation -----

Callable *name_to_callable(const char *name, const Callables *cs) {
	if (!cs) {
		fprintf(stderr, "Callables pointer is NULL\n");
		return NULL;
	}
	size_t len = strlen(name);
    for (size_t i = 0; i < cs->count; i++) {
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
	if (i % 100 >= 11 && i % 100 <= 13) return "th";
	switch (i % 10) {
		case 0: return "th";
		case 1: return "st";
		case 2: return "nd";
		case 3: return "rd";
		default: return "st";
	}
}

bool validate_command(Lexer *l, const Callables *cs) {
	Comm *comm = &l->comm;
	Callable *c = name_to_callable(comm->name, cs);
	if (!c) {
		fprint_context(stderr, comm->loc, "Command '%s' doesn't exist.\n", comm->name);
		return false;
	}
	if (comm->malformed) {
		// TODO: Elaborate ? Maybe a malformation struct or enum idk
		fprint_context(stderr, comm->loc, "Command '%s' is malformed.\n", comm->name);
		return false;
	}
	Args args = comm->args;
	if (args.count < c->signature.count) {
		fprint_context(stderr, comm->loc, "Command '%s' needs %zu arguments, only %zu were passed.\n", comm->name, c->signature.count, args.count);
		return false;
	}
	if (args.count > c->signature.count) {
		fprint_context(stderr, comm->loc, "Command '%s' needs %zu arguments, but %zu were passed.\n", comm->name, c->signature.count, args.count);
		return false;
	}
	bool valid_args = true;
	for (size_t i = 0; i < args.count; i++) {
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
	if (!valid_args) return false;
	comm->f = c->fnptr; 
	return true;
}

// ----- running -----

Comm *lexer_next_valid_comm(Lexer *l, const Callables *c) {
	while (lexer_next_command(l) && !validate_command(l, c));
	if (l->tok.kind == TOK_END) return NULL;
	return &l->comm;
}

void execute(Lexer *l, const Callables *c) {
	if (c->pre) c->pre();
	if (c->count > 0) {
		while(lexer_next_valid_comm(l, c)) l->comm.f(l->comm.args);
	}
	if (c->post) c->post();
}

void run_lln_file(const char *filename, const Callables *c) {
	StringBuilder file = {0};
	Lexer l = {0};
	read_whole_file(&file, filename);
	lexer_init(&l, file.content, filename);
	execute(&l, c);
	free(file.content);
	lexer_free(&l);
}

// ----- FFI -----

// globals
StringBuilder g_file;
Lexer g_l;
Comm *g_comm;

void load_file(const char *filename) {
	g_file.len = 0;
	read_whole_file(&g_file, filename);
	g_l = (Lexer) {0};
	lexer_init(&g_l, g_file.content, filename);
}

Comm *next_comm(Callables *c) {
	g_comm = lexer_next_valid_comm(&g_l, c);
	return g_comm;
}

// Actually returns a Callables *, but can be opaque
Callables *load_plugin(char *so_path) {
	StringBuilder sb_so_path = {0};
	if (so_path[0] != '/' && strncmp(so_path, "./", 2) != 0 && strncmp(so_path, "../", 3) != 0) {
		sb_append_cstr(&sb_so_path, "./");
	}
	sb_append_cstr(&sb_so_path, so_path);
	sb_term(&sb_so_path);
	void *handle = dlopen(sb_so_path.content, RTLD_LAZY | RTLD_LOCAL);
	free(sb_so_path.content);
	if (!handle) {
		fprintf(stderr, "%s\n", dlerror());
		exit(1);
	}
	dlerror();
	void (*reg_comms)(void);
	*(void **)(&reg_comms) = dlsym(handle, "__lln_preproc_register_commands");
	if (!reg_comms) {
		fprintf(stderr, "%s\n", dlerror());
		exit(1);
	}
	Callables *calls = (Callables *) dlsym(handle, "__lln_preproc_callables");
	if (!calls) {
		fprintf(stderr, "%s\n", dlerror());
		exit(1);
	}
	(*reg_comms)();
	return calls;
}
