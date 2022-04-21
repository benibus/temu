/*------------------------------------------------------------------------------*
 * This file is part of temu
 * Copyright (C) 2021-2022 Benjamin Harkins
 *
 * temu is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 *------------------------------------------------------------------------------*/

#include "utils.h"
#include "keycodes.h"
#include "pty.h"
#include "opcodes.h"
#include "term_private.h"
#include "term_parser.h"
#include "term_ring.h"
#include "gfx_draw.h"

#include <unistd.h> // for isatty()

static void cursor_init(Cursor *cur);
static int cursor_set_x_abs(Cursor *cur, int x, int max);
static int cursor_set_y_abs(Cursor *cur, int y, int max);
static int cursor_set_x_rel(Cursor *cur, int x, int max);
static int cursor_set_y_rel(Cursor *cur, int y, int max);

static size_t term_consume(Term *term, const uchar *data, size_t len);
static void term_write_printable(Term *term, uint32 ucs4, CellType type);
static void term_write_tab(Term *term);
static void term_write_newline(Term *term);
static int term_set_x_abs(Term *term, int x);
static int term_set_y_abs(Term *term, int y);
static int term_set_x_rel(Term *term, int x);
static int term_set_y_rel(Term *term, int y);
static void term_set_cursor_visibility(Term *term, bool visible);
static void term_set_cursor_style(Term *term, size_t style);
static void term_set_cell_bg(Term *term, uint16 idx);
static void term_set_cell_fg(Term *term, uint16 idx);
static void term_set_cell_bg_rgb(Term *term, uint8 r, uint8 g, uint8 b);
static void term_set_cell_fg_rgb(Term *term, uint8 r, uint8 g, uint8 b);
static void term_reset_cell_bg(Term *term);
static void term_reset_cell_fg(Term *term);
static void term_set_cell_attrs(Term *term, uint16 mask, bool enable);
static void term_reset_cell_attrs(Term *term);
static void term_reset_cell(Term *term);
static void term_set_screen(Term *term, bool alt);

static void alloc_frame(Frame *, uint16, uint16);
static void alloc_tabstops(uint8 **, uint16, uint16, uint16);
static void update_dimensions(Term *, uint16, uint16);

#define CELLINIT(t)                          \
    (Cell){                                  \
        .ucs4  = ' ',                        \
        .bg    = color_from_key(BACKGROUND), \
        .fg    = color_from_key(FOREGROUND), \
        .type  = CellTypeNormal,             \
        .width = 1,                          \
        .attrs = 0                           \
    }

// Routine for emulating a known VT function as specified by ISO/ECMA/DEC standards, XTerm,
// or any other terminal emulator. These functions are typically dispatched via
// opcodes/parameters derived from escape sequences embedded in the input stream
//
// As of now, simple 1-byte codes (e.g \n, \r, \t) are handled elsewhere
typedef void EmuFunc(Term *term, const Parser *parser);

#define XTABLE_EMUFUNCS \
    X_(WRITE)           \
    X_(RI)              \
    X_(DECSC)           \
    X_(DECRC)           \
    X_(ICH)             \
    X_(CUU)             \
    X_(CUD)             \
    X_(CUF)             \
    X_(CUB)             \
    X_(CNL)             \
    X_(CPL)             \
    X_(CHA)             \
    X_(CUP)             \
    X_(CHT)             \
    X_(DCH)             \
    X_(VPA)             \
    X_(VPR)             \
    X_(ED)              \
    X_(EL)              \
    X_(SGR)             \
    X_(DSR)             \
    X_(SM)              \
    X_(RM)              \
    X_(DECSET)          \
    X_(DECRST)          \
    X_(DECSCUSR)        \
    X_(OSC)

// Generate prototypes
#define X_(x) static EmuFunc emu_##x;
XTABLE_EMUFUNCS
#undef X_

// Generate dispatch table
#define X_(x) [OP_##x] = &emu_##x,
static EmuFunc *const emu_funcs[NUM_OPCODES] = { XTABLE_EMUFUNCS };
#undef X_

#undef XTABLE_EMUFUNCS

