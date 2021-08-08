#ifndef TERM_H__
#define TERM_H__

#include "ring.h"

/* typedef char Cell; */

enum {
	DESC_NONE,
	DESC_DUMMY_TAB  = (1 << 0),
	DESC_DUMMY_WIDE = (1 << 1),
	DESC_NEWLINE    = (1 << 2),
	DESC_DIRTY      = (1 << 3),
	DESC_MAX        = (1 << 4)
};

enum {
	ATTR_NONE,
	ATTR_BOLD       = (1 << 0),
	ATTR_UNDERLINE  = (1 << 1),
	ATTR_BLINK      = (1 << 2),
	ATTR_ITALIC     = (1 << 3),
	ATTR_INVERT     = (1 << 4),
	ATTR_INVISIBLE  = (1 << 5),
	ATTR_MAX        = (1 << 6)
};

enum {
	CELLTYPE_BLANK,
	CELLTYPE_NORMAL,
	CELLTYPE_COMPLEX,
	CELLTYPE_DUMMY_TAB,
	CELLTYPE_DUMMY_WIDE,
	CELLTYPE_COUNT
};

#define ATTR_MASK (ATTR_MAX - 1)

typedef struct { int x, y; } Pos;

typedef struct {
	uint16 fg; // foreground color index
	uint16 bg; // background color index
	uint16 hl; // highlight color index (rarely used)
} ColorSet;

#if 1
typedef struct {
	uint32 ucs4;    // UCS4 character code
	ColorSet color; // color context
	uint16 attr;    // visual attribute flags
	uint8 type;     // cell type ID
	uint8 width;    // glyph width in columns
} Cell;
#endif

typedef struct {
	uint16 flags;
	ColorSet color;
	uint8 width;
} Attr;

typedef struct {
	Cell *data;
	uint offset;
	int len;
	uint16 flags;
} Row;

typedef struct {
	pid_t pid; // process id
	int mfd;   // master file descriptor
	int sfd;   // slave file descriptor
} PTY;

typedef struct {
	Cell *cells;
	int size, max;
	Ring hist;
	uint8 *tabs;
	Pos pos;
	int top, bot;
	int cols, rows;
	int scroll;
	bool wrap_next;
} TTY;

bool tty_init(int, int);
size_t tty_write(const char *, size_t);
void tty_resize(uint, uint);

int pty_init(char *);
size_t pty_read(void);
void pty_resize(int, int, int, int);
size_t pty_write(const char *, size_t);

TTY *stream_init(TTY *, uint, uint, uint);

int stream_write(int, ColorSet, uint16);
void stream_realloc(size_t);
Cell *stream_get_row(const TTY *, uint, uint *);
void stream_set_row_cells(int, int, int, int);
void stream_clear_row_cells(int, int, int, bool, bool);
void stream_insert_cells(int, uint);
void stream_clear_rows(int, int);
void stream_set_cursor_col(int);
void stream_set_cursor_row(int);
void stream_set_cursor_pos(int, int);
void stream_move_cursor_col(int);
void stream_move_cursor_row(int);
void stream_move_cursor_pos(int, int);

size_t key_get_sequence(uint, uint, char *, size_t);

void dummy__(void);
void dbg_dump_history(TTY *);

extern TTY tty;
extern PTY pty;

extern int tabstop;
extern int histsize;
extern double min_latency;
extern double max_latency;

#define TAB_LEN(i) (tabstop - (i) % tabstop)
#define COLOR_BG 0
#define COLOR_FG 7

#endif
