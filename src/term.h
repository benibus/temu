#ifndef TERM_H__
#define TERM_H__

#include "defs.h"

#define ATTR_NONE      (0)
#define ATTR_BOLD      (1 << 0)
#define ATTR_UNDERLINE (1 << 1)
#define ATTR_BLINK     (1 << 2)
#define ATTR_ITALIC    (1 << 3)
#define ATTR_INVERT    (1 << 4)
#define ATTR_INVISIBLE (1 << 5)
#define ATTR_MAX       (1 << 6)
#define ATTR_MASK      (ATTR_MAX-1)

#define CURSOR_DEFAULT  (0)
#define CURSOR_WRAPNEXT (1 << 0)
#define CURSOR_HIDDEN   (1 << 1)
#define CURSOR_INVERT   (1 << 2)
#define CURSOR_BLINKING (1 << 3)

#define VT_COLOR_BG      0x0
#define VT_COLOR_FG      0x1
#define VT_COLOR_BLACK   0x0
#define VT_COLOR_RED     0x1
#define VT_COLOR_GREEN   0x2
#define VT_COLOR_YELLOW  0x3
#define VT_COLOR_BLUE    0x4
#define VT_COLOR_MAGENTA 0x5
#define VT_COLOR_CYAN    0x6
#define VT_COLOR_WHITE   0x7

typedef struct {
	enum ColorTag {
		ColorTagNone, // (use index) default background (0) or foreground (1)
		ColorTag256,  // (use index) one of the standard colors (0-16,17-256)
		ColorTagRGB   // (use r/g/b) not indexed, specified as a raw RGB value
	} tag:8;

	union {
		uint8 arr[3];
		uint8 index;
		struct {
			uint8 r;
			uint8 g;
			uint8 b;
		};
	};
} CellColor;

#define cellcolor(tag_,...) (CellColor){ .tag = (tag_), .arr = { __VA_ARGS__ } }

typedef enum {
	CursorStyleDefault,
	CursorStyleBlock      = 2,
	CursorStyleUnderscore = 4,
	CursorStyleBar        = 6
} CursorStyle;

typedef struct {
	uint32 ucs4;
	int col, row;
	CursorStyle style;
	CellColor bg;
	CellColor fg;
	uint16 attr;
} CursorDesc;

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
	CellColor bg;
	CellColor fg;
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
		CellColor bg;
		CellColor fg;
		int width;
		uint16 attr;
		CursorStyle cursor_style;
		bool cursor_hidden;
	} current;

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
bool term_get_cursor_desc(const Term *, CursorDesc *);
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
void cells_set_bg(Term *, CellColor);
void cells_set_fg(Term *, CellColor);
void cells_reset_bg(Term *);
void cells_reset_fg(Term *);
void cells_set_attrs(Term *, uint16);
void cells_add_attrs(Term *, uint16);
void cells_del_attrs(Term *, uint16);

#endif
