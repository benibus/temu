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
#include "term_private.h"
#include "term_opcodes.h"
#include "term_parser.h"
#include "term_ring.h"
#include "gfx_draw.h"

#include <unistd.h> // for isatty()

#define CELLINIT(t)              \
    (Cell){                      \
        .ucs4  = ' ',            \
        .bg    = (t)->colors.bg, \
        .fg    = (t)->colors.fg, \
        .type  = CellTypeNormal, \
        .width = 1,              \
        .attrs = 0               \
    }

static uint cellslen(const Cell *, int);

// TODO(ben): Fix naming, move to internal header
static size_t consume(Term *term, const uchar *data, size_t len);
static void write_codepoint(Term *term, uint32 ucs4);
static void write_printable(Term *, uint32, CellType);
static void write_tab(Term *);
static void write_newline(Term *);
static void set_active_bg(Term *, uint8);
static void set_active_fg(Term *, uint8);
static void set_active_bg_rgb(Term *, uint8, uint8, uint8);
static void set_active_fg_rgb(Term *, uint8, uint8, uint8);
static void reset_active_bg(Term *);
static void reset_active_fg(Term *);
static void set_active_attrs(Term *, uint16);
static void add_active_attrs(Term *, uint16);
static void del_active_attrs(Term *, uint16);
static void move_cursor_cols(Term *, int);
static void move_cursor_rows(Term *, int);
static void set_cursor_col(Term *, int);
static void set_cursor_row(Term *, int);
static void set_cursor_visibility(Term *, bool);
static void set_cursor_style(Term *, int);

static void alloc_frame(Frame *, uint16, uint16);
static void alloc_tabstops(uint8 **, uint16, uint16, uint16);
static void init_palette(Palette *);
static void update_dimensions(Term *, uint16, uint16);

#define FUNCPROTO(x) void x(Term *term, const char *data, const int *argv, int argc)
#define FUNCNAME(x)  func_##x
#define FUNCDECL(x)  static FUNCPROTO(FUNCNAME(x))
#define FUNCDEFN(x)  inline FUNCPROTO(FUNCNAME(x))

#define X_DISPATCH_FUNCS \
    X_(RI) \
    X_(DECSC) \
    X_(DECRC) \
    X_(ICH) \
    X_(CUU) \
    X_(CUD) \
    X_(CUF) \
    X_(CUB) \
    X_(CNL) \
    X_(CPL) \
    X_(CHA) \
    X_(CUP) \
    X_(CHT) \
    X_(DCH) \
    X_(VPA) \
    X_(VPR) \
    X_(ED) \
    X_(EL) \
    X_(SGR) \
    X_(DSR) \
    X_(DECSET) \
    X_(DECRST) \
    X_(DECSCUSR) \
    X_(OSC)

#define X_(cmd) FUNCDECL(cmd);
X_DISPATCH_FUNCS
#undef X_

typedef FUNCPROTO((*DispatchFunc));

static const DispatchFunc func_table[NumOpcodes] = {
#define X_(cmd) [OPIDX_##cmd] = &FUNCNAME(cmd),
    X_DISPATCH_FUNCS
#undef X_
};

Term *
term_create(App *app)
{
    Term *term = xcalloc(1, sizeof(*term));

    // Query known metrics/settings/resources from application
    term->fonts   = app_fonts(app);
    term->tabcols = app_tabcols(app);
    term->colpx   = app_font_width(app);
    term->rowpx   = app_font_height(app);
    term->width   = app_width(app);
    term->height  = app_height(app);
    term->border  = app_border(app);
    ASSERT(term->colpx > 0 && term->rowpx > 0);

    // Compute initial cols/rows and scrollback length
    term->cols = imax(0, term->width - 2 * term->border) / term->colpx;
    term->rows = imax(0, term->height - 2 * term->border) / term->rowpx;
    term->histlines = round_pow2(imax(term->rows, app_histlines(app)));

    // Initialize colors
    init_palette(&term->colors);
    app_get_palette(app, &term->colors);

    // Default starting cell
    term->cell.width = 1;
    term->cell.bg    = term->colors.bg;
    term->cell.fg    = term->colors.fg;
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
               term->colpx,
               term->rowpx,
               term->border,
               term->histlines);

    return term;
}

