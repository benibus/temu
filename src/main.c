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

typedef struct Config {
    char *wm_class;
    char *wm_instance;
    char *wm_title;
    char *geometry;
    char *font;
    char *fontfile;
    char *colors[MAX_CFG_COLORS];
    char *shell;
    uint tablen;
    uint border_px;
    uint histsize;
    uint columns, rows;
    struct { int x, y; } position;
} Config;
// NOTE: must include *after* definitions
#include "config.h"

typedef struct {
    Win *win;
    Term *term;
    FontSet *fontset;
    int width;
    int height;
    int borderpx;
    int cols;
    int rows;
    int colpx;
    int rowpx;
} App;

static App app_;

static void event_resize(void *param, int width, int height);
static void event_key_press(void *param, uint key, uint mod, const byte *data, int len);
static void handler_set_title(void *param, const char *str, size_t len);
static void handler_set_icon(void *param, const char *str, size_t len);
static int run(App *app);

int
main(int argc, char **argv)
{
    static_assert(FontStyleRegular == ATTR_NONE, "Bitmask mismatch.");
    static_assert(FontStyleBold == ATTR_BOLD, "Bitmask mismatch.");
    static_assert(FontStyleItalic == ATTR_ITALIC, "Bitmask mismatch.");
    static_assert(FontStyleBoldItalic == (ATTR_BOLD|ATTR_ITALIC), "Bitmask mismatch.");

    for (int opt; (opt = getopt(argc, argv, "T:N:C:S:F:f:c:r:x:y:b:m:s:")) != -1; ) {
        union {
            char *s;
            long n;
            double f;
        } arg;
        char *errp; // for strtol()

        switch (opt) {
        case 'T': config.wm_title = optarg; break;
        case 'N': config.wm_instance = optarg; break;
        case 'C': config.wm_class = optarg; break;
        case 'S': config.shell = optarg; break;
        case 'f': config.font = optarg; break;
        case 'F': config.fontfile = optarg; break;
        case 'c':
            arg.n = strtol(optarg, &errp, 10);
            if (!*errp) {
                if (arg.n > 0) {
                    config.columns = arg.n;
                }
            }
            break;
        case 'r':
            arg.n = strtol(optarg, &errp, 10);
            if (!*errp) {
                if (arg.n > 0) {
                    config.rows = arg.n;
                }
            }
            break;
        case 'x':
            arg.n = strtol(optarg, &errp, 10);
            if (!*errp) config.position.x = arg.n;
            break;
        case 'y':
            arg.n = strtol(optarg, &errp, 10);
            if (!*errp) config.position.y = arg.n;
            break;
        case 'b':
            arg.n = strtol(optarg, &errp, 10);
            if (!*errp) config.border_px = arg.n;
            break;
        case 'm':
            arg.n = strtol(optarg, &errp, 10);
            if (arg.n > 0 && !*errp)
                config.histsize = CLAMP(arg.n, MIN_HISTLINES, MAX_HISTLINES);
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

    if (platform_setup()) {
        dbgprint("Window server initialized");
    } else {
        dbgprint("Failed to initialize window server");
        return EXIT_FAILURE;
    }

    struct TermConfig termcfg = { 0 };

    for (uint i = 0; i < LEN(config.colors); i++) {
        ASSERT(config.colors[i]);

        uint32 *dst;

        switch (i) {
        case 0:  dst = &termcfg.color_bg;  break;
        case 1:  dst = &termcfg.color_fg;  break;
        default: dst = &termcfg.colors[i-2]; break;
        }
        if (!platform_parse_color_string(config.colors[i], dst)) {
            dbgprint("Failed to parse RGB string: %s", config.colors[i]);
            exit(EXIT_FAILURE);
        }
    }
    dbgprint("User colors parsed");

    App *const app = &app_;

    if (!fontmgr_init(platform_get_dpi())) {
        dbgprint("Failed to initialize font manager");
        return EXIT_FAILURE;
    } else {
        char *fontpath = NULL;

        if (config.fontfile) {
            fontpath = realpath(config.fontfile, NULL);
            if (fontpath) {
                dbgprint("Resolved file path: %s -> %s", config.fontfile, fontpath);
            } else {
                dbgprint("Failed to resolve file path: %s", config.fontfile);
            }
        }
        if (fontpath) {
            app->fontset = fontmgr_create_fontset_from_file(fontpath);
            FREE(fontpath);
        } else {
            app->fontset = fontmgr_create_fontset(config.font);
        }
        if (!app->fontset) {
            dbgprint("Failed to open fallback fonts. aborting...");
            return EXIT_FAILURE;
        }

        dbgprint("Fonts opened");
    }

    fontset_get_metrics(app->fontset, &app->colpx, &app->rowpx, NULL, NULL);

    Win *win = window_create(
        (WinConfig){
            .param = app,
            .smooth_resize = false,
            .wm_instance = config.wm_instance,
            .wm_class    = config.wm_class,
            .cols        = config.columns,
            .rows        = config.rows,
            .colpx       = app->colpx,
            .rowpx       = app->rowpx,
            .border      = config.border_px,
            .callbacks = {
                .resize     = event_resize,
                .key_press  = event_key_press,
                /* .text_input = event_text_input, */
                .expose     = NULL
            }
        }
    );

    if (win) {
        dbgprint("Window created");
        window_set_title(win, config.wm_title, strlen(config.wm_title));
    } else {
        dbgprint("Failed to create window");
        return EXIT_FAILURE;
    }

    if (fontset_init(app->fontset)) {
        dbgprint("Font cache initialized");
    } else {
        dbgprint("Failed to initialize font cache");
        return EXIT_FAILURE;
    }

    if (renderer_init()) {
        dbgprint("Renderer initialized");
    } else {
        dbgprint("Failed to initialize renderer");
        return EXIT_FAILURE;
    }

    if (window_show(win)) {
        window_get_dimensions(win, &app->width, &app->height, &app->borderpx);
    } else {
        dbgprint("Failed to display window");
        return EXIT_FAILURE;
    }

    app->win = win;
    app->cols = (app->width - 2 * app->borderpx) / app->colpx;
    app->rows = (app->height - 2 * app->borderpx) / app->rowpx;

    dbgprint(
        "Window displayed\n"
        "    width   = %d\n"
        "    height  = %d\n"
        "    border  = %d\n"
        "    columns = %dx%d\n"
        "    rows    = %dx%d",
        app->width,
        app->height,
        app->borderpx,
        app->cols, app->colpx,
        app->rows, app->rowpx
    );

    ASSERT(app->cols == (int)config.columns);
    ASSERT(app->rows == (int)config.rows);

    termcfg.cols      = app->cols;
    termcfg.rows      = app->rows;
    termcfg.colsize   = app->colpx;
    termcfg.rowsize   = app->rowpx;
    termcfg.shell     = config.shell;
    termcfg.histlines = MAX(config.histsize, termcfg.rows);
    termcfg.tabcols   = DEFAULT(config.tablen, 8);

    dbgprint(
        "Creating virtual terminal...\n"
        "    shell     = %s\n"
        "    histlines = %u\n"
        "    tabspaces = %u",
        DEFAULT(termcfg.shell, "$SHELL"),
        termcfg.histlines,
        termcfg.tabcols
    );

    if ((app->term = term_create(termcfg))) {
        dbgprint("Virtual terminal created");
    } else {
        dbgprint("Failed to create virtual terminal");
        return EXIT_FAILURE;
    }

    const struct TermHandlers handlers = {
        .set_title = handler_set_title,
        .set_icon  = handler_set_icon
    };
    term_setup_handlers(app->term, app, handlers);

    renderer_set_dimensions(
        app->width, app->height,
        app->cols, app->rows,
        app->colpx, app->rowpx,
        app->borderpx
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
    const int srvfd = platform_get_fileno();
    const int ptyfd = term_exec(term, config.shell);

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
event_resize(void *param, int width, int height)
{
    App *const app = param;

    if (width == app->width && height == app->height) {
        return;
    }

    const int cols = (width - 2 * app->borderpx) / app->colpx;
    const int rows = (height - 2 * app->borderpx) / app->rowpx;

    if (cols != app->term->cols || rows != app->term->rows) {
        term_resize(app->term, cols, rows);
    }

    renderer_set_dimensions(
        width, height,
        cols, rows,
        app->colpx, app->rowpx,
        app->borderpx
    );

    app->width  = width;
    app->height = height;
    app->cols   = cols;
    app->rows   = rows;
}

void
event_key_press(void *param, uint key, uint mod, const byte *data, int len)
{
    App *const app = param;

    switch (mod) {
    case MOD_ALT:
        switch (key) {
        case KeyF9:
            term_print_history(app->term);
            return;
        case 'k':
            term_scroll(app->term, -1);
            return;
        case 'j':
            term_scroll(app->term, +1);
            return;
        }
        break;
    case MOD_SHIFT:
        switch (key) {
        case KeyPageUp:
            term_scroll(app->term, -app->term->rows);
            return;
        case KeyPageDown:
            term_scroll(app->term, +app->term->rows);
            return;
        }
        break;
    default:
        break;
    }

    if (term_push_input(app->term, key, mod, data, len)) {
        term_reset_scroll(app->term);
    }
}

void
handler_set_title(void *param, const char *str, size_t len)
{
    const App *app = param;
    ASSERT(app == &app_);

    if (!str || !len) {
        str = config.wm_title;
        len = strlen(str);
    }
    window_set_title(app->win, str, len);
}

void
handler_set_icon(void *param, const char *str, size_t len)
{
    const App *app = param;
    ASSERT(app == &app_);

    if (!str || !len) {
        str = config.wm_title;
        len = strlen(str);
    }
    window_set_icon(app->win, str, len);
}

