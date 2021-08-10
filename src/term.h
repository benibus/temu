#ifndef TERM_H__
#define TERM_H__

#include "ring.h"

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

typedef struct {
	uint32 ucs4;    // UCS4 character code
	ColorSet color; // color context
	uint16 attr;    // visual attribute flags
	uint8 type;     // cell type ID
	uint8 width;    // glyph width in columns
} Cell;

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
	int top, bot;
	int cols, rows;
	int scroll;
	struct {
		int x, y;
		bool wrap_next;
		bool hide;
	} cursor;
} TTY;

bool tty_init(int, int);
size_t tty_write(const char *, size_t);
void tty_resize(uint, uint);

int pty_init(char *);
size_t pty_read(void);
void pty_resize(int, int, int, int);
size_t pty_write(const char *, size_t);

TTY *stream_init(TTY *, uint, uint, uint);
int stream_write(uint32, ColorSet, uint16);
Cell *stream_get_row(const TTY *, uint, uint *);

void cmd_set_cells(TTY *, const Cell *, uint, uint, uint);
void cmd_shift_cells(TTY *, uint, uint, int);
void cmd_insert_cells(TTY *, const Cell *, uint);
void cmd_clear_rows(TTY *, uint, uint);
void cmd_move_cursor_x(TTY *, int);
void cmd_move_cursor_y(TTY *, int);
void cmd_set_cursor_x(TTY *, uint);
void cmd_set_cursor_y(TTY *, uint);

size_t key_get_sequence(uint, uint, char *, size_t);

void dummy__(void);
void dbg_dump_history(TTY *);

extern TTY tty;
extern PTY pty;

extern int tabstop;
extern int histsize;
extern double min_latency;
extern double max_latency;

#define COLOR_DARK_BLACK    0x00
#define COLOR_DARK_RED      0x01
#define COLOR_DARK_GREEN    0x02
#define COLOR_DARK_YELLOW   0x03
#define COLOR_DARK_BLUE     0x04
#define COLOR_DARK_MAGENTA  0x05
#define COLOR_DARK_CYAN     0x06
#define COLOR_DARK_WHITE    0x07
#define COLOR_LIGHT_BLACK   0x08
#define COLOR_LIGHT_RED     0x09
#define COLOR_LIGHT_GREEN   0x0a
#define COLOR_LIGHT_YELLOW  0x0b
#define COLOR_LIGHT_BLUE    0x0c
#define COLOR_LIGHT_MAGENTA 0x0d
#define COLOR_LIGHT_CYAN    0x0e
#define COLOR_LIGHT_WHITE   0x0f

#define COLOR_BG COLOR_DARK_BLACK
#define COLOR_FG COLOR_DARK_WHITE

#endif
