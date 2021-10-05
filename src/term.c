#include "utils.h"
#include "term.h"
#include "parser.h"
#include "pty.h"

#define LINE_DEFAULT    (0)
#define LINE_WRAPPED    (1 << 0)
#define LINE_HASTABS    (1 << 1)
#define LINE_HASMULTI   (1 << 2)
#define LINE_HASCOMPLEX (1 << 3)

#define RING_ISFULL(r)  ((r)->count + 1 >= (r)->limit)
#define RING_ISEMPTY(r) ((r)->count == 0)

#define CELLINIT(t) (Cell){ \
	.ucs4  = ' ',             \
	.bg    = (t)->default_bg, \
	.fg    = (t)->default_fg, \
	.type  = CellTypeNormal,  \
	.width = 1,               \
	.attr  = 0,               \
}

// Translate row in screen-space to historical line index
#define row2hidx(term,row) ((term)->top + (row) - (term)->scrollback)
// Translate historical line index to row in screen-space
#define hidx2row(term,idx) ((idx) - (term)->top + (term)->scrollback)

// Point is visible in screen-space
#define ISVISIBLE(t,x,y) ((x) < (t)->cols && (y) <= ((t)->bot - (t)->top - (t)->scrollback))

// LUT for the standard terminal RGB values. 0-15 may be overridden by the user.
// https://wikipedia.org/wiki/ANSI_escape_code#Colors
//
// [0x00...0x07] Normal colors
// [0x08...0x0f] High-intensity colors
// [0x10...0xe7] 6x6x6 color cube
// [0xe8...0xff] Grayscale (dark -> light)
static const uint32 rgb_presets[256] = {
	0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xc0c0c0,
	0x808080, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff, 0xffffff,
	0x000000, 0x00005f, 0x000087, 0x0000af, 0x0000d7, 0x0000ff, 0x005f00, 0x005f5f,
	0x005f87, 0x005faf, 0x005fd7, 0x005fff, 0x008700, 0x00875f, 0x008787, 0x0087af,
	0x0087d7, 0x0087ff, 0x00af00, 0x00af5f, 0x00af87, 0x00afaf, 0x00afd7, 0x00afff,
	0x00d700, 0x00d75f, 0x00d787, 0x00d7af, 0x00d7d7, 0x00d7ff, 0x00ff00, 0x00ff5f,
	0x00ff87, 0x00ffaf, 0x00ffd7, 0x00ffff, 0x5f0000, 0x5f005f, 0x5f0087, 0x5f00af,
	0x5f00d7, 0x5f00ff, 0x5f5f00, 0x5f5f5f, 0x5f5f87, 0x5f5faf, 0x5f5fd7, 0x5f5fff,
	0x5f8700, 0x5f875f, 0x5f8787, 0x5f87af, 0x5f87d7, 0x5f87ff, 0x5faf00, 0x5faf5f,
	0x5faf87, 0x5fafaf, 0x5fafd7, 0x5fafff, 0x5fd700, 0x5fd75f, 0x5fd787, 0x5fd7af,
	0x5fd7d7, 0x5fd7ff, 0x5fff00, 0x5fff5f, 0x5fff87, 0x5fffaf, 0x5fffd7, 0x5fffff,
	0x870000, 0x87005f, 0x870087, 0x8700af, 0x8700d7, 0x8700ff, 0x875f00, 0x875f5f,
	0x875f87, 0x875faf, 0x875fd7, 0x875fff, 0x878700, 0x87875f, 0x878787, 0x8787af,
	0x8787d7, 0x8787ff, 0x87af00, 0x87af5f, 0x87af87, 0x87afaf, 0x87afd7, 0x87afff,
	0x87d700, 0x87d75f, 0x87d787, 0x87d7af, 0x87d7d7, 0x87d7ff, 0x87ff00, 0x87ff5f,
	0x87ff87, 0x87ffaf, 0x87ffd7, 0x87ffff, 0xaf0000, 0xaf005f, 0xaf0087, 0xaf00af,
	0xaf00d7, 0xaf00ff, 0xaf5f00, 0xaf5f5f, 0xaf5f87, 0xaf5faf, 0xaf5fd7, 0xaf5fff,
	0xaf8700, 0xaf875f, 0xaf8787, 0xaf87af, 0xaf87d7, 0xaf87ff, 0xafaf00, 0xafaf5f,
	0xafaf87, 0xafafaf, 0xafafd7, 0xafafff, 0xafd700, 0xafd75f, 0xafd787, 0xafd7af,
	0xafd7d7, 0xafd7ff, 0xafff00, 0xafff5f, 0xafff87, 0xafffaf, 0xafffd7, 0xafffff,
	0xd70000, 0xd7005f, 0xd70087, 0xd700af, 0xd700d7, 0xd700ff, 0xd75f00, 0xd75f5f,
	0xd75f87, 0xd75faf, 0xd75fd7, 0xd75fff, 0xd78700, 0xd7875f, 0xd78787, 0xd787af,
	0xd787d7, 0xd787ff, 0xd7af00, 0xd7af5f, 0xd7af87, 0xd7afaf, 0xd7afd7, 0xd7afff,
	0xd7d700, 0xd7d75f, 0xd7d787, 0xd7d7af, 0xd7d7d7, 0xd7d7ff, 0xd7ff00, 0xd7ff5f,
	0xd7ff87, 0xd7ffaf, 0xd7ffd7, 0xd7ffff, 0xff0000, 0xff005f, 0xff0087, 0xff00af,
	0xff00d7, 0xff00ff, 0xff5f00, 0xff5f5f, 0xff5f87, 0xff5faf, 0xff5fd7, 0xff5fff,
	0xff8700, 0xff875f, 0xff8787, 0xff87af, 0xff87d7, 0xff87ff, 0xffaf00, 0xffaf5f,
	0xffaf87, 0xffafaf, 0xffafd7, 0xffafff, 0xffd700, 0xffd75f, 0xffd787, 0xffd7af,
	0xffd7d7, 0xffd7ff, 0xffff00, 0xffff5f, 0xffff87, 0xffffaf, 0xffffd7, 0xffffff,
	0x080808, 0x121212, 0x1c1c1c, 0x262626, 0x303030, 0x3a3a3a, 0x444444, 0x4e4e4e,
	0x585858, 0x626262, 0x6c6c6c, 0x767676, 0x808080, 0x8a8a8a, 0x949494, 0x9e9e9e,
	0xa8a8a8, 0xb2b2b2, 0xbcbcbc, 0xc6c6c6, 0xd0d0d0, 0xdadada, 0xe4e4e4, 0xeeeeee
};

