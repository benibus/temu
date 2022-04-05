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

#include <errno.h>
#include <poll.h>

#include "utils.h"
#include "window.h"
#include "events.h"
#include "fonts.h"
#include "term.h"
#include "color.h"
#include "options.h"
#include "app.h"

// Option limits
enum {
    MIN_BORDER    = (0),      MAX_BORDER    = (INT16_MAX / 2),
    MIN_HISTLINES = (1 << 8), MAX_HISTLINES = (1 << 15),
    MIN_COLS      = (1),      MAX_COLS      = (1024),
    MIN_ROWS      = (1),      MAX_ROWS      = (MAX_COLS * 2),
    MIN_TABCOLS   = (1),      MAX_TABCOLS   = (32),
};

static const struct {
    Options opts;
    const char *colors[NUM_COLORS];
} defaults = {
    .opts = {
        .wm_class  = "Temu",
        .wm_name   = "temu",
        .wm_title  = "temu",
        .geometry  = NULL,
        .font      = NULL,
        .fontpath  = NULL,
        .shell     = NULL,
        .cols      = 140,
        .rows      = 40,
        .tabcols   = 8,
        .border    = 0,
        .histlines = 128,
    },
    .colors = {
        [BLACK]    = "#34373c",
        [RED]      = "#b25449",
        [GREEN]    = "#698754",
        [YELLOW]   = "#d88e61",
        [BLUE]     = "#547991",
        [MAGENTA]  = "#887190",
        [CYAN]     = "#578d85",
        [WHITE]    = "#8e929b",
        [LBLACK]   = "#56575f",
        [LRED]     = "#cb695c",
        [LGREEN]   = "#749c61",
        [LYELLOW]  = "#e3ac72",
        [LBLUE]    = "#6494af",
        [LMAGENTA] = "#a085a6",
        [LCYAN]    = "#6aa9a5",
        [LWHITE]   = "#c5c8c6",

        [BACKGROUND] = "#1b1c1e",
        [FOREGROUND] = "#a5a8a6",
    },
};

struct App {
    Options opts;
    Win *win;
    Term *term;
    FontSet *fontset;
    float dpi;   // Display DPI
    int width;   // Window width
    int height;  // Window height
    int cwidth;  // Cell/Font width
    int cheight; // Cell/Font height
    int ascent;  // Font ascender
    int descent; // Font descender
    int argc;
    const char **argv;
    Palette palette;
};

static App app_;

static void merge_options(Options *restrict dst, const Options *restrict src);
static void setup(App *app);
static void setup_fonts(App *app);
static void setup_window(App *app);
static void setup_terminal(App *app);
static int run(App *app);
static int run_frame(App *app, struct pollfd *pollset);
static bool run_updates(App *app, struct pollfd *pollset, int timeout, int *r_error);

static WinEventHandler on_event;
static void on_resize_event(App *app, const WinGeomEvent *event);
static void on_keypress_event(App *app, const WinKeyEvent *event);

int
app_main(const Options *opts)
{
    App *const app = &app_;

    // Ensure valid options
    merge_options(&app->opts, opts);

    setup(app);

    int result = run(app);

    term_destroy(app->term);
    fontset_destroy(app->fontset);
    window_destroy(app->win);

    return result;
}

void
merge_options(Options *restrict dst, const Options *restrict src)
{
    *dst = defaults.opts;

#define MERGE_NONNULL(M)     (dst->M = DEFAULT(src->M, dst->M))
#define MERGE_INRANGE(M,l,h) (dst->M = (src->M >= (l) && src->M <= (h)) ? src->M : dst->M)
    MERGE_NONNULL(wm_class);
    MERGE_NONNULL(wm_name);
    MERGE_NONNULL(wm_title);
    MERGE_NONNULL(geometry);
    MERGE_NONNULL(shell);
    MERGE_NONNULL(font);
    MERGE_NONNULL(fontpath);
    MERGE_INRANGE(border, MIN_BORDER, MAX_BORDER);
    MERGE_INRANGE(histlines, MIN_HISTLINES, MAX_HISTLINES);
    MERGE_INRANGE(cols, MIN_COLS, MAX_COLS);
    MERGE_INRANGE(rows, MIN_ROWS, MAX_ROWS);
#undef MERGE_NONNULL
#undef MERGE_INRANGE
}