// Allocate/initialize terminal instance using an initialized application handle
Term *
term_create(App *app)
{
    Term *term = xcalloc(1, sizeof(*term));

    // Query known metrics/settings/resources from application
    term->fonts   = app_fonts(app);
    term->palette = app_palette(app);
    term->tabcols = app_tabcols(app);
    term->cwidth  = app_font_width(app);
    term->cheight = app_font_height(app);
    term->width   = app_width(app);
    term->height  = app_height(app);
    term->border  = app_border(app);
    ASSERT(term->cwidth > 0 && term->cheight > 0);

    // Compute initial cols/rows and scrollback length
    term->cols = imax(0, term->width - 2 * term->border) / term->cwidth;
    term->rows = imax(0, term->height - 2 * term->border) / term->cheight;
    term->histlines = round_pow2(imax(term->rows, app_histlines(app)));

    cursor_init(&term->cur);
    // Default starting cell
    term->cell.width = 1;
    term->cell.bg = color_from_key(BACKGROUND);
    term->cell.fg = color_from_key(FOREGROUND);
    term->cell.attrs = 0;

    // Allocate buffers, set target ring to default
    term->rings[0] = ring_create(term->histlines, term->cols, term->rows);
    term->rings[1] = ring_create(term->rows, term->cols, term->rows);
    term->ring = term->rings[0];

    alloc_frame(&term->frame, term->cols, term->rows);
    alloc_tabstops(&term->tabstops, 0, term->cols, term->tabcols);
    update_dimensions(term, term->cols, term->rows);

    // Done
    term->app = app;

    dbg_printf("Terminal created: x=%d y=%d tx=%d w=%d h=%d cw=%d ch=%d b=%d l=%d\n",
               term->cols,
               term->rows,
               term->tabcols,
               term->width,
               term->height,
               term->cwidth,
               term->cheight,
               term->border,
               term->histlines);

    return term;
}

// Deallocate terminal instance and kill any child processes
void
term_destroy(Term *term)
{
    ASSERT(term);
    parser_fini(&term->parser);
    if (term->frame.cells) {
        free(term->frame.cells);
    }
    if (term->tabstops) {
        free(term->tabstops);
    }
    ring_destroy(term->rings[0]);
    ring_destroy(term->rings[1]);
    term->ring = NULL;
    pty_hangup(term->pid);
    free(term);
}

// Start terminal child process using the specified shell and command-line.
// Initialize the escape sequence parser
int
term_exec(Term *term, const char *shell, int argc, const char *const *argv)
{
    UNUSED(argc);
    UNUSED(argv);

    ASSERT(term);

    if (!term->pid) {
        parser_init(&term->parser);
        term->pid = pty_init(shell, &term->mfd, &term->sfd);
        pty_resize(term->mfd, term->cols, term->rows, term->cwidth, term->cheight);
    }

    return term->mfd;
}

int term_cols(const Term *term) { return term->cols; }
int term_rows(const Term *term) { return term->rows; }

// Temporary glue code for passing screen data to the renderer
static Frame *
generate_frame(Term *term)
{
    Frame *frame = &term->frame;

    ring_copy_framebuffer(term->ring, frame->cells);
    frame->cols = term->cols;
    frame->rows = term->rows;
    frame->width = term->cols * term->cwidth;
    frame->height = term->rows *term->cheight;
    frame->cursor.col = term->cur.x;
    frame->cursor.row = term->cur.y;
    frame->cursor.style = term->cur.style;
    frame->time = timer_msec(NULL);
    frame->palette = term->palette;

    if (!term->cur.hidden && check_visible(term->ring, term->cur.x, term->cur.y)) {
        frame->cursor.visible = true;
        frame->cursor.row += ring_get_scroll(term->ring);
        ASSERT(frame->cursor.row < term->rows);
    } else {
        frame->cursor.visible = false;
    }

    return frame;
}

void
term_draw(Term *term)
{
    ASSERT(term);

    gfx_clear_rgb1u(term->palette->bg);
    if (term->pid) {
        gfx_draw_frame(generate_frame(term), term->fonts);
    }
}

// Send data to the child
size_t
term_push(Term *term, const void *data, size_t len)
{
    return pty_write(term->mfd, data, len);
}