#define VTCOLOR(idx) (rgb_presets[(idx)])

// Ring buffer of lines of cells for scrollback history.
//
// Currently, the entire buffer is a single allocation of raw bytes. Each row of cells is preceded
// by a line info header, followed by a row of cells.
// Each row, including the header, occurs on an interval of ".pitch" bytes - which is effectively
// (offsetof(Line, data) + term->cols * sizeof(Cell)), although the pitch member is used separately to
// allow for future resizing optimizations.
typedef struct Ring {
	int read, write;
	int count, limit;
	int pitch;
	uchar data[];
} Ring;

// Header embedded before each line of cells in history buffer
typedef struct Line {
	uint16 flags;
	Cell cells[];
} Line;

static Ring *ring_create(int, int);
static void ring_destroy(Ring *);
static int ring_advance(Ring *);
static int ring_get_mem_index(const Ring *, int);
static int ring_get_mem_offset(const Ring *, int);
static Line *ring_get_line(const Ring *, int);
static Cell *ring_get_cell(const Ring *, int, int);
static Line *ring_query_line(const Ring *, int);
static Line *ring_prep_line(Ring *, int);
static Line *ring_append_line(Ring *);
static uint cellslen(const Cell *, int);
static Ring *rewrap(Term *, int, int, int *, int *);
static void set_screen(Term *);

Term *
term_create(struct TermConfig config)
{
	Term *term = xmalloc(1, sizeof(*term));

	if (!term_init(term, config)) {
		FREE(term);
	}

	return term;
}

