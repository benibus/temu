#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>

#include "utils.h"
#include "term.h"
#include "ring.h"

typedef struct { int    x, y, w, h; } Rect;
typedef struct { int32  x, y, w, h; } Rect32;
typedef struct { int64  x, y, w, h; } Rect64;
typedef struct { uint   x, y, w, h; } URect;
typedef struct { uint32 x, y, w, h; } URect32;
typedef struct { uint64 x, y, w, h; } URect64;

static void put_glyph(int, ColorSet, uint16);
static void put_linefeed(void);
static void put_tab(ColorSet, uint16);
static Row *row_create(int, bool);
static Row *row_alloc(int, bool);
static void cursor_set_row_abs(int);
static void cursor_set_col_abs(int);
static void cursor_set_row_rel(int);
static void cursor_set_col_rel(int);
static void update_bounds(void);
static void update_cursor(void);
static uint row_length(Row *);

/* #define row_data(t,n) ((Row *)ring_data(&(t)->hist, (n))) */

#if 0
#define LOG_STATE do { \
	Row *crow_ = row_data(&tty, tty.pos.y); \
	msg_log("StreamState",                                         \
	    "txtstream: (%04d/%04d) | "                                \
	    "rowstream: (%04d/%04d) %04d->%04d | "                     \
	    "cursor: { %04d, %04d } = %04d | "                         \
	    "row: offset = (%04d/%04d), len = %03d, static ?= %-1d\n", \
	    tty.size, tty.max,                                         \
	    tty.hist.count, tty.hist.max, tty.top, tty.bot,  \
	    tty.pos.x, tty.pos.y, tty.c.offset,                        \
	    crow_->offset, tty.size, crow_->len, crow_->flags & DESC_NEWLINE); \
} while (0)
#else
#define LOG_STATE
#endif

void dummy__(void) { LOG_STATE; }

static inline Row *
row_data(TTY *this, uint y)
{
	return ring_data(&this->hist, y);
}

static inline uint
cell_offset(TTY *this, uint x, uint y)
{
	return row_data(this, y)->offset + x;
}

static inline Cell *
cell_data(TTY *this, uint x, uint y)
{
	return this->cells + cell_offset(this, x, y);
}

Row *
row_prepare(TTY *this, int idx, bool isnewline)
{
	ASSERT(this);
	Row *row = NULL;

	if (idx < this->hist.count) {
		row = row_data(this, idx);
	} else {
		row = ring_push(&this->hist, NULL);
		if (!RING_FULL(&this->hist)) {
			/* this->size += this->cols; */
			ASSERT(this->size <= this->max);
		} else {
			memclear(this->cells + row->offset,
			         this->cols,
			         sizeof(*this->cells));
		}
		if (this->size < this->max) {
			this->size += this->cols;
		}
		row->len = 0;
	}

	row->flags = 0;
	row->flags |= (isnewline) ? DESC_NEWLINE : 0;

	update_bounds();

	return row;
}

void
dbg_dump_history(TTY *this)
{
	int n = 0;

	putchar('\n');

	for (int y = 0; y < this->hist.count; n++, y++) {
		Row *row = row_data(this, y);
		Cell *cells = this->cells + row->offset;
		int idx = ring_index(&this->hist, y);
		printf("[%03d|%03d] (%03d) $%05u/%05u 0x%.2x %c |",
		       idx, y, row->len, row->offset, this->size, row->flags,
		       (y == this->top) ? '>' :
		       ((y == this->bot) ? '<' : ' '));
		for (int x = 0; x < this->cols; x++) {
			char *esc = (x == row->len) ? "\033[97;41m" : "";
			printf("%s%lc\033[0m", esc, (cells[x].ucs4) ? cells[x].ucs4 : L' ');
		}
		printf("|\n");
	}

	if (n) putchar('\n');
}

int
stream_write(int ucs4, ColorSet color, uint16 flags)
{
	int offset = tty.pos.y * tty.cols + tty.pos.x;

	if (!tty.hist.count) {
		row_prepare(&tty, 0, true);
	}

	switch (ucs4) {
		case '\r': {
			cursor_set_col_abs(0);
			break;
		}
		case '\n':
		case '\v':
		case '\f': {
			put_linefeed();
			break;
		}
		case '\b': {
			cursor_set_col_rel(-1);
			break;
		}
		case '\t': {
			put_tab(color, flags);
			break;
		}
		case 0x07: {
			break;
		}
		default: {
			put_glyph(ucs4, color, flags);
			break;
		}
	}

	update_cursor();

	LOG_STATE;

	return (tty.pos.y * tty.cols + tty.pos.x) - offset;
}

