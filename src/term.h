#ifndef TERM_H__
#define TERM_H__

#include "defs.h"

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
	CursorStyleBlock      = 2,
	CursorStyleUnderscore = 4,
	CursorStyleBar        = 6
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

#define CELLSPEC(...) ((Cell){ __VA_ARGS__ })
#define CELLDFL(c) CELLSPEC( \
  .ucs4  = (c),              \
  .type  = CellTypeNormal,   \
  .width = 1,                \
  .color.bg = COLOR_BG,      \
  .color.fg = COLOR_FG       \
)
#define CELLCLEAR CELLSPEC(0)
#define CELLSPACE CELLDFL(' ')

typedef struct {
	uint16 fg; // foreground color index
	uint16 bg; // background color index
} ColorSet;

typedef struct {
	uint32 ucs4;    // UCS4 character code
	ColorSet color; // color context
	uint16 attr;    // visual attribute flags
	uint8 type;     // cell type ID
	uint8 width;    // glyph width in columns
} Cell;

typedef struct PTY_ PTY;
typedef struct Seq_ Seq;
typedef struct Ring_ Ring;
typedef struct Parser_ Parser;

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
		Cell cell;
		uint8 style;
		bool wrap;
		bool hide;
	} cursor;

	struct PTY_ {
		int pid;
		int mfd, sfd;
		uchar buf[4096];
		uint size;
	} pty;

	Cell cell;

	struct Parser_ {
		uint state;  // Current FSM state
		uint32 ucs4; // Current codepoint being decoded
		Cell cell;   // Current template to write to stream

		// Dynamic buffer for OSC/DCS/APC string sequences
		uchar *data;

		// Stashed intermediate tokens for lookback
		uchar stash[2];
		int stash_index;
		// Offsets to OSC parameter strings within the byte buffer
		int osc_offsets[16];
		int osc_index;
		// Numeric CSI parameters
		int csi_params[16];
		int csi_index;
	} parser;
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
void cmd_update_cursor(TTY *);

size_t key_get_sequence(uint, uint, char *, size_t);

void dummy__(TTY *);
void dbg_print_history(TTY *);
void dbg_print_tty(const TTY *, uint);

enum {
	ColorBlack         = 0x00,
	ColorRed           = 0x01,
	ColorGreen         = 0x02,
	ColorYellow        = 0x03,
	ColorBlue          = 0x04,
	ColorMagenta       = 0x05,
	ColorCyan          = 0x06,
	ColorWhite         = 0x07,
	ColorBrightBlack   = 0x08,
	ColorBrightRed     = 0x09,
	ColorBrightGreen   = 0x0a,
	ColorBrightYellow  = 0x0b,
	ColorBrightBlue    = 0x0c,
	ColorBrightMagenta = 0x0d,
	ColorBrightCyan    = 0x0e,
	ColorBrightWhite   = 0x0f
};

#define COLOR_BG ColorBlack
#define COLOR_FG ColorWhite

#define ISBRIGHT(color) (!!((color) & 0x08))

#endif