bool
term_init(Term *term, struct TermConfig config)
{
	ASSERT(term);

	memclear(term, 1, sizeof(*term));

	term->cols = config.cols;
	term->rows = config.rows;
	term->histlines = bitround(config.histlines, 1);
	term->ring = ring_create(term->histlines, term->cols);
	term->tabcols = config.tabcols;
	term->tabstops = xcalloc(term->cols, sizeof(*term->tabstops));

	for (int i = 0; ++i < term->cols; ) {
		term->tabstops[i] |= (i % term->tabcols == 0) ? 1 : 0;
	}

	ring_prep_line(term->ring, 0);

	arr_reserve(term->parser.data, 4);

	term->generic = config.generic;
	term->colsize = config.colsize;
	term->rowsize = config.rowsize;

	term->default_bg = config.default_bg;
	term->default_fg = config.default_fg;

	static_assert(LEN(term->colormap) == LEN(config.colors));

	for (uint i = 0; i < LEN(term->colormap); i++) {
		if (config.colors[i]) {
			term->colormap[i] = config.colors[i];
		} else {
			term->colormap[i] = VTCOLOR(i);
		}
	}

	term->current.width = 1;
	term->current.bg = term->default_bg;
	term->current.fg = term->default_fg;
	term->current.attr = 0;

	return true;
}

int
term_get_fileno(const Term *term)
{
	return term->pty.mfd;
}

int
term_exec(Term *term, const char *shell)
{
	if (!term->pty.mfd && pty_init(term, shell) > 0) {
		pty_resize(term, term->cols, term->rows);
	}

	return term_get_fileno(term);
}

size_t
term_push(Term *term, const char *str, size_t len)
{
	return pty_write(term, str, len);
}

size_t
term_pull(Term *term)
{
	return pty_read(term);
}

void
term_scroll(Term *term, int dy)
{
	term->scrollback = CLAMP(term->scrollback - dy, 0, term->top);
}

void
term_reset_scroll(Term *term)
{
	term->scrollback = 0;
}

void
term_resize(Term *term, int cols, int rows)
{
	cols = MAX(cols, 1);
	rows = MAX(rows, 1);

	if (cols != term->cols || rows != term->rows) {
		pty_resize(term, cols, rows);

		Ring *ring = term->ring;

		// New cursor coordinates in scrollback buffer
		int cx = term->pos.x;
		int cy = row2hidx(term, term->pos.y);

		if (cols != term->cols) {
			term->tabstops = xrealloc(term->tabstops, cols, sizeof(*term->tabstops));
			for (int i = term->cols; i < cols; i++) {
				term->tabstops[i] = (i && i % term->tabcols == 0) ? 1 : 0;
			}
			ring = rewrap(term, cols, rows, &cx, &cy);
		}

		if (ring != term->ring) {
			ASSERT(ring);
			free(term->ring);
			term->ring = ring;
		}
		term->cols = cols;
		term->rows = rows;

		set_screen(term);
		cursor_set_col(term, cx);
		cursor_set_row(term, hidx2row(term, cy));
	}
}

const Cell *
term_get_row(const Term *term, int row)
{
	if (row >= 0 && row <= term->bot) {
		const Line *hdr = ring_get_line(term->ring, row2hidx(term, row));
		if (hdr->cells[0].ucs4) {
			return hdr->cells;
		}
	}

	return NULL;
}

Cell
term_get_cell(const Term *term, int col, int row)
{
	Cell cell = *ring_get_cell(term->ring, row2hidx(term, row), col);

	return (Cell){
		.ucs4 = DEFAULT(cell.ucs4, ' '),
		.attr = cell.attr,
		.bg = (cell.attr & ATTR_INVERT) ? cell.fg : cell.bg,
		.fg = (cell.attr & ATTR_INVERT) ? cell.bg : cell.fg
	};
}

Cursor
term_get_cursor(const Term *term)
{
	Cursor cursor = { 0 };

	cursor.col = term->pos.x;
	cursor.row = term->pos.y;
	cursor.style = term->current.cursor_style;
	cursor.isvisible = !term->current.cursor_hidden && ISVISIBLE(term, cursor.col, cursor.row);
	cursor.color = term->default_fg;

	return cursor;
}

size_t
term_consume(Term *term, const uchar *str, size_t len)
{
	struct Parser *parser = &term->parser;
	uint i = 0;
	static uchar *input_; // Debug


	for (; str[i] && i < len; i++) {
		parse_byte(term, str[i]);

	// FIXME(ben): Debug output is completely broken now...
#if 0
#define DBGOPT_PRINT_INPUT 1
		{
			const char *tmp = charstring(str[i]);
			int len = strlen(tmp);
			for (int j = 0; j + !j <= len; j++) {
				arr_push(input_, (j <= len) ? tmp[j] : ' ');
			}
		}
#else
		(void)input_;
#endif
	}

#ifdef DBGOPT_PRINT_INPUT
	if (arr_count(input_)) {
		arr_push(input_, 0);
		msg_log("Input", "%s\n", input_);
		arr_clear(input_);
	}
#endif

	return i;
}