void
put_glyph(int ucs4, ColorSet color, uint16 flags)
{
	Pos pos = tty.pos;

	if (pos.x + 1 < tty.cols) {
		tty.wrap_next = false;
	} else if (!tty.wrap_next) {
		tty.wrap_next = true;
	} else {
		tty.wrap_next = false;
		pos.x = 0;
		pos.y++;
	}

	Row *row = (pos.y != tty.pos.y)
	         ? row_prepare(&tty, pos.y, false)
	         : row_data(&tty, pos.y);
	int offset = row->offset + pos.x;

	if (!tty.cells[offset].ucs4) {
		row->len = pos.x + 1;
	}
	tty.cells[offset].ucs4  = ucs4;
	tty.cells[offset].color = color;
	tty.cells[offset].attr  = flags;

	if (pos.y != tty.pos.y) {
		cursor_set_col_abs(0);
		cursor_set_row_rel(1);
	}
	if (!tty.wrap_next) {
		cursor_set_col_rel(1);
	}
}

uint
row_length(Row *row)
{
	Cell *cells = tty.cells + row->offset;
#if 1
	for (int n = 0; n < tty.cols; n++) {
		if (!cells[n].ucs4) {
			return n;
		}
	}

	return tty.cols;
#else
	return strnlen(cell, tty.cols);
#endif
}

void
put_linefeed(void)
{
	row_prepare(&tty, tty.pos.y + 1, true);

	cursor_set_col_abs(0);
	cursor_set_row_rel(1);
}

void
put_tab(ColorSet color, uint16 flags)
{
	for (int n = 0; tty.pos.x + 1 < tty.cols; n++) {
		if (tty.tabs[tty.pos.x] && n > 0)
			break;
		put_glyph(' ', color, flags);
	}
}

void
update_bounds(void)
{
	/* tty.bot = tty.rows.count - 1; */
	/* tty.top = max(tty.bot - tty.rows + 1, 0); */
	ASSERT(tty.hist.count);
	tty.bot = tty.hist.count - 1;
	tty.top = max(tty.bot - tty.rows + 1, 0);
}

void
cursor_set_col_rel(int cols)
{
	Row *row = row_data(&tty, tty.pos.y);
	uint xdest = clamp(tty.pos.x + cols, 0, tty.cols - 1);

	for (uint x = tty.pos.x; x < xdest; x++) {
		if (!tty.cells[row->offset+x].ucs4) {
			tty.cells[row->offset+x].ucs4 = ' ';
			row_data(&tty, tty.pos.y)->len++;
		}
	}

	tty.pos.x = xdest;
}

void
cursor_set_row_rel(int rows)
{
	tty.pos.y = clamp(tty.pos.y + rows, tty.top, tty.bot);
}

void
cursor_set_col_abs(int col)
{
	tty.pos.x = clamp(col, 0, tty.cols - 1);
}

void
cursor_set_row_abs(int row)
{
	tty.pos.y = clamp(row, tty.top, tty.bot);
}

void
update_cursor(void)
{
	return;
}


/* void */
/* stream_realloc(size_t max) */
/* { */
/* 	if (max == 0) { */
/* 		FREE(tty.cells); */
/* 		FREE(tty.tabs); */
/* 		ring_free(&tty.hist); */
/* 		tty.max = tty.size = 0; */
/* 	} else { */
/* 		if (!tty.data) { */
/* 			ASSERT(!tty.attr); */
/* 			tty.cells = xcalloc(max, sizeof(*tty.cells)); */
/* 			tty.tabs = xcalloc(tty.cols, sizeof(*tty.tabs)); */
/* 			ASSERT(ring_init(&tty.hist, histsize, sizeof(Row))); */
/* 		} else { */
/* 			tty.cells = xrealloc(tty.data, max, sizeof(*tty.cells)); */
/* 			if ((ssize_t)max > tty.max) { */
/* 				memclear(tty.cells + tty.size, max - tty.size, sizeof(*tty.cells)); */
/* 			} */
/* 		} */
/* 	} */

/* 	tty.max = max; */
/* } */

TTY *
stream_init(TTY *this, uint ncols, uint nrows, uint nhist_)
{
	ASSERT(this);
	ASSERT(ncols && nrows && nhist_);

	ring_init(&this->hist, nhist_, sizeof(Row));
	ASSERT(this->hist.data && this->hist.max > 0);

	uint nhist = this->hist.max;
	uint ncells = ncols * nhist;

	this->cells = xcalloc(ncells, sizeof(*this->cells));
	this->tabs = xcalloc(ncols, sizeof(*this->tabs));

	this->cols = ncols;
	this->rows = nrows;
	this->max = ncells;

	return this;
}