// Receive data from the child
size_t
term_pull(Term *term)
{
    ASSERT(term && term->pid);

    const size_t len = pty_read(term->mfd, term->input, LEN(term->input));
    if (len > 0) {
        term_consume(term, term->input, len);
    }

    return len;
}

void
term_scroll(Term *term, int delta)
{
    // (delta < 0): scroll back in history
    // (delta > 0): scroll forward in history
    ring_adjust_scroll(term->ring, -delta);
}

void
term_reset_scroll(Term *term)
{
    ring_reset_scroll(term->ring);
}

bool
term_toggle_trace(Term *term)
{
    term->tracing = !term->tracing;
    fprintf(stderr, "[!] Trace %s\n", (term->tracing) ? "enabled" : "disabled");
    return term->tracing;
}

void
alloc_frame(Frame *frame, uint16 cols_, uint16 rows_)
{
    ASSERT(frame);

    const int cols = MAX((int)cols_, frame->cols);
    const int rows = MAX((int)rows_, frame->rows);

    if (cols && rows && (cols > frame->cols || rows > frame->rows)) {
        if (!frame->cells) {
            frame->cells = xcalloc(cols * rows, sizeof(*frame->cells));
        } else {
            frame->cells = xrealloc(frame->cells, cols * rows, sizeof(*frame->cells));
        }
        frame->cols = cols;
        frame->rows = rows;
    }
}

void
alloc_tabstops(uint8 **r_tabstops, uint16 begcol, uint16 endcol, uint16 tabcols)
{
    ASSERT(r_tabstops);

    if (endcol > begcol) {
        uint8 *tabstops = *r_tabstops;
        tabstops = xrealloc(tabstops, endcol, sizeof(*tabstops));
        if (!tabcols) {
            memset(&tabstops[begcol], 0, (endcol - begcol) * sizeof(*tabstops));
        } else {
            for (uint i = begcol; i < endcol; i++) {
                tabstops[i] = (i && i % tabcols == 0) ? 1 : 0;
            }
        }
        *r_tabstops = tabstops;
    }
}

void
update_dimensions(Term *term, uint16 cols_, uint16 rows_)
{
    ASSERT(term);

    const int cols = DEFAULT(cols_, term->cols);
    const int rows = DEFAULT(rows_, term->rows);

    term->cols = cols;
    term->rows = rows;
    term->max_cols = MAX(cols, term->max_cols);
    term->max_rows = MAX(rows, term->max_rows);
}

void
term_resize(Term *term, uint width, uint height)
{
    ASSERT(term);

    ASSERT(width <= INT_MAX && height <= INT_MAX);
    const int cols = iclamp(((int)width - 2 * term->border) / term->cwidth, 0, MAX_COLS);
    const int rows = iclamp(((int)height - 2 * term->border) / term->cheight, 0, MAX_ROWS);

    if (cols == term->cols && rows == term->rows) {
        return;
    }

    // Compress the screen vertically.
    if (rows <= term->cur.y) {
        ring_adjust_head(term->rings[0], term->rows - rows);
        term->cur.y -= term->rows - rows;
    }

    // Expand the screen vertically while history lines exist.
    if (rows > term->rows) {
        int delta = imin(rows - term->rows, ring_histlines(term->rings[0]));
        ring_adjust_head(term->rings[0], -delta);
        term->cur.y += delta;
    }

    // Resize history
    ring_set_dimensions(term->rings[0], cols, rows);
    ring_set_dimensions(term->rings[1], cols, rows);

    // Resize extra buffers
    alloc_tabstops(&term->tabstops, term->max_cols, cols, term->tabcols);
    alloc_frame(&term->frame, cols, rows);

    // Resize psuedoterminal
    pty_resize(term->mfd, cols, rows, term->cwidth, term->cheight);

    // Commit changes
    update_dimensions(term, cols, rows);
}