void
write_codepoint(Term *term, uint32 ucs4, CellType type)
{
	V4(int, x1, y1, x2, y2) pos = { 0 };

	pos.x1 = pos.x2 = term->pos.x;
	pos.y1 = pos.y2 = term->pos.y;

	Line *line = ring_get_line(term->ring, row2hidx(term, pos.y1));

	if (pos.x1 + 1 < term->cols) {
		term->wrapnext = false;
	} else if (!term->wrapnext) {
		term->wrapnext = true;
	} else {
		term->wrapnext = false;
		pos.x2 = 0;
		pos.y2++;
	}

	if (pos.y2 > pos.y1) {
		Line *tmp = line;
		line->flags |= LINE_WRAPPED;
		line = ring_prep_line(term->ring, row2hidx(term, pos.y2));
		set_screen(term);
		ASSERT(tmp->flags & LINE_WRAPPED);
	}

	line->cells[pos.x2] = (Cell){
		.ucs4  = ucs4,
		.width = 1,
		.bg    = term->current.bg,
		.fg    = term->current.fg,
		.attr  = term->current.attr,
		.type  = type
	};

	if (type == CellTypeTab) {
		line->flags |= LINE_HASTABS;
	}

	if (pos.y2 != pos.y1) {
		cursor_set_col(term, 0);
		cursor_move_rows(term, 1);
	}
	if (!term->wrapnext) {
		cursor_move_cols(term, 1);
	}
}

void
write_newline(Term *term)
{
	if (!ring_query_line(term->ring, row2hidx(term, term->pos.y + 1))) {
		ring_append_line(term->ring);
		set_screen(term);
	}
	cursor_set_col(term, 0);
	cursor_move_rows(term, 1);
}

void
write_tab(Term *term)
{
	int type = CellTypeTab;

	for (int n = 0; term->pos.x + 1 < term->cols; n++) {
		if (term->tabstops[term->pos.x] && n > 0) {
			break;
		}
		write_codepoint(term, ' ', type);
		type = CellTypeDummyTab;
	}
}

Ring *
ring_create(int lines, int cols)
{
	int pitch = offsetof(Line, cells) + (cols * sizeof(Cell));
	int size = pitch * (lines + 1);

	Ring *ring = xcalloc(offsetof(Ring, data) + size, 1);

	ring->limit = lines + 1;
	ring->pitch = pitch;
	ring->count = 0;
	ring->read  = 1;
	ring->write = 1;

	return ring;
}

void
ring_destroy(Ring *ring)
{
	memset(ring, 0, offsetof(Ring, data) + ring->pitch * ring->limit);
	free(ring);
}

int
ring_advance(Ring *ring)
{
	int added = 0;

	if (!RING_ISFULL(ring)) {
		ring->count++;
		added++;
	} else {
		ring->read = (ring->read + 1) % ring->limit;
		added--;
	}
	ring->write = (ring->write + 1) % ring->limit;

	return added;
}

int
ring_get_mem_index(const Ring *ring, int hidx)
{
	return (ring->read + hidx) % ring->limit;
}

int
ring_get_mem_offset(const Ring *ring, int hidx)
{
	return ring_get_mem_index(ring, hidx) * ring->pitch;
}

Line *
ring_get_line(const Ring *ring, int hidx)
{
	return (Line *)(ring->data + ring_get_mem_offset(ring, hidx));
}

Cell *
ring_get_cell(const Ring *ring, int hidx, int hoff)
{
	return ring_get_line(ring, hidx)->cells + hoff;
}

Line *
ring_query_line(const Ring *ring, int hidx)
{
	if (hidx >= 0 && hidx < ring->count) {
		return ring_get_line(ring, hidx);
	}

	return NULL;
}

Line *
ring_prep_line(Ring *ring, int line)
{
	ASSERT(line >= 0 && line <= ring->count);

	Line *hdr = ring_get_line(ring, line);

	if (line == ring->count) {
		ring_advance(ring);
		memset(hdr, 0, ring->pitch * sizeof(*ring->data));
	}

	return hdr;
}

Line *
ring_append_line(Ring *ring)
{
	return ring_prep_line(ring, ring->count);
}

