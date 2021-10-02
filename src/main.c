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
	Term *term;
	struct {
		double min;
		double max;
	} latency;
	int scrollinc;
} Client;

static Client client_;

static FontFace *fonts[4];
static bool toggle_render = true;

static void run(Client *);
static void render_frame(Client *);
static void event_key_press(void *, int, int, char *, int);
static void event_resize(void *, int, int);

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

	struct TermConfig tc = { 0 };
	Win *win;
	RC rc = { 0 };

	// temporary
	srand(time_get_mono_msec(NULL));

	if (!(win = win_create_client()))
		return 2;
	if (!win_init_render_context(win, &rc))
		return 3;

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

	for (uint i = 0; i < LEN(config.colors); i++) {
		ASSERT(config.colors[i]);

		uint32 *dst;

		switch (i) {
		case 0:  dst = &tc.default_bg;  break;
		case 1:  dst = &tc.default_fg;  break;
		default: dst = &tc.colors[i-2]; break;
		}
		if (!win_parse_color_string(&rc, config.colors[i], dst)) {
			dbgprintf("failed to parse RGB string: %s\n", config.colors[i]);
			exit(EXIT_FAILURE);
		}
	}

	{
		FontMetrics metrics = { 0 };
		font_get_face_metrics(fonts[0], &metrics);
		tc.colsize = metrics.width;
		tc.rowsize = metrics.ascent + metrics.descent;
	}

	{
		win->title = config.wm_title;
		win->instance = config.wm_instance;
		win->class = config.wm_class;
		win->w = tc.colsize * config.columns;
		win->h = tc.rowsize * config.rows;
		win->bw = config.border_px;
		win->flags = WINATTR_RESIZABLE;
	}

	if (!win_init_client(win)) {
		return 4;
	}

	tc.cols = (win->w / tc.colsize) - (2 * win->bw);
	tc.rows = (win->h / tc.rowsize) - (2 * win->bw);

	ASSERT(tc.cols == (int)config.columns);
	ASSERT(tc.rows == (int)config.rows);

	tc.generic = &client_;
	tc.shell = config.shell;
	tc.histlines = MAX(config.histsize, tc.rows);
	tc.tabcols = DEFAULT(config.tablen, 8);

	if (!(client_.term = term_create(tc))) {
		return 6;
	}

	win->ref = &client_;
	win->events.key_press = event_key_press;
	win->events.resize = event_resize;
	win_show_client(win);

	rc.font = fonts[0];
	rc.color.bg = client_.term->default_bg;
	rc.color.fg = client_.term->default_fg;

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
	Term *term = client->term;

	double timeout = -1.0;
	double minlat = client->latency.min;
	double maxlat = client->latency.max;
	bool busy = false;
	TimeVal t0 = { 0 };
	TimeVal t1 = { 0 };

	int ptyfd = term_exec(term, config.shell);
	int maxfd = MAX(win->fd, ptyfd);

	while (win->state) {
		static fd_set rset;
		FD_ZERO(&rset);
		FD_SET(ptyfd, &rset);
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

		if (FD_ISSET(ptyfd, &rset)) {
			term_pull(term);
		}

		int num_events = win_process_events(win, 0.0);

		if (FD_ISSET(ptyfd, &rset) || num_events) {
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

void
render_frame(Client *client)
{
	Win *win = client->win;
	RC *rc   = &client->rc;
	Term *term = client->term;

	rc->color.bg = term->default_bg;
	rc->color.fg = term->default_fg;
	draw_rect_solid(rc, rc->color.bg, 0, 0, win->w, win->h);

	int i = 0;
	for (; i < term->rows; i++) {
		GlyphRender glyphs[256] = { 0 };

		FontFace *font = rc->font;
		const Cell *cells = term_get_row(term, i);
		int x = 0;

		for (; cells && x < term->cols && x < (int)LEN(glyphs); x++) {
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
			glyphs[x].bg = cell.bg;
			glyphs[x].fg = cell.fg;

			if (cell.attr & ATTR_INVERT) {
				SWAP(uint32, glyphs[x].bg, glyphs[x].fg);
			}
		}

		draw_text_utf8(rc, glyphs, x, 0, i * term->rowsize);
	}

	// draw cursor last
	CursorDesc cursor = { 0 };
	if (term_get_cursor_desc(term, &cursor)) {
		GlyphRender glyph = {
			.ucs4 = cursor.ucs4,
			.bg   = cursor.bg,
			.fg   = cursor.fg,
			.font = (cursor.attr & ATTR_BOLD) ? fonts[3] : fonts[0]
		};
		draw_text_utf8(rc, &glyph, 1, cursor.col * term->colsize, cursor.row * term->rowsize);
	}

	win_render_frame(win);
}

void
event_key_press(void *ref, int key, int mod, char *buf, int len)
{
	Client *client = ref;
	Term *term = client->term;

	char seq[64];
	int seqlen = 0;

#if 1
	if (mod == ModAlt) {
		switch (key) {
		case KeyF9:
			term_print_history(term);
			return;
		case KeyF10:
			term_print_summary(term, ~0);
			return;
		case 'u':
			term_scroll(term, -client->scrollinc);
			return;
		case 'd':
			term_scroll(term, +client->scrollinc);
			return;
		}
	}
#endif

	if ((seqlen = term_make_key_string(term, key, mod, seq, LEN(seq)))) {
		term_push(term, seq, seqlen);
	} else if (len == 1) {
		term_reset_scroll(term);
		term_push(term, buf, len);
	}
}

void
event_resize(void *ref, int width, int height)
{
	Client *client = ref;
	Win *win = client->win;
	Term *term = client->term;

	int cols = (width / term->colsize) - (2 * win->bw);
	int rows = (height / term->rowsize) - (2 * win->bw);

	win_resize_client(win, width, height);
	if (cols != term->cols || rows != term->rows) {
		term_resize(term, cols, rows);
	}
}