void
setup(App *app)
{
    // Creates a "window", but actually just opens the server connection and gives us a
    // blank handle for later. The terminal size depends on the the window size, which we
    // can't deduce until the window is visible - but the window size we *ask for* depends
    // on the font size, which depends on the DPI, which depends on the window server
    // being online... which is why we do this first.
    app->win = window_create();

    if (!app->win) {
        err_printf("Failed to initialize window server\n");
        exit(1);
    }

    app->dpi = window_get_dpi(app->win);

    // Set palette default values
    palette_init(&app->palette, false);
    // Translate color strings to integer palette (Requires a server connection)
    for (uint i = 0; i < LEN(defaults.colors); i++) {
        const char *const str = defaults.colors[i];
        if (strempty(str)) {
            continue;
        }
        if (!window_query_color(app->win, str, &app->palette.table[i])) {
            err_printf("Failed to parse color string: \"%s\"\n", str);
            exit(1);
        }
    }

    // Everything else
    setup_fonts(app);
    setup_window(app);
    setup_terminal(app);
}

void
setup_fonts(App *app)
{
    if (!fontmgr_init(app->dpi)) {
        err_printf("Failed to initialize font manager\n");
        exit(1);
    }

    char *fontpath = NULL;
    if (app->opts.fontpath) {
        fontpath = realpath(app->opts.fontpath, NULL);
        if (fontpath) {
            dbg_printf("Resolved file path: %s -> %s\n", app->opts.fontpath, fontpath);
            app->fontset = fontmgr_create_fontset_from_file(fontpath);
            FREE(fontpath);
        } else {
            dbg_printf("Failed to resolve file path: %s\n", app->opts.fontpath);
        }
    }
    if (!app->fontset) {
        app->fontset = fontmgr_create_fontset(app->opts.font);
        if (!app->fontset) {
            err_printf("Failed to open fallback fonts. aborting...\n");
            exit(1);
        }
    }

    fontset_get_metrics(app->fontset,
                        &app->cwidth,
                        &app->cheight,
                        &app->ascent,
                        &app->descent);

    dbg_printf("Fonts opened: w=%d h=%d a=%d d=%d\n",
               app->cwidth,
               app->cheight,
               app->ascent,
               app->descent);

    if (!fontset_init(app->fontset)) {
        err_printf("Failed to initialize font cache\n");
        exit(1);
    }

    dbg_printf("Font cache initialized\n");
}

void
setup_window(App *app)
{
    ASSERT(app);
    ASSERT(app->win);

    WinConfig cfg = {
        .wm_name    = app->opts.wm_name,
        .wm_class   = app->opts.wm_class,
        .wm_title   = app->opts.wm_title,
        .width      = app->opts.cols * app->cwidth,
        .height     = app->opts.rows * app->cheight,
        .inc_width  = app->cwidth,
        .inc_height = app->cheight,
        .min_width  = app->cwidth + 2 * app->opts.border,
        .min_height = app->cheight + 2 * app->opts.border,
    };

    if (!window_configure(app->win, cfg)) {
        err_printf("Failed to configure window\n");
        exit(1);
    }

    if (!window_open(app->win, &app->width, &app->height)) {
        err_printf("Failed to open window\n");
        exit(1);
    }

    if ((app->width - 2 * app->opts.border) / app->cwidth <= 0 ||
        (app->height - 2 * app->opts.border) / app->cheight <= 0)
    {
        err_printf("Insufficient initial window size: w=%d, h=%d\n",
                   app->width,
                   app->height);
        exit(1);
    }

    dbg_printf("Window opened: w=%d h=%d cw=%d ch=%d b=%d\n",
               app->width,
               app->height,
               app->cwidth,
               app->cheight,
               app->opts.border);
}

void
setup_terminal(App *app)
{
    app->term = term_create(app);

    if (!app->term) {
        err_printf("Failed to create terminal\n");
        exit(1);
    }
}

int
run(App *app)
{
    Term *term = app->term;

    if (!window_online(app->win)) {
        err_printf("Window is not online\n");
        return 1;
    }

    int result = 0;

    const int srvfd = window_get_fileno(app->win);
    ASSERT(srvfd);
    const int ptyfd = term_exec(term, app->opts.shell, app->argc, app->argv);

    if (ptyfd) {
        dbg_printf("Terminal online: fd=%d\n", ptyfd);
    } else {
        err_printf("Failed to start terminal\n");
        result = 1;
    }

    struct pollfd pollset[2] = {
        { .fd = ptyfd, .events = POLLIN, .revents = 0 },
        { .fd = srvfd, .events = POLLIN, .revents = 0 }
    };

    while (!result && window_online(app->win)) {
        result = run_frame(app, pollset);
    }

    return (result && result != ECHILD) ? result : 0;
}

bool
run_updates(App *app, struct pollfd *pollset, int timeout, int *r_error)
{
    ASSERT(r_error);
    *r_error = 0;

    int nbytes = 0;
    int nevents = 0;
    errno = 0;

    const int res = poll(pollset, 2, timeout);
    if (res < 0) {
        err_printf("poll: %s\n", strerror(errno));
        *r_error = errno;
    } else if ((pollset[0].revents|pollset[1].revents) & POLLHUP) {
        *r_error = ECHILD;
    } else {
        nevents = window_pump_events(app->win, on_event, app);
        if (res && pollset[0].revents & POLLIN) {
            nbytes = term_pull(app->term);
        }
    }

    return (nevents || nbytes);
}

