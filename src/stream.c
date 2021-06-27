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
static Row  *row_append(CellPos, bool);
static void  row_update(Row *);
static void *rows_realloc(size_t);
static void  set_cell_value(CellPos, int);
static void  screen_update(void);
static int   cursor_set_index(void);
static void  cursor_move(int, uint, int, uint);

static void dbg__print_row(int);

enum { PosAbs, PosRel };

static char mbuf[BUFSIZ];
static size_t msize = 0;

#define NEED_WRAP(w_) \
    ( tty.max_cols - ROW(C_ROW).len < (int)(w_) && ROW(C_ROW).len - C_COL <= (int)(w_) )

int
stream_write(int codept)
{
	int i_ = tty.i;

	/* fprintf(stderr, "%s * [%05d] = #%d\n%s", "\n", tty.i, codept, "\n"); */
	if (hist_is_empty()) {
		put_linefeed();
	}

	switch (codept) {
	case '\r':
		cursor_move(0, PosAbs, 0, PosRel);
		break;
	case '\n':
	case '\v':
	case '\f':
		put_linefeed();
		break;
	case '\b':
		cursor_move(-1, PosRel, 0, PosRel);
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

	return tty.i - i_;
}

void
stream_realloc(size_t max)
{
	if (max == 0) {
		xfree(tty.data);
		xfree(tty.rows.buf);
		xfree(tty.hist.buf);
		tty.max = tty.i = 0;
		tty.rows.max = tty.rows.n = 0;
	} else {
		if (!tty.data) {
			tty.data = CALLOC(tty.data, max);
			tty.rows.buf = CALLOC(tty.rows.buf, histsize);
			tty.rows.max = histsize;
			hist_init(histsize + 1);
		} else {
			tty.data = REALLOC(tty.data, max);
			if ((ssize_t)max > tty.max) {
				MEMCLEAR(&tty.data[tty.i], max - tty.i);
			}
		}
	}

	tty.max = max;
}

Row *
row_append(CellPos cell, bool is_static)
{
	tty.rows.n += (!is_static || (ROW(tty.rows.n).len && !hist_is_empty()));
	if (tty.rows.n == tty.rows.max) {
		rows_realloc(tty.rows.max * 2);
	}
	int index = tty.rows.n;

	ROW(index).offset = cell.idx;
	/* ROW(index).data = cell.ptr; */
	ROW(index).len  = 0;
	ROW(index).is_static = is_static;

	if (is_static) {
		hist_append(index);
	}

	DBG_PRINT(history, 0);

	return &ROW(index);
}

void
row_update(Row *row)
{
	const Row *lastrow = &ROW(tty.rows.n);
	ASSERT(row == lastrow);

	int idx = row->offset;
	int len = clamp(tty.c.i - idx, 0, tty.max_cols - 1);
	int tmp = row->len;

	if (!len) {
		row->len = (streamptr_s(row->offset)[0] == '\n') ? 1 : len;
	} else if (len < tty.max_cols - 1) {
		row->len = len;
	} else {
		row->len = (tty.c.i < tty.i) ? tty.max_cols : len;
	}

	return;
}

void *
rows_realloc(size_t max)
{
	tty.rows.buf = REALLOC(tty.rows.buf, max);
	if ((ssize_t)max > tty.rows.max) {
		MEMCLEAR(&tty.rows.buf[tty.rows.n], max - tty.rows.n);
	}

	tty.rows.max = max;

	return tty.rows.buf;
}

void
screen_update(void)
{
	// increase top row anchor if necessary
	tty.anchor = max(tty.anchor, tty.rows.n - tty.max_rows + 1);
	tty.anchor = CLAMP(tty.anchor, 0, tty.rows.n);

	tty.bot = tty.rows.n; // not affected by scrollback
	tty.top = MAX(0, tty.anchor - tty.scroll);
}

int
cursor_set_index(void)
{
	uint n = C_ROW;
	tty.c.i = streamidx_v(C_ROW, C_COL);

	ASSERT(BETWEEN(tty.c.i, 0, tty.i));

	while (n > 0 && !ROW(n).is_static) n--;

	tty.c.start = streamidx_v(n, 0);

	return tty.c.i;
}

void
cursor_move(int x, uint opt_x, int y, uint opt_y)
{
	uint dest_x = max(0, x + ((opt_x) ? tty.c.x : 0));
	uint dest_y = max(0, y + ((opt_y) ? tty.c.y : 0));

	screen_update();

	tty.c.y = clamp(dest_y, 0, tty.bot - tty.top);
	tty.c.x = clamp(dest_x, 0, tty.max_cols - 1);

	cursor_set_index();
}

void
set_cell_value(CellPos cell, int glyph)
{
	int tmp = cell.ptr[0];

	cell.ptr[0] = glyph;
	tty.i += ((int)cell.idx == tty.i);
	tty.data[tty.i] = 0;
}

void
put_linefeed(void)
{
	ASSERT(BETWEEN(tty.rows.n, 0, tty.rows.max));

	Row *row = &ROW(C_ROW);
	CellPos cell = cellpos_s(row->offset + row->len);

	row = row_append(cell, true);
	set_cell_value(cell, '\n');
	cursor_move(0, PosRel, +1, PosRel);
	row_update(row);

	DBG_PRINT(history, 0);
}

void
put_glyph(int glyph, uint width)
{
	ASSERT(width);
	// if we need to wrap, start the new row at the cell following the cursor, not the current cell
	int idx = tty.c.i + (NEED_WRAP(width) ? (int)width : 0);

	Row *row = &ROW(C_ROW);
	CellPos cell = cellpos_s(idx);
	set_cell_value(cell, glyph);

	if (idx > tty.c.i) { // wrap to next row
		row = row_append(cell, false);
		cursor_move(width, PosAbs, +1, PosRel);
	} else {
		cursor_move(+1, PosRel, 0, PosRel);
	}
	row_update(row);

	DBG_PRINT(state, 0);
}

void
put_tab(void)
{
	uint tab_len = TAB_LEN(tty.c.i - streamidx_v(tty.top + tty.c.y, 0));

	for (uint n = 0; n < tab_len; n++) {
		if (tty.c.x == tty.max_cols - 1)
			break;
		put_glyph(' ', 1);
	}
}

Cell *
streamptr_s(size_t offset)
{
	return &tty.data[offset];
}

Cell *
streamptr_v(size_t row, size_t col)
{
	return &tty.data[ROW(row).offset+col];
}

size_t
streamidx_v(size_t row, size_t col)
{
	return ROW(row).offset + col;
}

size_t
streamidx_p(const Cell *ptr)
{
	return MEMLEN(ptr, streamptr_v(0, 0));
}

CellPos
cellpos_p(Cell *ptr)
{
	ASSERT(ptr);

	CellPos cell = {
		.ptr = ptr,
		.idx = MEMLEN(cell.ptr, streamptr_v(0, 0))
	};

	ASSERT(cell.ptr);

	return cell;
}

CellPos
cellpos_s(size_t idx)
{
	ASSERT((int)idx <= tty.i);

	CellPos cell = {
		.ptr = streamptr_s(idx),
		.idx = idx
	};

	ASSERT(cell.ptr);

	return cell;
}

CellPos
cellpos_v(size_t col, size_t row)
{
	CellPos cell = {
		.ptr = streamptr_v(row, col),
		.idx = MEMLEN(cell.ptr, streamptr_v(0, 0))
	};

	ASSERT(cell.ptr);

	return cell;
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

void
dbg_print_state(const char *srcfile, const char *srcfunc, int srcline)
{
	fprintf(stderr, "buf: (%04d/%04d) | ",
	    tty.i, tty.max);
	fprintf(stderr, "crs: { (%04d/%04d), %03d|%03d } | ",
	    C_IDX, tty.i, tty.c.x, tty.c.y);
	fprintf(stderr, "rows: { (%03d/%03d), %03d|%03d -> %03d } | ",
	    tty.rows.n, tty.rows.max, tty.anchor, tty.top, tty.bot);
	fprintf(stderr, "hist: { (%03d|%03d/%03d), %02d, [^] = %04d, [@] = %04d } | ",
	    tty.hist.r, tty.hist.w, tty.hist.max, tty.hist.lap, HIST_FIRST, HIST_LAST);
	fprintf(stderr, "max: (%03d/%03d) | ",
	    tty.max_cols, tty.max_rows);
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
		for (int j = h.buf[i] + 1; j < tmp && j <= tty.rows.n; j++) {
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
	    C_IDX, tty.i, tty.c.start,
	    tty.c.x, tty.c.y, asciistr(tty.data[C_IDX]),
	    C_ROW, tty.rows.n, ROW(C_ROW).len, ROW(C_ROW).is_static, ROW_IS_BLANK(C_ROW),
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
