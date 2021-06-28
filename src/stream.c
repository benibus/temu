#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>

#include "utils.h"
#include "term.h"
#include "ring.h"

static void  put_glyph(int, uint);
static void  put_tab(void);
static void  put_linefeed(void);
static Row  *row_append(Cell *, bool);
static void  row_update(Row *);
static void *rows_realloc(size_t);
static void  set_cell_value(Cell *, int);
static void  screen_update(void);
static int   cursor_set_index(void);
static void  cursor_move(int, uint, int, uint);

static void dbg__print_row(int);

enum { POS_ABS, POS_REL };

static char mbuf[BUFSIZ];
static size_t msize = 0;

static inline Cell *
cellptr(size_t record, size_t offset)
{
	Cell *ptr = tty.data + tty.rows.buf[record].offset + offset;

	return (ptr <= tty.data + tty.size) ? ptr : NULL;
}

static inline Row *
rowptr(size_t record)
{
	Row *ptr = tty.rows.buf + record;

	return (ptr <= tty.rows.buf + tty.rows.count) ? ptr : NULL;
}

static inline size_t
celloffset(Cell *cell)
{
	ptrdiff_t diff = ptrcmp(cell, cellptr(0, 0), sizeof(*cell));
	ASSERT(BETWEEN(diff, 0, tty.size));

	return diff;
}

int
stream_write(int codept)
{
	int i_ = tty.size;

	if (hist_is_empty()) {
		put_linefeed();
	}

	switch (codept) {
	case '\r':
		cursor_move(0, POS_ABS, 0, POS_REL);
		break;
	case '\n':
	case '\v':
	case '\f':
		put_linefeed();
		break;
	case '\b':
		cursor_move(-1, POS_REL, 0, POS_REL);
		break;
	case '\t':
		put_tab();
		break;
	case 0x07:
		break;
	default:
#if 0
		if (isprint(codept))
#endif
			put_glyph(codept, 1);
		break;
	}

	DBG_PRINT(state, 0);

	return tty.size - i_;
}

void
stream_realloc(size_t max)
{
	if (max == 0) {
		xfree(tty.data);
		xfree(tty.rows.buf);
		xfree(tty.hist.buf);
		tty.max = tty.size = 0;
		tty.rows.max = tty.rows.count = 0;
	} else {
		if (!tty.data) {
			tty.data = xcalloc(max, sizeof(*tty.data));
			tty.rows.buf = xcalloc(histsize, sizeof(*tty.rows.buf));
			tty.rows.max = histsize;
			hist_init(histsize + 1);
		} else {
			tty.data = xrealloc(tty.data, max, sizeof(*tty.data));
			if ((ssize_t)max > tty.max) {
				MEMCLEAR(&tty.data[tty.size], max - tty.size);
			}
		}
	}

	tty.max = max;
}

Row *
row_append(Cell *cell, bool is_static)
{

	if (!is_static || (rowptr(tty.rows.count)->len && !hist_is_empty())) {
		tty.rows.count++;
	}

	if (tty.rows.count == tty.rows.max) {
		rows_realloc(tty.rows.max * 2);
	}

	Row *row = rowptr(tty.rows.count);

	row->offset = celloffset(cell);
	row->len = 0;
	row->is_static = is_static;

	if (row->is_static) {
		hist_append(row->offset);
	}

	return row;
}

size_t
stream_get_row(uint n, char **str)
{
	if ((int)n <= tty.rows.count) {
		Row *row = rowptr(n);
		char *cell = cellptr(0, row->offset);

		if (row->len && *cell != '\n') {
			if (str) *str = cell;
			return row->len;
		}
	}

	if (str) *str = NULL;
	return 0;
}

void
row_update(Row *row)
{
	ASSERT(row == rowptr(tty.rows.count));

	int offset = row->offset;
	int len = clamp(tty.c.offset - offset, 0, tty.maxcols - 1);
	int tmp = row->len;

	if (!len) {
		row->len = (*cellptr(0, row->offset) == '\n') ? 1 : len;
	} else if (len < tty.maxcols - 1) {
		row->len = len;
	} else {
		row->len = (tty.c.offset < tty.size) ? tty.maxcols : len;
	}

	return;
}

void *
rows_realloc(size_t max)
{
	tty.rows.buf = xrealloc(tty.rows.buf, max, sizeof(*tty.rows.buf));
	if ((ssize_t)max > tty.rows.max) {
		MEMCLEAR(&tty.rows.buf[tty.rows.count], max - tty.rows.count);
	}

	tty.rows.max = max;

	return tty.rows.buf;
}

void
screen_update(void)
{
	// increase top row anchor if necessary
	tty.anchor = max(tty.anchor, tty.rows.count - tty.maxrows + 1);
	tty.anchor = clamp(tty.anchor, 0, tty.rows.count);

	tty.bot = tty.rows.count; // not affected by scrollback
	tty.top = max(0, tty.anchor - tty.scroll);
}

int
cursor_set_index(void)
{
	Row *row = rowptr(tty.c.row);

	tty.c.offset = row->offset + tty.c.col;
	ASSERT(BETWEEN(tty.c.offset, 0, tty.size));

	for (int n = tty.c.row; n > tty.top; n--) {
		if ((row = rowptr(n))->is_static) {
			break;
		}
	}

	tty.c.start = row->offset;

	return tty.c.offset;
}

