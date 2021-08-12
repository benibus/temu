#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <locale.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>

#include "utils.h"
#include "term.h"
#include "window.h"

typedef struct Config {
	char *wm_class;
	char *wm_instance;
	char *wm_title;
	char *geometry;
	char *font;
	char *colors[MAX_COLORS];
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

static FontFace *fonts[4];
static FontMetrics metrics;
static Color colors[MAX_COLORS];
static Win *win;
static RC rc;
static bool toggle_render = true;

static void event_key_press(int, int, char *, int);
static void run(void);
static void render_frame(void);

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

	if (!(win = win_create_client()))
		return 2;
	if (!win_init_render_context(win, &rc))
		return 3;
	{
		int n = 0;

		for (uint i = 0; i < LEN(config.colors); i++) {
			if (n == MAX_COLORS) {
				break;
			}
			if (config.colors[i]) {
				Color *res;
				res = color_create_name(&rc, &colors[n], config.colors[i]);
				ASSERT(res);
				n++;
			}
		}

		ASSERT(n >= 2);
	}

	fonts[0] = font_create_face(&rc, config.font);
	ASSERT(fonts[0]);

	fonts[1] = font_create_derived_face(fonts[0], STYLE_ITALIC);
	fonts[2] = font_create_derived_face(fonts[0], STYLE_ITALIC|STYLE_BOLD);
	fonts[3] = font_create_derived_face(fonts[0], STYLE_BOLD);
	ASSERT(fonts[1] && fonts[2] && fonts[3]);

	ASSERT(font_init_face(fonts[0]));
	ASSERT(font_init_face(fonts[1]));
	ASSERT(font_init_face(fonts[2]));
	ASSERT(font_init_face(fonts[3]));
	font_get_face_metrics(fonts[0], &metrics);

	{
		win->title = config.wm_title;
		win->instance = config.wm_instance;
		win->class = config.wm_class;
		win->iw = metrics.width;
		win->ih = metrics.ascent + metrics.descent;
		win->w = win->iw * config.columns;
		win->h = win->ih * config.rows;
		win->bw = config.border_px;
		win->flags = WINATTR_RESIZABLE;
	}

	if (!win_init_client(win)) {
		return 4;
	}

	cols = (win->w / metrics.width) - (2 * win->bw);
	rows = (win->h / (metrics.ascent + metrics.descent)) - (2 * win->bw);
	ASSERT(cols == (int)config.columns);
	ASSERT(rows == (int)config.rows);

	rc.font = fonts[0];
	rc.color.fg = &colors[COLOR_FG];
	rc.color.bg = &colors[COLOR_BG];

	if (!tty_init(cols, rows))
		return 6;

	win->events.key_press = event_key_press;
	win_show_client(win);

	run();

	return 0;
}

void
run(void)
{
	double timeout = -1.0;
	bool busy = false;
	TimeVal t0 = { 0 };
	TimeVal t1 = { 0 };

	pty_init(config.shell);
	pty_resize(win->w, win->h, tty.cols, tty.rows);

	fd_set rset;
	int maxfd = MAX(win->fd, pty.mfd);

	while (win->state) {
		FD_ZERO(&rset);
		FD_SET(pty.mfd, &rset);
		FD_SET(win->fd, &rset);

		if (win_events_pending(win)) {
			timeout = 0.0;
		}

		struct timespec *ts, ts_ = { 0 };
		ts_.tv_sec = timeout / 1E3;
		ts_.tv_nsec = 1E6 * (timeout - 1E3 * ts_.tv_sec);
		ts = (timeout >= 0.0) ? &ts_ : NULL;

		if (pselect(maxfd + 1, &rset, NULL, NULL, ts, NULL) < 0) {
			exit(2);
		}
		time_get_mono(&t1);

		if (FD_ISSET(pty.mfd, &rset)) {
			pty_read();
		}

		int num_events = win_process_events(win, 0.0);

		if (FD_ISSET(pty.mfd, &rset) || num_events) {
			if (!busy) {
				busy = true;
				t0 = t1;
			}

			double elapsed = time_diff_msec(&t1, &t0);
			timeout = (max_latency - elapsed) / max_latency;
			timeout *= min_latency;

			if (timeout > 0.0) continue;
		}
		timeout = -1.0;
		busy = false;

		if (toggle_render) {
			render_frame();
		}
	}
}

void
render_frame(void)
{
	rc.color.fg = &colors[COLOR_FG];
	rc.color.bg = &colors[COLOR_BG];
	draw_rect_solid(&rc, rc.color.bg, 0, 0, win->w, win->h);

	for (uint y = 0; (int)y <= tty.bot - tty.top; y++) {
		GlyphRender glyphs[256] = { 0 };

		FontFace *font = rc.font;
		Cell *cells = NULL;
		uint x, len = 0;

		cells = stream_get_row(&tty, tty.top + y, &len);
		ASSERT((int)len <= tty.cols);

		for (x = 0; cells && x < len && x < LEN(glyphs); x++) {
			Cell cell = cells[x];
			ColorSet color = { 0 };

			if (cell.attr & ATTR_ITALIC) {
				font = fonts[1];
				if (cell.attr & ATTR_BOLD) {
					font = fonts[2];
				}
			} else if (cell.attr & ATTR_BOLD) {
				font = fonts[3];
			} else {
				font = fonts[0];
			}

			if (cell.attr & ATTR_INVERT) {
				color.fg = cell.color.bg;
				color.bg = cell.color.fg;
			} else {
				color.fg = cell.color.fg;
				color.bg = cell.color.bg;
			}
			color.hl = cell.color.hl;

			glyphs[x].ucs4 = cell.ucs4;
			glyphs[x].font = font;
			glyphs[x].foreground = &colors[color.fg];
			glyphs[x].background = &colors[color.bg];
		}

		// draw cursor
		if (!tty.cursor.hide && tty.top + (int)y == tty.cursor.y) {
			uint cx = tty.cursor.x;
			glyphs[cx].ucs4 = DEFAULT(glyphs[cx].ucs4, L' ');
			glyphs[cx].font = DEFAULT(glyphs[cx].font, font);
			glyphs[cx].foreground = &colors[COLOR_BG];
			glyphs[cx].background = &colors[COLOR_FG];
			x += (cx == len);
		}

		draw_text_utf8(&rc, glyphs,
		               x, 0, y * (metrics.ascent + metrics.descent));
	}

	win_render_frame(win);
}

void
event_key_press(int key, int mod, char *buf, int len)
{
	char seq[64];
	int seqlen = 0;

	if (mod == MOD_ALT && key == KEY_F9) {
		dbg_dump_history(&tty);
		return;
	}
	if (mod == MOD_ALT && key == KEY_F10) {
		toggle_render = !toggle_render;
		return;
	}

	if ((seqlen = key_get_sequence(key, mod, seq, LEN(seq)))) {
		pty_write(seq, seqlen);
	} else if (len == 1) {
		pty_write(buf, len);
	}
}

