#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>

#include "utils.h"
#include "term.h"
#include "ring.h"

typedef struct {
	int16 y, x;
	int32 i;
} CellPos;

typedef struct { int    x, y, w, h; } Rect;
typedef struct { int32  x, y, w, h; } Rect32;
typedef struct { int64  x, y, w, h; } Rect64;
typedef struct { uint   x, y, w, h; } URect;
typedef struct { uint32 x, y, w, h; } URect32;
typedef struct { uint64 x, y, w, h; } URect64;

static void put_glyph(int);
static void put_linefeed(void);
static void put_tab(void);
static Row *row_create(int, bool);
static Row *row_alloc(int, bool);
static void cursor_set_row_abs(int);
static void cursor_set_col_abs(int);
static void cursor_set_row_rel(int);
static void cursor_set_col_rel(int);
static void cursor_set_offset(int, int);
static void update_bounds(void);
static void update_cursor(void);
static size_t row_length(int);

#if 1
#define LOG_STATE do { \
	Row crow_ = tty.rows.buf[tty.c.row]; \
	msg_log("StreamState",                                         \
	    "txtstream: (%04d/%04d) | "                                \
	    "rowstream: (%04d/%04d) %04d->%04d | "                     \
	    "cursor: { %04d, %04d } = %04d | "                         \
	    "row: offset = (%04d/%04d), len = %03d, static ?= %-1d\n", \
	    tty.size, tty.max,                                         \
	    tty.rows.count, tty.rows.max, tty.rows.top, tty.rows.bot,  \
	    tty.c.col, tty.c.row, tty.c.offset,                        \
	    crow_.offset, tty.size, crow_.len, crow_.flags & DESC_NEWLINE); \
} while (0)
#else
#define LOG_STATE
#endif

#define celloffset(row,col) (((row) * tty.maxcols * sizeof(Cell)) + ((col) * sizeof(Cell)))
#define cellptr(row,col)    (tty.data + celloffset(row, col))
#define rowptr(row)         (tty.rows.buf + (row))
#define rowlen(row)         (rowptr(row)->len)

void dummy__(void) { LOG_STATE; }

Row *
row_create(int index, bool isnewline)
{
	ASSERT(index <= tty.rows.count);

	index = (index < 0) ? tty.rows.count : index;

	if (tty.rows.count == tty.rows.max) {
		int max = tty.rows.max * 2;
		tty.rows.buf = xrealloc(tty.rows.buf, max, sizeof(*tty.rows.buf));
		memclear(rowptr(tty.rows.count), max - tty.rows.count, sizeof(*tty.rows.buf));
		tty.rows.max = max;
	}

	Row *rp = rowptr(index);

	if (!index) {
		rp[0].offset = 0;
	} else if (index == tty.rows.count) {
		rp[0].offset = rp[-1].offset + celloffset(1, 0);
	} else {
		memmove(rp + 1, rp, sizeof(*rp) * (tty.rows.count - index));
		memclear(rp, 1, sizeof(*rp));
		rp[0].offset = rp[1].offset;
		for (int n = index; ++n < tty.rows.count; ) {
			rowptr(n)->offset += tty.maxcols * sizeof(*tty.data);
		}
	}

	rp->len = 0;
	rp->flags |= (isnewline) ? DESC_NEWLINE : 0;

	tty.size = celloffset(tty.rows.count + 1, 0);
	tty.rows.count++;

	update_bounds();

	return rp;
}

Row *
row_alloc(int index, bool isnewline)
{
	if (index < 0 || index >= tty.rows.count)
		return row_create(index, isnewline);

	Row *rp = tty.rows.buf + index;

	if (!index) {
		rp[0].offset = 0;
	} else {
		rp[0].offset = rp[-1].offset + celloffset(1, 0);
	}

	rp->flags |= (isnewline) ? DESC_NEWLINE : 0;

	return rp;
}

int
stream_write(int u)
{
	int offset = tty.c.offset;

	if (tty.rows.count == 0) {
		if (!row_create(0, true))
			return -1;
		memclear(&tty.c, 1, sizeof(tty.c));
	}

	switch (u) {
	case '\r':
		cursor_set_col_abs(0);
		break;
	case '\n':
	case '\v':
	case '\f':
		put_linefeed();
		break;
	case '\b':
		cursor_set_col_rel(-1);
		break;
	case '\t':
		put_tab();
		break;
	case 0x07:
		break;
	default:
		put_glyph(u);
		break;
	}

	update_cursor();

	LOG_STATE;

	return tty.c.offset - offset;
}

void
put_glyph(int u)
{
	Row *rp = rowptr(tty.c.row);
	int rcurr = tty.c.row;
	int rnext = rcurr;
	int column = tty.c.col;

	if (column + 1 < tty.maxcols) {
		tty.c.wrap = false;
	} else if (!tty.c.wrap) {
		tty.c.wrap = true;
	} else {
		rp = row_alloc(++rnext, false);
		ASSERT((int)rp->offset == tty.c.offset + 1);
		tty.c.wrap = false;
		column = 0;
	}

	int base = celloffset(rnext, 0);
	int occupant = tty.data[base+column];

	tty.data[base+column] = u;
	if (!occupant) rp->len = column + 1;

	if (rnext > rcurr) {
		cursor_set_col_abs(0);
		cursor_set_row_rel(1);
	}
	if (!tty.c.wrap) {
		cursor_set_col_rel(1);
	}
}

size_t
row_length(int row)
{
	Cell *cell = cellptr(row, 0);
#if 1
	return strnlen(cell, tty.maxcols);
#else
	return (cell[tty.maxcols-1]) ? (size_t)tty.maxcols : strlen(cell);
#endif
}

