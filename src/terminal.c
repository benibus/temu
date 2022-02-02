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
#include "terminal.h"
#include "keymap.h"
#include "pty.h"
#include "fsm.h"
#include "ring.h"

#if BUILD_DEBUG
  #define DEBUG_PRINT_INPUT 1
  #define DEBUG_PRINT_ESC   1
#else
  #define DEBUG_PRINT_INPUT 0
  #define DEBUG_PRINT_ESC   0
#endif

#if DEBUG_PRINT_INPUT
static uchar *dbginput; // For human-readable printing
#endif

#define CELLINIT(t) (Cell){  \
    .ucs4  = ' ',            \
    .bg    = (t)->colors.bg,  \
    .fg    = (t)->colors.fg,  \
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

#define XCALLBACK_(M,T) \
void term_callback_##M(Term *term, void *param, TermFunc##T *func) { \
    ASSERT(term);                                                    \
    term->callbacks.M.param = DEFAULT(param, term);                  \
    term->callbacks.M.func  = func;                                  \
}
XCALLBACK_(settitle, SetTitle)
XCALLBACK_(seticon,  SetIcon)
XCALLBACK_(setprop,  SetProp)
#undef XCALLBACK_

static void alloc_frame(Frame *, uint16, uint16);
static void alloc_tabstops(uint8 **, uint16, uint16, uint16);
static void init_color_table(Term *);
static void init_parser(Term *);
static void update_dimensions(Term *, uint16, uint16);

Term *
term_create(uint16 histlines, uint8 tabcols)
{
    Term *term = xcalloc(1, sizeof(*term));

    term->histlines = round_pow2(DEFAULT(histlines, 1024));
    term->tabcols = DEFAULT(tabcols, 8);
    init_color_table(term);

    return term;
}

void
term_set_display(Term *term, FontSet *fonts, uint width, uint height)
{
    // Sanity checks, since we can only be half-initialized by this point
    ASSERT(term);
    ASSERT(!term->ring);
    ASSERT(term->histlines);
    ASSERT(term->tabcols);
    ASSERT(fonts);
    ASSERT(width);
    ASSERT(height);

    // Query the nominal cell resolution. This was already done by the caller to set the
    // initial window size, but we're doing it again here since we're going to start using
    // the primary font API within the terminal anyway
    fontset_get_metrics(fonts, &term->colpx, &term->rowpx, NULL, NULL);

    ASSERT(term->rowpx > 0);
    ASSERT(term->colpx > 0);

    term->fonts = fonts;
    term->cols = (int)width / term->colpx;
    term->rows = (int)height / term->rowpx;

    if (term->rows > term->histlines) {
        term->histlines = round_pow2(term->rows);
    }

    term->rings[0] = ring_create(term->histlines, term->cols, term->rows);
    term->rings[1] = ring_create(term->rows, term->cols, term->rows);
    term->ring = term->rings[0];

    alloc_frame(&term->frame, term->cols, term->rows);
    alloc_tabstops(&term->tabstops, 0, term->cols, term->tabcols);

    update_dimensions(term, term->cols, term->rows);
}

void
init_color_table(Term *term)
{
    ASSERT(term);
    static_assert(LEN(term->colors.base256) == 256, "Invalid color table size");

    memset(term->colors.base256, 0, sizeof(term->colors.base256));

    // Default standard colors (0-15)
    static const uint32 base16[16] = {
        0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xc0c0c0,
        0x808080, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff, 0xffffff
    };

    uint32 *const table = term->colors.base256;

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

    term->colors.bg = table[0];
    term->colors.fg = table[7];
}

void
term_set_background_color(Term *term, uint32 color)
{
    ASSERT(term);

    term->colors.bg = (color & 0xffffff);
}

void
term_set_foreground_color(Term *term, uint32 color)
{
    ASSERT(term);

    term->colors.fg = (color & 0xffffff);
}

void
term_set_default_colors(Term *term, uint32 bg_color, uint32 fg_color)
{
    ASSERT(term);

    term_set_background_color(term, bg_color);
    term_set_foreground_color(term, fg_color);
}

void
term_set_base16_color(Term *term, uint8 idx, uint32 color)
{
    ASSERT(term);
    ASSERT(idx < 16);

    term_set_base256_color(term, (idx & 0xf), color);
}

void
term_set_base256_color(Term *term, uint8 idx, uint32 color)
{
    ASSERT(term);

    term->colors.base256[idx] = (color & 0xffffff);
}

void
init_parser(Term *term)
{
    ASSERT(term);

    // Default cell
    term->cell.width = 1;
    term->cell.bg = term->colors.bg;
    term->cell.fg = term->colors.fg;
    term->cell.attrs = 0;

    arr_reserve(term->parser.data, 4);
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
    ring_destroy(term->rings[0]);
    ring_destroy(term->rings[1]);
    term->ring = NULL;
    pty_hangup(term->pid);
    free(term);

#if DEBUG_PRINT_INPUT
    arr_free(dbginput);
#endif
}

