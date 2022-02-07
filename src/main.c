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
#include "fonts.h"
#include "term.h"

#define MAX_CFG_COLORS (2+16)
#define MIN_HISTLINES 128
#define MAX_HISTLINES 4096

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

static WinFuncResize on_event_resize;
static WinFuncKeyPress on_event_keypress;
static TermFuncSetTitle on_osc_set_title;
static TermFuncSetIcon on_osc_set_icon;

static void setup(App *app, const AppPrefs *prefs);
static void setup_preferences(App *app, const AppPrefs *restrict prefs);
static void setup_fonts(App *app, float dpi);
static void setup_window(App *app);
static void setup_display(App *app);
static void setup_terminal(App *app);
static int run(App *app);

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
        char *errp; // for strtol()

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
    // DEPENDS:
    // app->prefs.font
    // app->prefs.fontpath

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
    // DEPENDS:
    // app->prefs.cols
    // app->prefs.rows
    // app->prefs.border
    // app->prefs.wm_title
    // app->prefs.wm_name
    // app->prefs.wm_class
    // app->win
    // app->cell_width
    // app->cell_height

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

    window_callback_resize(app->win, app, &on_event_resize);
    window_callback_keypress(app->win, app, &on_event_keypress);
}

void
setup_display(App *app)
{
    // DEPENDS:
    // app->win

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
    // DEPENDS:
    // app->prefs.histlines
    // app->prefs.tabcols
    // app->prefs.colors
    // app->fontset
    // app->width
    // app->height

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

    static const int rate = 16;
    bool hangup = false;
    bool draw = true;

    while (!result && !hangup && window_is_online(app->win)) {
        if (draw) {
            term_draw(term);
            window_update(app->win);
        }

        draw = false;

        errno = 0;
        const int status = poll(pollset, LEN(pollset), rate);

        if (status < 0) {
            printerr("ERROR poll: %s\n", strerror(errno));
            result = DEFAULT(errno, EXIT_FAILURE); // paranoia
        } else if (!status) {
            draw = !!window_poll_events(app->win);
        } else if ((pollset[0].revents|pollset[1].revents) & POLLHUP) {
            hangup = true;
        } else {
            if (pollset[0].revents & POLLIN) {
                draw = !!term_pull(term, 0);
            }
            if (pollset[1].revents & POLLIN) {
                draw = !!window_poll_events(app->win);
            }
        }
    }

    return result;
}

void
on_event_resize(void *param, int width, int height)
{
    App *const app = param;

    ASSERT(width > app->cell_width);
    ASSERT(height > app->cell_height);

    if (width == app->width && height == app->height) {
        return;
    }

    term_resize(app->term, width, height);

    app->width  = width;
    app->height = height;
}

void
on_event_keypress(void *param, uint key, uint mods, const uchar *text, int len)
{
    App *const app = param;

    switch (mods & ~KEYMOD_NUMLK) {
    case KEYMOD_SHIFT:
        switch (key) {
        case KeyPgUp:
            term_scroll(app->term, -term_rows(app->term));
            return;
        case KeyPgDown:
            term_scroll(app->term, +term_rows(app->term));
            return;
        }
        break;
    case KEYMOD_ALT:
        switch (key) {
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

    if (term_push_input(app->term, key, mods, text, len)) {
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

