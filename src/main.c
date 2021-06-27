#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <locale.h>
#include <unistd.h>
#include <sys/select.h>

#include "utils.h"
#include "term.h"
#include "window.h"

typedef struct Config {
	char *wm_class;
	char *wm_instance;
	char *wm_title;
	char *geometry;
	char *font;
	char *colors[2];
	char *shell;
	uint tabstop;
	uint border_px;
	uint history_size;
	uint columns, rows;
	struct { int x, y; } position;
	struct { uint min, max; } latency;
} Config;
// NOTE: must include *after* definitions
#include "config.h"

int histsize;
int tabstop;
double min_latency;
double max_latency;
u32 log_flags = 0;

TTY tty = { 0 };
PTY pty = { 0 };

static int fonts[MAX_FONTS];
static int colors[MAX_COLORS];
static Win *win;
static RC *rc;
static int cw, ch;

static void event_key_press(int, int, char *, int);
static void run(void);
static void render(void);

int
main(int argc, char **argv)
{
	for (int opt; (opt = getopt(argc, argv, "T:N:C:f:c:r:x:y:b:m:")) != -1; ) {
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
		case 'f': config.font = optarg; break;
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
				config.history_size = arg.n;
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

	histsize = (config.rows > config.history_size) ? config.rows : config.history_size;
	tabstop = config.tabstop;
	min_latency = config.latency.min;
	max_latency = config.latency.max;

	int cols, rows;

	if (!(win = ws_init_window()))
		return 2;
	if ((fonts[0] = wsr_load_font(win, config.font)) < 0)
		return 3;
	{
		assert(wsr_get_avg_font_size(fonts[0], FACE_REGULAR, &cw, &ch));

		win->title = config.wm_title;
		win->instance = config.wm_instance;
		win->class = config.wm_class;
		win->iw = cw;
		win->ih = ch;
		win->w = win->iw * config.columns;
		win->h = win->ih * config.rows;
		win->bw = config.border_px;
		win->flags = WINATTR_RESIZABLE;
	}
	if (!(ws_create_window(win)))
		return 4;
	{
		assert((cols = (win->w / cw) - (2 * win->bw)) == (int)config.columns);
		assert((rows = (win->h / ch) - (2 * win->bw)) == (int)config.rows);
	}
	if (!(rc = wsr_init_context(win)))
		return 5;
	wsr_set_font(rc, fonts[0], FACE_REGULAR);
	{
		int n = 0;

		for (int i = 0; i < (ssize_t)LEN(config.colors); i++) {
			if (n == MAX_COLORS)
				break;
			if (config.colors[i]) {
				colors[n] = wsr_load_color_name(rc, config.colors[i]);
				assert(colors[n] >= 0);
				n++;
			}
		}

		assert(n >= 2);
	}
	assert(wsr_set_colors(rc, colors[0], colors[1]));

	if (!tty_init(cols, rows))
		return 6;

	win->events.key_press = event_key_press;
	ws_show_window(win);

	run();

	return 0;
}

void
run(void)
{
	fd_set rset;
	double timeout = min_latency / 1E3;

	pty_init(config.shell);
	pty_resize(win->w, win->h, tty.max_cols, tty.max_rows);

	while (win->state) {
		FD_ZERO(&rset);
		FD_SET(pty.mfd, &rset);

		struct timeval dummy = { 0 };
		if (select(pty.mfd + 1, &rset, NULL, NULL, &dummy) < 0) {
			exit(2);
		}
		if (FD_ISSET(pty.mfd, &rset)) {
			pty_read();
		}

		ws_process_events(win, timeout);
		render();
	}
}

void
render(void)
{
	wsr_clear_screen(rc);
	for (int n = tty.top; n <= tty.bot; n++) {
		Row *p = &ROW(n);
		if (!p->len || IS_BLANK(*p)) {
			continue;
		}
		wsr_draw_string(rc, streamptr_s(p->offset), p->len, 0, n - tty.top, false);
	}
	wsr_fill_region(rc, tty.c.x, tty.c.y, tty.c.x + 1, tty.c.y + 1, cw, ch);
	ws_swap_buffers(win);
}

void
event_key_press(int key, int mod, char *buf, int len)
{
	printf("\"%s\"\n", buf);
	if (len == 1) {
		pty_write(buf, len);
	}
}