void
cursor_move(int x, uint opt_x, int y, uint opt_y)
{
	uint dest_x = max(0, x + ((opt_x) ? tty.c.col : 0));
	uint dest_y = max(0, y + ((opt_y) ? tty.c.row : 0));

	screen_update();

	tty.c.row = clamp(dest_y, tty.top, tty.bot);
	tty.c.col = clamp(dest_x, 0, tty.maxcols - 1);

	cursor_set_index();
}

void
set_cell_value(Cell *cell, int glyph)
{
	int tmp = *cell;
	*cell = glyph;
	tty.size += (cell == cellptr(0, tty.size));
	tty.data[tty.size] = 0;
}

void
put_linefeed(void)
{
	ASSERT(BETWEEN(tty.rows.count, 0, tty.rows.max));

	Cell *cell = cellptr(tty.c.row, rowptr(tty.c.row)->len);

	Row *row = row_append(cell, true);
	set_cell_value(cell, '\n');
	cursor_move(0, POS_REL, +1, POS_REL);

	row_update(row);

	DBG_PRINT(history, 0);
}

void
put_glyph(int glyph, uint width)
{
	ASSERT(width);
	// if we need to wrap, start the new row at the cell following the cursor, not the current cell
	Row *row = rowptr(tty.c.row);
	int offset = tty.c.offset;

	if (tty.maxcols - row->len < (int)width && row->len - tty.c.col <= (int)width) {
		offset += width;
	}

	Cell *cell = cellptr(0, offset);
	set_cell_value(cell, glyph);

	if (offset > tty.c.offset) { // wrap to next row
		row = row_append(cell, false);
		cursor_move(width, POS_ABS, +1, POS_REL);
	} else {
		cursor_move(+1, POS_REL, 0, POS_REL);
	}
	row_update(row);

	DBG_PRINT(state, 0);
}

void
put_tab(void)
{
	uint tab_len = TAB_LEN(tty.c.offset - rowptr(tty.c.row)->offset);

	for (uint n = 0; n < tab_len; n++) {
		if (tty.c.col == tty.maxcols - 1)
			break;
		put_glyph(' ', 1);
	}
}

void
screen_set_dirty(int row)
{
	tty.dirty = row;
}

void
screen_set_all_dirty(void)
{
	tty.dirty = 0;
}

void
screen_set_clean(void)
{
	tty.dirty = tty.bot - tty.top;
}

#if 0
void
dbg_print_state(const char *srcfile, const char *srcfunc, int srcline)
{
	fprintf(stderr, "buf: (%04d/%04d) | ",
	    tty.size, tty.max);
	fprintf(stderr, "crs: { (%04d/%04d), %03d|%03d } | ",
	    C_IDX, tty.size, tty.c.col, tty.c.row);
	fprintf(stderr, "rows: { (%03d/%03d), %03d|%03d -> %03d } | ",
	    tty.rows.count, tty.rows.max, tty.anchor, tty.top, tty.bot);
	fprintf(stderr, "hist: { (%03d|%03d/%03d), %02d, [^] = %04d, [@] = %04d } | ",
	    tty.hist.r, tty.hist.w, tty.hist.max, tty.hist.lap, HIST_FIRST, HIST_LAST);
	fprintf(stderr, "max: (%03d/%03d) | ",
	    tty.maxcols, tty.maxrows);
	fprintf(stderr, "scroll: %03d | ",
	    tty.scroll);
	fprintf(stderr, "%s:%d/%s()\n", srcfile, srcline, srcfunc);
}

void
dbg_print_history(const char *srcfile, const char *srcfunc, int srcline)
{
	HistBuf h = tty.hist;
	int i = h.r, next;

	for (int count = 0, i = h.r; i != h.w; count++, i = next) {
		next = WRAP(i, h.max);
		fprintf(stderr, "%s(%03d|%03d) ", (!count) ? "\n" : "", count, i);
		dbg__print_row(h.buf[i]);

		int tmp = (next != h.w) ? h.buf[next] : h.buf[i];
		for (int j = h.buf[i] + 1; j < tmp && j <= tty.rows.count; j++) {
			fprintf(stderr, "%-10s", "");
			dbg__print_row(j);
		}
	}
	fprintf(stderr, "%s:%d/%s()\n", srcfile, srcline, srcfunc);
}

void
dbg_print_cursor(const char *srcfile, const char *srcfunc, int srcline)
{
	fprintf(stderr,
	    "global: { (%04d/%04d), %04d } | "
	    "local: { %03d %03d, %s } | "
	    "row: { (%03d/%03d), len = %02d, %d|%d } | "
	    "%s:%d/%s()\n",
	    C_IDX, tty.size, tty.c.start,
	    tty.c.col, tty.c.row, asciistr(tty.data[C_IDX]),
	    C_ROW, tty.rows.count, ROW(C_ROW).len, ROW(C_ROW).is_static, ROW_IS_BLANK(C_ROW),
	    srcfile, srcline, srcfunc);
}

void
dbg__print_row(int idx)
{
	Row r = ROW(idx);
	Cell *data = streamptr_s(r.offset);

	fprintf(stderr, "row[%03d] { %03d, %d, %d, %p (%04lu) } | ",
	    idx, r.len, r.is_static, IS_BLANK(r), data, streamidx_v(idx, 0));
	fprintf(stderr, "\"");
	for (int i = 0; i < r.len; i++) {
		if (data[i] != '\n') {
			fputc(data[i], stderr);
		}
	}
	fprintf(stderr, "\"\n");
}
#endif
