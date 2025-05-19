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
	"TOK_MARKER_COMMENT",
	"TOK_KEYWORD",
	"TOK_CPREPROC",
	"CLEXTOK_STRLIT",
	"CLEXTOK_CHARLIT",
	"CLEXTOK_NUMLIT",
	"TOK_UNKNOWN",
};

typedef enum {
	CLEXKW_CHAR,
	CLEXKW_INT,
	CLEXKW_FLT,
	CLEXKW_VOID,
} ClexKeyword;

const char *CLEX_KEYWORDS[] = {
	"char", "int", "float", "void", NULL
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

static inline bool clex_in_char(Clex *l) {
	if (l->cur[0] == '\'') return false;
	if (l->cur[0] == '\\') {
		char *c;
		c = clex_chop_char(l);
		c = clex_chop_char(l);
		return false;
	}
	return true;
}

bool clex_is_sep(Clex *l) {
	char separators[] = ".,;()[]{}*= ";
	for (size_t i = 0; separators[i] != '\0'; i++) {
		if (l->cur[0] == separators[i]) return true;
	}
	return false;
}

bool clex_is_not_sep(Clex *l) {
	return !clex_is_sep(l);
}

bool clex_is_digit(Clex *l) {
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
		clex_chop_char(l);
		t.kind = CLEXTOK_COMMENT;
		if (l->cur[0] == '/') {
			clex_chop_while_predicate(l, clex_whole_line);
		} else if (l->cur[0] == '*') {
			clex_chop_while_predicate(l, clex_multil_comm);
		}
	} else if (l->cur[0] == '"') {
		t.kind = CLEXTOK_STRLIT;
		clex_chop_char(l); // chop leading quotes
		clex_chop_while_predicate(l, clex_in_str);
		clex_chop_char(l);
	} else if (l->cur[0] == '\'') {
		t.kind = CLEXTOK_CHARLIT;
		clex_chop_char(l); // chop leading single quote
		clex_chop_while_predicate(l, clex_in_str);
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

int main(void) {
	const char *file_in = "./commands.c";
	const char *file_out = "./commands_preproc.c";
	StringBuilder file = {0};
	StringBuilder out = {0};
	Clex l = {0};
	read_whole_file(&file, file_in);
	clex_init(&l, file.content, file_in);

	while(clex_next_token(&l)) {
		ClexToken t = l.tok;
		printf("%s: %s\n", CLEXTOKKIND_STR[t.kind], t.sb_cstr.content);
	}

	free(file.content);
	free(out.content);
	free(l.tok.sb_cstr.content);
	printf("Hello, world!\n");
	return 0;
}
