#include "utils.h"
#include "terminal.h"
#include "pty.h"
#include "fsm.h"
#include "ring.h"

#if BUILD_DEBUG
  #define DEBUG_PRINT_INPUT 0
  #define DEBUG_PRINT_ESC   1
#else
  #define DEBUG_PRINT_INPUT 0
  #define DEBUG_PRINT_ESC   0
#endif

#if DEBUG_PRINT_INPUT
static uchar *dbginput; // For human-readable printing
#endif

/*
 * LUT for the standard terminal RGB values. 0-15 may be overridden by the user.
 * https://wikipedia.org/wiki/ANSI_escape_code#Colors
 *
 * [0x00...0x07] Normal colors
 * [0x08...0x0f] High-intensity colors
 * [0x10...0xe7] 6x6x6 color cube
 * [0xe8...0xff] Grayscale (dark -> light)
 *
**/
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

#define CELLINIT(t) (Cell){  \
    .ucs4  = ' ',            \
    .bg    = (t)->color_bg,  \
    .fg    = (t)->color_fg,  \
    .type  = CellTypeNormal, \
    .width = 1,              \
    .attrs = 0,              \
}

static uint cellslen(const Cell *, int);

static void write_codepoint(Term *, uint32, CellType);
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

static void do_action(Term *, StateCode, ActionCode, uchar);

