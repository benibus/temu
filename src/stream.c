#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>

#include "utils.h"
#include "term.h"
#include "ring.h"

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
	    crow_.offset, tty.size, crow_.len, crow_.newline);         \
} while (0)
#else
#define LOG_STATE
#endif

#define celloffset(row,col) (((row) * tty.maxcols * sizeof(Cell)) + ((col) * sizeof(Cell)))
#define cellptr(row,col)    (tty.data + celloffset(row, col))
#define rowptr(row)         (tty.rows.buf + (row))

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
	rp->newline = isnewline;

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

	rp->newline = isnewline;

	return rp;
}

int
stream_write(int u)
{
	int size = tty.size;

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

	return tty.size - size;
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
		FREE(tty.rows.buf);
		FREE(tty.hist.buf);
		FREE(tty.tabs);
		tty.max = tty.size = 0;
		tty.rows.max = tty.rows.count = 0;
	} else {
		if (!tty.data) {
			tty.data = xcalloc(max, sizeof(*tty.data));
			tty.rows.buf = xcalloc(histsize, sizeof(*tty.rows.buf));
			tty.rows.max = histsize;
			hist_init(histsize + 1);
			tty.tabs = xcalloc(tty.maxcols, sizeof(*tty.tabs));
		} else {
			tty.data = xrealloc(tty.data, max, sizeof(*tty.data));
			if ((ssize_t)max > tty.max) {
				memclear(tty.data + tty.size, max - tty.size, sizeof(*tty.data));
			}
		}
	}

	tty.max = max;
}
void
stream_clear_columns(int col, int num)
{
	ASSERT(UXCEIL(col, tty.maxcols));
	ASSERT(num >= 0);

	if (num == 0) return;

	int start = celloffset(tty.c.row, 0);
	int max = start + row_length(tty.c.row);
	int beg = min(start + col, max - 1);
	int end = min(beg + num, max);
	int delta = end - beg;

	memclear(tty.data + beg, delta, sizeof(*tty.data));
	memshift(tty.data + end, -delta, max - end, sizeof(*tty.data));

	if (beg < tty.c.offset) {
		cursor_set_col_abs(beg);
		update_cursor();
	}

	rowptr(tty.c.row)->len -= delta;

	LOG_STATE;
}

void
stream_clear_from_cursor(int arg)
{
	struct { int col, row; } pos1, pos2, cpos;

	switch (arg) {
	case -1: // cursor to top corner of screen
		pos1.row = tty.rows.top, pos1.col = 0;
		pos2.row = tty.c.row,    pos2.col = tty.c.col;
		cpos = pos2;
		break;
	case +1: // cursor to bottom corner of screen
		pos1.row = tty.c.row,    pos1.col = tty.c.col;
		pos2.row = tty.rows.bot, pos2.col = tty.maxcols;
		cpos = pos1;
		break;
	default: // entire screen (cursor moves to orgin)
		pos1.row = tty.rows.top, pos1.col = 0;
		pos2.row = tty.rows.bot, pos2.col = tty.maxcols;
		cpos = pos1;
		break;
	}

	int beg = celloffset(pos1.row, pos1.col);
	int end = celloffset(pos2.row, pos2.col);
	int delta = end - beg;

	if (delta == 0) return;

	memclear(tty.data + beg, delta, sizeof(*tty.data));
	// empty rows store some persistent attributes
	for (int n = pos1.row; n <= pos2.row; n++) {
		rowptr(n)->len = 0;
		rowptr(n)->newline = (n <= cpos.row);
	}

	rowptr(cpos.row)->len = cpos.col;
	cursor_set_col_abs(cpos.col);
	cursor_set_row_abs(cpos.row);

	update_cursor();
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

