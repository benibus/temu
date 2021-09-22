#include <locale.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>

#include "utils.h"
#include "term.h"
#include "window.h"

#define MAX_CFG_COLORS (2+16)

typedef struct Config {
	char *wm_class;
	char *wm_instance;
	char *wm_title;
	char *geometry;
	char *font;
	char *colors[MAX_CFG_COLORS];
	char *shell;
	uint tablen;
	uint border_px;
	uint histsize;
	uint columns, rows;
	uint scrollinc;
	struct { int x, y; } position;
	struct { uint min, max; } latency;
} Config;
// NOTE: must include *after* definitions
#include "config.h"

typedef struct Client_ {
	Win *win;
	RC rc;
	TTY tty;
	struct {
		double min;
		double max;
	} latency;
	int scrollinc;
} Client;

static Client client_;

static FontFace *fonts[4];
static Color colors[2+256];
static bool toggle_render = true;

static void run(Client *);
static Color fetch_color(TermColor);
static void render_frame(Client *);
static void event_key_press(void *, int, int, char *, int);
static void event_resize(void *, int, int);

static void *
passbuf(void *raw)
{
	struct Buf_ *buf = raw;
	PRINTBUF(buf);
	return buf;
}

int
main(int argc, char **argv)
{
	for (int opt; (opt = getopt(argc, argv, "T:N:C:f:c:r:x:y:b:m:s:")) != -1; ) {
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
				config.histsize = arg.n;
			break;
		case 's':
			arg.n = strtol(optarg, &errp, 10);
			if (arg.n > 0 && !*errp)
				config.scrollinc = arg.n;
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

	struct TTYConfig tc = { 0 };
	Win *win;
	RC rc = { 0 };

	if (!(win = win_create_client()))
		return 2;
	if (!win_init_render_context(win, &rc))
		return 3;

	// load the user colors (or any fallbacks, if necessary) + the 240 standard colors.
	// there should be no surprises here - if anything fails, we terminate
	for (uint i = 0; i < LEN(colors); i++) {
		uint32 argb = 0;

		// 2+16 user/programmer-configured colors (0 and 1 are foreground/background)
		if (i < LEN(config.colors)) {
			// All default strings MUST exist at compile-time
			ASSERT(config.colors[i]);
			if (!win_parse_color_string(&rc, config.colors[i], &argb)) {
				dbgprintf("failed to parse RGB string: %s\n", config.colors[i]);
				exit(EXIT_FAILURE);
			}
			for (uint j = 0; j < i; j++) {
				if (argb == colors[j].argb) {
					dbgprintf("Mapping color[%03u] to color[%03u]\n", i, j);
					colors[i] = colors[j];
					break;
				}
			}
			if (colors[i].id) continue;
		} else if (i < 232 + 2) { // the classic 6^3 color cube
			uint8 n = i - 16 - 2;
			argb = pack_argb(
				((n / 36) % 6) ? (((n / 36) % 6) * 40 + 55) : 0,
				((n /  6) % 6) ? (((n /  6) % 6) * 40 + 55) : 0,
				((n /  1) % 6) ? (((n /  1) % 6) * 40 + 55) : 0,
				0xff
			);
		} else { // dark -> light grayscale till the end
			ASSERT(i < 256 + 2);
			uint8 n = i - 232 - 2;
			argb = pack_argb(
				n * 10 + 8,
				n * 10 + 8,
				n * 10 + 8,
				0xff
			);
		}

		ColorID handle = win_alloc_color(&rc, argb);
		if (!handle) {
			dbgprintf("failed to create RGBA fill: [%03u]\n", i);
			exit(EXIT_FAILURE);
		}

		dbgprintf("Mapping color[%03u] to RGBA(%.08X)\n", i, argb);

		colors[i].id = handle;
		colors[i].argb = argb;
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

	{
		FontMetrics metrics = { 0 };
		font_get_face_metrics(fonts[0], &metrics);
		tc.colpx = metrics.width;
		tc.rowpx = metrics.ascent + metrics.descent;
	}

	{
		win->title = config.wm_title;
		win->instance = config.wm_instance;
		win->class = config.wm_class;
		win->w = tc.colpx * config.columns;
		win->h = tc.rowpx * config.rows;
		win->bw = config.border_px;
		win->flags = WINATTR_RESIZABLE;
	}

	if (!win_init_client(win)) {
		return 4;
	}

	tc.cols = (win->w / tc.colpx) - (2 * win->bw);
	tc.rows = (win->h / tc.rowpx) - (2 * win->bw);

	ASSERT(tc.cols == (int)config.columns);
	ASSERT(tc.rows == (int)config.rows);

	tc.ref = &client_;
	tc.shell = config.shell;
	tc.histsize = MAX(config.histsize, tc.rows);
	tc.tablen = DEFAULT(config.tablen, 8);

	if (!tty_init(&client_.tty, tc)) {
		return 6;
	}

	win->ref = &client_;
	win->events.key_press = event_key_press;
	win->events.resize = event_resize;
	win_show_client(win);

	rc.font = fonts[0];
	rc.color.bg = colors[TCOLOR_BG];
	rc.color.fg = colors[TCOLOR_FG];

	client_.win = win;
	client_.rc  = rc;
	client_.latency.min = config.latency.min;
	client_.latency.max = config.latency.max;
	client_.scrollinc = DEFAULT(config.scrollinc, 1);

	run(&client_);

	return 0;
}

void
run(Client *client)
{
	Win *win = client->win;
	TTY *tty = &client->tty;

	double timeout = -1.0;
	double minlat = client->latency.min;
	double maxlat = client->latency.max;
	bool busy = false;
	TimeVal t0 = { 0 };
	TimeVal t1 = { 0 };

	tty_exec(tty, config.shell);
#if 1
	fprintf(stderr, "App(%s) TTY PID: %d\n"
	                "App(%s) PTY PID: %d\n",
	  __FILE__, win->pid,
	  __FILE__, tty->pty.pid
	);
#endif

	fd_set rset;
	int maxfd = MAX(win->fd, tty->pty.mfd);

	while (win->state) {
		FD_ZERO(&rset);
		FD_SET(tty->pty.mfd, &rset);
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

		if (FD_ISSET(tty->pty.mfd, &rset)) {
			tty_read(tty);
		}

		int num_events = win_process_events(win, 0.0);

		if (FD_ISSET(tty->pty.mfd, &rset) || num_events) {
			if (!busy) {
				busy = true;
				t0 = t1;
			}

			double elapsed = time_diff_msec(&t1, &t0);
			timeout = (maxlat - elapsed) / maxlat;
			timeout *= minlat;

			if (timeout > 0.0) continue;
		}
		timeout = -1.0;
		busy = false;

		if (toggle_render) {
			render_frame(client);
		}
	}
}

Color
fetch_color(TermColor tcolor)
{
	Color result;

	switch (tcolor.tag) {
	case ColorTagNone:
		result = colors[!!tcolor.index];
		break;
	case ColorTag256:
		result = colors[tcolor.index + 2];
		break;
	case ColorTagRGB:
		result.id = 0;
		result.argb = pack_argb(tcolor.r, tcolor.g, tcolor.b, 0xff);
		break;
	default:
		result = (Color){ 0 };
		errprint("invalid color tag");
		break;
	}

	return result;
}

void
render_frame(Client *client)
{
	Win *win = client->win;
	RC *rc   = &client->rc;
	TTY *tty = &client->tty;

	rc->color.bg = colors[TCOLOR_BG];
	rc->color.fg = colors[TCOLOR_FG];
	draw_rect_solid(rc, rc->color.bg, 0, 0, win->w, win->h);

	int i = 0;
	int x = 0;
	int y = tty->top + tty->scroll;

	for (; i < tty->rows && y <= tty->bot; i++, y++) {
		GlyphRender glyphs[256] = { 0 };

		FontFace *font = rc->font;
		Cell *cells = stream_get_line(tty, y, NULL);

		for (x = 0; cells && x < tty->cols && x < (int)LEN(glyphs); x++) {
			Cell cell = cells[x];

			if (!cell.ucs4) break;

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

			glyphs[x].ucs4 = cell.ucs4;
			glyphs[x].font = font;
			glyphs[x].bg = fetch_color(cell.color.bg);
			glyphs[x].fg = fetch_color(cell.color.fg);

			if (cell.attr & ATTR_INVERT) {
				SWAP(Color, glyphs[x].bg, glyphs[x].fg);
			}
		}

		draw_text_utf8(rc, glyphs, x, 0, i * tty->rowpx);
	}

	// draw cursor last
	if (!tty->cursor.hide) {
		int sx = tty->pos.x;
		int sy = tty->pos.y - (tty->top + tty->scroll);
		if (sy < tty->rows) {
			GlyphRender glyph = {
				.ucs4 = tty->cursor.cell.ucs4,
				.bg   = fetch_color(tty->cursor.cell.color.bg),
				.fg   = fetch_color(tty->cursor.cell.color.fg),
				.font = (tty->cursor.cell.attr & ATTR_BOLD) ? fonts[3] : fonts[0]
			};
			draw_text_utf8(rc, &glyph, 1, sx * tty->colpx, sy * tty->rowpx);
		}
	}

	win_render_frame(win);
}

void
event_key_press(void *ref, int key, int mod, char *buf, int len)
{
	Client *client = ref;
	TTY *tty = &client->tty;

	char seq[64];
	int seqlen = 0;

#if 1
	if (mod == ModAlt) {
		switch (key) {
		case KeyF9:
			dbg_print_history(tty);
			return;
		case KeyF10:
			dbg_print_tty(tty, ~0);
			return;
		case 'u':
			tty_scroll(tty, -client->scrollinc);
			return;
		case 'd':
			tty_scroll(tty, +client->scrollinc);
			return;
		}
	}
#endif

	if ((seqlen = key_get_sequence(key, mod, seq, LEN(seq)))) {
		tty_write(tty, seq, seqlen, INPUT_KEY);
	} else if (len == 1) {
		tty_scroll(tty, -tty->scroll);
		tty_write(tty, buf, len, INPUT_CHAR);
	}
}

void
event_resize(void *ref, int width, int height)
{
	Client *client = ref;
	Win *win = client->win;
	TTY *tty = &client->tty;

	int cols = (width / tty->colpx) - (2 * win->bw);
	int rows = (height / tty->rowpx) - (2 * win->bw);

	win_resize_client(win, width, height);
	if (cols != tty->cols || rows != tty->rows) {
		tty_resize(tty, cols, rows);
	}
}

