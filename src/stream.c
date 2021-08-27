#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>

#include "utils.h"
#include "term.h"
#include "ring.h"

enum { LINE_DEFAULT, LINE_WRAPPED };
enum { INIT_NEVER, INIT_IFOLD, INIT_IFNEW };

typedef struct Line_ {
	struct Line_ *next;
	uint16 flags;
	Cell cells[];
} Line;

#define HDROFF offsetof(Line, cells)

#define LINEOFF(x) (HDROFF + ((x) * sizeof(Cell)))
#define LINEHDR(p) ((Line *)((uchar *)(p) - HDROFF))

static void put_glyph(TTY *, const Cell *);
static void put_linefeed(TTY *);
static void put_tab(TTY *, ColorSet, uint16);
static bool line_init(Ring *, Line *, uint16);
static Line *line_push(Ring *);
static Line *line_alloc(Ring *, int, uint8);
static uint linelen(Cell *, uint);
static void screen_reset(TTY *);
static Ring *stream_rewrap(TTY *, int, int);

static inline Line *
lineheader(const Ring *ring, int n)
{
	return (Line *)(ring->data + ringoffset(ring, n));
}

static inline Cell *
linedata(const Ring *ring, int n)
{
	return lineheader(ring, n)->cells;
}

#if 0
#define DBG_PRINT_STATE(t) do { \
	CellDesc desc = celldesc((t), 0, (t)->cursor.y);            \
	msg_log("StreamState",                                      \
	        "txtstream: (%03d/%03d), %04d | "                   \
	        "history: (%04d/%04d) %04d->%04d | "                \
	        "cursor: { %04d, %04d } | "                         \
	        "line: index = %04d, len = %03d, static ?= %-1d\n", \
	        (t)->cols, (t)->pitch,                              \
	        (t)->max,                                           \
	        (t)->hist.count, (t)->hist.max, (t)->top, (t)->bot, \
	        (t)->cursor.x, (t)->cursor.y,                       \
	        desc.line->index, desc.line->len, desc.line->flags & DESC_NEWLINE); \
} while (0)
#else
#define DBG_PRINT_STATE(...)
#endif

void dummy__(TTY *tty) { (void)tty; DBG_PRINT_STATE(tty); }

TTY *
stream_init(TTY *tty, uint cols, uint rows, uint histsize)
{
	ASSERT(tty);
	ASSERT(cols && rows && histsize);

	tty->histsize = bitround(histsize, 1);
	tty->ring = ring_create(tty->histsize, LINEOFF(cols));
	tty->tabstops = xcalloc(cols, sizeof(*tty->tabstops));

	tty->cols = cols;
	tty->rows = rows;

	return tty;
}

void
stream_resize(TTY *tty, int cols, int rows)
{
	Ring *ring = tty->ring;

	if (cols != tty->cols) {
		tty->tabstops = xrealloc(tty->tabstops,
		                         cols,
		                         sizeof(*tty->tabstops));
		for (int i = tty->cols; i < cols; i++) {
			tty->tabstops[i] = (i && i % tty->tablen) ? 1 : 0;
		}
		ring = stream_rewrap(tty, cols, rows);
	}

	if (ring != tty->ring) {
		ASSERT(ring);
		free(tty->ring);
		tty->ring = ring;
	}
	tty->cols = cols;
	tty->rows = rows;

	screen_reset(tty);
}

Ring *
stream_rewrap(TTY *tty, int cols, int rows)
{
	struct {
		Ring *ring;
		Line *line;
		int x1, x2, y;
		int cols, rows;
	} a = { 0 },
	  b = { 0 };

	a.cols = tty->cols, b.cols = cols;
	a.rows = tty->rows, b.rows = rows;
	a.y = -1;
	b.y = -1;

	const int maxhist = tty->ring->count;
	int len = 0;

	if (maxhist <= 0 || a.cols == b.cols) {
		return tty->ring;
	}

	a.ring = tty->ring;
	b.ring = ring_create(tty->histsize, LINEOFF(b.cols));

	for (;;) {
		if (!a.x1) {
			if (a.y + 1 >= maxhist) {
				if (b.line) {
					b.line->flags &= ~LINE_WRAPPED;
				}
				break;
			}
			a.y++;
			a.line = lineheader(a.ring, a.y);
			len = linelen(a.line->cells, a.cols);
		}
		if (!b.x1) {
			b.y++;
			b.line = line_alloc(b.ring,
			                    MIN(b.y, tty->histsize),
			                    ~0);
		}

		const int lim_a = len;
		const int lim_b = b.cols;
		const int rem_a = lim_a - a.x1;
		const int rem_b = lim_b - b.x1;
		const int dx = MIN(rem_a, rem_b);

#if 1
#define PRINT_REWRAP 1
		printf("Wrapping: <%c> { %03d, %03d }:%03d (%03d/%03d) to "
		                 "<%c> { %03d, %03d } (---/%03d) ... ",
		       !!a.line->flags ? '*' : ' ',
		       a.x1, a.y, dx, lim_a, a.cols,
		       !!b.line->flags ? '*' : ' ',
		       b.x1, b.y, lim_b);
#endif

		if (rem_a < rem_b) {
			if (a.line->flags & LINE_WRAPPED) {
				ASSERT(lim_a == a.cols);
				b.line->flags |= LINE_WRAPPED;
				b.x2 = (b.x2 + dx) % lim_b;
				a.x2 = 0;
			} else {
				b.line->flags &= ~LINE_WRAPPED;
				a.x2 = b.x2 = 0;
			}
		} else if (rem_a == rem_b) {
			b.line->flags &= ~LINE_WRAPPED;
			a.x2 = b.x2 = 0;
		} else {
			b.line->flags |= LINE_WRAPPED;
			a.x2 += dx;
			b.x2 = 0;
		}

		memcpy(b.line->cells + b.x1,
		       a.line->cells + a.x1,
		       dx * sizeof(*b.line->cells));

#ifdef PRINT_REWRAP
		printf("Done: <%c|%c> [ %03d, %03d ] -> [ %03d, %03d ]\n",
		       !!a.line->flags ? '*' : ' ',
		       !!b.line->flags ? '*' : ' ',
		       a.x1, b.x1, a.x2, b.x2);
#endif

		a.x1 = a.x2;
		b.x1 = b.x2;
	}

	return b.ring;
}

