#ifndef TERM_H__
#define TERM_H__

#include "defs.h"
#include "cells.h"
#include "ring.h"

#define ATTR_NONE      (0)
#define ATTR_BOLD      (1 << 0)
#define ATTR_ITALIC    (1 << 1)
#define ATTR_UNDERLINE (1 << 2)
#define ATTR_BLINK     (1 << 3)
#define ATTR_INVERT    (1 << 4)
#define ATTR_INVISIBLE (1 << 5)
#define ATTR_MAX       (1 << 6)
#define ATTR_MASK      (ATTR_MAX-1)

typedef enum {
	CursorStyleDefault,
	CursorStyleBlock       = 2,
	CursorStyleUnderscore  = 4,
	CursorStyleBar         = 5,
	CursorStyleOutline     = 7
} CursorStyle;

typedef struct {
	int col, row;
	CursorStyle style;
	uint32 color;
} Cursor;

struct TermConfig {
	char *shell;
	uint16 cols, rows;
	uint16 colsize, rowsize;
	uint16 tabcols;
	uint16 histlines;
	uint32 color_bg;
	uint32 color_fg;
	uint32 colors[16];
};

#define IOBUF_MAX 4096

typedef struct {
	int pid;                // PTY PID
	int mfd;                // PTY master file descriptor
	int sfd;                // PTY slave file descriptor
	uchar input[IOBUF_MAX]; // PTY input buffer

	Ring *ring;
	uint8 *tabstops;

	int cols;
	int rows;
	int max_cols;
	int max_rows;

	int colsize;
	int rowsize;
	int histlines;
	int tabcols;

	int x;
	int y;

	bool wrapnext;
	bool hidecursor;

	Cell *framebuf;
	Cell cell;
	CursorStyle crs_style;
	uint32 crs_color;

	uint32 color_bg;
	uint32 color_fg;
	uint32 colors[16];

	struct Parser {
		uint state;      // Current FSM state
		uint32 ucs4;     // Current codepoint being decoded
		uchar *data;     // Dynamic buffer for OSC/DCS/APC string sequences
		uchar tokens[2]; // Stashed intermediate tokens
		int depth;       // Intermediate token index
		int argv[16];    // Numeric parameters
		int argi;        // Numeric parameter index
	} parser;
} Term;

Term *term_create(struct TermConfig);
void term_destroy(Term *);
bool term_init(Term *, struct TermConfig);
int term_exec(Term *, const char *);
size_t term_pull(Term *);
size_t term_push(Term *, const char *, size_t);
size_t term_consume(Term *, const uchar *, size_t);
void term_scroll(Term *, int);
void term_reset_scroll(Term *);
void term_resize(Term *, int, int);
Cell *term_get_row(const Term *, int);
Cell term_get_cell(const Term *, int, int);
bool term_get_cursor(const Term *, Cursor *);
size_t term_make_key_string(const Term *, uint, uint, char *, size_t);
Cell *term_get_framebuffer(Term *);

void term_print_summary(const Term *, uint);
void term_print_history(const Term *);
void term_print_stream(const Term *);

#endif
