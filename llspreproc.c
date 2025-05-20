#include "lls.c"

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

static char buf[16];
StringBuilder *build_new_file(Clex *l, StringBuilder *sb) {
	size_t level = 0;
	while(clex_next_token(l)) {
		ClexToken tok = l->tok;
		if (tok.sb_cstr.content[0] == '{') level++;
		if (tok.sb_cstr.content[0] == '}') level--;
		if (tok.kind == CLEXTOK_COMMENT) {
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

				if (cm.name) {
					sb_append_cstr(sb, "lls_declare_command_custom_name(\"");
					sb_append_cstr(sb, cm.name);
					sb_append_cstr(sb, "\", ");
				} else {
					sb_append_cstr(sb, "lls_declare_command(\"");
				}
				sb_append_cstr(sb, fn_name);
				sb_append_cstr(sb, ", ");
				for (size_t i = 0; i < args.count; i++) {
					sb_append_cstr(sb, "ARG_");
					sb_append_cstr(sb, ARGTYPE_STR[args.items[i].type]);
					if (i < args.count - 1) sb_append_cstr(sb, ", ");
				}
				sb_append_cstr(sb, ") {\n");
				for (size_t i = 0; i < args.count; i++) {
					switch (args.items[i].type) {
						case ARG_STR: 
							sb_append_cstr(sb, "char *");
							sb_append_cstr(sb, args.items[i].name);
							sb_append_cstr(sb, " = lls_arg_str(");
							break;
						case ARG_INT: 
							sb_append_cstr(sb, "int ");
							sb_append_cstr(sb, args.items[i].name);
							sb_append_cstr(sb, " = lls_arg_int(");
							break;
						case ARG_FLT: 
							sb_append_cstr(sb, "float ");
							sb_append_cstr(sb, args.items[i].name);
							sb_append_cstr(sb, " = lls_arg_flt(");
							break;
						case ARG_BOOL:
							sb_append_cstr(sb, "bool ");
							sb_append_cstr(sb, args.items[i].name);
							sb_append_cstr(sb, " = lls_arg_bool(");
							break;
					}
					sprintf(buf, "%zu", i);
					sb_append_cstr(sb, buf);
					sb_append_cstr(sb, ");\n");
				}
			}
			free(cm.name);
		} else {
			sb_append_strn(sb, tok.sb_cstr.content, tok.len);
			if (isspace(l->cur[0])) {
				sprintf(buf, "%c", l->cur[0]);
				sb_append_cstr(sb, buf);
			}
		}
	}

	return sb;
}

int main(void) {
	const char *file_in = "./commands.c";
	const char *file_out = "./commands_preproc.c";
	StringBuilder file = {0};
	StringBuilder out = {0};
	Clex l = {0};
	read_whole_file(&file, file_in);
	clex_init(&l, file.content, file_in);
	build_new_file(&l, &out);
	printf("%s", out.content);

	free(file.content);
	free(out.content);
	free(l.tok.sb_cstr.content);
	return 0;
}
