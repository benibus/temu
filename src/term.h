#ifndef TERM_H__
#define TERM_H__

typedef char Cell; // temporary until utf-8 support is added
typedef int CodePt;

typedef struct attr_t_ {
	uint flags;
	u8 width;
} Attr;

typedef struct row_t_ {
	uint offset;
	int len;        // length of row
	bool is_static; // whether the row is hard-wrapped
} Row;

typedef struct histbuf_t_ {
	int *buf;           // buffer of lines (stored as row numbers)
	int max;            // max index of buffer
	int r, w;           // internal read/write indices
	uint lap;           // write index lap (gets reset after row-pruning)
} HistBuf;

typedef struct pty_t_ {
	pid_t pid;          // process id
	int mfd;            // master file descriptor
	int sfd;            // slave file descriptor
} PTY;

typedef struct tty_t_ {
	Cell *data;        // text data stream
	Attr *attr;         // text attributes stream
	bool *tabs;
	HistBuf hist;       // ring buffer of static rows (scrollback history)
	struct {
		Row *buf;   // buffer of rows in the data stream
		int n, max; // current/max number of rows in buffer
	} rows;
	struct {
		int i;
		int x, y;
		int start;
	} c;                // cursor
	int i, max;         // current/max index of data stream
	int max_cols, max_rows; // current cell dimensions (updated by X11)
	int top, bot;       // top and bottom screen row numbers
	int anchor;         // the minimum top-of-screen row between resets, assuming no scrollback
	int scroll;         // current number of scrollback rows
	int dirty;
} TTY;

typedef struct pos_t_ {
	int i;              // index within the data stream
	int row, col;       // absolute row/col
} Pos;

typedef struct cellpos_s_ {
	Cell *ptr;
	size_t idx;
} CellPos;

bool   tty_init(int, int);
size_t tty_write(const char *, size_t);
int    pty_init(char *);
size_t pty_read(void);
void   pty_resize(int, int, int, int);
size_t pty_write(const char *, size_t);
int    stream_write(int);
void   stream_realloc(size_t);
void   screen_set_dirty(int);
void   screen_set_all_dirty(void);
void   screen_set_clean(void);

CellPos cellpos_p(Cell *);
CellPos cellpos_s(size_t);
CellPos cellpos_v(size_t, size_t);
Cell   *streamptr_s(size_t);
Cell   *streamptr_v(size_t, size_t);
size_t  streamidx_p(const Cell *);
size_t  streamidx_v(size_t, size_t);

extern TTY tty;
extern PTY pty;

extern int tabstop;
extern int histsize;
extern double min_latency;
extern double max_latency;

#define ROW(idx)            ( tty.rows.buf[(idx)] )
#define ATTR(memb,idx)      ( tty.attr.(memb)[(idx)] )

/* #define ROWPTR(n_)          ( &tty.data[ROW((n_).offset)] ) */
#define IS_BLANK(row)       ( (row).is_static && tty.data[(row).offset] == '\n' )
#define ROW_IS_BLANK(idx)   ( IS_BLANK(ROW((idx))) )

#define STREAM_IDX(row,col) ( ROW((row)).offset + (col) )
/* #define STREAM_IDX(row,col) ( MEMLEN(&(ROW((row)).data[(col)]), ROW(0).data) ) */
#define POS(idx_,row_,col_) ( (Pos){ .i = (idx_), .row = (row_), .col = (col_) } )
#define GET_POS(row,col)    ( POS(STREAM_IDX((row), (col)), (row), (col)) )
#define C_IDX               ( tty.c.i )
#define C_COL               ( tty.c.x )
#define C_ROW               ( tty.top + tty.c.y )
#define C_POS               ( POS(C_IDX, C_ROW, C_COL) )

#define HIST_FIRST          ( tty.hist.buf[tty.hist.r] )
#define HIST_LAST           ( tty.hist.buf[WRAP_INV(tty.hist.w, tty.hist.max)] )

#define TAB_LEN(i)          ( tabstop - (i) % tabstop )

/*------------------------------------------------*
 * DEBUG
 *------------------------------------------------*/
void dbg_print_cursor(const char *, const char *, int);
void dbg_print_history(const char *, const char *, int);
void dbg_print_state(const char *, const char *, int);

#define DEBUG 0

#define PRINTSRC do { fprintf(stderr, "==> %s:%d/%s()\n", __FILE__, __LINE__, __func__); } while (0)

#define DBG_PRINT(group_,force_) do { \
  if (DEBUG || (force_)) { \
    fprintf(stderr, "\nTTY(%s) --- ", #group_); \
    dbg_print_##group_(__FILE__, __func__, __LINE__); \
  } \
} while (0)

#endif