static inline void
print_trace(FILE *fp,
            uint64 time,
            uint32 op,
            const Parser *parser,
            const uchar *input,
            uint len)
{
    const char *const opname = opcode_name(op);
    const bool iswrite = (op == OP_WRITE);
    const bool impl = (iswrite || !!emu_funcs[op]);
    const int color = isatty(fileno(fp)) ?
                          (op) ? (iswrite) ? 34 : (impl) ? 36 : 33 : 31 : 0;

    if (color) {
        fprintf(fp, "\033[0;%dm%lu ", color, time);
    } else {
        fprintf(fp, "%lu %c ", time, (op) ? (impl) ? '+' : '-' : '?');
    }
    fprintf(fp, "%s(", opname);

    switch (opcode_type(op)) {
    case SEQ_DCS:
    case SEQ_OSC:
        fprintf(fp, "\"%.*s\"", (int)arr_count(parser->data), (char *)parser->data);
        break;
    case 0:
        for (uint i = 0; i < parser->nargs; i++) {
            fprintf(fp, "%s%s", (i) ? ", " : "", charstring(parser->args[i]));
        }
        break;
    default:
        for (uint i = 0; i < parser->nargs; i++) {
            fprintf(fp, "%s%zu", (i) ? ", " : "", parser->args[i]);
        }
        break;
    }

    fprintf(fp, ")%s ", (color) ? "\033[0;90m" : "");
    for (uint i = 0; i < len; i++) {
        fprintf(fp, "%s ", charstring(input[i]));
    }
    fprintf(fp, "%s\n", (color) ? "\033[m" : "");
}

size_t
term_consume(Term *term, const uchar *str, size_t len)
{
    const uint64 time = timer_usec(NULL);
    size_t i = 0;

    while (i < len) {
        size_t adv;
        const uint32 op = parser_emit(&term->parser, &str[i], len - i, &adv);

        if (term->tracing) {
            print_trace(stdout, time, op, &term->parser, &str[i], adv);
        }

        if (op) {
            ASSERT(op < NUM_OPCODES);
            if (emu_funcs[op]) {
                emu_funcs[op](term, &term->parser);
            }
        }

        i += adv;
    }

    return i;
}

void
term_write_printable(Term *term, uint32 ucs4, CellType type)
{
    if (term->cur.x + 1 < term->cols) {
        term->cur.wrapnext = false;
    } else if (!term->cur.wrapnext) {
        term->cur.wrapnext = true;
    } else {
        term->cur.wrapnext = false;
        row_set_wrap(term->ring, term->cur.y, true);
        if (term->cur.y + 1 == term->rows) {
            ring_adjust_head(term->ring, 1);
        } else {
            term->cur.y++;
        }
        term->cur.x = 0;
    }

    Cell *cell = cells_get(term->ring, term->cur.x, term->cur.y);

    // If the cursor position was ever set independently, there may be unitialized cells
    // preceding it - so we turn them into spaces
    for (int n = 1; n <= term->cur.x; n++) {
        Cell *c = &cell[-n];
        if (c->ucs4) break;
        *c = CELLINIT(term);
    }

    cell[0] = (Cell){
        .ucs4  = ucs4,
        .width = 1,
        .bg    = term->cell.bg,
        .fg    = term->cell.fg,
        .attrs = term->cell.attrs,
        .type  = type
    };

    if (!term->cur.wrapnext) {
        ASSERT(term->cur.x + 1 < term->cols);
        term->cur.x++;
    }
}

void
term_write_newline(Term *term)
{
    if (term->cur.y + 1 == term->rows) {
        ring_adjust_head(term->ring, 1);
        rows_clear(term->ring, term->cur.y, 1);
    } else {
        term->cur.y++;
    }
}

void
term_write_tab(Term *term)
{
    int type = CellTypeTab;

    for (int n = 0; term->cur.x + 1 < term->cols; n++) {
        if (term->tabstops[term->cur.x] && n > 0) {
            break;
        }
        term_write_printable(term, ' ', type);
        type = CellTypeDummyTab;
    }
}

void
cursor_init(Cursor *cur)
{
    if (cur) {
        memset(cur, 0, sizeof(*cur));
        cur->bg = color_from_key(FOREGROUND);
        cur->fg = color_from_key(BACKGROUND);
    }
}

static inline int
set_dim__(int *val, int new, int max)
{
    const int old = *val;
    *val = iclamp(((new < 0) ? max : 0) + new, 0, max - 1);
    return *val - old;
}