int
term_exec(Term *term, const char *shell, int argc, const char *const *argv)
{
    UNUSED(argc);
    UNUSED(argv);

    ASSERT(term);

    if (!term->pid) {
        init_parser(term);
        term->pid = pty_init(shell, &term->mfd, &term->sfd);
        pty_resize(term->mfd, term->cols, term->rows, term->colpx, term->rowpx);
    }

    return term->mfd;
}

int term_cols(const Term *term) { return term->cols; }
int term_rows(const Term *term) { return term->rows; }

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

size_t
term_push(Term *term, const void *data, size_t len)
{
    return pty_write(term->mfd, data, len);
}

size_t
term_pull(Term *term, uint32 msec)
{
    ASSERT(msec < 1E3);

    size_t accum = 0;
    size_t len;
    const int basetime = timer_msec(NULL);
    int timeout = msec;

    do {
        len = pty_read(term->mfd, term->input, LEN(term->input), timeout);
        accum += len;
        timeout -= timer_msec(NULL) - basetime;
    } while (term_consume(term, term->input, len) && timeout > 0);

    return accum;
}

void
term_scroll(Term *term, int delta)
{
    /*
     * (delta < 0): scroll back in history
     * (delta > 0): scroll forward in history
     */
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
static void emu_csi_vpa(Term *, const int *, int);
static void emu_csi_vpr(Term *, const int *, int);
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
    X_(CSI, VPA,      emu_csi_vpa) \
    X_(CSI, VPR,      emu_csi_vpr) \
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

#define OPCODE(esc) OP##esc
// Define opcodes for C1 and CSI escape sequences
enum {
    NOOP,
#define X_(group,name,...) OPCODE(name),
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
#define X_(group_,name_,func_) [OPCODE(name_)] = { \
    .group = #group_,  \
    .name  = #name_,   \
    .func  = func_     \
},
    HANDLER_TABLE
#undef X_
};

#undef OPCODE
#undef HANDLER_TABLE

#include <unistd.h> // for isatty()
/*
 * Quick and dirty helper function for logging human-readable trace data
 * Its primary purpose is to make unhandled/unknown escape sequences easy to spot
 */
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

/*
 * Dispatch to the standard C1 and CSI functions that are implemented internally
 */
static inline void
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

/*
 * Dispatch to a secondary OSC parser that calls external platform-dependent handlers
 */
static inline void
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
    parser->overflow = false;
    arr_clear(parser->data);
}

static inline int
parser_add_digit(struct Parser *parser, int digit)
{
    ASSERT(digit >= 0 && digit < 10);

    int *const argp = &parser->argv[parser->argi];

    if (!parser->overflow) {
        if (INT_MAX / 10 - digit > *argp) {
            *argp = *argp * 10 + digit;
        } else {
            parser->overflow = true;
            *argp = 0; // TODO(ben): Fallback to default param or abort the sequence?
            dbgprint("warning: parameter integer overflow");
        }
    }

    return *argp;
}

static inline bool
parser_next_param(struct Parser *parser)
{
    if (parser->argi + 1 >= (int)LEN(parser->argv)) {
        dbgprint("warning: ignoring excess parameter");
        return false;
    }

    parser->argv[++parser->argi] = 0;
    parser->overflow = false;

    return true;
}

/*
 * The central routine for performing actions emitted by the state machine - i.e. parsing,
 * UTF-8 validation, writing to the ring buffer, and executing control sequences
 */
void
do_action(Term *term, StateCode state, ActionCode action, uchar c)
{
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
    case ActionNone:
    case ActionIgnore:
        return;
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
        if (parser->argi == 0) {
            if (c >= '0' && c <= '9') {
                parser_add_digit(parser, c - '0');
            } else {
                // NOTE(ben): Assuming OSC sequences take a default '0' parameter
                if (c != ';') {
                    parser->argv[parser->argi] = 0;
                }
                parser_next_param(parser);
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
            parser_next_param(parser);
        } else {
            parser_add_digit(parser, c - '0');
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
            case 'e': opcode = OPVPR;     break;
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
     */
    UNUSED(argc);

    const TermCallbacks cb = term->callbacks;

    switch (argv[0]) {
    case 0:
    case 1:
        if (cb.seticon.func) {
            cb.seticon.func(cb.seticon.param, str, arr_count(str));
        }
        if (argv[0] != 0) break;
        // fallthrough
    case 2:
        if (cb.settitle.func) {
            cb.settitle.func(cb.settitle.param, str, arr_count(str));
        }
        break;
    case 3:
        if (cb.setprop.func) {
            cb.setprop.func(cb.setprop.param, str, arr_count(str));
        }
        break;
    }

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
emu_csi_vpa(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    set_cursor_row(term, MAX(argv[1], 1) - 1);
}

void
emu_csi_vpr(Term *term, const int *argv, int argc)
{
    UNUSED(argc);
    move_cursor_rows(term, MAX(argv[1], 1) - 1);
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
        term_push(term, (uchar *)str, len);
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