void
put_linefeed(void)
{
	Row *rp;

	rp = row_alloc(tty.c.row + 1, true);
	(void)rp;

	cursor_set_col_abs(0);
	cursor_set_row_rel(1);
}

void
put_tab(void)
{
	for (int n = 0; tty.c.col + 1 < tty.maxcols; n++) {
		if (tty.tabs[tty.c.col] && n > 0)
			break;
		put_glyph(' ');
	}
}

void
update_bounds(void)
{
	tty.rows.bot = tty.rows.count - 1;
	tty.rows.top = max(tty.rows.bot - tty.maxrows + 1, 0);
}

void
cursor_set_offset(int col, int row)
{
	tty.c.offset = celloffset(row, col);
}

void
cursor_set_col_rel(int cols)
{
	tty.c.col = clamp(tty.c.col + cols, 0, tty.maxcols - 1);
}

void
cursor_set_row_rel(int rows)
{
	tty.c.row = clamp(tty.c.row + rows, tty.rows.top, tty.rows.bot);
}

void
cursor_set_col_abs(int col)
{
	tty.c.col = clamp(col, 0, tty.maxcols - 1);
}

void
cursor_set_row_abs(int row)
{
	tty.c.row = clamp(row, tty.rows.top, tty.rows.bot);
}

void
update_cursor(void)
{
	cursor_set_offset(tty.c.col, tty.c.row);
}


void
stream_realloc(size_t max)
{
	if (max == 0) {
		FREE(tty.data);
		FREE(tty.attr);
		FREE(tty.rows.buf);
		FREE(tty.hist.buf);
		FREE(tty.tabs);
		tty.max = tty.size = 0;
		tty.rows.max = tty.rows.count = 0;
	} else {
		if (!tty.data) {
			ASSERT(!tty.attr);
			tty.data = xcalloc(max, sizeof(*tty.data));
			tty.attr = xcalloc(max, sizeof(*tty.attr));
			tty.rows.buf = xcalloc(histsize, sizeof(*tty.rows.buf));
			tty.rows.max = histsize;
			hist_init(histsize + 1);
			tty.tabs = xcalloc(tty.maxcols, sizeof(*tty.tabs));
		} else {
			tty.data = xrealloc(tty.data, max, sizeof(*tty.data));
			tty.attr = xrealloc(tty.attr, max, sizeof(*tty.attr));
			if ((ssize_t)max > tty.max) {
				memclear(tty.data + tty.size, max - tty.size, sizeof(*tty.data));
				memclear(tty.attr + tty.size, max - tty.size, sizeof(*tty.attr));
			}
		}
	}

	tty.max = max;
}

static inline Rect32
get_bounded_rect(int x, int y, int w, int h)
{
	Rect32 rect = { 0 };

	rect.x = CLAMP(x, 0, tty.maxcols);
	rect.y = CLAMP(y, tty.rows.top, tty.rows.bot);
	rect.w = CLAMP(w, 0, tty.maxcols - rect.x);
	rect.h = CLAMP(h, 0, tty.rows.bot - rect.y + 1);

	return rect;
}

void
stream_shift_row_cells(int row, int col, int delta_)
{
	Rect32 rect1 = get_bounded_rect(col, row, tty.maxcols, 1);
	Rect32 rect2 = get_bounded_rect(rect1.x + delta_, rect1.y, tty.maxcols, 1);

	int offset = celloffset(rect1.y, rect1.x);
	int delta = rect2.x - rect1.x;
	int count = MIN(rect1.w, rect2.w);

	memcshift(tty.data + offset, delta, count, sizeof(*tty.data));
	memcshift(tty.attr + offset, delta, count, sizeof(*tty.attr));

	rowptr(rect1.y)->len = row_length(rect1.y);
}

void
stream_set_row_cells(int row, int col, int c, int num)
{
	Rect32 rect = get_bounded_rect(col, row, num, 1);

	if (rect.w == 0) return;

	int offset = celloffset(rect.y, rect.x);
	memset(tty.data + offset, c, rect.w * sizeof(*tty.data));
	memset(tty.attr + offset, 0, rect.w * sizeof(*tty.attr));

	rowptr(rect.y)->len = row_length(rect.y);
}

void
stream_insert_cells(int c, uint num)
{
	stream_shift_row_cells(tty.c.row, tty.c.col, num);
	stream_set_row_cells(tty.c.row, tty.c.col, c, num);

	LOG_STATE;
}

void
stream_clear_row_cells(int row, int col, int num, bool delete, bool selective)
{
	Rect32 rect = get_bounded_rect(col, row, num, 1);
	int clearchar = ' ';

	if (delete) {
		if (rect.x + rect.w < rowlen(rect.y)) {
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
	memclear(tty.rows.buf + row, 1, sizeof(*tty.rows.buf));
	stream_clear_row_cells(row, 0, tty.maxcols, true, false);
}

void
stream_clear_rows(int row, int count)
{
	Rect32 rect = get_bounded_rect(0, row, tty.maxcols, count);

	// If we overshoot the screen bounds, cancel silently.
	// If we undershoot, the rect orgin clamps up to the top row;
	if (rect.h == 0 || rect.y < row) {
		return;
	}

	for (int y = rect.y; y - rect.y < rect.h; y++) {
		clear_row(y);
		if (y - rect.y + 1 == rect.h && rect.h < tty.rows.count) {
			rowptr(y+1)->flags |= DESC_NEWLINE;
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

size_t
stream_get_row(uint n, char **str)
{
	if ((int)n < tty.rows.count) {
		Cell *cell = cellptr(n, 0);
		if (*cell) {
			if (str) *str = cell;
			return tty.rows.buf[n].len;
		}
	}

	if (str) *str = NULL;
	return 0;
}

