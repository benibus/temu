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
	Row *row;
	Cell *data;
	Pos pos;
	uint offset;
} CellStatus;

static void put_glyph(TTY *, uint32, ColorSet, uint16);
static void put_linefeed(TTY *);
static void put_tab(TTY *, ColorSet, uint16);
static Row *row_prepare(TTY *, int, bool);
static uint row_compute_length(TTY *, Row *);
static void update_bounds(TTY *);

#if 0
#define LOG_STATE do { \
	Row *crow_ = row_data(tty, tty.cursor.y); \
	msg_log("StreamState",                                         \
	    "txtstream: (%04d/%04d) | "                                \
	    "rowstream: (%04d/%04d) %04d->%04d | "                     \
	    "cursor: { %04d, %04d } = %04d | "                         \
	    "row: offset = (%04d/%04d), len = %03d, static ?= %-1d\n", \
	    tty.size, tty.max,                                         \
	    tty.hist.count, tty.hist.max, tty.top, tty.bot,  \
	    tty.cursor.x, tty.cursor.y, tty.c.offset,                        \
	    crow_->offset, tty.size, crow_->len, crow_->flags & DESC_NEWLINE); \
} while (0)
#else
#define LOG_STATE
#endif

void dummy__(void) { LOG_STATE; }

static inline Row *
row_data(TTY *tty, uint y)
{
	return ring_data(&tty->hist, y);
}

static inline CellStatus
cell_status(TTY *tty, int x, int y)
{
	CellStatus res = { 0 };
	ASSERT(y >= 0 && y < tty->hist.count);
	ASSERT(x >= 0 && x < tty->cols);

	res.pos.x = x;
	res.pos.y = y;
	res.row = row_data(tty, y);
	res.offset = res.row->offset + res.pos.x;
	res.data = tty->cells + res.offset;

	return res;
}

TTY *
stream_init(TTY *tty, uint ncols, uint nrows, uint nhist_)
{
	ASSERT(tty);
	ASSERT(ncols && nrows && nhist_);

	ring_init(&tty->hist, nhist_, sizeof(Row));
	ASSERT(tty->hist.data && tty->hist.max > 0);

	uint nhist = tty->hist.max;
	uint ncells = ncols * nhist;

	tty->cells = xcalloc(ncells, sizeof(*tty->cells));
	tty->tabstops = xcalloc(ncols, sizeof(*tty->tabstops));

	tty->cols = ncols;
	tty->rows = nrows;
	tty->max = ncells;

	return tty;
}

Cell *
stream_get_row(TTY *tty, uint idx, uint *len)
{
	if ((int)idx < tty->hist.count) {
		Row *row = row_data(tty, idx);
		if (tty->cells[row->offset].ucs4) {
			if (len) *len = row->len;
			return tty->cells + row->offset;
		}
	}

	if (len) *len = 0;

	return NULL;
}

int
stream_write(TTY *tty, uint32 ucs4, ColorSet color, uint16 flags)
{
	int offset = tty->cursor.y * tty->cols + tty->cursor.x;

	if (!tty->hist.count) {
		row_prepare(tty, 0, true);
	}

	switch (ucs4) {
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
			put_tab(tty, color, flags);
			break;
		}
		case 0x07: {
			break;
		}
		default: {
			put_glyph(tty, ucs4, color, flags);
			break;
		}
	}

	LOG_STATE;

	return (tty->cursor.y * tty->cols + tty->cursor.x) - offset;
}

void
put_glyph(TTY *tty, uint32 ucs4, ColorSet color, uint16 flags)
{
	Pos pos = (Pos){ tty->cursor.x, tty->cursor.y };

	if (pos.x + 1 < tty->cols) {
		tty->cursor.wrap_next = false;
	} else if (!tty->cursor.wrap_next) {
		tty->cursor.wrap_next = true;
	} else {
		tty->cursor.wrap_next = false;
		pos.x = 0;
		pos.y++;
	}

	Row *row = (pos.y != tty->cursor.y)
	         ? row_prepare(tty, pos.y, false)
	         : row_data(tty, pos.y);
	int offset = row->offset + pos.x;

	if (!tty->cells[offset].ucs4) {
		row->len = pos.x + 1;
	}
	tty->cells[offset].ucs4  = ucs4;
	tty->cells[offset].color = color;
	tty->cells[offset].attr  = flags;

	if (pos.y != tty->cursor.y) {
		cmd_set_cursor_x(tty, 0);
		cmd_move_cursor_y(tty, 1);
	}
	if (!tty->cursor.wrap_next) {
		cmd_move_cursor_x(tty, 1);
	}
}

Row *
row_prepare(TTY *tty, int idx, bool isnewline)
{
	ASSERT(tty);
	Row *row = NULL;

	if (idx < tty->hist.count) {
		row = row_data(tty, idx);
	} else {
		row = ring_push(&tty->hist, NULL);
		if (!RING_FULL(&tty->hist)) {
			ASSERT(tty->size <= tty->max);
		} else {
			memclear(tty->cells + row->offset,
			         tty->cols,
			         sizeof(*tty->cells));
		}
		if (tty->size < tty->max) {
			tty->size += tty->cols;
		}
		row->len = 0;
	}

	row->flags = 0;
	row->flags |= (isnewline) ? DESC_NEWLINE : 0;

	update_bounds(tty);

	return row;
}

