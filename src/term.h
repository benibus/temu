#ifndef TERM_H__
#define TERM_H__

#include "types.h"

#define INPUT_CHAR 1
#define INPUT_KEY  2

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

enum {
	CursorStyleDefault,
	CursorStyleBar,
	CursorStyleBlock,
	CursorStyleUnderscore,
};

enum {
	CellTypeBlank,
	CellTypeNormal,
	CellTypeComplex,
	CellTypeTab,
	CellTypeDummyTab,
	CellTypeDummyWide,
	CellTypeCount
};

typedef struct {
	uint16 fg; // foreground color index
	uint16 bg; // background color index
	uint16 hl; // highlight color index (rarely used)
} ColorSet;

typedef struct {
#if 1
	wchar_t ucs4;   // UCS4 character code
#else
	uint32 ucs4;    // UCS4 character code
#endif
	ColorSet color; // color context
	uint16 attr;    // visual attribute flags
	uint8 type;     // cell type ID
	uint8 width;    // glyph width in columns
} Cell;

typedef struct PTY_ PTY;
typedef struct Seq_ Seq;
typedef struct Ring_ Ring;

typedef uint LineID;

struct TTYConfig {
	void *ref;
	char *shell;
	uint16 cols, rows;
	uint16 colpx, rowpx;
	uint16 histsize;
	uint16 tablen;
};

typedef struct TTY_ {
	void *ref;

	Ring *ring;
	uint8 *tabstops;

	int top, bot;
	int cols, rows;
	int scroll;
	int colpx, rowpx;
	int tablen;
	int histsize;

	struct { int x, y; } pos;

	struct {
		uint32 ucs4;
		uint8 style;
		bool wrap;
		bool hide;
	} cursor;

	struct PTY_ {
		pid_t pid;
		int mfd, sfd;
		uchar buf[4096];
		uint size;
	} pty;

	struct Seq_ {
		Cell templ;
		uchar *buf, *args;
		uint32 *opts;
		char tokens[8];
		uint8 depth;
		uint8 state;
	} seq;
} TTY;

TTY *tty_create(struct TTYConfig);
bool tty_init(TTY *, struct TTYConfig);
int tty_exec(TTY *, const char *);
size_t tty_read(TTY *);
size_t tty_write(TTY *, const char *, size_t, uint);
size_t tty_write_raw(TTY *, const uchar *, size_t, uint8);
void tty_scroll(TTY *, int);
void tty_resize(TTY *, int, int);

TTY *stream_init(TTY *, uint, uint, uint);
int stream_write(TTY *, const Cell *);
void stream_resize(TTY *, int, int);
Cell *stream_get_line(TTY *, LineID, uint *);

void cmd_set_cells(TTY *, const Cell *, uint, uint, uint);
void cmd_shift_cells(TTY *, uint, uint, int);
void cmd_insert_cells(TTY *, const Cell *, uint);
void cmd_clear_rows(TTY *, uint, uint);
void cmd_move_cursor_x(TTY *, int);
void cmd_move_cursor_y(TTY *, int);
void cmd_set_cursor_x(TTY *, uint);
void cmd_set_cursor_y(TTY *, uint);

size_t key_get_sequence(uint, uint, char *, size_t);

void dummy__(TTY *);
void dbg_print_history(TTY *);
void dbg_print_tty(const TTY *, uint);

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