void
cursor_move_cols(Term *term, int cols)
{
	Line *line = ring_get_line(term->ring, row2hidx(term, term->pos.y));

	int x0 = term->pos.x;
	int x1 = CLAMP(x0 + cols, 0, term->cols - 1);

	for (int x = x0; x < x1; x++) {
		if (!line->cells[x].ucs4) {
			line->cells[x] = CELLINIT(term);
		}
	}

	term->pos.x = x1;
}

void
cursor_move_rows(Term *term, int rows)
{
	term->pos.y = CLAMP(term->pos.y + rows, 0, term->bot - term->top);
}

void
cursor_set_col(Term *term, int col)
{
	term->pos.x = MIN(col, term->cols - 1);
}

void
cursor_set_row(Term *term, int row)
{
	term->pos.y = CLAMP(row, 0, term->bot - term->top);
}

void
cursor_set_hidden(Term *term, bool ishidden)
{
	term->current.cursor_hidden = ishidden;
}

void
cursor_set_style(Term *term, int style)
{
	ASSERT(style >= 0);

	if (style <= 7) {
		term->current.cursor_style = style;
	}
}

void
cells_init(Term *term, int col, int row, int n)
{
	ASSERT(row >= 0 && col >= 0);

	Cell *cells = ring_get_cell(term->ring, row2hidx(term, row), 0);

	const int x1 = MIN(col, term->cols);
	const int x2 = MIN(x1 + n, term->cols);

	for (int x = x1; x < x2; x++) {
		cells[x] = CELLINIT(term);
	}
}

void
cells_clear(Term *term, int col, int row, int n)
{
	ASSERT(row >= 0 && col >= 0);

	Cell *cells = ring_get_cell(term->ring, row2hidx(term, row), 0);
	const int x1 = MIN(col, term->cols);
	const int x2 = MIN(x1 + n, term->cols);

	memset(&cells[x1], 0, (x2 - x1) * sizeof(*cells));
}

void
cells_delete(Term *term, int col, int row, int n)
{
	ASSERT(row >= 0 && col >= 0);

	Cell *cells = ring_get_cell(term->ring, row2hidx(term, row), 0);
	const int x1 = MIN(col, term->cols);
	const int x2 = MIN(x1 + n, term->cols);

	memmove(&cells[x1], &cells[x2], (term->cols - x2) * sizeof(Cell));
	cells_clear(term, x2, row, term->cols);
}

void
cells_insert(Term *term, int col, int row, int n)
{
	ASSERT(row >= 0 && col >= 0);

	Cell *cells = ring_get_cell(term->ring, row2hidx(term, row), 0);
	const int x1 = MIN(col, term->cols);
	const int x2 = MIN(x1 + n, term->cols);

	memmove(&cells[x2], &cells[x1], (term->cols - x2) * sizeof(Cell));
	cells_init(term, x1, row, x2 - x1);
}

void
cells_clear_lines(Term *term, int beg, int n)
{
	const int end = MIN(beg + n, term->bot - term->top + 1);

	for (int row = beg; row < end; row++) {
		Line *hdr = ring_get_line(term->ring, row2hidx(term, row));
		cells_clear(term, 0, row, term->cols);
		hdr->flags = 0;
	}
}

void
cells_set_bg(Term *term, uint8 idx)
{
	term->current.bg = (idx < 16) ? term->colormap[idx] : VTCOLOR(idx);
}

void
cells_set_fg(Term *term, uint8 idx)
{
	term->current.fg = (idx < 16) ? term->colormap[idx] : VTCOLOR(idx);
}

void
cells_set_bg_rgb(Term *term, uint8 r, uint8 g, uint8 b)
{
	term->current.bg = pack_argb(r, g, b, 0xff);
}

void
cells_set_fg_rgb(Term *term, uint8 r, uint8 g, uint8 b)
{
	term->current.fg = pack_argb(r, g, b, 0xff);
}

void
cells_reset_bg(Term *term)
{
	term->current.bg = term->default_bg;
}

void
cells_reset_fg(Term *term)
{
	term->current.fg = term->default_fg;
}

void
cells_set_attrs(Term *term, uint16 attr)
{
	term->current.attr = attr;
}

void
cells_add_attrs(Term *term, uint16 attr)
{
	term->current.attr |= attr;
}

void
cells_del_attrs(Term *term, uint16 attr)
{
	term->current.attr &= ~attr;
}