static inline Rect32
get_bounded_rect(int x, int y, int w, int h)
{
	Rect32 rect = { 0 };

	rect.x = CLAMP(x, 0, tty.cols);
	rect.y = CLAMP(y, tty.top, tty.bot);
	rect.w = CLAMP(w, 0, tty.cols - rect.x);
	rect.h = CLAMP(h, 0, tty.bot - rect.y + 1);

	return rect;
}

void
stream_shift_row_cells(int y, int x, int delta_)
{
	Rect32 rect1 = get_bounded_rect(x, y, tty.cols, 1);
	Rect32 rect2 = get_bounded_rect(rect1.x + delta_, rect1.y, tty.cols, 1);

	/* int offset = celloffset(rect1.y, rect1.x); */
	Row *row = row_data(&tty, rect1.y);
	int offset = row->offset + rect1.x;
	int delta = rect2.x - rect1.x;
	int count = MIN(rect1.w, rect2.w);

	memcshift(tty.cells + offset, delta, count, sizeof(*tty.cells));

	row->len = row_length(row);
}

void
stream_set_row_cells(int y, int x, int ucs4, int num)
{
	Rect32 rect = get_bounded_rect(x, y, num, 1);
	if (rect.w == 0) return;

	Row *row = row_data(&tty, rect.y);
	Cell *cell = tty.cells + row->offset + rect.x;

	for (int i = 0; i < rect.w; cell++, i++) {
		memclear(cell, 1, sizeof(*cell));
		cell->ucs4 = ucs4;
	}

	row->len = row_length(row);
}

void
stream_insert_cells(int ucs4, uint num)
{
	stream_shift_row_cells(tty.pos.y, tty.pos.x, num);
	stream_set_row_cells(tty.pos.y, tty.pos.x, ucs4, num);

	LOG_STATE;
}

void
stream_clear_row_cells(int row, int col, int num, bool delete, bool selective)
{
	Rect32 rect = get_bounded_rect(col, row, num, 1);
	int clearchar = ' ';

	if (delete) {
		/* if (rect.x + rect.w < rowlen(rect.y)) { */
		if (rect.x + rect.w < row_data(&tty, rect.y)->len) {
			stream_shift_row_cells(rect.y, rect.x + rect.w, -rect.w);
			return;
		}
		clearchar = '\0';
	}

	stream_set_row_cells(rect.y, rect.x, clearchar, rect.w);
}

void
clear_row(int row)
{
	Row *rp = row_data(&tty, row);
	stream_clear_row_cells(row, 0, tty.cols, true, false);
	rp->flags = DESC_NEWLINE;
}

void
stream_clear_rows(int row, int count)
{
	Rect32 rect = get_bounded_rect(0, row, tty.cols, count);

	// If we overshoot the screen bounds, cancel silently.
	// If we undershoot, the rect orgin clamps up to the top row;
	if (rect.h == 0 || rect.y < row) {
		return;
	}

	for (int y = rect.y; y - rect.y < rect.h; y++) {
		clear_row(y);
		if (y - rect.y + 1 == rect.h && rect.h < tty.hist.count) {
			row_data(&tty, y+1)->flags |= DESC_NEWLINE;
		}
	}

	LOG_STATE;
}

void
stream_move_cursor_pos(int cols, int rows)
{
	cursor_set_col_rel(cols);
	cursor_set_row_rel(rows);
	update_cursor();
}

void
stream_move_cursor_col(int cols)
{
	cursor_set_col_rel(cols);
	update_cursor();
}

void
stream_move_cursor_row(int rows)
{
	cursor_set_row_rel(rows);
	update_cursor();
}

void
stream_set_cursor_col(int col)
{
	cursor_set_col_abs(col);
	update_cursor();
}

void
stream_set_cursor_row(int row)
{
	cursor_set_row_abs(row);
	update_cursor();
}

void
stream_set_cursor_pos(int col, int row)
{
	cursor_set_col_abs(col);
	cursor_set_row_abs(row);
	update_cursor();
}

Cell *
stream_get_row(const TTY *this, uint idx, uint *len)
{
	if ((int)idx < this->hist.count) {
		Row *row = row_data(&tty, idx);
		if (this->cells[row->offset].ucs4) {
			if (len) *len = row->len;
			return this->cells + row->offset;
		}
	}

	if (len) *len = 0;

	return NULL;
}