int
run_frame(App *app, struct pollfd *pollset)
{
    const int t0 = timer_msec(NULL);
    int t1 = t0;

    bool need_draw = false;
    int error = 0;

    // Slightly faster than 60 hz. The refresh rate should become configurable at some point
    const int min_time = 1e3 / (60 * 1.15);
    const int max_time = min_time * 2;

    for (int limit = min_time;;) {
        const bool res = run_updates(app, pollset, 2, &error);
        t1 = timer_msec(NULL);
        if (error) {
            goto done_frame;
        }
        need_draw |= res;
        // We try to extend this frame into the next one if we receive updates in the
        // last polling interval. It usually means there's more input coming, but the
        // PTY hasn't given it to us yet
        if ((t1 - t0 >= limit &&
             (!res || (limit += min_time) > max_time)))
        {
            break;
        }
    }

    if (need_draw) {
        term_draw(app->term);
        window_refresh(app->win);
    }

done_frame:
    return error;
}

void
on_event(void *arg, const WinEvent *event)
{
    App *app = arg;

    if (event->info.error) {
        return;
    }

    switch (event->tag) {
    case EVENT_RESIZE:
        on_resize_event(app, &event->as_geom);
        break;
    case EVENT_KEYPRESS:
        on_keypress_event(app, &event->as_key);
        break;
    default:
        break;
    }
}

void
on_resize_event(App *app, const WinGeomEvent *event)
{
    if (event->width == app->width && event->height == app->height) {
        return;
    }

    term_resize(app->term,
                imax(0, event->width - 2 * app->opts.border),
                imax(0, event->height - 2 * app->opts.border));

    app->width  = event->width;
    app->height = event->height;
}

void
on_keypress_event(App *app, const WinKeyEvent *event)
{
    switch (event->mods & ~KEYMOD_NUMLK) {
    case KEYMOD_SHIFT:
        switch (event->key) {
        case KeyPgUp:
            term_scroll(app->term, -term_rows(app->term));
            return;
        case KeyPgDown:
            term_scroll(app->term, +term_rows(app->term));
            return;
        }
        break;
    case KEYMOD_ALT:
        switch (event->key) {
        case 'k':
            term_scroll(app->term, -1);
            return;
        case 'j':
            term_scroll(app->term, +1);
            return;
        case KeyF9:
            term_print_history(app->term);
            return;
        case KeyF10:
            term_toggle_trace(app->term);
            return;
        }
        break;
    default:
        break;
    }

    if (term_push_input(app->term, event->key, event->mods, event->data, event->len)) {
        term_reset_scroll(app->term);
    }
}

int app_width(const App *app) { return (app) ? app->width : 0; }
int app_height(const App *app) { return (app) ? app->height : 0; }
void *app_fonts(const App *app) { return (app) ? app->fontset : NULL; }
int app_font_width(const App *app) { return (app) ? app->cwidth : 0; }
int app_font_height(const App *app) { return (app) ? app->cheight : 0; }
int app_border(const App *app) { return (app) ? app->opts.border : 0; }
int app_histlines(const App *app) { return (app) ? app->opts.histlines : 0; }
int app_tabcols(const App *app) { return (app) ? app->opts.tabcols : 0; }
Palette *app_palette(App *app) { return (app) ? &app->palette : NULL; }

void
app_get_dimensions(const App *app, int *r_width, int *r_height, int *r_border)
{
    if (app) {
        SETPTR(r_width,  app_width(app));
        SETPTR(r_height, app_height(app));
        SETPTR(r_border, app_border(app));
    }
}

void
app_get_font_metrics(const App *app,
                     int *r_width,
                     int *r_height,
                     int *r_ascent,
                     int *r_descent)
{
    if (app) {
        SETPTR(r_width, app_font_width(app));
        SETPTR(r_height, app_font_height(app));
        SETPTR(r_ascent, (app) ? app->ascent : 0);
        SETPTR(r_descent, (app) ? app->descent : 0);
    }
}

void
app_set_properties(App *app, uint8 props, const char *str, size_t len)
{
    app = DEFAULT(app, &app_);

    if (len >= INT_MAX) return;

    dbg_printf("props=0x%01x str=\"%.*s\"\n", props, (int)len, str);

    if (strempty(str) || !len) {
        str = app->opts.wm_title;
        len = strlen(str);
    }
    if (props & APPPROP_ICON) {
        window_set_icon(app->win, str, len);
    }
    if (props & APPPROP_TITLE) {
        window_set_title(app->win, str, len);
    }
}