uint
cellslen(const Cell *cells, int lim)
{
	int l = 0;
	int m = 0;
	int h = MAX(0, lim - 1);

	// Modified binary search
	for (;;) {
		m = (l + h) / 2;
		if (cells[m].ucs4) {
			if (cells[h].ucs4) {
				return h + 1;
			}
			l = m + 1;
		} else {
			if (!cells[l].ucs4) {
				return l;
			}
			h = m - 1;
		}
	}

	return 0;
}

/* TODO(ben):
 * Calling this function in random places is kind of goofy... a more automatic way of
 * updating the screen would be better.
 */
void
set_screen(Term *term)
{
	term->bot = DEFAULT(term->ring->count, 1) - 1;
	term->top = MAX(term->bot - term->rows + 1, 0);
}

#define ring_memoff_(r,p) ((uchar *)(p) - (r)->data)
#define ring_memidx_(r,p) (ring_memoff_(r, p) / (r)->pitch)

/* TODO(ben):
 * Rewrapping works well enough for development purposes right now, but:
 *   - Tabs are not properly wrapped, resized, or cleared.
 *   - Multi-cell glyphs are not handled at all.
 *   - Cells may be permanently dropped from scrollback history when the width
 *     decreases and increases again.
 *
 * There is also a possibility that an atomic downsize request could cause the new ring to
 * wrap multiple times while copying, meaning the new cursor position found on the first pass
 * would become invalid again - although I haven't run the numbers yet.
 */
Ring *
rewrap(Term *term, int cols, int rows, int *posx, int *posy)
{
	struct {
		Ring *ring;
		Line *hdr;
		int x1, x2, y;
		int cols, rows;
	} src = { 0 }, dst = { 0 };

	src.cols = term->cols, dst.cols = cols;
	src.rows = term->rows, dst.rows = rows;
	src.y = -1;
	dst.y = -1;

	int ocx, ocy, ncx, ncy;
	ncx = ocx = term->pos.x;
	ncy = ocy = row2hidx(term, term->pos.y);

	const int maxhist = term->ring->count;
	int len = 0;
	int cx = -1;

	if (maxhist <= 0 || src.cols == dst.cols) {
		return term->ring;
	}

	src.ring = term->ring;
	dst.ring = ring_create(term->histlines, dst.cols);

	for (;;) {
		if (!src.x1) {
			if (++src.y >= maxhist) {
				if (dst.hdr) {
					dst.hdr->flags &= ~LINE_WRAPPED;
				}
				break;
			}
			src.hdr = ring_get_line(src.ring, src.y);
			len = cellslen(src.hdr->cells, src.cols);
			cx = (src.y == ocy) ? ocx : -1;
		}
		if (!dst.x1) {
			dst.hdr = ring_append_line(dst.ring);
			dst.y++;
		}

		const int lim_a = len;
		const int lim_b = dst.cols;

		int rem_a = lim_a - src.x1;
		int rem_b = lim_b - dst.x1;
		int adv_a = MIN(rem_a, rem_b);
		int adv_b = adv_a;

#if 0
#define PRINT_REWRAP 1
		printf("Wrapping: <%c> { %03d, %03d }:%03d (%03d/%03d) to "
		                 "<%c> { %03d, %03d } (---/%03d) "
		                 "$%-6lu|%3lu > $%-6lu|%3lu ... ",
		       !!src.hdr->flags ? '*' : ' ',
		       src.x1, src.y, adv_a, lim_a, src.cols,
		       !!dst.hdr->flags ? '*' : ' ',
		       dst.x1, dst.y, lim_b,
		       ring_memoff_(src.ring, src.hdr),
		       ring_memidx_(src.ring, src.hdr),
		       ring_memoff_(dst.ring, dst.hdr),
		       ring_memidx_(dst.ring, dst.hdr));
#endif

		if (rem_a < rem_b) {
			if (src.hdr->flags & LINE_WRAPPED) {
				ASSERT(lim_a == src.cols);
				dst.hdr->flags |= LINE_WRAPPED;
				dst.x2 = (dst.x2 + adv_b) % lim_b;
				src.x2 = 0;
			} else {
				dst.hdr->flags &= ~LINE_WRAPPED;
				src.x2 = dst.x2 = 0;
			}
		} else if (rem_a == rem_b) {
			dst.hdr->flags &= ~LINE_WRAPPED;
			src.x2 = dst.x2 = 0;
		} else {
			dst.hdr->flags |= LINE_WRAPPED;
			src.x2 += adv_a;
			dst.x2 = 0;
		}

		if (cx >= 0) {
			if (cx >= src.x1 && cx - src.x1 <= adv_a) {
				ncx = dst.x1 + (cx - src.x1);
				ncy = dst.y;
				cx = -1;
			}
		}

		memcpy(dst.hdr->cells + dst.x1,
		       src.hdr->cells + src.x1,
		       adv_a * sizeof(*dst.hdr->cells));

#ifdef PRINT_REWRAP
		printf("Done: <%c|%c> [ %03d, %03d ] -> [ %03d, %03d ]\n",
		       !!src.hdr->flags ? '*' : ' ',
		       !!dst.hdr->flags ? '*' : ' ',
		       src.x1, dst.x1, src.x2, dst.x2);
#endif
		src.x1 = src.x2;
		dst.x1 = dst.x2;
	}

	printf("Cursor(2): { %03d, %03d } -> { %03d, %03d }\n",
	       ocx, ocy, ncx, ncy);
	*posx = ncx;
	*posy = ncy;

	return dst.ring;
}