void
init_palette(Palette *palette)
{
    static_assert(LEN(palette->base256) == 256, "Invalid color table size");
    ASSERT(palette);

    memset(palette->base256, 0, sizeof(palette->base256));

    // Default standard colors (0-15)
    static const uint32 base16[16] = {
        0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xc0c0c0,
        0x808080, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff, 0xffffff
    };

    uint32 *const table = palette->base256;

    for (uint i = 0; i < 256; i++) {
        if (i < 16) {
            table[i] = base16[i];
        } else if (i < 232) {
            // 6x6x6 color cube (16-231)
            uint8 n;
            table[i] |= ((n = ((i - 16) / 36) % 6) ? 40 * n + 55 : 0) << 16;
            table[i] |= ((n = ((i - 16) /  6) % 6) ? 40 * n + 55 : 0) <<  8;
            table[i] |= ((n = ((i - 16) /  1) % 6) ? 40 * n + 55 : 0) <<  0;
        } else {
            // Grayscale from darkest to lightest (232-255)
            const uint8 k = (i - 232) * 10 + 8;
            table[i] |= k << 16;
            table[i] |= k <<  8;
            table[i] |= k <<  0;
        }
    }

    palette->bg = table[0];
    palette->fg = table[7];
}

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

int
term_exec(Term *term, const char *shell, int argc, const char *const *argv)
{
    UNUSED(argc);
    UNUSED(argv);

    ASSERT(term);

    if (!term->pid) {
        parser_init(&term->parser);
        term->pid = pty_init(shell, &term->mfd, &term->sfd);
        pty_resize(term->mfd, term->cols, term->rows, term->colpx, term->rowpx);
    }

    return term->mfd;
}

int term_cols(const Term *term) { return term->cols; }
int term_rows(const Term *term) { return term->rows; }