uint
row_compute_length(TTY *tty, Row *row)
{
	Cell *cells = tty->cells + row->offset;

	for (int n = 0; n < tty->cols; n++) {
		if (!cells[n].ucs4) {
			return n;
		}
	}

	return tty->cols;
}

void
put_linefeed(TTY *tty)
{
	row_prepare(tty, tty->cursor.y + 1, true);

	cmd_set_cursor_x(tty, 0);
	cmd_move_cursor_y(tty, 1);
}

void
put_tab(TTY *tty, ColorSet color, uint16 flags)
{
	for (int n = 0; tty->cursor.x + 1 < tty->cols; n++) {
		if (tty->tabstops[tty->cursor.x] && n > 0)
			break;
		put_glyph(tty, ' ', color, flags);
	}
}

void
update_bounds(TTY *tty)
{
	ASSERT(tty->hist.count);
	tty->bot = tty->hist.count - 1;
	tty->top = max(tty->bot - tty->rows + 1, 0);
}

void
cmd_set_cells(TTY *tty, const Cell *spec, uint x, uint y, uint n)
{
	CellStatus cell = cell_status(tty, x, y);
	int x0 = cell.pos.x;
	int x1 = MIN(x0 + (int)n, tty->cols);

	for (int i = 0; i < (x1 - x0); i++) {
		memcpy(cell.data + i, spec, sizeof(*cell.data));
	}
	cell.row->len = row_compute_length(tty, cell.row);
}

void
cmd_shift_cells(TTY *tty, uint x, uint y, int dx_)
{
	Cell templ = { 0 };
	CellStatus cell = cell_status(tty, x, y);

	int x0 = cell.pos.x;
	int x1 = CLAMP(x0 + dx_, 0, tty->cols);
	int dx = x1 - x0;
	int nx = tty->cols - MAX(x0, x1);
	int x2 = (dx < 0) ? x1 + nx : x0;

	memmove(cell.data + dx, cell.data, nx * sizeof(*cell.data));

	if (dx > 0) {
		templ.ucs4 = ' ';
		templ.width = 1;
	}
	cmd_set_cells(tty, &templ, x2, cell.pos.y, ABS(dx));
}

void
cmd_insert_cells(TTY *tty, const Cell *spec, uint n)
{
	uint cx = tty->cursor.x;
	uint cy = tty->cursor.y;

	cmd_shift_cells(tty, cx, cy, n);
	cmd_set_cells(tty, spec, cx, cy, n);
}

void
cmd_clear_rows(TTY *tty, uint y, uint n)
{
	Cell templ = { 0 };
	CellStatus cell = { 0 };

	for (uint i = 0; i < n && (int)(y + i) < tty->hist.count; i++) {
		cell = cell_status(tty, 0, y + i);
		cmd_set_cells(tty, &templ, 0, y + i, tty->cols);
		cell.row->flags = DESC_NEWLINE;
	}

	if (cell.row && cell.pos.y + 1 < tty->hist.count) {
		row_data(tty, cell.pos.y + 1)->flags |= DESC_NEWLINE;
	}
}

void
cmd_move_cursor_x(TTY *tty, int dx)
{
	Cell templ = { .ucs4 = ' ', .width = 1 };
	CellStatus cell = cell_status(tty, tty->cursor.x, tty->cursor.y);

	int x0 = cell.pos.x;
	int x1 = CLAMP(x0 + dx, 0, tty->cols - 1);

	for (int i = 0; i < (x1 - x0); i++) {
		if (!cell.data[i].ucs4) {
			memcpy(cell.data + i, &templ, sizeof(*cell.data));
			cell.row->len++;
		}
	}

	tty->cursor.x = x1;
}

void
cmd_move_cursor_y(TTY *tty, int dy)
{
	tty->cursor.y = CLAMP(tty->cursor.y + dy, tty->top, tty->bot);
}

void
cmd_set_cursor_x(TTY *tty, uint x)
{
	tty->cursor.x = MIN((int)x, tty->cols - 1);
}

void
cmd_set_cursor_y(TTY *tty, uint y)
{
	tty->cursor.y = CLAMP((int)y, tty->top, tty->bot);
}

void
dbg_dump_history(TTY *tty)
{
	int n = 0;

	putchar('\n');

	for (int y = 0; y < tty->hist.count; n++, y++) {
		Row *row = row_data(tty, y);
		Cell *cells = tty->cells + row->offset;
		int idx = ring_index(&tty->hist, y);
		printf("[%03d|%03d] (%03d) $%05u/%05u 0x%.2x %c |",
		       idx, y, row->len, row->offset, tty->size, row->flags,
		       (y == tty->top) ? '>' :
		       ((y == tty->bot) ? '<' : ' '));
		for (int x = 0; x < tty->cols; x++) {
			char *esc = (x == row->len) ? "\033[97;41m" : "";
			printf("%s%lc\033[0m", esc, (cells[x].ucs4) ? cells[x].ucs4 : L' ');
		}
		printf("|\n");
	}

	if (n) putchar('\n');
}

