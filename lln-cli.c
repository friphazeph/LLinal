#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <time.h>

#define LLN_STRIP_PREFIX
#include "lln.h"
#include "lln-internal.h"

// ===== LLNpreproc =====

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
	CLEXKW_BOOL,
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
	"bool",
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
	ClexKeyword kw;

	char* text_view;
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
	StringBuilder sb_tok_text;
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
		l->tok.kind = CLEXTOK_END;
		l->tok.len = 0;
		return NULL;
	} 

	ClexToken t = {0};
	t.loc = l->loc;
	t.start = l->cur;
	t.text_view = l->tok.text_view;
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
		if (k != (ClexKeyword) -1) {
			t.kw = k;
			t.kind = CLEXTOK_KEYWORD;
		} else {
			t.kind = CLEXTOK_SYMBOL;
		}
	}


	t.len = l->cur - t.start;
	t.text_view = clextok_to_cstr(&t, &l->sb_tok_text);
	l->tok = t;
	if (t.len == 0) {
		l->tok.kind = CLEXTOK_END;
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
	CMTKW_PRE,
	CMTKW_POST,
	CMTKW_COUNT
} CommentKeyword;

const char *COMMENT_KEYWORDS[] = {
	"@cmd", 
	"@pre", 
	"@post", 
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
	CommentKeyword kw;
	char *text_view;
} ComToken;

typedef struct {
	const char *content;

	char *cur;
	ComToken tok;
	StringBuilder sb_tok_text;
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
		l->tok.kind = COMTOK_END;
		l->tok.len = 0;
		return NULL;
	} 

	ComToken t = {0};
	t.start = l->cur;
	t.text_view = l->tok.text_view;
	comlex_chop_while_predicate(l, comlex_is_not_space);	
	CommentKeyword k = comlex_strn_to_keyword(t.start, (size_t) (l->cur - t.start));
	if (k != (CommentKeyword) -1) {
		t.kw = k;
		t.kind = COMTOK_KEYWORD;
	} else {
		t.kind = COMTOK_SYMBOL;
	}

	t.len = l->cur - t.start;
	t.text_view = comtok_to_cstr(&t, &l->sb_tok_text);
	l->tok = t;
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
	bool is_tag;
	CommentKeyword kind;
} CmtMeta;