void
term_print_summary(const Term *term, uint flags)
{
#define print_(lab,fmt,...) fprintf(stderr, "%*s%s: "fmt"\n", n * 4, "", lab, __VA_ARGS__)
	int n = 0;

	print_("Term", "(%p)", (void *)term);
	n++;

	if (flags) {
		print_("state", "%s", "");
		{
			n++;
			print_("pos", "{ %d, %d }", term->pos.x, term->pos.y);
			print_("cols/rows", "[ %d, %d ]", term->cols, term->rows);
			print_("top/bot", "[ %d, %d ]", term->top, term->bot);
			print_("scroll", "%d", term->scrollback);
			print_("colpx/rowpx", "[ %d, %d ]", term->colsize, term->rowsize);
			print_("histsize", "%d", term->histlines);
			n--;
		}
	}
	if (flags) {
		print_("cursor", "%s", "");
		{
			Cursor cursor = term_get_cursor(term);
			Cell cell = term_get_cell(term, cursor.col, cursor.row);

			n++;
			print_("ucs4", "{ %d, %d }", cursor.col, cursor.row);
			print_("ucs4", "%u", cell.ucs4);
			print_("attr", "%u", cell.attr);
			print_("color", "[ #%08X, #%08X ] | #%08X", cell.bg, cell.fg, cursor.color);
			print_("style", "%u", cursor.style);
			print_("wrap", "%d", term->wrapnext);
			print_("visible", "%d", cursor.isvisible);
			n--;
		}
	}
	if (flags) {
		print_("ring", "(%p)", (void *)term->ring);
		{
			n++;
			print_("read/write", "{ %d, %d }", term->ring->read, term->ring->write);
			print_("count", "%d", term->ring->count);
			print_("limit", "%d", term->ring->limit);
			print_("pitch", "%d", term->ring->pitch);
			print_("data", "(%p)", (void *)term->ring->data);
			n--;
		}
	}
	if (flags) {
		print_("tabs", "(%p)", (void *)term->tabstops);
		{
			n++;
			print_("tabcols", "%d", term->tabcols);
			{
				n++;
				fprintf(stderr, "%*s", n * 4, "");
				for (int i = 0; term->tabstops && i < term->cols; i++) {
					fprintf(stderr, "%u", term->tabstops[i]);
				}
				fprintf(stderr, "\n");
				n--;
			}
			n--;
		}
	}
#undef print_
}

void
term_print_history(const Term *term)
{
	int n = 0;

	putchar('\n');

	for (int y = 0; y < term->ring->count; n++, y++) {
		Line *hdr = ring_get_line(term->ring, y);

		size_t off = ring_memoff_(term->ring, hdr);
		size_t idx = ring_memidx_(term->ring, hdr);
		int len = cellslen(hdr->cells, term->cols);

		printf("[%03zu|%03d] (%03d) $%-6zu 0x%.2x %c |",
		       idx, y, len, off, hdr->flags,
		       (y == term->top) ? '>' :
		       ((y == term->bot) ? '<' : ' '));
		for (int x = 0; x < term->cols; x++) {
			char *esc = (x == len) ? "\033[97;41m" : "";
			printf("%s%lc\033[0m", esc,
			       (hdr->cells[x].ucs4) ? hdr->cells[x].ucs4 : L' ');
		}
		printf("|\n");
	}

	if (n) putchar('\n');
}

#undef ring_memoff_
#undef ring_memidx_

