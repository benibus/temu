#ifndef TERM_H__
#define TERM_H__

typedef char Cell;

typedef struct attr_t_ {
	uint flags;
	u8 width;
} Attr;

typedef struct row_t_ {
	uint offset;
	int len;
	bool newline;
} Row;

typedef struct histbuf_t_ {
	int *buf; // buffer of lines (stored as row numbers)
	int max;  // max index of buffer
	int r, w; // internal read/write indices
	uint lap; // write index lap (gets reset after row-pruning)
} HistBuf;

typedef struct pty_t_ {
	pid_t pid; // process id
	int mfd;   // master file descriptor
	int sfd;   // slave file descriptor
} PTY;

typedef struct tty_t_ {
	Cell *data; // text stream
	Attr *attr; // text attributes stream
	int size, max; // size of parallel stream
	bool *tabs;
	struct {
		Row *buf;
		int count, max;
		int top, bot;
	} rows;
	struct {
		int offset;
		int col, row;
		bool wrap;
	} c; // cursor
	HistBuf hist;
	int maxcols, maxrows;
	int scroll;
} TTY;

bool tty_init(int, int);
size_t tty_write(const char *, size_t);

int pty_init(char *);
size_t pty_read(void);
void pty_resize(int, int, int, int);
size_t pty_write(const char *, size_t);

int stream_write(int);
void stream_realloc(size_t);
size_t stream_get_row(uint, char **);
void stream_delete_columns(int, int);
void stream_set_cursor_col(int);
void stream_set_cursor_row(int);
void stream_set_cursor_pos(int, int);
void stream_move_cursor_col(int);
void stream_move_cursor_row(int);
void stream_move_cursor_pos(int, int);

size_t key_get_sequence(uint, uint, char *, size_t);

void dummy__(void);

extern TTY tty;
extern PTY pty;

extern int tabstop;
extern int histsize;
extern double min_latency;
extern double max_latency;

#define TAB_LEN(i)          (tabstop - (i) % tabstop)

#endif