CmtMeta get_comment_metadata(char *text) {
	CmtMeta cmt = {0};
	Comlex l = {0};
	comlex_init(&l, text);
	while (comlex_next_token(&l)) {
		if (l.tok.kind == COMTOK_KEYWORD) {
			cmt.is_tag = true;
			cmt.kind = l.tok.kw;
			if (l.tok.kw == CMTKW_CMD) {
				if (comlex_next_token(&l)) cmt.name = l.tok.text_view;
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
	while (l->tok.text_view[0] != ')') {
		FnArg a = {0};
		clex_next_token(l);
		if (l->tok.text_view[0] == ')') {
			assert(args.count == 0);
			return args;
		}
		switch (l->tok.kw) {
			case CLEXKW_CONST:
				continue;
			case CLEXKW_CHAR:
				clex_next_token(l);
				if (l->tok.text_view[0] != '*') {
					fprint_context(stderr, l->tok.loc, "ERROR: command functions only accept argument types 'char *', 'int', 'bool' and 'float'.\n");
					exit(1);
				}
				a.type = ARG_STR;
				break;
			case CLEXKW_INT: a.type = ARG_INT; break;
			case CLEXKW_FLT: a.type = ARG_FLT; break;
			case CLEXKW_BOOL: a.type = ARG_BOOL; break;
			case CLEXKW_VOID: 
                clex_next_token(l);
				if (l->tok.text_view[0] != ')') {
					fprint_context(stderr, l->tok.loc, "ERROR: command functions only accept argument types 'char *', 'int', 'bool' and 'float'.\n");
					exit(1);
				}
                assert(args.count == 0);
                return args;
			default:
				fprint_context(stderr, l->tok.loc, "ERROR: command functions only accept argument types 'char *', 'int', 'bool' and 'float'.\n");
				exit(1);
		}
		clex_next_token(l);
		assert(l->tok.kind == CLEXTOK_SYMBOL);
		a.name = sb_new_cstr(&l->sb_tok_text);
		clex_next_token(l);
		da_append(&args, a);
	}
	return args;
}

typedef struct {
	char **items;
	size_t count;
	size_t capacity;
	bool pre;
	bool post;
} FnNames;

typedef struct {
	size_t *items;
	size_t count;
	size_t capacity;
} FnLines;

void preproc_parse_cmd_fnsign(Clex *l, CmtMeta cm) {
	if (cm.name && cm.name[0] != '!') {
		fprint_context(stderr, l->tok.loc, "ERROR: command names must start with a '!'.\n");
		exit(1);
	}
	clex_next_token(l);
	if (l->tok.kind == CLEXTOK_END || l->tok.kw != CLEXKW_VOID) {
		fprint_context(stderr, l->tok.loc, "ERROR: '@cmd' tags can only come before 'void *' function declarations.\n");
		exit(1);
	}
	clex_next_token(l);
	if (l->tok.kind != CLEXTOK_SEPARATOR || l->tok.text_view[0] != '*') {
		fprint_context(stderr, l->tok.loc, "ERROR: '@cmd' tags can only come before 'void *' function declarations.\n");
		exit(1);
	}
	clex_next_token(l);
	if (l->tok.kind != CLEXTOK_SYMBOL) {
		fprint_context(stderr, l->tok.loc, "ERROR: '@cmd' tags can only come before 'void *' function declarations.\n");
		exit(1);
	}
}

void preproc_add_prelude(StringBuilder *sb, FnArgs args) {
	for (size_t i = 0; i < args.count; i++) {
		switch (args.items[i].type) {
			case ARG_STR: 
				sb_appendf(sb, "\tchar *%s = LLN_arg_str(%zu);\n", args.items[i].name, i);
				break;
			case ARG_INT: 
				sb_appendf(sb, "\tint %s = LLN_arg_int(%zu);\n", args.items[i].name, i);
				break;
			case ARG_FLT: 
				sb_appendf(sb, "\tfloat %s = LLN_arg_flt(%zu);\n", args.items[i].name, i);
				break;
			case ARG_BOOL:
				sb_appendf(sb, "\tbool %s = LLN_arg_bool(%zu);\n", args.items[i].name, i);
				break;
			default: assert(false && "unreachable");
		}
	}
}

void preproc_parse_cmd(StringBuilder *sb, Clex *l, ClexToken tok, CmtMeta cm, FnLines *fnlines, FnNames *fnnames) {
	preproc_parse_cmd_fnsign(l, cm);
	char *fnname = sb_new_cstr(&l->sb_tok_text);
	da_append(fnnames, fnname);
	da_append(fnlines, tok.loc.row);

	clex_next_token(l);
	if (l->tok.kind != CLEXTOK_SEPARATOR || l->tok.text_view[0] != '(') {
		fprint_context(stderr, l->tok.loc, "ERROR: '@cmd' tags can only come before 'void *' function declarations.\n");
		exit(1);
	}

	FnArgs args = parse_fnargs(l);
	clex_next_token(l);
	if (l->tok.text_view[0] != '{') {
		fprint_context(stderr, l->tok.loc, "ERROR: tagged functions must have a body.\n");
		exit(1);
	}

	if (cm.name) {
		sb_appendf(sb, "LLN_declare_command_custom_name(\"%s\", ", cm.name);
	} else {
		sb_appendf(sb, "LLN_declare_command(");
	}
	sb_appendf(sb, "%s, ", fnname);
	for (size_t i = 0; i < args.count; i++) {
		sb_appendf(sb, "ARG_%s", ARGTYPE_STR[args.items[i].type]);
		if (i < args.count - 1) sb_append_cstr(sb, ", ");
	}
	sb_append_cstr(sb, ") {\n");
	preproc_add_prelude(sb, args);
}

void preproc_parse_pre_post(StringBuilder *sb, Clex *l, CmtMeta cm) {
	clex_next_token(l);
	if (l->tok.kind == CLEXTOK_END || l->tok.kw != CLEXKW_VOID) {
		fprint_context(stderr, l->tok.loc, "ERROR: '@pre'/'@post' tags can only come before 'void' -> 'void' function declarations.\n");
		exit(1);
	}
	clex_next_token(l);
	if (l->tok.kind != CLEXTOK_SYMBOL) {
		fprint_context(stderr, l->tok.loc, "ERROR: '@pre'/'@post'  tags can only come before 'void' -> 'void' function declarations.\n");
		exit(1);
	}
	clex_next_token(l);
	if (l->tok.kind != CLEXTOK_SEPARATOR || l->tok.text_view[0] != '(') {
		fprint_context(stderr, l->tok.loc, "ERROR: '@pre'/'@post'  tags can only come before 'void' -> 'void' function declarations.\n");
		exit(1);
	}
	clex_next_token(l);
	if (l->tok.kw == CLEXKW_VOID) {
		clex_next_token(l);
		if (l->tok.kind != CLEXTOK_SEPARATOR || l->tok.text_view[0] != ')') {
			fprint_context(stderr, l->tok.loc, "ERROR: '@pre'/'@post'  tags can only come before 'void' -> 'void' function declarations.\n");
			exit(1);
		}
	} else if (l->tok.kind != CLEXTOK_SEPARATOR || l->tok.text_view[0] != ')') {
		fprint_context(stderr, l->tok.loc, "ERROR: '@pre'/'@post'  tags can only come before 'void' -> 'void' function declarations.\n");
		exit(1);
	}

	clex_next_token(l);
	if (l->tok.text_view[0] != '{') {
		fprint_context(stderr, l->tok.loc, "ERROR: tagged functions must have a body.\n");
		exit(1);
	}

	if (cm.kind == CMTKW_PRE) {
		sb_appendf(sb, "LLN_declare_pre");
	} else if (cm.kind == CMTKW_POST) {
		sb_appendf(sb, "LLN_declare_post");
	}
	sb_append_cstr(sb, " {\n");
}

void preproc_add_register(StringBuilder *sb, FnNames fnnames, FnLines fnlines, const char *og_file) {
	sb_append_cstr(sb, "void __lln_preproc_register_commands(void) {\n");
	if (fnnames.pre) sb_append_cstr(sb, "\t__lln_preproc_callables.pre = __LLN_pre;\n");
	if (fnnames.post) sb_append_cstr(sb, "\t__lln_preproc_callables.post = __LLN_post;\n");
	for (size_t i = 0; i < fnnames.count; i++) {
		sb_appendf(sb, "#line %zu \"%s\"\n", fnlines.items[i], og_file);
		sb_appendf(sb,
			"\tLLN_register_command(&__lln_preproc_callables, %s);\n",
			fnnames.items[i]);
	}
	sb_append_cstr(sb, "}\n");
}

StringBuilder *build_new_file(Clex *l, StringBuilder *sb, const char *og_file) {
	sb_append_cstr(sb, "#define __LLN_PREPROCESSED_FILE\n");
	FnLines fnlines = {0};
	FnNames fnnames = {0};
	size_t level = 0;
	while(clex_next_token(l)) {
		ClexToken tok = l->tok;
		if (tok.text_view[0] == '{') level++;
		if (tok.text_view[0] == '}') level--;
		if (tok.kind == CLEXTOK_COMMENT && level == 0) {
			sb_append_strn(sb, tok.text_view, tok.len);
			CmtMeta cm = get_comment_metadata(tok.text_view);
			if (cm.is_tag) {
				switch(cm.kind) {
					case CMTKW_CMD:
						preproc_parse_cmd(sb, l, tok, cm, &fnlines, &fnnames);
						break;
					case CMTKW_PRE:
						fnnames.pre = true;
						preproc_parse_pre_post(sb, l, cm);
						break;
					case CMTKW_POST:
						fnnames.post = true;
						preproc_parse_pre_post(sb, l, cm);
						break;
					default: assert(false && "unreachable");
				}
				level++;
			}
			free(cm.name);
		} else {
			sb_append_cstr(sb, tok.text_view);
			while (clex_is_space(l)) {
				sb_append(sb, l->cur[0]);
				clex_chop_char(l);
			}
		}
	}
	preproc_add_register(sb, fnnames, fnlines, og_file);
	free(fnnames.items);
	free(fnlines.items);
	sb_term(sb);

	return sb;
}

int commandf(const char *fmt, ...) {
	StringBuilder cmd = {0};
	va_list args;
	va_start(args, fmt);
	sb_vappendf(&cmd, fmt, args);
	va_end(args);
	sb_term(&cmd);
	printf("%s\n", cmd.content);
	return system(cmd.content);
}

void lln_preproc_file(const char *file_in, const char *file_out) {
	StringBuilder file = {0};
	StringBuilder out = {0};
	Clex l = {0};

	int result = commandf("cc -fsyntax-only %s", file_in);
	if (result != 0) {
		fprintf(stderr, "ERROR: Cannot preprocess files with syntax errors.\n");
		exit(1);
	}

	read_whole_file(&file, file_in);
	clex_init(&l, file.content, file_in);
	build_new_file(&l, &out, file_in);

	FILE *f = fopen(file_out, "w");
	if (!f) {
		fprintf(stderr, "Could not create new file %s\n", file_out);
		exit(1);
	}
	fputs(out.content, f);
	fclose(f);
}

void lln_preproc_and_compile_file(const char *file_in, const char *file_out) {
	char tmp_name[64];
	srand(time(NULL));
	sprintf(tmp_name, "lln_preproc_tmp_%X.c", rand());
	lln_preproc_file(file_in, tmp_name);

	commandf("cc -o %s %s", file_out, tmp_name);
	remove(tmp_name);
}

void lln_preproc_and_compile_to_so(const char *file_in, const char *file_out) {
	char tmp_name[64];
	srand(time(NULL));
	sprintf(tmp_name, "lln_preproc_tmp_%X.c", rand());
	lln_preproc_file(file_in, tmp_name);

	commandf("cc -fPIC -shared -o %s %s", file_out, tmp_name);
	remove(tmp_name);

	StringBuilder sb_so_path = {0};
	if (file_out[0] != '/' && strncmp(file_out, "./", 2) != 0 && strncmp(file_out, "../", 3) != 0) {
		sb_append_cstr(&sb_so_path, "./");
	}
	sb_append_cstr(&sb_so_path, file_out);
	sb_term(&sb_so_path);
	
	void *handle = dlopen(sb_so_path.content, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "ERROR: %s\n", dlerror());
		remove(file_out);
        exit(1);
    }
	dlerror();
	void *main_func = dlsym(handle, "main");
	char *error = dlerror();
    if (error == NULL && main_func != NULL) {
        fprintf(stderr, "ERROR: `%s` contains a `main()` function.\n", file_in);
        fprintf(stderr, "INFO: main functions are disallowed in LLN shared onject files.\n");
		remove(file_out);
        dlclose(handle);
        exit(1);
    }
	dlclose(handle);
}

void lln_run_from_so(char *lln_path, char *so_path) {
	StringBuilder sb_so_path = {0};
	if (so_path[0] != '/' && strncmp(so_path, "./", 2) != 0 && strncmp(so_path, "../", 3) != 0) {
		sb_append_cstr(&sb_so_path, "./");
	}
	sb_append_cstr(&sb_so_path, so_path);
	sb_term(&sb_so_path);
	void *handle = dlopen(sb_so_path.content, RTLD_LAZY);
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
	Callables *calls;
	calls = (Callables *) dlsym(handle, "__lln_preproc_callables");
	if (!calls) {
		fprintf(stderr, "%s\n", dlerror());
		exit(1);
	}
	(*reg_comms)();
	lln_run_lln_file(lln_path, calls);
}

void lln_run_from_c(char *lln_path, char *c_path) {
	char tmp_name[64];
	srand(time(NULL));
	sprintf(tmp_name, "lln_preproc_tmp_%X.so", rand());
	lln_preproc_and_compile_to_so(c_path, tmp_name);
	lln_run_from_so(lln_path, tmp_name);
	remove(tmp_name);
}

// ===== CLI TOOL =====

void fprint_usage(FILE *f, const char *prog) {
	fprintf(f, "Usage:\n\n");

	fprintf(f, "Preprocessing:\n");
	fprintf(f, "  %s -p  [input_file.c] [output_file.c]\n", prog);
	fprintf(f, "      Preprocess C source file, output another C source file.\n\n");

	fprintf(f, "Compilation:\n");
	fprintf(f, "  %s -c  [input_file.c] [output_executable]\n", prog);
	fprintf(f, "      Preprocess and compile C file (with main) to self-contained executable.\n");
	fprintf(f, "  %s -co [input_file.c] [output_file.so]\n", prog);
	fprintf(f, "      Preprocess and compile main-less C file to shared object (.so).\n\n");

	fprintf(f, "Running:\n");
	fprintf(f, "  %s -ro [input_file.lln] [input_file.so]\n", prog);
	fprintf(f, "      Run .lln script using command implementations from shared object.\n");
	fprintf(f, "  %s -rc [input_file.lln] [input_file.c]\n", prog);
	fprintf(f, "      Run .lln script using command implementations from unprocessed main-less C source.\n");
}

int main(int argc, char **argv) {
	const char *program_name = argv[0];
	if (!argv[1]) {
		fprintf(stderr, "No argument was provided.\n");
		fprint_usage(stderr, program_name);
		exit(1);
	}
	const char *arg = argv[1];
	if (strcmp(arg, "-p") == 0) {
		if (argc < 4) {
			fprintf(stderr, "ERROR: Too few arguments.\n");
			fprint_usage(stderr, program_name);
			exit(1);
		}
		lln_preproc_file(argv[2], argv[3]);
	} else if (strcmp(arg, "-c") == 0) {
		if (argc < 4) {
			fprintf(stderr, "ERROR: Too few arguments.\n");
			fprint_usage(stderr, program_name);
			exit(1);
		}
		lln_preproc_and_compile_file(argv[2], argv[3]);
	} else if (strcmp(arg, "-co") == 0) {
		if (argc < 4) {
			fprintf(stderr, "ERROR: Too few arguments.\n");
			fprint_usage(stderr, program_name);
			exit(1);
		}
		lln_preproc_and_compile_to_so(argv[2], argv[3]);
	} else if (strcmp(arg, "-ro") == 0) {
		if (argc < 4) {
			fprintf(stderr, "ERROR: Too few arguments.\n");
			fprint_usage(stderr, program_name);
			exit(1);
		}
		lln_run_from_so(argv[2], argv[3]);
	} else if (strcmp(arg, "-rc") == 0) {
		if (argc < 4) {
			fprintf(stderr, "ERROR: Too few arguments.\n");
			fprint_usage(stderr, program_name);
			exit(1);
		}
		lln_run_from_c(argv[2], argv[3]);
	} else if (strcmp(arg, "-h") == 0) {
		fprint_usage(stderr, program_name);
		exit(0);
	} else {
		fprintf(stderr, "Invalid argument '%s'\n", arg);
		fprint_usage(stderr, program_name);
		exit(1);
	}
	exit(0);
}