int
cursor_set_x_abs(Cursor *cur, int x, int max)
{
    return set_dim__(&cur->x, x, max);
}

int
cursor_set_y_abs(Cursor *cur, int y, int max)
{
    return set_dim__(&cur->y, y, max);
}

int
cursor_set_x_rel(Cursor *cur, int x, int max)
{
    return cursor_set_x_abs(cur, imax(0, cur->x + x), max);
}

int
cursor_set_y_rel(Cursor *cur, int y, int max)
{
    return cursor_set_y_abs(cur, imax(0, cur->y + y), max);
}

int
term_set_x_abs(Term *term, int x)
{
    return cursor_set_x_abs(&term->cur, x, term->cols);
}

int
term_set_y_abs(Term *term, int y)
{
    return cursor_set_y_abs(&term->cur, y, term->rows);
}

int
term_set_x_rel(Term *term, int x)
{
    return cursor_set_x_rel(&term->cur, x, term->cols);
}

int
term_set_y_rel(Term *term, int y)
{
    return cursor_set_y_rel(&term->cur, y, term->rows);
}

void
term_set_cursor_visibility(Term *term, bool visible)
{
    term->cur.hidden = !visible;
}

void
term_set_cursor_style(Term *term, size_t style)
{
    if (style < 8) {
        term->cur.style = style;
    }
}

void
term_save_cursor(Term *term)
{
    term->saved.cur = term->cur;
}

void
term_restore_cursor(Term *term)
{
    term->cur = term->saved.cur;
}

void
term_set_cell_bg(Term *term, uint16 idx)
{
    term->cell.bg = color_from_key(idx);
}

void
term_set_cell_fg(Term *term, uint16 idx)
{
    term->cell.fg = color_from_key(idx);
}

void
term_set_cell_bg_rgb(Term *term, uint8 r, uint8 g, uint8 b)
{
    term->cell.bg = color_from_rgb_3u(r, g, b);
}

void
term_set_cell_fg_rgb(Term *term, uint8 r, uint8 g, uint8 b)
{
    term->cell.fg = color_from_rgb_3u(r, g, b);
}

void
term_reset_cell_bg(Term *term)
{
    term_set_cell_bg(term, BACKGROUND);
}

void
term_reset_cell_fg(Term *term)
{
    term_set_cell_fg(term, FOREGROUND);
}

void
term_reset_cell_attrs(Term *term)
{
    term->cell.attrs = 0;
}

void
term_set_cell_attrs(Term *term, uint16 mask, bool enable)
{
    BSET(term->cell.attrs, mask, enable);
}

void
term_reset_cell(Term *term)
{
    term_reset_cell_attrs(term);
    term_reset_cell_bg(term);
    term_reset_cell_fg(term);
}

void
term_set_screen(Term *term, bool alt)
{
    term->ring = term->rings[alt];
}

void term_print_history(const Term *term)
{
    dbg_print_ring(term->ring);
}

// Will be used for line rewrapping
#if 0
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
#endif

// Returns parser argument at specified index, or 0 if out of bounds
static inline size_t
get_arg(const Parser *parser, uint idx)
{
    return (parser && idx < parser->nargs) ? parser->args[idx] : 0;
}

// Same as get_arg() but clamped to a range
static inline size_t
get_clamped_arg(const Parser *parser, uint idx, size_t min, size_t max)
{
    const size_t arg = get_arg(parser, idx);
    if (!max || max < min) {
        max = SIZE_MAX;
    }
    return CLAMP(arg, min, max);
}

// Returns argument suitable for cursor operations, i.e. limited to a nonzero signed
// integer range
#define get_cursor_arg(p,i) get_clamped_arg(p, i, 1, INT16_MAX)

// Extracts numeric argument at start of control string - terminated by ';' or 0.
// Sets readlen to the index *after* the delimiter
static size_t
parse_arg(const uchar *str, uint len, uint *readlen)
{
    size_t arg = 0;
    uint i = 0;

    for (; str && i < len; i++) {
        const uchar c = str[i];
        if (c >= '0' && c <= '9') {
            if (arg <= SIZE_MAX / 10) {
                const uint8 digit = c - '0';
                arg *= 10;
                arg += MIN(digit, SIZE_MAX - arg);
            } else {
                arg = SIZE_MAX;
            }
        } else if (c == ';' || !c) {
            i += !!c;
            break;
        } else {
            arg = 0;
            break;
        }
    }

    SETPTR(readlen, i);
    return arg;
}