Term *
term_create(struct TermConfig config)
{
    Term *term = xmalloc(1, sizeof(*term));

    if (!term_init(term, config)) {
        free(term);
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
    term->max_cols = term->cols;
    term->max_rows = term->rows;
    term->histlines = round_pow2(config.histlines);

    term->ring_prim = ring_create(term->histlines, term->cols, term->rows);
    term->ring_alt  = ring_create(term->rows, term->cols, term->rows);
    term->ring = term->ring_prim;
    term->frame.cells = xcalloc(term->cols * term->rows, sizeof(*term->frame.cells));

    term->tabcols = config.tabcols;
    term->tabstops = xcalloc(term->cols, sizeof(*term->tabstops));
    for (int i = 0; ++i < term->cols; ) {
        term->tabstops[i] |= (i % term->tabcols == 0) ? 1 : 0;
    }

    arr_reserve(term->parser.data, 4);

    term->color_bg = config.color_bg;
    term->color_fg = config.color_fg;

    static_assert(LEN(term->colors) == LEN(config.colors), "User colors array mismatch.");

    for (uint i = 0; i < LEN(term->colors); i++) {
        if (config.colors[i]) {
            term->colors[i] = config.colors[i];
        } else {
            term->colors[i] = VTCOLOR(i);
        }
    }

    term->cell.width = 1;
    term->cell.bg = term->color_bg;
    term->cell.fg = term->color_fg;
    term->cell.attrs = 0;

    term->colsize = config.colsize;
    term->rowsize = config.rowsize;

    return true;
}

void
term_destroy(Term *term)
{
    ASSERT(term);
    arr_free(term->parser.data);
    if (term->frame.cells) {
        free(term->frame.cells);
    }
    if (term->tabstops) {
        free(term->tabstops);
    }
    ring_destroy(term->ring_prim);
    ring_destroy(term->ring_alt);
    term->ring = NULL;
    pty_hangup(term->pid);
    free(term);

#if DEBUG_PRINT_INPUT
    arr_free(dbginput);
#endif
}

int
term_exec(Term *term, const char *shell)
{
    if (!term->pid) {
        term->pid = pty_init(shell, &term->mfd, &term->sfd);
        pty_resize(term->mfd, term->cols, term->rows, term->colsize, term->rowsize);
    }

    return term->mfd;
}

Frame *
term_generate_frame(Term *term)
{
    Frame *frame = &term->frame;

    ring_copy_framebuffer(term->ring, frame->cells);
    frame->cols = term->cols;
    frame->rows = term->rows;
    frame->cursor.col = term->x;
    frame->cursor.row = term->y;
    frame->cursor.style = term->crs_style;
    frame->cursor.color = term->color_fg;
    frame->time = timer_msec(NULL);
    frame->default_bg = term->color_bg;
    frame->default_fg = term->color_fg;

    if (!term->hidecursor && check_visible(term->ring, term->x, term->y)) {
        frame->cursor.visible = true;
    } else {
        frame->cursor.visible = false;
    }

    return frame;
}

size_t
term_push(Term *term, const char *str, size_t len)
{
    return pty_write(term->mfd, (const uchar *)str, len);
}

size_t
term_pull(Term *term, uint32 msec)
{
    ASSERT(msec < 1E3);

    size_t accum = 0;
    const uint32 basetime = timer_msec(NULL);
    int timeout = msec;

    do {
        size_t len = pty_read(term->mfd, term->input, LEN(term->input), timeout);
        if (!len) {
            timeout = 0;
        } else {
            term_consume(term, term->input, len);
            accum += len;
            timeout -= (timer_msec(NULL) - basetime);
        }
    } while (timeout > 0);

    return accum;
}

void
term_scroll(Term *term, int delta)
{
    ring_move_screen_offset(term->ring, -delta);
}

void
term_reset_scroll(Term *term)
{
    ring_reset_screen_offset(term->ring);
}

void
term_resize(Term *term, int cols, int rows)
{
    cols = MAX(cols, 1);
    rows = MAX(rows, 1);

    if (cols != term->cols || rows != term->rows) {
        if (cols > term->max_cols || rows > term->max_rows) {
            term->max_cols = MAX(cols, term->max_cols);
            term->max_rows = MAX(rows, term->max_rows);
            term->frame.cells = xrealloc(
                term->frame.cells,
                term->max_cols * term->max_rows,
                sizeof(*term->frame.cells)
            );
        }

        if (cols != term->cols) {
            term->tabstops = xrealloc(term->tabstops, cols, sizeof(*term->tabstops));
            for (int i = term->cols; i < cols; i++) {
                term->tabstops[i] = (i && i % term->tabcols == 0) ? 1 : 0;
            }
        }

        // Compress the screen vertically.
        if (rows <= term->y) {
            ring_move_screen_head(term->ring_prim, term->rows - rows);
            term->y -= term->rows - rows;
        }

        // Expand the screen vertically while history lines exist.
        if (rows > term->rows) {
            int delta = imin(rows - term->rows, ring_histlines(term->ring_prim));
            ring_move_screen_head(term->ring_prim, -delta);
            term->y += delta;
        }

        ring_set_dimensions(term->ring_prim, cols, rows);
        ring_set_dimensions(term->ring_alt, cols, rows);

        term->cols = cols;
        term->rows = rows;

        pty_resize(term->mfd, cols, rows, term->colsize, term->rowsize);
    }
}

Cell *
term_get_row(const Term *term, int row)
{
    ASSERT(row >= 0 && row < term->rows);

    Cell *cells = cells_get_visible(term->ring, 0, row);
    if (cells[0].ucs4) {
        return cells;
    }

    return NULL;
}

Cell
term_get_cell(const Term *term, int col, int row)
{
    Cell cell = *cells_get(term->ring, col, row);

    return (Cell){
        .ucs4 = DEFAULT(cell.ucs4, ' '),
        .attrs = cell.attrs,
        .bg = (cell.attrs & ATTR_INVERT) ? cell.fg : cell.bg,
        .fg = (cell.attrs & ATTR_INVERT) ? cell.bg : cell.fg
    };
}

bool
term_get_cursor(const Term *term, CursorDesc *cursor)
{
    if (!term->hidecursor && check_visible(term->ring, term->x, term->y)) {
        if (cursor) {
            cursor->col = term->x;
            cursor->row = term->y;
            cursor->style = term->crs_style;
            cursor->color = term->color_fg;
        }

        return true;
    }

    return false;
}

size_t
term_consume(Term *term, const uchar *str, size_t len)
{
    uint i = 0;

    for (; str[i] && i < len; i++) {
        StateTrans result = fsm_next_state(term->parser.state, str[i]);

        for (uint n = 0; n < LEN(result.actions); n++) {
            do_action(term, result.state, result.actions[n], str[i]);
        }

        term->parser.state = result.state;

#if DEBUG_PRINT_INPUT
        {
            const char *tmp = charstring(str[i]);
            int len = strlen(tmp);
            if (len) {
                for (int j = 0; j < len; j++) {
                    arr_push(dbginput, tmp[j]);
                }
                arr_push(dbginput, ' ');
            }
        }
#endif
    }

#if DEBUG_PRINT_INPUT
    if (arr_count(dbginput)) {
        dbgprint("Input: %.*s", (int)arr_count(dbginput), (char *)dbginput);
        arr_clear(dbginput);
    }
#endif

    return i;
}

void
write_codepoint(Term *term, uint32 ucs4, CellType type)
{
    if (term->x + 1 < term->cols) {
        term->wrapnext = false;
    } else if (!term->wrapnext) {
        term->wrapnext = true;
    } else {
        term->wrapnext = false;
        row_set_wrap(term->ring, term->y, true);
        if (term->y + 1 == term->rows) {
            ring_move_screen_head(term->ring, 1);
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
        ring_move_screen_head(term->ring, 1);
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
        write_codepoint(term, ' ', type);
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
        .color   = term->color_fg,
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
    term->cell.bg = (idx < 16) ? term->colors[idx] : VTCOLOR(idx);
}

void
set_active_fg(Term *term, uint8 idx)
{
    term->cell.fg = (idx < 16) ? term->colors[idx] : VTCOLOR(idx);
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
    term->ring = term->ring_alt;
}

void
term_use_screen_primary(Term *term)
{
    term->ring = term->ring_prim;
}

void
reset_active_bg(Term *term)
{
    term->cell.bg = term->color_bg;
}

void
reset_active_fg(Term *term)
{
    term->cell.fg = term->color_fg;
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

void term_print_history(const Term *term) { dbg_print_ring(term->ring); }

// C0 control functions
static void emu_c0_ctrl(Term *, char);
// C1 control functions
static void emu_c1_ri(Term *, const int *, int);
static void emu_c1_decsc(Term *, const int *, int);
static void emu_c1_decrc(Term *, const int *, int);
// CSI functions
static void emu_csi_ich(Term *, const int *, int);
static void emu_csi_cuu(Term *, const int *, int);
static void emu_csi_cud(Term *, const int *, int);
static void emu_csi_cuf(Term *, const int *, int);
static void emu_csi_cub(Term *, const int *, int);
static void emu_csi_cnl(Term *, const int *, int);
static void emu_csi_cpl(Term *, const int *, int);
static void emu_csi_cha(Term *, const int *, int);
static void emu_csi_cup(Term *, const int *, int);
static void emu_csi_cht(Term *, const int *, int);
static void emu_csi_dch(Term *, const int *, int);
static void emu_csi_ed(Term *, const int *, int);
static void emu_csi_el(Term *, const int *, int);
static void emu_csi_sgr(Term *, const int *, int);
static void emu_csi_dsr(Term *, const int *, int);
static void emu_csi_decset(Term *, const int *, int);
static void emu_csi_decrst(Term *, const int *, int);
static void emu_csi_decscusr(Term *, const int *, int);
// OSC/DCS functions
static void emu_osc(Term *, const char *, const int *, int);

// Expandable table for immutable VT functions
#define HANDLER_TABLE \
    X_(C1,  NEL,      NULL) \
    X_(C1,  HTS,      NULL) \
    X_(C1,  RI,       emu_c1_ri) \
    X_(C1,  ST,       NULL) \
    X_(C1,  DECBI,    NULL) \
    X_(C1,  DECSC,    emu_c1_decsc) \
    X_(C1,  DECRC,    emu_c1_decrc) \
    X_(C1,  DECFI,    NULL) \
    X_(C1,  DECPAM,   NULL) \
    X_(C1,  DECPNM,   NULL) \
    X_(C1,  RIS,      NULL) \
    X_(C1,  LS2,      NULL) \
    X_(C1,  LS3,      NULL) \
    X_(C1,  LS3R,     NULL) \
    X_(C1,  LS2R,     NULL) \
    X_(C1,  LS1R,     NULL) \
    X_(CSI, ICH,      emu_csi_ich) \
    X_(CSI, CUU,      emu_csi_cuu) \
    X_(CSI, CUD,      emu_csi_cud) \
    X_(CSI, CUF,      emu_csi_cuf) \
    X_(CSI, CUB,      emu_csi_cub) \
    X_(CSI, CNL,      emu_csi_cnl) \
    X_(CSI, CPL,      emu_csi_cpl) \
    X_(CSI, CHA,      emu_csi_cha) \
    X_(CSI, CUP,      emu_csi_cup) \
    X_(CSI, CHT,      emu_csi_cht) \
    X_(CSI, ED,       emu_csi_ed) \
    X_(CSI, EL,       emu_csi_el) \
    X_(CSI, IL,       NULL) \
    X_(CSI, DL,       NULL) \
    X_(CSI, DCH,      emu_csi_dch) \
    X_(CSI, SU,       NULL) \
    X_(CSI, SD,       NULL) \
    X_(CSI, ECH,      NULL) \
    X_(CSI, CBT,      NULL) \
    X_(CSI, HPA,      NULL) \
    X_(CSI, HPR,      NULL) \
    X_(CSI, REP,      NULL) \
    X_(CSI, VPA,      NULL) \
    X_(CSI, HVP,      NULL) \
    X_(CSI, TBC,      NULL) \
    X_(CSI, SM,       NULL) \
    X_(CSI, MC,       NULL) \
    X_(CSI, RM,       NULL) \
    X_(CSI, DECSTBM,  NULL) \
    X_(CSI, SGR,      emu_csi_sgr) \
    X_(CSI, DSR,      emu_csi_dsr) \
    X_(CSI, DA,       NULL) \
    X_(CSI, SCOSC,    NULL) \
    X_(CSI, XTERMWM,  NULL) \
    X_(CSI, DECSCUSR, emu_csi_decscusr) \
    X_(CSI, DECSTR,   NULL) \
    X_(CSI, DECSCL,   NULL) \
    X_(CSI, DECCARA,  NULL) \
    X_(CSI, DECCRA,   NULL) \
    X_(CSI, DECFRA,   NULL) \
    X_(CSI, DECERA,   NULL) \
    X_(CSI, DECIC,    NULL) \
    X_(CSI, DECDC,    NULL) \
    X_(CSI, DECEFR,   NULL) \
    X_(CSI, DECELR,   NULL) \
    X_(CSI, DECSLE,   NULL) \
    X_(CSI, DECRQLP,  NULL) \
    X_(CSI, DECSED,   NULL) \
    X_(CSI, DECSEL,   NULL) \
    X_(CSI, DECSET,   emu_csi_decset) \
    X_(CSI, DECMC,    NULL) \
    X_(CSI, DECRST,   emu_csi_decrst) \
    X_(CSI, DECDSR,   NULL)

#define SYMBOL(suffix) OP##suffix

// Define opcodes for C1 and CSI escape sequences
enum {
    NOOP,
#define X_(group,opcode,...) SYMBOL(opcode),
    HANDLER_TABLE
#undef X_
};

struct HandlerInfo {
    // For debugging/logging
    const char *group;
    const char *name;
    // VT function implementation
    void (*func)(Term *, const int *argv, int argc);
};

// Define table entries for C1 and CSI escape sequences
static const struct HandlerInfo dispatch_table[] = {
    [NOOP] = { .group = "UNK" },
#define X_(group_,opcode_,func_) [SYMBOL(opcode_)] = { \
    .group = #group_,  \
    .name  = #opcode_, \
    .func  = func_     \
},
    HANDLER_TABLE
#undef X_
};

#undef SYMBOL
#undef HANDLER_TABLE

static void dispatch_static(Term *term, uint8 opcode);
static void dispatch_osc(Term *term);

#include <unistd.h> // for isatty()

static inline void
dbg_print_sequence(const char *group,
                   const char *name,
                   const char *prefix,
                   const int *argv, int argc,
                   const uchar *data,
                   const char *suffix,
                   bool implemented)
{
    if (isatty(2)) {
        printerr("\033[1;%dm*\033[m ", 30 + ((implemented) ? 6 : 3));
    } else {
        printerr("%c ", (implemented) ? '+' : '-');
    }

    printerr("%-3s %-8s \\e%s",
        DEFAULT(group, "---"),
        DEFAULT(name, "---"),
        DEFAULT(prefix, "")
    );

    char delim[2] = { "" };
    for (int i = 0; argv && i < argc; i++) {
        printerr("%s%d", delim, argv[i]);
        delim[0] = ';';
    }

    if (arr_count(data)) {
        printerr("%s%.*s", delim, (int)arr_count(data), (const char *)data);
    }

    printerr("%s\n", DEFAULT(suffix, ""));
}

inline void
dispatch_static(Term *term, uint8 opcode)
{
    ASSERT(opcode < LEN(dispatch_table));

    const struct HandlerInfo info = dispatch_table[opcode];
    const struct Parser *const parser = &term->parser;

#if DEBUG_PRINT_ESC
    {
        char prefix[3] = { "[" };
        char suffix[2] = { "" };

        if (parser->depth > 1) {
            prefix[1] = parser->tokens[0];
            suffix[0] = parser->tokens[1];
        } else {
            suffix[0] = parser->tokens[0];
        }

        dbg_print_sequence(
            info.group,
            info.name,
            prefix,
            parser->argv,
            parser->argi + 1,
            parser->data,
            suffix,
            !!info.func
        );

        ASSERT(parser->depth <= 1 || parser->depth == 2);
    }
#endif

    if (info.func) {
        info.func(term, parser->argv, parser->argi + 1);
    }
}

inline void
dispatch_osc(Term *term)
{
    const struct Parser *const parser = &term->parser;

#if DEBUG_PRINT_ESC
    {
        dbg_print_sequence(
            "OSC",
            NULL,
            "]",
            parser->argv,
            parser->argi,
            parser->data,
            NULL,
            true
        );
    }
#endif

    emu_osc(term, (char *)parser->data, parser->argv, parser->argi);
}

static inline void
parser_clear(struct Parser *parser)
{
    memset(parser->tokens, 0, sizeof(parser->tokens));
    parser->depth = 0;
    memset(parser->argv, 0, sizeof(parser->argv));
    parser->argi = 0;
    arr_clear(parser->data);
}

void
do_action(Term *term, StateCode state, ActionCode action, uchar c)
{
    if (!action) return;

    struct Parser *parser = &term->parser;

#if 0
    dbgprint("FSM(%s|%#.02x): State%s -> State%s ... %s()",
      charstring(c), c,
      fsm_get_state_string(parser->state),
      fsm_get_state_string(state),
      fsm_get_action_string(action));
#endif

    // TODO(ben): DCS functions
    switch (action) {
    case ActionIgnore:
        break;
    case ActionPrint:
        write_codepoint(term, parser->ucs4|(c & 0x7f), CellTypeNormal);
        parser->ucs4 = 0;
        break;
    case ActionUtf8Start:
        switch (state) {
        case StateUtf8B3: parser->ucs4 |= (c & 0x07) << 18; break;
        case StateUtf8B2: parser->ucs4 |= (c & 0x0f) << 12; break;
        case StateUtf8B1: parser->ucs4 |= (c & 0x1f) <<  6; break;
        default: break;
        }
        break;
    case ActionUtf8Cont:
        switch (state) {
        case StateUtf8B2: parser->ucs4 |= (c & 0x3f) << 12; break;
        case StateUtf8B1: parser->ucs4 |= (c & 0x3f) <<  6; break;
        default: break;
        }
        break;
    case ActionUtf8Fail:
        dbgprint("discarding malformed UTF-8 sequence");
        parser->ucs4 = 0;
        break;
    case ActionExec:
        emu_c0_ctrl(term, c);
        break;
    case ActionHook:
        // sets handler based on DCS parameters, intermediates, and new char
        break;
    case ActionUnhook:
        break;
    case ActionPut:
        arr_push(parser->data, c);
        break;
    case ActionOscStart:
        memset(parser->argv, 0, sizeof(parser->argv));
        parser->argi = 0;
        arr_clear(parser->data);
        break;
    case ActionOscPut:
        // All OSC sequences take a leading numeric parameter, which we consume in the arg buffer.
        // Semicolon-separated string parameters are handled by the OSC parser itself
        if (!parser->argi) {
            if (c >= '0' && c <= '9') {
                parser->argv[0] *= 10;
                parser->argv[0] += c - '0';
            } else {
                // NOTE(ben): Assuming OSC sequences take a default '0' parameter
                if (c != ';') {
                    parser->argv[0] = 0;
                }
                parser->argv[++parser->argi] = 0;
            }
        } else {
            arr_push(parser->data, c);
        }
        break;
    case ActionOscEnd:
        arr_push(parser->data, 0);
        dispatch_osc(term);
        break;
    case ActionCollect:
        if (parser->depth == LEN(parser->tokens)) {
            dbgprint("warning: ignoring excess intermediate");
        } else {
            parser->tokens[parser->depth++] = c;
        }
        break;
    case ActionParam:
        if (c == ';') {
            if (parser->argi + 1 == LEN(parser->argv)) {
                dbgprint("warning: ignoring excess parameter");
            } else {
                parser->argv[++parser->argi] = 0;
            }
        } else {
            ASSERT(INT_MAX / 10 > parser->argv[parser->argi]);
            parser->argv[parser->argi] *= 10;
            parser->argv[parser->argi] += (c - '0');
        }
        break;
    case ActionClear:
        parser_clear(parser);
        break;
    case ActionEscDispatch: {
        parser->tokens[parser->depth++] = c;

        uint8 opcode = NOOP;

        switch (c) {
        case 'E':  opcode = OPNEL;    break;
        case 'H':  opcode = OPHTS;    break;
        case 'M':  opcode = OPRI;     break;
        case '\\': opcode = OPST;     break;
        case '6':  opcode = OPDECBI;  break;
        case '7':  opcode = OPDECSC;  break;
        case '8':  opcode = OPDECRC;  break;
        case '9':  opcode = OPDECFI;  break;
        case '=':  opcode = OPDECPAM; break;
        case '>':  opcode = OPDECPNM; break;
        case 'F':  break; // cursor lower-left
        case 'c':  opcode = OPRIS;    break;
        case 'l':  break; // memory lock
        case 'm':  break; // memory unlock
        case 'n':  opcode = OPLS2;    break;
        case 'o':  opcode = OPLS3;    break;
        case '|':  opcode = OPLS3R;   break;
        case '}':  opcode = OPLS2R;   break;
        case '~':  opcode = OPLS1R;   break;
        }

        dispatch_static(term, opcode);
        break;
    }
    case ActionCsiDispatch: {
        parser->tokens[parser->depth] = c;

        uint8 opcode = NOOP;

        switch ((parser->depth++ > 0) ? parser->tokens[0] : 0) {
        case 0:
            switch (c) {
            case '@': opcode = OPICH;     break;
            case 'A': opcode = OPCUU;     break;
            case 'B': opcode = OPCUD;     break;
            case 'C': opcode = OPCUF;     break;
            case 'D': opcode = OPCUB;     break;
            case 'E': opcode = OPCNL;     break;
            case 'F': opcode = OPCPL;     break;
            case 'G': opcode = OPCHA;     break;
            case 'H': opcode = OPCUP;     break;
            case 'I': opcode = OPCHT;     break;
            case 'J': opcode = OPED;      break;
            case 'K': opcode = OPEL;      break;
            case 'L': opcode = OPIL;      break;
            case 'M': opcode = OPDL;      break;
            case 'P': opcode = OPDCH;     break;
            case 'S': opcode = OPSU;      break;
            case 'T': opcode = OPSD;      break;
            case 'X': opcode = OPECH;     break;
            case 'Z': opcode = OPCBT;     break;
            case '`': opcode = OPHPA;     break;
            case 'a': opcode = OPHPR;     break;
            case 'b': opcode = OPREP;     break;
            case 'd': opcode = OPVPA;     break;
            case 'f': opcode = OPHVP;     break;
            case 'g': opcode = OPTBC;     break;
            case 'h': opcode = OPSM;      break;
            case 'i': opcode = OPMC;      break;
            case 'l': opcode = OPRM;      break;
            case 'm': opcode = OPSGR;     break;
            case 'n': opcode = OPDSR;     break;
            case 'r': opcode = OPDECSTBM; break;
            case 'c': opcode = OPDA;      break;
            case 's': opcode = OPSCOSC;   break;
            case 't': opcode = OPXTERMWM; break;
            }
            break;
        case ' ':
            switch (c) {
            case 'q': opcode = OPDECSCUSR; break;
            }
            break;
        case '!':
            switch (c) {
            case 'p': opcode = OPDECSTR; break;
            }
            break;
        case '"':
            switch (c) {
            case 'p': opcode = OPDECSCL; break;
            }
            break;
        case '$':
            switch (c) {
            case 't': opcode = OPDECCARA; break;
            case 'v': opcode = OPDECCRA;  break;
            case 'x': opcode = OPDECFRA;  break;
            case 'z': opcode = OPDECERA;  break;
            }
            break;
        case '\'':
            switch (c) {
            case '}': opcode = OPDECIC; break;
            case '~': opcode = OPDECDC; break;
            }
            break;
        case '>':
            switch (c) {
            case 'w': opcode = OPDECEFR;  break;
            case 'z': opcode = OPDECELR;  break;
            case '{': opcode = OPDECSLE;  break;
            case '|': opcode = OPDECRQLP; break;
            }
            break;
        case '?':
            switch (c) {
            case 'J': opcode = OPDECSED; break;
            case 'K': opcode = OPDECSEL; break;
            case 'h': opcode = OPDECSET; break;
            case 'i': opcode = OPDECMC;  break;
            case 'l': opcode = OPDECRST; break;
            case 'n': opcode = OPDECDSR; break;
            }
            break;
        case '}':
            break;
        case '~':
            break;
        default:
            break;
        }

        dispatch_static(term, opcode);
        break;
    }
    default:
        break;
    }
}

inline void
emu_c0_ctrl(Term *term, char c)
{
    switch (c) {
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
        dbgprint("unhandled control character: %s", charstring(c));
        break;
    }
}

inline void
emu_osc(Term *term, const char *str, const int *argv, int argc)
{
    /*
     * Leading arguments:
     *   0 - Set icon name and window title
     *   1 - Set icon name
     *   2 - Set window title
     *   3 - Set window property
     *   4 - Set following color specification
    **/
    UNUSED(argv);
    UNUSED(argc);

    return;
}

void
emu_c1_ri(Term *term, const int *argv, int argc)
{
    UNUSED(argv);
    UNUSED(argc);

    if (term->y > 0) {
        move_cursor_rows(term, -1);
    } else {
        rows_move(term->ring, 0, term->rows, 1);
    }
}

void
emu_c1_decsc(Term *term, const int *argv, int argc)
{
    UNUSED(argv);
    UNUSED(argc);
    cursor_save(term);
}

void
emu_c1_decrc(Term *term, const int *argv, int argc)
{
    UNUSED(argv);
    UNUSED(argc);
    cursor_restore(term);
}

void
emu_csi_ich(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    cells_insert(term->ring, CELLINIT(term), term->x, term->y, MAX(argv[0], 1));
}

void
emu_csi_cuu(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    move_cursor_rows(term, -MAX(argv[0], 1));
}

void
emu_csi_cud(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    move_cursor_rows(term, +MAX(argv[0], 1));
}

void
emu_csi_cuf(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    move_cursor_cols(term, +MAX(argv[0], 1));
}

void
emu_csi_cub(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    move_cursor_cols(term, -MAX(argv[0], 1));
}

static
void emu_csi_cnl(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    move_cursor_rows(term, +MAX(argv[0], 1));
}

static
void emu_csi_cpl(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    move_cursor_rows(term, -MAX(argv[0], 1));
}

void
emu_csi_cha(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    set_cursor_col(term, MAX(argv[0], 1) - 1);
}

void
emu_csi_cup(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    set_cursor_col(term, MAX(argv[1], 1) - 1);
    set_cursor_row(term, MAX(argv[0], 1) - 1);
}

void
emu_csi_cht(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    for (int n = DEFAULT(argv[0], 1); n > 0; n--) {
        write_tab(term);
    }
}

void
emu_csi_dch(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    cells_delete(term->ring, term->x, term->y, argv[0]);
}

void
emu_csi_ed(Term *term, const int *argv, int argc)
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

void
emu_csi_el(Term *term, const int *argv, int argc)
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

void
emu_csi_sgr(Term *term, const int *argv, int argc)
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
                dbgprint("skiping invalid CSI:SGR sequence");
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

void
emu_csi_dsr(Term *term, const int *argv, int argc)
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
        term_push(term, str, len);
    }
}

static inline void
emu__csi_decprv(Term *term, int mode, bool enable)
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
void
emu_csi_decset(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    emu__csi_decprv(term, argv[0], true);
}

// NOTE(ben): Wrapper
void
emu_csi_decrst(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    emu__csi_decprv(term, argv[0], false);
}

void
emu_csi_decscusr(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    set_cursor_style(term, argv[0]);
}

