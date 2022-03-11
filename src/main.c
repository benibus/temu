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
#include <locale.h>
#include <poll.h>
#include <unistd.h>

#include "utils.h"
#include "platform.h"
#include "events.h"
#include "fonts.h"
#include "term.h"

enum {
    MAX_CFG_COLORS = 2 + 16,
    MIN_HISTLINES  = 128,
    MAX_HISTLINES  = 4096,
};

typedef struct {
    char *wm_class;
    char *wm_name;
    char *wm_title;
    char *geometry;
    char *font;
    char *fontpath;
    char *colors[MAX_CFG_COLORS];
    char *shell;
    uint cols;
    uint rows;
    uint border;
    uint tabcols;
    uint histlines;
    uint refresh;
} AppPrefs;
// NOTE: must include *after* definitions
#include "config.h"

typedef struct {
    AppPrefs prefs;
    Win *win;
    Term *term;
    FontSet *fontset;
    int width;
    int height;
    int cell_width;
    int cell_height;
    int argc;
    const char **argv;
} App;

static App app_;

static TermFuncSetTitle on_osc_set_title;
static TermFuncSetIcon on_osc_set_icon;

static WinEventHandler on_event;
static void on_resize_event(App *app, const WinGeomEvent *event);
static void on_keypress_event(App *app, const WinKeyEvent *event);

static void setup(App *app, const AppPrefs *prefs);
static void setup_preferences(App *app, const AppPrefs *restrict prefs);
static void setup_fonts(App *app, float dpi);
static void setup_window(App *app);
static void setup_display(App *app);
static void setup_terminal(App *app);
static int run(App *app);
static int run_frame(App *app, struct pollfd *pollset);
static bool run_updates(App *app, struct pollfd *pollset, int timeout, int *r_error);

int
main(int argc, char **argv)
{
    AppPrefs prefs = { 0 };

    for (int opt; (opt = getopt(argc, argv, "T:N:C:S:F:f:c:r:b:m:s:")) != -1; ) {
        union {
            char *s;
            long i;
            ulong u;
            double f;
        } arg;
        char *errp;

        switch (opt) {
        case 'T': prefs.wm_title = (!strempty(optarg)) ? optarg : NULL; break;
        case 'N': prefs.wm_name  = (!strempty(optarg)) ? optarg : NULL; break;
        case 'C': prefs.wm_class = (!strempty(optarg)) ? optarg : NULL; break;
        case 'S': prefs.shell    = (!strempty(optarg)) ? optarg : NULL; break;
        case 'f': prefs.font     = (!strempty(optarg)) ? optarg : NULL; break;
        case 'F': prefs.fontpath = (!strempty(optarg)) ? optarg : NULL; break;
        case 'c':
            arg.u = strtoul(optarg, &errp, 10);
            if (!*errp && arg.u < UINT_MAX) {
                prefs.cols = arg.u;
            }
            break;
        case 'r':
            arg.u = strtol(optarg, &errp, 10);
            if (!*errp && arg.u < UINT_MAX) {
                prefs.rows = arg.u;
            }
            break;
        case 'b':
            arg.u = strtoul(optarg, &errp, 10);
            if (!*errp && arg.u < UINT_MAX) {
                prefs.border = arg.u;
            }
            break;
        case 'm':
            arg.u = strtoul(optarg, &errp, 10);
            if (!*errp && arg.u < UINT_MAX) {
                prefs.histlines = arg.u;
            }
            break;
        case '?':
        case ':':
            goto error_invalid;
            break;
        }

        continue;
error_invalid:
        exit(1);
    }

    App *const app = &app_;

    setup(app, &prefs);

    dbgprint("Running temu...");
    int result = run(app);

    term_destroy(app->term);
    fontset_destroy(app->fontset);
    window_destroy(app->win);

    return result;
}

void
setup(App *app, const AppPrefs *prefs)
{
    setup_preferences(app, prefs);

    app->win = window_create();

    if (!app->win) {
        dbgprint("Failed to initialize window server");
        exit(EXIT_FAILURE);
    }

    setup_fonts(app, window_get_dpi(app->win));
    setup_window(app);
    setup_display(app);
    setup_terminal(app);
}

void
setup_preferences(App *app, const AppPrefs *restrict prefs)
{
    app->prefs = default_prefs;

#define SETDEFAULT(opt) (app->prefs.opt = DEFAULT(prefs->opt, app->prefs.opt))
    SETDEFAULT(cols);
    SETDEFAULT(rows);
    SETDEFAULT(border);
    SETDEFAULT(wm_class);
    SETDEFAULT(wm_name);
    SETDEFAULT(wm_title);
    SETDEFAULT(geometry);
    SETDEFAULT(shell);
    SETDEFAULT(font);
    SETDEFAULT(fontpath);
#undef SETDEFAULT
    if (prefs->histlines >= MIN_HISTLINES && prefs->histlines <= MAX_HISTLINES) {
        app->prefs.histlines = prefs->histlines;
    }
}