Cell *
stream_get_line(TTY *tty, LineID id, uint *ret)
{
	const int n = id;

	if (BETWEEN(n + 1, 1, tty->ring->count)) {
		Line *line = lineheader(tty->ring, n);
		if (line->cells[0].ucs4) {
			if (ret) {
				int len = linelen(line->cells, tty->cols);
				*ret = MIN(len, tty->cols);
			}
			return line->cells;
		}
	}

	if (ret) *ret = 0;

	return NULL;
}

int
stream_write(TTY *tty, const Cell *templ)
{
	int offset = tty->pos.y * tty->cols + tty->pos.x;

	if (RING_EMPTY(tty->ring)) {
		line_alloc(tty->ring, 0, INIT_IFOLD|INIT_IFNEW);
		screen_reset(tty);
	}

	switch (templ->ucs4) {
		case '\r': {
			cmd_set_cursor_x(tty, 0);
			break;
		}
		case '\n':
		case '\v':
		case '\f': {
			put_linefeed(tty);
			break;
		}
		case '\b': {
			cmd_move_cursor_x(tty, -1);
			break;
		}
		case '\t': {
			put_tab(tty, templ->color, templ->attr);
			break;
		}
		case 0x07: {
			break;
		}
		default: {
			put_glyph(tty, templ);
			break;
		}
	}

	return (tty->pos.y * tty->cols + tty->pos.x) - offset;
}

void
put_glyph(TTY *tty, const Cell *templ)
{
	Vec4I pos = { 0 };
	pos.x1 = tty->pos.x, pos.x2 = pos.x1;
	pos.y1 = tty->pos.y, pos.y2 = pos.y1;

	Line *line = lineheader(tty->ring, pos.y1);

	if (pos.x1 + 1 < tty->cols) {
		tty->cursor.wrap = false;
	} else if (!tty->cursor.wrap) {
		tty->cursor.wrap = true;
	} else {
		tty->cursor.wrap = false;
		pos.x2 = 0;
		pos.y2++;
	}

	if (pos.y2 > pos.y1) {
		Line *tmp = line;
		line->flags |= LINE_WRAPPED;
		line = line_alloc(tty->ring, pos.y2, INIT_IFOLD|INIT_IFNEW);
		screen_reset(tty);
		ASSERT(tmp->flags & LINE_WRAPPED);
		tmp->next = line;
	}

	line->cells[pos.x2] = *templ;

	if (pos.y2 != pos.y1) {
		cmd_set_cursor_x(tty, 0);
		cmd_move_cursor_y(tty, 1);
	}
	if (!tty->cursor.wrap) {
		cmd_move_cursor_x(tty, 1);
	}
}

bool
line_init(Ring *ring, Line *line, uint16 flags)
{
	memclear(line, ring->pitch, sizeof(*ring->data));
	line->next  = NULL;
	line->flags = flags;

	return true;
}

Line *
line_push(Ring *ring)
{
	Line *line = lineheader(ring, ring->count);
	ring_advance(ring);

	return line;
}

Line *
line_alloc(Ring *ring, int n, uint8 opt)
{
	ASSERT(ring);
	Line *line = NULL;

	if (n >= 0 && n < ring->count) {
		line = lineheader(ring, n);
		if (opt & INIT_IFOLD) {
			line_init(ring, line, 0);
		}
	} else if (n == ring->count) {
		line = line_push(ring);
		if (opt & INIT_IFNEW) {
			line_init(ring, line, 0);
		}
	}

	return line;
}

