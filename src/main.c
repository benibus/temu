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
#include "terminal.h"
#include "platform.h"
#include "fonts.h"
#include "renderer.h"

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
    uint padding;
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
    int cols;
    int rows;
    int padpx;
    int colpx;
    int rowpx;
} App;

static App app_;

static WinFuncResize on_event_resize;
static WinFuncKeyPress on_event_keypress;
static void on_osc_set_title(void *param, const char *str, size_t len);
static void on_osc_set_icon(void *param, const char *str, size_t len);
static int run(App *app);

int
main(int argc, char **argv)
{
    static_assert(FontStyleRegular == ATTR_NONE, "Bitmask mismatch.");
    static_assert(FontStyleBold == ATTR_BOLD, "Bitmask mismatch.");
    static_assert(FontStyleItalic == ATTR_ITALIC, "Bitmask mismatch.");
    static_assert(FontStyleBoldItalic == (ATTR_BOLD|ATTR_ITALIC), "Bitmask mismatch.");

    AppPrefs prefs = { 0 };

    for (int opt; (opt = getopt(argc, argv, "T:N:C:S:F:f:c:r:p:m:s:")) != -1; ) {
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
        case 'p':
            arg.u = strtoul(optarg, &errp, 10);
            if (!*errp && arg.u < UINT_MAX) {
                prefs.padding = arg.u;
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

    app->prefs = default_prefs;
#define MERGEOPT(opt) (app->prefs.opt = DEFAULT(prefs.opt, app->prefs.opt))
    MERGEOPT(cols);
    MERGEOPT(rows);
    MERGEOPT(padding);

    MERGEOPT(wm_class);
    MERGEOPT(wm_name);
    MERGEOPT(wm_title);
    MERGEOPT(geometry);
    MERGEOPT(shell);
    MERGEOPT(font);
    MERGEOPT(fontpath);

    if (prefs.histlines >= MIN_HISTLINES && prefs.histlines <= MAX_HISTLINES) {
        app->prefs.histlines = prefs.histlines;
    }
#undef MERGEOPT

    app->win = window_create();

    if (app->win) {
        dbgprint("Window server initialized");
    } else {
        dbgprint("Failed to initialize window server");
        return EXIT_FAILURE;
    }

    if (fontmgr_init(window_get_dpi(app->win))) {
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
                return EXIT_FAILURE;
            }
        }

        dbgprint("Fonts opened");
    } else {
        dbgprint("Failed to initialize font manager");
        return EXIT_FAILURE;
    }

    fontset_get_metrics(app->fontset, &app->colpx, &app->rowpx, NULL, NULL);

    if (fontset_init(app->fontset)) {
        dbgprint("Font cache initialized");
    } else {
        dbgprint("Failed to initialize font cache");
        return EXIT_FAILURE;
    }

    app->padpx = app->prefs.padding;
    if (!window_set_size(app->win, app->prefs.cols * app->colpx + 2 * app->padpx,
                                   app->prefs.rows * app->rowpx + 2 * app->padpx))
    {
        return EXIT_FAILURE;
    }
    window_set_size_hints(app->win, app->colpx + 2 * app->padpx,
                                    app->rowpx + 2 * app->padpx,
                                    app->colpx,
                                    app->rowpx);

    window_set_class_hints(app->win, app->prefs.wm_name, app->prefs.wm_class);
    window_set_title(app->win, app->prefs.wm_title, strlen(app->prefs.wm_title));

    window_callback_resize(app->win, app, &on_event_resize);
    window_callback_keypress(app->win, app, &on_event_keypress);

    if (renderer_init()) {
        dbgprint("Renderer initialized");
    } else {
        dbgprint("Failed to initialize renderer");
        return EXIT_FAILURE;
    }

    TermConfig termcfg = { 0 };

    for (uint i = 0; i < LEN(app->prefs.colors); i++) {
        ASSERT(app->prefs.colors[i]);

        uint32 *dst;

        switch (i) {
        case 0:  dst = &termcfg.color_bg;  break;
        case 1:  dst = &termcfg.color_fg;  break;
        default: dst = &termcfg.colors[i-2]; break;
        }
        if (!window_query_color(app->win, app->prefs.colors[i], dst)) {
            dbgprint("Failed to parse RGB string: %s", app->prefs.colors[i]);
            exit(EXIT_FAILURE);
        }
    }
    dbgprint("User colors parsed");

    if (window_init(app->win)) {
        window_get_size(app->win, &app->width, &app->height);
    } else {
        dbgprint("Failed to display window");
        return EXIT_FAILURE;
    }

    app->cols = (app->width - 2 * app->padpx) / app->colpx;
    app->rows = (app->height - 2 * app->padpx) / app->rowpx;

    dbgprint(
        "Window displayed\n"
        "    width   = %d\n"
        "    height  = %d\n"
        "    cols    = %dx%d\n"
        "    rows    = %dx%d\n"
        "    padding = %d",
        app->width,
        app->height,
        app->cols, app->colpx,
        app->rows, app->rowpx,
        app->padpx
    );

    ASSERT(app->cols == (int)app->prefs.cols);
    ASSERT(app->rows == (int)app->prefs.rows);

    termcfg.cols      = app->cols;
    termcfg.rows      = app->rows;
    termcfg.colsize   = app->colpx;
    termcfg.rowsize   = app->rowpx;
    termcfg.shell     = app->prefs.shell;
    termcfg.histlines = MAX(app->prefs.histlines, termcfg.rows);
    termcfg.tabcols   = DEFAULT(app->prefs.tabcols, 8);

    termcfg.param = app;
    termcfg.handlers.set_title = on_osc_set_title;
    termcfg.handlers.set_icon  = on_osc_set_icon;

    dbgprint(
        "Creating virtual terminal...\n"
        "    shell     = %s\n"
        "    histlines = %u\n"
        "    tabspaces = %u",
        DEFAULT(termcfg.shell, "$SHELL"),
        termcfg.histlines,
        termcfg.tabcols
    );

    if ((app->term = term_create(&termcfg))) {
        dbgprint("Virtual terminal created");
    } else {
        dbgprint("Failed to create virtual terminal");
        return EXIT_FAILURE;
    }

    renderer_set_dimensions(
        app->width, app->height,
        app->cols, app->rows,
        app->colpx, app->rowpx,
        app->padpx
    );
    dbgprint("Renderer online");

    dbgprint("Running temu...");

    int result = run(app);

    term_destroy(app->term);
    fontset_destroy(app->fontset);
    renderer_shutdown();
    platform_shutdown();

    return result;
}