static Frame *
generate_frame(Term *term)
{
    Frame *frame = &term->frame;

    ring_copy_framebuffer(term->ring, frame->cells);
    frame->cols = term->cols;
    frame->rows = term->rows;
    frame->width = term->cols * term->colpx;
    frame->height = term->rows *term->rowpx;
    frame->cursor.col = term->x;
    frame->cursor.row = term->y;
    frame->cursor.style = term->crs_style;
    frame->cursor.color = term->colors.fg;
    frame->time = timer_msec(NULL);
    frame->default_bg = term->colors.bg;
    frame->default_fg = term->colors.fg;

    if (!term->hidecursor && check_visible(term->ring, term->x, term->y)) {
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

    gfx_clear_rgb1u(term->colors.bg);
    if (term->pid) {
        gfx_draw_frame(generate_frame(term), term->fonts);
    }
}

size_t
term_push(Term *term, const void *data, size_t len)
{
    return pty_write(term->mfd, data, len);
}

size_t
term_pull(Term *term)
{
    ASSERT(term && term->pid);

    const size_t len = pty_read(term->mfd, term->input, LEN(term->input));
    if (len > 0) {
        consume(term, term->input, len);
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
    ASSERT(width);
    ASSERT(height);

    const int cols = (int)width / term->colpx;
    const int rows = (int)height / term->rowpx;

    if (cols == term->cols && rows == term->rows) {
        return;
    }

    // Compress the screen vertically.
    if (rows <= term->y) {
        ring_adjust_head(term->rings[0], term->rows - rows);
        term->y -= term->rows - rows;
    }

    // Expand the screen vertically while history lines exist.
    if (rows > term->rows) {
        int delta = imin(rows - term->rows, ring_histlines(term->rings[0]));
        ring_adjust_head(term->rings[0], -delta);
        term->y += delta;
    }

    // Resize history
    ring_set_dimensions(term->rings[0], cols, rows);
    ring_set_dimensions(term->rings[1], cols, rows);

    // Resize extra buffers
    alloc_tabstops(&term->tabstops, term->max_cols, cols, term->tabcols);
    alloc_frame(&term->frame, cols, rows);

    // Resize psuedoterminal
    pty_resize(term->mfd, cols, rows, term->colpx, term->rowpx);

    // Commit changes
    update_dimensions(term, cols, rows);
}

static inline void
print_parsed_params(const TermParser *parse)
{
    for (int i = 0; i < parse->argi + 1; i++) {
        fprintf(stderr, "%s%d", (i) ? ";" : "", parse->argv[i]);
    }
}

static inline void
print_parsed_string(const TermParser *parse)
{
    fprintf(stderr,
            "%d;%.*s",
            parse->argi ? parse->argv[0] : 0,
            (int)arr_count(parse->data),
            (char *)parse->data);
}

static inline void
trace_dispatch(uint32 oc, uint16 idx, const TermParser *parse)
{
    const char *const name = opcode_to_string(oc);
    const bool implemented = (OPCODE_TAG(oc) == OPTAG_WRITE || !!func_table[idx]);

    if (isatty(2)) {
        fprintf(stderr, "\033[1;%dm*\033[m ", implemented ? 36 : 33);
    } else {
        fprintf(stderr, "%c ", implemented ? '*' : ' ');
    }
    fprintf(stderr, "0x%x [%3u] %-16s ", oc, idx, name);

    const uint tag = OPCODE_TAG(oc);

    switch (tag) {
    case OPTAG_ESC:
    case OPTAG_CSI:
    case OPTAG_DCS: {
        char buf[3] = {0};
        opcode_get_chars(oc, buf);

        switch (tag) {
        case OPTAG_ESC:
            fprintf(stderr, "\\e%.1s%.1s", &buf[1], &buf[0]);
            break;
        case OPTAG_CSI:
            fprintf(stderr, "\\e[%.1s", &buf[2]);
            print_parsed_params(parse);
            fprintf(stderr, "%.1s%.1s", &buf[1], &buf[0]);
            break;
        case OPTAG_DCS:
            fprintf(stderr, "\\eP%.1s%.1s%.1s", &buf[2], &buf[1], &buf[0]);
            print_parsed_string(parse);
            break;
        }
        break;
    case OPTAG_OSC:
        fprintf(stderr, "\\e]");
        print_parsed_string(parse);
        break;
    }
    default:
        break;
    }

    fprintf(stderr, "\n");
}

size_t
consume(Term *term, const uchar *str, size_t len)
{
    size_t i = 0;

    while (i < len) {
        size_t adv;
        const uint32 opcode = parser_emit(&term->parser, &str[i], len - i, &adv);

        if (opcode) {
            const uint opidx = opcode_to_index(opcode);

            // trace_dispatch(opcode, opidx, &term->parser);

            if (OPCODE_TAG(opcode) == OPTAG_WRITE) {
                write_codepoint(term, OPCODE_NO_TAG(opcode));
            } else if (opcode) {
                if (func_table[opidx]) {
                    func_table[opidx](term,
                                      (char *)term->parser.data,
                                      term->parser.argv,
                                      term->parser.argi+1);
                }
            }
        }

        i += adv;
    }

    return i;
}

void
write_printable(Term *term, uint32 ucs4, CellType type)
{
    if (term->x + 1 < term->cols) {
        term->wrapnext = false;
    } else if (!term->wrapnext) {
        term->wrapnext = true;
    } else {
        term->wrapnext = false;
        row_set_wrap(term->ring, term->y, true);
        if (term->y + 1 == term->rows) {
            ring_adjust_head(term->ring, 1);
        } else {
            term->y++;
        }
        term->x = 0;
    }

    Cell *cell = cells_get(term->ring, term->x, term->y);

    cell[0] = (Cell){
        .ucs4  = ucs4,
        .width = 1,
        .bg    = term->cell.bg,
        .fg    = term->cell.fg,
        .attrs = term->cell.attrs,
        .type  = type
    };

    if (!term->wrapnext) {
        ASSERT(term->x + 1 < term->cols);
        term->x++;
    }
}

void
write_newline(Term *term)
{
    if (term->y + 1 == term->rows) {
        ring_adjust_head(term->ring, 1);
        rows_clear(term->ring, term->y, 1);
    } else {
        term->y++;
    }
}

void
write_tab(Term *term)
{
    int type = CellTypeTab;

    for (int n = 0; term->x + 1 < term->cols; n++) {
        if (term->tabstops[term->x] && n > 0) {
            break;
        }
        write_printable(term, ' ', type);
        type = CellTypeDummyTab;
    }
}

void
move_cursor_cols(Term *term, int cols)
{
    Cell *cells = cells_get(term->ring, 0, term->y);

    const int beg = term->x;
    const int end = CLAMP(beg + cols, 0, term->cols - 1);

    for (int at = beg; at < end; at++) {
        if (!cells[at].ucs4) {
            cells[at] = CELLINIT(term);
        }
    }

    term->x = end;
}

void
move_cursor_rows(Term *term, int rows)
{
    term->y = CLAMP(term->y + rows, 0, term->rows - 1);
}

void
set_cursor_col(Term *term, int col)
{
    term->x = CLAMP(col, 0, term->cols - 1);
}

void
set_cursor_row(Term *term, int row)
{
    term->y = CLAMP(row, 0, term->rows - 1);
}

void
set_cursor_visibility(Term *term, bool ishidden)
{
    term->hidecursor = ishidden;
}

void
set_cursor_style(Term *term, int style)
{
    ASSERT(style >= 0);

    if (style <= 7) {
        term->crs_style = style;
    }
}

void
cursor_save(Term *term)
{
    term->saved_crs = (CursorDesc){
        .col     = term->x,
        .row     = term->y,
        .style   = term->crs_style,
        .color   = term->colors.fg,
        .visible = !term->hidecursor
    };
}

void
cursor_restore(Term *term)
{
    CursorDesc saved = term->saved_crs;
    term->x          = saved.col;
    term->y          = saved.row;
    term->crs_style  = saved.style;
    term->hidecursor = !saved.visible;
}

void
set_active_bg(Term *term, uint8 idx)
{
    term->cell.bg = term->colors.base256[idx];
}

void
set_active_fg(Term *term, uint8 idx)
{
    term->cell.fg = term->colors.base256[idx];
}

void
set_active_bg_rgb(Term *term, uint8 r, uint8 g, uint8 b)
{
    term->cell.bg = pack_argb(r, g, b, 0xff);
}

void
set_active_fg_rgb(Term *term, uint8 r, uint8 g, uint8 b)
{
    term->cell.fg = pack_argb(r, g, b, 0xff);
}

void
term_use_screen_alt(Term *term)
{
    term->ring = term->rings[1];
}

void
term_use_screen_primary(Term *term)
{
    term->ring = term->rings[0];
}

void
reset_active_bg(Term *term)
{
    term->cell.bg = term->colors.bg;
}

void
reset_active_fg(Term *term)
{
    term->cell.fg = term->colors.fg;
}

void
set_active_attrs(Term *term, uint16 attrs)
{
    term->cell.attrs = attrs;
}

void
add_active_attrs(Term *term, uint16 attrs)
{
    term->cell.attrs |= attrs;
}

void
del_active_attrs(Term *term, uint16 attrs)
{
    term->cell.attrs &= ~attrs;
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

void term_print_history(const Term *term)
{
    dbg_print_ring(term->ring);
}

inline void
write_codepoint(Term *term, uint32 ucs4)
{
    switch (ucs4) {
    case '\n':
    case '\v':
    case '\f':
        write_newline(term);
        break;
    case '\t':
        write_tab(term);
        break;
    case '\r':
        set_cursor_col(term, 0);
        break;
    case '\b':
        move_cursor_cols(term, -1);
        break;
    case '\a':
        break;
    default:
        write_printable(term, ucs4, CellTypeNormal);
        break;
    }
}

FUNCDEFN(OSC)
{
    /*
     * Leading arguments:
     *   0 - Set icon name and window title
     *   1 - Set icon name
     *   2 - Set window title
     *   3 - Set window property
     *   4 - Set following color specification
     */
    UNUSED(argc);

    uint8 props = 0;

    switch (argv[0]) {
    case 0:
    case 1:
        props |= APPPROP_ICON;
        if (argv[0]) break;
        // fallthrough
    case 2:
        props |= APPPROP_TITLE;
        break;
    case 3:
        break;
    }

    if (props) {
        app_set_properties(0, props, data, arr_count(data));
    }

    return;
}

FUNCDEFN(RI)
{
    UNUSED(argv);
    UNUSED(argc);

    if (term->y > 0) {
        move_cursor_rows(term, -1);
    } else {
        rows_move(term->ring, 0, term->rows, 1);
    }
}

FUNCDEFN(DECSC)
{
    UNUSED(argv);
    UNUSED(argc);
    cursor_save(term);
}

FUNCDEFN(DECRC)
{
    UNUSED(argv);
    UNUSED(argc);
    cursor_restore(term);
}

FUNCDEFN(ICH)
{
    UNUSED(argc);
    cells_insert(term->ring, CELLINIT(term), term->x, term->y, MAX(argv[0], 1));
}

FUNCDEFN(CUU)
{
    UNUSED(argc);
    move_cursor_rows(term, -MAX(argv[0], 1));
}

FUNCDEFN(CUD)
{
    UNUSED(argc);
    move_cursor_rows(term, +MAX(argv[0], 1));
}

FUNCDEFN(CUF)
{
    UNUSED(argc);
    move_cursor_cols(term, +MAX(argv[0], 1));
}

FUNCDEFN(CUB)
{
    UNUSED(argc);
    move_cursor_cols(term, -MAX(argv[0], 1));
}

FUNCDEFN(CNL)
{
    UNUSED(argc);
    move_cursor_rows(term, +MAX(argv[0], 1));
}

FUNCDEFN(CPL)
{
    UNUSED(argc);
    move_cursor_rows(term, -MAX(argv[0], 1));
}

FUNCDEFN(CHA)
{
    UNUSED(argc);
    set_cursor_col(term, MAX(argv[0], 1) - 1);
}

FUNCDEFN(CUP)
{
    UNUSED(argc);
    set_cursor_col(term, MAX(argv[1], 1) - 1);
    set_cursor_row(term, MAX(argv[0], 1) - 1);
}

FUNCDEFN(CHT)
{
    UNUSED(argc);
    for (int n = DEFAULT(argv[0], 1); n > 0; n--) {
        write_tab(term);
    }
}

FUNCDEFN(DCH)
{
    UNUSED(argc);
    cells_delete(term->ring, term->x, term->y, argv[0]);
}

FUNCDEFN(VPA)
{
    UNUSED(argc);
    set_cursor_row(term, MAX(argv[1], 1) - 1);
}

FUNCDEFN(VPR)
{
    UNUSED(argc);
    move_cursor_rows(term, MAX(argv[1], 1) - 1);
}

FUNCDEFN(ED)
{
    switch (argv[0]) {
    case 0:
        rows_clear(term->ring, term->y + 1, term->rows);
        cells_clear(term->ring, term->x, term->y, term->cols);
        break;
    case 1:
        rows_clear(term->ring, 0, term->y);
        cells_set(term->ring, CELLINIT(term), 0, term->y, term->x);
        break;
    case 2:
        rows_clear(term->ring, 0, term->rows);
        set_cursor_row(term, 0);
        break;
    }
}

FUNCDEFN(EL)
{
    switch (argv[0]) {
    case 0:
        cells_clear(term->ring, term->x, term->y, term->cols);
        break;
    case 1:
        cells_set(term->ring, CELLINIT(term), 0, term->y, term->x);
        break;
    case 2:
        cells_clear(term->ring, 0, term->y, term->cols);
        set_cursor_col(term, 0);
        break;
    }
}

FUNCDEFN(SGR)
{
    int i = 0;

    do {
        const int start = i;

        switch (argv[i]) {
        case 0:
            set_active_attrs(term, 0);
            reset_active_bg(term);
            reset_active_fg(term);
            break;

        case 1:  add_active_attrs(term, ATTR_BOLD);      break;
        case 3:  add_active_attrs(term, ATTR_ITALIC);    break;
        case 4:  add_active_attrs(term, ATTR_UNDERLINE); break;
        case 5:  add_active_attrs(term, ATTR_BLINK);     break;
        case 7:  add_active_attrs(term, ATTR_INVERT);    break;
        case 8:  add_active_attrs(term, ATTR_INVISIBLE); break;
        case 22: del_active_attrs(term, ATTR_BOLD);      break;
        case 23: del_active_attrs(term, ATTR_ITALIC);    break;
        case 24: del_active_attrs(term, ATTR_UNDERLINE); break;
        case 25: del_active_attrs(term, ATTR_BLINK);     break;
        case 27: del_active_attrs(term, ATTR_INVERT);    break;
        case 28: del_active_attrs(term, ATTR_INVISIBLE); break;

        case 30: case 31:
        case 32: case 33:
        case 34: case 35:
        case 36: case 37:
            set_active_fg(term, argv[i] - 30);
            break;
        case 39:
            reset_active_fg(term);
            break;

        case 40: case 41:
        case 42: case 43:
        case 44: case 45:
        case 46: case 47:
            set_active_bg(term, argv[i] - 40);
            break;
        case 49:
            reset_active_bg(term);
            break;

        case 38:
        case 48:
            if (++i + 1 < argc) {
                if (argv[i] == 5) {
                    i++;
                } else if (argv[i] == 2 && i + 3 < argc) {
                    i += 3;
                }
            }
            if (i - start == 2) {
                if (argv[start] == 48) {
                    set_active_bg(term, argv[i] & 0xff);
                } else if (argv[start] == 38) {
                    set_active_fg(term, argv[i] & 0xff);
                }
            } else if (i - start == 4) {
                if (argv[start] == 48) {
                    set_active_bg_rgb(term,
                        argv[i-2] & 0xff,
                        argv[i-1] & 0xff,
                        argv[i-0] & 0xff
                    );
                } else if (argv[start] == 38) {
                    set_active_fg_rgb(term,
                        argv[i-2] & 0xff,
                        argv[i-1] & 0xff,
                        argv[i-0] & 0xff
                    );
                }
            } else {
                // TODO(ben): confirm whether errors reset the defaults
                dbg_printf("skiping invalid CSI:SGR sequence\n");
                set_active_attrs(term, 0);
                reset_active_bg(term);
                reset_active_fg(term);
                return;
            }
            break;

        case 90: case 91:
        case 92: case 93:
        case 94: case 95:
        case 96: case 97:
            set_active_fg(term, argv[i] - 90 + 8);
            break;

        case 100: case 101:
        case 102: case 103:
        case 104: case 105:
        case 106: case 107:
            set_active_bg(term, argv[i] - 100 + 8);
            break;
        }
    } while (++i < argc);
}

FUNCDEFN(DSR)
{
    UNUSED(argc);

    char str[64] = { 0 };
    int len = 0;

    switch (argv[0]) {
    case 5: // Respond with an "OK" status
        len = snprintf(str, sizeof(str), "\033[0n");
        break;
    case 6: // Respond with current cursor coordinates
        len = snprintf(str, sizeof(str), "\033[%d;%dR", term->y + 1, term->x + 1);
        break;
    }

    ASSERT(len < (int)sizeof(str));

    if (len > 0) {
        term_push(term, (uchar *)str, len);
    }
}

static inline void
decprv_helper(Term *term, int mode, bool enable)
{
    switch (mode) {
    case 1: // DECCKM (application/normal cursor keys)
        break;
    case 25: // DECTCEM
        set_cursor_visibility(term, !enable);
        break;
    case 1049: // DECSC/DECRC
        if (enable) {
            cursor_save(term);
            term_use_screen_alt(term);
        } else {
            cursor_restore(term);
            term_use_screen_primary(term);
        }
        break;
    }
}

// NOTE(ben): Wrapper
FUNCDEFN(DECSET)
{
    UNUSED(argc);
    decprv_helper(term, argv[0], true);
}

// NOTE(ben): Wrapper
FUNCDEFN(DECRST)
{
    UNUSED(argc);
    decprv_helper(term, argv[0], false);
}

FUNCDEFN(DECSCUSR)
{
    UNUSED(argc);
    set_cursor_style(term, argv[0]);
}