uint
linelen(Cell *cells, uint lim)
{
	V3(int, l, m, h) i = {
		.l = 0,
		.m = 0,
		.h = MAX(lim - 1, 0)
	};

	// Modified binary search
	for (;;) {
		i.m = (i.l + i.h) / 2;
		if (cells[i.m].ucs4) {
			if (cells[i.h].ucs4) {
				return i.h + 1;
			}
			i.l = i.m + 1;
		} else {
			if (!cells[i.l].ucs4) {
				return i.l;
			}
			i.h = i.m - 1;
		}
	}

	return 0;
}

void
put_linefeed(TTY *tty)
{
	if (line_alloc(tty->ring, tty->pos.y + 1, INIT_IFNEW)) {
		screen_reset(tty);
		cmd_set_cursor_x(tty, 0);
		cmd_move_cursor_y(tty, 1);
	}
}

void
put_tab(TTY *tty, ColorSet color, uint16 attr)
{
	Cell templ = {
		.ucs4  = ' ',
		.width = 1,
		.color = color,
		.attr  = attr,
		.type  = CELLTYPE_NORMAL
	};

	for (int n = 0; tty->pos.x + 1 < tty->cols; n++) {
		if (tty->tabstops[tty->pos.x] && n > 0)
			break;
		put_glyph(tty, &templ);
		templ.type = CELLTYPE_DUMMY_TAB;
	}
}

void
screen_reset(TTY *tty)
{
	tty->bot = DEFAULT(tty->ring->count, 1) - 1;
	tty->top = MAX(tty->bot - tty->rows + 1, 0);
}

void
cmd_set_cells(TTY *tty, const Cell *templ, uint x, uint y, uint n)
{
	Line *line = lineheader(tty->ring, y);
	int x1 = x;
	int x2 = MIN(x1 + (int)n, tty->cols);

	for (int x = x1; x < x2; x++) {
		line->cells[x] = *templ;
	}
}

void
cmd_shift_cells(TTY *tty, uint x, uint y, int dx_)
{
	Cell templ = { 0 };
	Line *line = lineheader(tty->ring, y);

	int x0 = x;
	int x1 = CLAMP(x0 + dx_, 0, tty->cols);
	int dx = x1 - x0;
	int nx = tty->cols - MAX(x0, x1);
	int x2 = (dx < 0) ? x1 + nx : x0;

	memmove(line->cells + x0 + dx,
	        line->cells + x0,
	        nx * sizeof(*line->cells));

	if (dx > 0) {
		templ.ucs4 = ' ';
		templ.width = 1;
	}
	cmd_set_cells(tty, &templ, x2, y, ABS(dx));
}

void
cmd_insert_cells(TTY *tty, const Cell *templ, uint n)
{
	uint cx = tty->pos.x;
	uint cy = tty->pos.y;

	cmd_shift_cells(tty, cx, cy, n);
	cmd_set_cells(tty, templ, cx, cy, n);
}

void
cmd_clear_rows(TTY *tty, uint y, uint n)
{
	Cell templ = { 0 };

	for (uint i = 0; i < n && (int)(y + i) < tty->ring->count; i++) {
		Line *line = lineheader(tty->ring, y + i);
		cmd_set_cells(tty, &templ, 0, y + i, tty->cols);
		line->flags = 0;
		line->next = NULL;
	}
}

void
cmd_move_cursor_x(TTY *tty, int dx)
{
	Cell templ = { .ucs4 = ' ', .width = 1 };
	Line *line = lineheader(tty->ring, tty->pos.y);

	int x0 = tty->pos.x;
	int x1 = CLAMP(x0 + dx, 0, tty->cols - 1);

	for (int x = x0; x < x1; x++) {
		if (!line->cells[x].ucs4) {
			line->cells[x] = templ;
		}
	}

	tty->pos.x = x1;
}

void
cmd_move_cursor_y(TTY *tty, int dy)
{
	tty->pos.y = CLAMP(tty->pos.y + dy, tty->top, tty->bot);
}

void
cmd_set_cursor_x(TTY *tty, uint x)
{
	tty->pos.x = MIN((int)x, tty->cols - 1);
}

void
cmd_set_cursor_y(TTY *tty, uint y)
{
	tty->pos.y = CLAMP((int)y, tty->top, tty->bot);
}

void
dbg_dump_history(TTY *tty)
{
	int n = 0;

	putchar('\n');

	for (int y = 0; y < tty->ring->count; n++, y++) {
		Line *line = lineheader(tty->ring, y);

		size_t off = (uchar *)line - tty->ring->data;
		size_t idx = off / tty->ring->pitch;
		int len = linelen(line->cells, tty->cols);

		printf("[%03zu|%03d] (%03d) $%-6zu 0x%.2x %c |",
		       idx, y, len, off, line->flags,
		       (y == tty->top) ? '>' :
		       ((y == tty->bot) ? '<' : ' '));
		for (int x = 0; x < tty->cols; x++) {
			char *esc = (x == len) ? "\033[97;41m" : "";
			printf("%s%lc\033[0m", esc,
			       (line->cells[x].ucs4) ? line->cells[x].ucs4 : L' ');
		}
		printf("|\n");
	}

	if (n) putchar('\n');

	DBG_PRINT_STATE(tty);
}

