#ifndef TERM_H__
#define TERM_H__

#include "defs.h"

#define ATTR_NONE      (0)
#define ATTR_BOLD      (1 << 0)
#define ATTR_ITALIC    (1 << 1)
#define ATTR_UNDERLINE (1 << 2)
#define ATTR_BLINK     (1 << 3)
#define ATTR_INVERT    (1 << 4)
#define ATTR_INVISIBLE (1 << 5)
#define ATTR_MAX       (1 << 6)
#define ATTR_MASK      (ATTR_MAX-1)

#define CURSOR_DEFAULT  (0)
#define CURSOR_WRAPNEXT (1 << 0)
#define CURSOR_HIDDEN   (1 << 1)
#define CURSOR_INVERT   (1 << 2)
#define CURSOR_BLINKING (1 << 3)

typedef enum {
	CursorStyleDefault,
	CursorStyleBlock       = 2,
	CursorStyleUnderscore  = 4,
	CursorStyleBar         = 5
} CursorStyle;

typedef struct {
	int col, row;
	CursorStyle style;
	uint32 color;
	bool isvisible;
} Cursor;

typedef enum {
	CellTypeBlank,
	CellTypeNormal,
	CellTypeComplex,
	CellTypeTab,
	CellTypeDummyTab,
	CellTypeDummyWide,
	CellTypeCount
} CellType;

typedef struct {
	uint32 ucs4;
	uint32 bg;
	uint32 fg;
	CellType type:8;
	uint8 width;
	uint16 attr;
} Cell;

struct TermConfig {
	void *generic;
	char *shell;
	uint16 cols, rows;
	uint16 colsize, rowsize;
	uint16 tabcols;
	uint16 histlines;
	uint32 default_bg;
	uint32 default_fg;
	uint32 colors[16];
};

typedef struct PTY {
	int pid;
	int mfd, sfd;
	uchar buf[4096];
	uint size;
} PTY;

typedef struct Parser {
	uint state;      // Current FSM state
	uint32 ucs4;     // Current codepoint being decoded
	uchar *data;     // Dynamic buffer for OSC/DCS/APC string sequences
	uchar tokens[2]; // Stashed intermediate tokens
	int depth;       // Intermediate token index
	int argv[16];    // Numeric parameters
	int argi;        // Numeric parameter index
} Parser;

typedef struct Ring Ring;

typedef struct {
	void *generic;

	Ring *ring;
	uint8 *tabstops;

	int cols, rows;
	int colsize, rowsize;
	int histlines;
	int scrollback;
	int top, bot;
	int tabcols;

	bool wrapnext;

	struct { int x, y; } pos;

	struct {
		uint32 bg;
		uint32 fg;
		int width;
		uint16 attr;
		CursorStyle cursor_style;
		bool cursor_hidden;
	} current;

	uint32 default_bg;
	uint32 default_fg;
	uint32 colormap[16];

	PTY pty;

	Parser parser;
} Term;

Term *term_create(struct TermConfig);
bool term_init(Term *, struct TermConfig);
int term_exec(Term *, const char *);
size_t term_pull(Term *);
size_t term_push(Term *, const char *, size_t);
size_t term_consume(Term *, const uchar *, size_t);
void term_scroll(Term *, int);
void term_reset_scroll(Term *);
void term_resize(Term *, int, int);
int term_get_fileno(const Term *);
const Cell *term_get_row(const Term *, int);
Cell term_get_cell(const Term *, int, int);
Cursor term_get_cursor(const Term *);
size_t term_make_key_string(const Term *, uint, uint, char *, size_t);

void term_print_summary(const Term *, uint);
void term_print_history(const Term *);

void write_codepoint(Term *, uint32, CellType);
void write_tab(Term *);
void write_newline(Term *);

void cursor_move_cols(Term *, int);
void cursor_move_rows(Term *, int);
void cursor_set_col(Term *, int);
void cursor_set_row(Term *, int);
void cursor_set_hidden(Term *, bool);
void cursor_set_style(Term *, int);

void cells_init(Term *, int, int, int);
void cells_clear(Term *, int, int, int);
void cells_delete(Term *, int, int, int);
void cells_insert(Term *, int, int, int);
void cells_clear_lines(Term *, int, int);
void cells_set_bg(Term *, uint8);
void cells_set_fg(Term *, uint8);
void cells_set_bg_rgb(Term *, uint8, uint8, uint8);
void cells_set_fg_rgb(Term *, uint8, uint8, uint8);
void cells_reset_bg(Term *);
void cells_reset_fg(Term *);
void cells_set_attrs(Term *, uint16);
void cells_add_attrs(Term *, uint16);
void cells_del_attrs(Term *, uint16);

#endif
