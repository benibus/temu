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

typedef struct {
	Cell *data;
	Row *row;
	Vec2I pos;
	uint offset;
} CellDesc;

static CellDesc celldesc(TTY *, int, int);
static void put_glyph(TTY *, const Cell *);
static void put_linefeed(TTY *);
static void put_tab(TTY *, ColorSet, uint16);
static bool row_init(TTY *, Row *, uint16);
static Row *row_alloc(TTY *, int, uint8);
static uint row_compute_length(TTY *, Row *);
static void screen_reset(TTY *);

#if 0
#define DBG_PRINT_STATE(t) do { \
	CellDesc desc = celldesc((t), 0, (t)->cursor.y);            \
	msg_log("StreamState",                                      \
	        "txtstream: (%03d/%03d), %04d | "                   \
	        "rowstream: (%04d/%04d) %04d->%04d | "              \
	        "cursor: { %04d, %04d } | "                         \
	        "row: index = %04d, len = %03d, static ?= %-1d\n",  \
	        (t)->cols, (t)->pitch,                              \
	        (t)->max,                                           \
	        (t)->hist.count, (t)->hist.max, (t)->top, (t)->bot, \
	        (t)->cursor.x, (t)->cursor.y,                       \
	        desc.row->index, desc.row->len, desc.row->flags & DESC_NEWLINE); \
} while (0)
#else
#define DBG_PRINT_STATE(...)
#endif

void dummy__(TTY *tty) { (void)tty; DBG_PRINT_STATE(tty); }

inline CellDesc
celldesc(TTY *tty, int x, int y)
{
	CellDesc desc = { 0 };
	ASSERT(y >= 0 && y < tty->hist.count);
	ASSERT(x >= 0 && x < tty->cols);

	desc.row = ring_data(&tty->hist, y);
	desc.offset = desc.row->index * tty->pitch + x;
	desc.data = tty->cells + desc.offset;
	desc.pos.x = x;
	desc.pos.y = y;

	return desc;
}

TTY *
stream_init(TTY *tty, uint ncols, uint nrows, uint nhist)
{
	ASSERT(tty);
	ASSERT(ncols && nrows && nhist);

	ring_init(&tty->hist, nhist, sizeof(Row));
	ASSERT(tty && tty->hist.data && tty->hist.max > 0);

	int ncells = tty->hist.max * ncols;

	tty->cells = xcalloc(ncells, sizeof(*tty->cells));
	tty->tabstops = xcalloc(ncols, sizeof(*tty->tabstops));

	tty->cols = ncols;
	tty->rows = nrows;
	tty->pitch = tty->cols;
	tty->max = ncells;

	return tty;
}

void
stream_resize(TTY *tty, int cols, int rows)
{
	Vec4I dim = {
		.x1 = tty->cols,
		.y1 = tty->rows,
		.x2 = MAX(0, cols),
		.y2 = MAX(0, rows)
	};

	if (dim.x2 == dim.x1 && dim.y2 == dim.y1) {
		return;
	}

	int pitch = MAX(dim.x2, tty->pitch);
	int max = tty->max;

	if (pitch > tty->pitch) {
		max = tty->hist.max * pitch;

		Cell *tmp = xcalloc(max, sizeof(*tmp));
		Cell *src = tty->cells;
		Cell *dst = tmp;

		for (int n = 0; n < tty->hist.count + 1; n++) {
			memcpy(dst, src, tty->pitch * sizeof(*src));
			src += tty->pitch;
			dst += pitch;
		}

		tty->tabstops = xrealloc(tty->tabstops,
		                         pitch,
		                         sizeof(*tty->tabstops));
		free(tty->cells);
		tty->cells = tmp;
	}

	for (int i = tty->pitch; i < pitch; i++) {
		tty->tabstops[i] = (i && i % tty->tablen) ? 1 : 0;
	}

	tty->cols = dim.x2;
	tty->rows = dim.y2;
	tty->pitch = pitch;
	tty->max = max;

	screen_reset(tty);
}

Cell *
stream_get_row(TTY *tty, int y, uint *len)
{
	if (BETWEEN(y + 1, 1, tty->hist.count)) {
		CellDesc desc = celldesc(tty, 0, y);
		if (desc.data[0].ucs4) {
			if (len) {
				*len = MIN(desc.row->len, tty->cols);
			}
			return desc.data;
		}
	}

	if (len) *len = 0;

	return NULL;
}