void
setup_fonts(App *app, float dpi)
{
    if (!fontmgr_init(dpi)) {
        dbgprint("Failed to initialize font manager");
        exit(EXIT_FAILURE);
    }

    char *fontpath = NULL;
    if (app->prefs.fontpath) {
        fontpath = realpath(app->prefs.fontpath, NULL);
        if (fontpath) {
            dbgprint("Resolved file path: %s -> %s", app->prefs.fontpath, fontpath);
            app->fontset = fontmgr_create_fontset_from_file(fontpath);
            FREE(fontpath);
        } else {
            dbgprint("Failed to resolve file path: %s", app->prefs.fontpath);
        }
    }
    if (!app->fontset) {
        app->fontset = fontmgr_create_fontset(app->prefs.font);
        if (!app->fontset) {
            dbgprint("Failed to open fallback fonts. aborting...");
            exit(EXIT_FAILURE);
        }
    }

    dbgprint("Fonts opened");
    fontset_get_metrics(app->fontset, &app->cell_width, &app->cell_height, NULL, NULL);

    if (!fontset_init(app->fontset)) {
        dbgprint("Failed to initialize font cache");
        exit(EXIT_FAILURE);
    }

    dbgprint("Font cache initialized");
}

void
setup_window(App *app)
{
    ASSERT(app);
    ASSERT(app->win);

    const int width  = app->prefs.cols * app->cell_width;
    const int height = app->prefs.rows * app->cell_height;

    window_set_class_hints(app->win, app->prefs.wm_name, app->prefs.wm_class);
    window_set_title(app->win, app->prefs.wm_title, strlen(app->prefs.wm_title));

    window_set_size_hints(app->win, app->cell_width, app->cell_height, app->prefs.border);
    if (!window_resize(app->win, width, height)) {
        exit(EXIT_FAILURE);
    }
}

void
setup_display(App *app)
{
    if (!window_init(app->win)) {
        dbgprint("Failed to display window");
        exit(EXIT_FAILURE);
    }

    app->width = window_width(app->win);
    app->height = window_height(app->win);

    dbgprint(
        "Window displayed\n"
        "    width       = %d\n"
        "    height      = %d\n"
        "    cell_width  = %d\n"
        "    cell_height = %d\n",
        app->width,
        app->height,
        app->cell_width,
        app->cell_height
    );
}

void
setup_terminal(App *app)
{
    Term *const term = term_create(app->prefs.histlines, app->prefs.tabcols);

    term_set_display(term, app->fontset, app->width, app->height);

    for (uint i = 0; i < LEN(app->prefs.colors); i++) {
        ASSERT(app->prefs.colors[i]);
        uint32 color;

        if (!window_query_color(app->win, app->prefs.colors[i], &color)) {
            dbgprint("Failed to parse RGB string: %s", app->prefs.colors[i]);
            exit(EXIT_FAILURE);
        }

        switch (i) {
        case 0:  term_set_background_color(term, color);    break;
        case 1:  term_set_foreground_color(term, color);    break;
        default: term_set_base16_color(term, i - 2, color); break;
        }
    }

    term_callback_settitle(term, app, &on_osc_set_title);
    term_callback_seticon(term, app, &on_osc_set_icon);

    app->term = term;
}

int
run(App *app)
{
    Term *term = app->term;

    window_make_current(app->win);

    if (!window_is_online(app->win)) {
        dbgprint("Window is not online");
        return EXIT_FAILURE;
    }

    int result = 0;

    const int srvfd = window_get_fileno(app->win);
    ASSERT(srvfd);
    const int ptyfd = term_exec(term, app->prefs.shell, app->argc, app->argv);

    if (ptyfd) {
        dbgprint("Terminal online. FD = %d", ptyfd);
    } else {
        dbgprint("Failed to start terminal");
        result = EXIT_FAILURE;
    }

    struct pollfd pollset[2] = {
        { .fd = ptyfd, .events = POLLIN, .revents = 0 },
        { .fd = srvfd, .events = POLLIN, .revents = 0 }
    };

    while (!result && window_is_online(app->win)) {
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
        printerr("ERROR poll: %s\n", strerror(errno));
        *r_error = DEFAULT(errno, EXIT_FAILURE); // paranoia
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
        window_update(app->win);
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
    }
}

void
on_resize_event(App *app, const WinGeomEvent *event)
{
    ASSERT(event->width > app->cell_width);
    ASSERT(event->height > app->cell_height);

    if (event->width == app->width && event->height == app->height) {
        return;
    }

    term_resize(app->term, event->width, event->height);

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
        }
        break;
    default:
        break;
    }

    if (term_push_input(app->term, event->key, event->mods, event->data, event->len)) {
        term_reset_scroll(app->term);
    }
}

void
on_osc_set_title(void *param, const char *str, size_t len)
{
    const App *app = param;
    ASSERT(app == &app_);

    if (str && len) {
        window_set_title(app->win, str, len);
    } else {
        window_set_title(app->win, app->prefs.wm_title, strlen(app->prefs.wm_title));
    }
}

void
on_osc_set_icon(void *param, const char *str, size_t len)
{
    const App *app = param;
    ASSERT(app == &app_);

    if (str && len) {
        window_set_icon(app->win, str, len);
    } else {
        window_set_icon(app->win, app->prefs.wm_title, strlen(app->prefs.wm_title));
    }
}