// Write codepoint to ring buffer or execute control function
inline void
emu_WRITE(Term *term, const Parser *parser)
{
    const size_t c = get_arg(parser, 0);

    switch (c) {
    case '\n':
    case '\v':
    case '\f':
        term_write_newline(term);
        break;
    case '\t':
        term_write_tab(term);
        break;
    case '\r':
        term_set_x_abs(term, 0);
        break;
    case '\b':
        term_set_x_rel(term, -1);
        break;
    case '\a':
        break;
    default:
        term_write_printable(term, c, CellTypeNormal);
        break;
    }
}

// Operating system command
void
emu_OSC(Term *term, const Parser *parser)
{
    const uchar *str = parser->data;
    uint len = arr_count(str);
    uint beg = 0;

    const size_t arg = parse_arg(str, len, &beg);
    str += beg;
    len -= beg;

    // 0 - Set icon name and window title
    // 1 - Set icon name
    // 2 - Set window title
    // 3 - Set window property
    // 4 - Set following color specification
    uint8 props = 0;

    switch (arg) {
    case 0:
    case 1:
        props |= APPPROP_ICON;
        if (arg) break;
        // fallthrough
    case 2:
        props |= APPPROP_TITLE;
        break;
    case 3:
        break;
    }

    if (props) {
        app_set_properties(term->app, props, (const char *)str, len);
    }

    return;
}

// Reverse index
void
emu_RI(Term *term, const Parser *parser)
{
    UNUSED(parser);

    if (term->cur.y > 0) {
        term_set_y_rel(term, -1);
    } else {
        rows_move(term->ring, 0, term->rows, 1);
    }
}

// Save cursor
void
emu_DECSC(Term *term, const Parser *parser)
{
    UNUSED(parser);

    term_save_cursor(term);
}

// Restore cursor
void
emu_DECRC(Term *term, const Parser *parser)
{
    UNUSED(parser);

    term_restore_cursor(term);
}

// Insert characters
void
emu_ICH(Term *term, const Parser *parser)
{
    const int arg = get_cursor_arg(parser, 0);

    cells_insert(term->ring, CELLINIT(term), term->cur.x, term->cur.y, arg);
}

// Cursor up
void
emu_CUU(Term *term, const Parser *parser)
{
    const int arg = get_cursor_arg(parser, 0);

    term_set_y_rel(term, -arg);
}

// Cursor down
void
emu_CUD(Term *term, const Parser *parser)
{
    const int arg = get_cursor_arg(parser, 0);

    term_set_y_rel(term, +arg);
}

// Cursor forward
void
emu_CUF(Term *term, const Parser *parser)
{
    const int arg = get_cursor_arg(parser, 0);

    term_set_x_rel(term, +arg);
}

// Cursor backward
void
emu_CUB(Term *term, const Parser *parser)
{
    const int arg = get_cursor_arg(parser, 0);

    term_set_x_rel(term, -arg);
}

// Cursor next line
void
emu_CNL(Term *term, const Parser *parser)
{
    const int arg = get_cursor_arg(parser, 0);

    term_set_y_rel(term, +arg);
}

// Cursor previous line
void
emu_CPL(Term *term, const Parser *parser)
{
    const int arg = get_cursor_arg(parser, 0);

    term_set_y_rel(term, -arg);
}

// Cursor horizontal absolute
void
emu_CHA(Term *term, const Parser *parser)
{
    const int arg = get_cursor_arg(parser, 0) - 1;

    term_set_x_abs(term, arg);
}

// Cursor position
void
emu_CUP(Term *term, const Parser *parser)
{
    const int args[2] = {
        [0] = get_cursor_arg(parser, 0) - 1,
        [1] = get_cursor_arg(parser, 1) - 1,
    };

    term_set_y_abs(term, args[0]);
    term_set_x_abs(term, args[1]);
}