int
stream_write(TTY *tty, const Cell *templ)
{
	int offset = tty->pos.y * tty->cols + tty->pos.x;

	if (RING_EMPTY(&tty->hist)) {
		row_alloc(tty, 0, INIT_IFOLD|INIT_IFNEW);
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
	Vec2I pos = tty->pos;

	if (pos.x + 1 < tty->cols) {
		tty->cursor.wrap = false;
	} else if (!tty->cursor.wrap) {
		tty->cursor.wrap = true;
	} else {
		tty->cursor.wrap = false;
		pos.x = 0;
		pos.y++;
	}

	Row *row = row_alloc(tty, pos.y, INIT_NEVER);
	if (pos.y > tty->pos.y) {
		row_init(tty, row, LINE_WRAPPED);
	}

	int offset = row->index * tty->pitch + pos.x;

	if (!tty->cells[offset].ucs4) {
		row->len = pos.x + 1;
	}
	memcpy(tty->cells + offset, templ, sizeof(*templ));

	if (pos.y != tty->pos.y) {
		cmd_set_cursor_x(tty, 0);
		cmd_move_cursor_y(tty, 1);
	}
	if (!tty->cursor.wrap) {
		cmd_move_cursor_x(tty, 1);
	}
}

bool
row_init(TTY *tty, Row *row, uint16 flags)
{
	memclear(tty->cells + row->index * tty->pitch,
	         tty->pitch,
	         sizeof(*tty->cells));
	row->len = 0;
	row->flags = flags;

	return true;
}

Row *
row_alloc(TTY *tty, int n, uint8 opt)
{
	Row *row = NULL;

	if (n >= 0 && n < tty->hist.count) {
		row = ring_data(&tty->hist, n);
		if (opt & INIT_IFOLD) {
			row_init(tty, row, 0);
		}
	} else if (n == tty->hist.count) {
		row = ring_push(&tty->hist, NULL);
		if (opt & INIT_IFNEW) {
			row_init(tty, row, 0);
		}
		screen_reset(tty);
	}

	return row;
}

uint
row_compute_length(TTY *tty, Row *row)
{
	Cell *cells = tty->cells + row->index * tty->pitch;

	for (int n = 0; n < tty->pitch; n++) {
		if (!cells[n].ucs4) {
			return n;
		}
	}

	return tty->pitch;
}

void
put_linefeed(TTY *tty)
{
	if (row_alloc(tty, tty->pos.y + 1, INIT_IFNEW)) {
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
	tty->bot = DEFAULT(tty->hist.count, 1) - 1;
	tty->top = MAX(tty->bot - tty->rows + 1, 0);
}

void
cmd_set_cells(TTY *tty, const Cell *spec, uint x, uint y, uint n)
{
	CellDesc desc = celldesc(tty, x, y);
	int x0 = desc.pos.x;
	int x1 = MIN(x0 + (int)n, tty->pitch);

	for (int i = 0; i < (x1 - x0); i++) {
		memcpy(desc.data + i, spec, sizeof(*desc.data));
	}
	desc.row->len = row_compute_length(tty, desc.row);
}

void
cmd_shift_cells(TTY *tty, uint x, uint y, int dx_)
{
	Cell templ = { 0 };
	CellDesc desc = celldesc(tty, x, y);

	int x0 = desc.pos.x;
	int x1 = CLAMP(x0 + dx_, 0, tty->pitch);
	int dx = x1 - x0;
	int nx = tty->pitch - MAX(x0, x1);
	int x2 = (dx < 0) ? x1 + nx : x0;

	memmove(desc.data + dx, desc.data, nx * sizeof(*desc.data));

	if (dx > 0) {
		templ.ucs4 = ' ';
		templ.width = 1;
	}
	cmd_set_cells(tty, &templ, x2, desc.pos.y, ABS(dx));
}

void
cmd_insert_cells(TTY *tty, const Cell *spec, uint n)
{
	uint cx = tty->pos.x;
	uint cy = tty->pos.y;

	cmd_shift_cells(tty, cx, cy, n);
	cmd_set_cells(tty, spec, cx, cy, n);
}

void
cmd_clear_rows(TTY *tty, uint y, uint n)
{
	Cell templ = { 0 };
	CellDesc desc = { 0 };

	for (uint i = 0; i < n && (int)(y + i) < tty->hist.count; i++) {
		desc = celldesc(tty, 0, y + i);
		cmd_set_cells(tty, &templ, 0, y + i, tty->pitch);
		desc.row->flags = 0;
	}

	if (desc.row && desc.pos.y + 1 < tty->hist.count) {
		Row *row = ring_data(&tty->hist, desc.pos.y + 1);
		row->flags &= ~LINE_WRAPPED;
	}
}

void
cmd_move_cursor_x(TTY *tty, int dx)
{
	Cell templ = { .ucs4 = ' ', .width = 1 };
	CellDesc desc = celldesc(tty, tty->pos.x, tty->pos.y);

	int x0 = desc.pos.x;
	int x1 = CLAMP(x0 + dx, 0, tty->cols - 1);

	for (int i = 0; i < (x1 - x0); i++) {
		if (!desc.data[i].ucs4) {
			memcpy(desc.data + i, &templ, sizeof(*desc.data));
			desc.row->len++;
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

	for (int y = 0; y < tty->hist.count; n++, y++) {
		CellDesc desc = celldesc(tty, 0, y);
		int idx = ring_index(&tty->hist, y);

		printf("[%03d|%03d] (%03d) #%05u$%05u/%05u 0x%.2x %c |",
		       idx, y, desc.row->len, desc.row->index, desc.offset, tty->max, desc.row->flags,
		       (y == tty->top) ? '>' :
		       ((y == tty->bot) ? '<' : ' '));
		for (int x = 0; x < tty->cols; x++) {
			char *esc = (x == desc.row->len) ? "\033[97;41m" : "";
			printf("%s%lc\033[0m", esc,
			       (desc.data[x].ucs4) ? desc.data[x].ucs4 : L' ');
		}
		printf("|\n");
	}

	if (n) putchar('\n');

	DBG_PRINT_STATE(tty);
}