int
run(App *app)
{
    Term *term = app->term;

    window_make_current(app->win);

    if (!window_online(app->win)) {
        return 0;
    }

    int result = 0;
    const int srvfd = window_get_fileno(app->win);
    const int ptyfd = term_exec(term, app->prefs.shell);

    if (ptyfd && srvfd) {
        dbgprint("Virtual terminal online. FD = %d", ptyfd);
    } else {
        dbgprint("Failed to start virtual terminal");
        result = EXIT_FAILURE;
        goto quit;
    }

    do {
        renderer_draw_frame(term_generate_frame(term), app->fontset);
        window_update(app->win);
    } while (window_poll_events(app->win));

    struct pollfd pollset[] = {
        { .fd = ptyfd, .events = POLLIN, .revents = 0 },
        { .fd = srvfd, .events = POLLIN, .revents = 0 }
    };

    static_assert(LEN(pollset) == 2, "Unexpected pollset size");

    // Target polling rate
    static const uint32 msec = 16;

    while (window_online(app->win)) {
        const uint32 basetime = timer_msec(NULL);
        bool draw = true;

        switch (poll(pollset, LEN(pollset), msec)) {
        case -1:
            if (errno) {
                perror("poll()");
            }
            result = errno;
            goto quit;
        case 0:
            if (!window_poll_events(app->win)) {
                draw = false;
            }
            break;
        case LEN(pollset) - 1:
            if (pollset[0].revents & POLLIN) {
#if 1
                const int timeout = 0;
#else
                const int timeout = msec - (timer_msec(NULL) - basetime);
#endif
                term_pull(term, MAX(timeout, 0));
            } else {
                if (!window_poll_events(app->win)) {
                    draw = false;
                }
            }
            break;
        case LEN(pollset):
            break;
        }

        if (draw && window_online(app->win)) {
            renderer_draw_frame(term_generate_frame(app->term), app->fontset);
            window_update(app->win);
        }
    }

quit:
    window_destroy(app->win);

    return result;
}

void
on_event_resize(void *param, int width, int height)
{
    App *const app = param;

    if (width == app->width && height == app->height) {
        return;
    }

    const int cols = (width - 2 * app->padpx) / app->colpx;
    const int rows = (height - 2 * app->padpx) / app->rowpx;

    term_resize(app->term, cols, rows);

    renderer_set_dimensions(
        width, height,
        cols, rows,
        app->colpx, app->rowpx,
        app->padpx
    );

    app->width  = width;
    app->height = height;
    app->cols   = cols;
    app->rows   = rows;
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