// Cursor horizontal tabulation
void
emu_CHT(Term *term, const Parser *parser)
{
    const int arg = get_cursor_arg(parser, 0);

    const int limit = term->cols / term->tabcols;
    for (int n = 0; n < arg && n < limit; n++) {
        term_write_tab(term);
    }
}

// Delete characters
void
emu_DCH(Term *term, const Parser *parser)
{
    const int arg = get_cursor_arg(parser, 0);

    cells_delete(term->ring, term->cur.x, term->cur.y, arg);
}

// Vertical position absolute
void
emu_VPA(Term *term, const Parser *parser)
{
    const int arg = get_cursor_arg(parser, 0);

    term_set_y_abs(term, arg - 1);
}

// Vertical position relative
void
emu_VPR(Term *term, const Parser *parser)
{
    const int arg = get_cursor_arg(parser, 0);

    term_set_y_rel(term, arg);
}

// Erase in display
void
emu_ED(Term *term, const Parser *parser)
{
    const size_t arg = get_arg(parser, 0);

    switch (arg) {
    case 0:
        rows_clear(term->ring, term->cur.y + 1, term->rows);
        cells_clear(term->ring, term->cur.x, term->cur.y, term->cols);
        break;
    case 1:
        rows_clear(term->ring, 0, term->cur.y);
        cells_set(term->ring, CELLINIT(term), 0, term->cur.y, term->cur.x);
        break;
    case 2:
        rows_clear(term->ring, 0, term->rows);
        term_set_y_abs(term, 0);
        break;
    }
}

// Erase in line
void
emu_EL(Term *term, const Parser *parser)
{
    const size_t arg = get_arg(parser, 0);

    switch (arg) {
    case 0:
        cells_clear(term->ring, term->cur.x, term->cur.y, term->cols);
        break;
    case 1:
        cells_set(term->ring, CELLINIT(term), 0, term->cur.y, term->cur.x);
        break;
    case 2:
        cells_clear(term->ring, 0, term->cur.y, term->cols);
        term_set_x_abs(term, 0);
        break;
    }
}

// Select graphic rendition
void
emu_SGR(Term *term, const Parser *parser)
{
    uint i = 0;
    size_t args[5] = { 0 };

    do {
        args[0] = get_arg(parser, i);

        switch (args[0]) {
        // Reset defaults
        case 0:
            term_reset_cell(term);
            break;

        // Set visual attributes
        case 1:  term_set_cell_attrs(term, ATTR_BOLD,      1); break;
        case 3:  term_set_cell_attrs(term, ATTR_ITALIC,    1); break;
        case 4:  term_set_cell_attrs(term, ATTR_UNDERLINE, 1); break;
        case 5:  term_set_cell_attrs(term, ATTR_BLINK,     1); break;
        case 7:  term_set_cell_attrs(term, ATTR_INVERT,    1); break;
        case 8:  term_set_cell_attrs(term, ATTR_INVISIBLE, 1); break;
        case 22: term_set_cell_attrs(term, ATTR_BOLD,      0); break;
        case 23: term_set_cell_attrs(term, ATTR_ITALIC,    0); break;
        case 24: term_set_cell_attrs(term, ATTR_UNDERLINE, 0); break;
        case 25: term_set_cell_attrs(term, ATTR_BLINK,     0); break;
        case 27: term_set_cell_attrs(term, ATTR_INVERT,    0); break;
        case 28: term_set_cell_attrs(term, ATTR_INVISIBLE, 0); break;

        // Set background 0-7
        case 30: case 31:
        case 32: case 33:
        case 34: case 35:
        case 36: case 37:
            term_set_cell_fg(term, args[0] - 30);
            break;
        // Reset default foreground
        case 39:
            term_reset_cell_fg(term);
            break;

        // Set background 0-7
        case 40: case 41:
        case 42: case 43:
        case 44: case 45:
        case 46: case 47:
            term_set_cell_bg(term, args[0] - 40);
            break;
        // Reset default background
        case 49:
            term_reset_cell_bg(term);
            break;

        case 38: // Set foreground to next arg(s)
        case 48: // Set background to next arg(s)
            i += 1;
            if (i + 1 < parser->nargs) {
                args[1] = get_arg(parser, i);
                if (args[1] == 5) {
                    // Set 0-255 (1 arg)
                    i += 1;
                    args[2] = get_arg(parser, i);
                    if (args[0] == 48) {
                        term_set_cell_bg(term, args[2] & 0xff);
                    } else if (args[0] == 38) {
                        term_set_cell_fg(term, args[2] & 0xff);
                    }
                } else if (args[1] == 2 && i + 3 < parser->nargs) {
                    // Set literal RGB (3 args)
                    i += 3;
                    args[2] = get_arg(parser, i - 2);
                    args[3] = get_arg(parser, i - 1);
                    args[4] = get_arg(parser, i - 0);
                    if (args[0] == 48) {
                        term_set_cell_bg_rgb(term, args[2], args[3], args[4]);
                    } else if (args[0] == 38) {
                        term_set_cell_fg_rgb(term, args[2], args[3], args[4]);
                    }
                }
            } else {
                // TODO(ben): confirm whether errors reset the defaults
                dbg_printf("skiping invalid CSI:SGR sequence\n");
                term_reset_cell(term);
                return;
            }
            break;

        // Set foreground 8-15
        case 90: case 91:
        case 92: case 93:
        case 94: case 95:
        case 96: case 97:
            term_set_cell_fg(term, args[0] - 90 + 8);
            break;

        // Set background 8-15
        case 100: case 101:
        case 102: case 103:
        case 104: case 105:
        case 106: case 107:
            term_set_cell_bg(term, args[0] - 100 + 8);
            break;
        }
    } while (++i < parser->nargs);
}

// Device status report
void
emu_DSR(Term *term, const Parser *parser)
{
    const size_t arg = get_arg(parser, 0);
    char str[64] = { 0 };
    int len = 0;

    switch (arg) {
    case 5: // Respond with an "OK" status
        len = snprintf(str, sizeof(str), "\033[0n");
        break;
    case 6: // Respond with current cursor coordinates
        len = snprintf(str, sizeof(str), "\033[%d;%dR", term->cur.y + 1, term->cur.x + 1);
        break;
    }

    ASSERT(len < (int)sizeof(str));

    if (len > 0) {
        term_push(term, (uchar *)str, len);
    }
}

// Helper for setting/resetting standard modes via SM/RM
static inline void
set_modes(Term *term, const size_t *args, uint nargs, bool enable)
{
    for (uint i = 0; args && i < nargs; i++) {
        switch (args[i]) {
        case 2: // KAM - Keyboard Action Mode
            break;
        case 4: // IRM - Insertion Replacement Mode
            break;
        case 12: // SRM - Send/Receive Mode
            break;
        case 20: // LNM - Automatic Linefeed Mode (not in ECMA-48)
            break;
        }
    }
}

// Set mode
void
emu_SM(Term *term, const Parser *parser)
{
    set_modes(term, parser->args, parser->nargs, true);
}

// Reset mode
void
emu_RM(Term *term, const Parser *parser)
{
    set_modes(term, parser->args, parser->nargs, false);
}

// Helper for setting/resetting private modes via DECSET/DECRST
static inline void
set_modes_priv(Term *term, const size_t *args, uint nargs, bool enable)
{
    for (uint i = 0; args && i < nargs; i++) {
        switch (args[i]) {
        case 1: // DECCKM - Application cursor keys
            break;
        case 25: // DECTCEM - Text cursor enable/disable
            term_set_cursor_visibility(term, enable);
            break;
        case 1049: // DECSC/DECRC + Set alt/primary screen
            if (enable) {
                term_save_cursor(term);
                term_set_screen(term, 1);
            } else {
                term_restore_cursor(term);
                term_set_screen(term, 0);
            }
            break;
        }
    }
}

// Set private mode
void
emu_DECSET(Term *term, const Parser *parser)
{
    set_modes_priv(term, parser->args, parser->nargs, true);
}

// Reset private mode
void
emu_DECRST(Term *term, const Parser *parser)
{
    set_modes_priv(term, parser->args, parser->nargs, false);
}

// Set cursor style
void
emu_DECSCUSR(Term *term, const Parser *parser)
{
    const size_t arg = get_arg(parser, 0);

    term_set_cursor_style(term, arg);
}

