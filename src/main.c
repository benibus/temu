#include <locale.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>

#include "utils.h"
#include "term.h"
#include "window.h"
#include "fonts.h"

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
	Term *term;
	struct {
		double min;
		double max;
	} latency;
	int scrollinc;
} Client;

static Client client_;

static FontID fonts[4];
static bool toggle_render = true;

static void run(Client *);
static void client_draw_screen(Client *);
static void client_draw_cursor(const Client *);
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

	// temporary
	srand(time_get_mono_msec(NULL));

	if (!(win = win_create_client()))
		return 2;

	fonts[0] = font_create(win, config.font);
	if (!fonts[0]) {
		errfatal(1, "Failed to create default font");
	} else {
		fonts[ATTR_BOLD] = font_create_derivative(fonts[0], STYLE_BOLD);
		fonts[ATTR_ITALIC] = font_create_derivative(fonts[0], STYLE_ITALIC);
		fonts[ATTR_BOLD|ATTR_ITALIC] = font_create_derivative(fonts[0], STYLE_BOLD|STYLE_ITALIC);
	}

	for (int i = 0; i < 4; i++) {
		ASSERT(fonts[i]);
		dbgprintf("Initializing font[%d] = %#.08x\n", i, fonts[i]);
		if (!font_init(fonts[i])) {
			dbgprintfl("Failed to initialize font %d", i);
		}
	}

	for (uint i = 0; i < LEN(config.colors); i++) {
		ASSERT(config.colors[i]);

		uint32 *dst;

		switch (i) {
		case 0:  dst = &tc.color_bg;  break;
		case 1:  dst = &tc.color_fg;  break;
		default: dst = &tc.colors[i-2]; break;
		}
		if (!win_parse_color_string(win, config.colors[i], dst)) {
			dbgprintf("failed to parse RGB string: %s\n", config.colors[i]);
			exit(EXIT_FAILURE);
		}
	}

	{
		int width, height, ascent, descent;

		font_get_extents(fonts[0], &width, &height, &ascent, &descent);
		tc.colsize = width;
		tc.rowsize = ascent + descent;
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

	/* FIXME(ben):
	 * Border sizes aren't being factored into returned window dimensions.
	 */
	tc.cols = (win->w - 2 * win->bw) / tc.colsize;
	tc.rows = (win->h - 2 * win->bw) / tc.rowsize;
#if 0
	ASSERT(tc.cols == (int)config.columns);
	ASSERT(tc.rows == (int)config.rows);
#endif

	tc.generic = &client_;
	tc.shell = config.shell;
	tc.maxhist = MAX(config.histsize, tc.rows);
	tc.tabcols = DEFAULT(config.tablen, 8);

	if (!(client_.term = term_create(tc))) {
		return 6;
	}

	win->ref = &client_;
	win->events.key_press = event_key_press;
	win->events.resize = event_resize;
	win_show_client(win);

	client_.win = win;
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
		time_get_mono_msec(&t1);

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
			client_draw_screen(client);
		}
	}
}

void
client_draw_screen(Client *client)
{
	Win *win = client->win;
	Term *term = client->term;

#define ROWBUF_MAX 256
	GlyphRender cmds[ROWBUF_MAX] = { 0 };

	draw_rect(win, pack_xrgb(term->color_bg, 0xff), 0, 0, win->w, win->h);

	for (int row = 0; row < term->maxrows; row++) {
		const Cell *cells = term_get_row(term, row);
		int len = 0;

		if (!cells) { continue; }

		for (int col = 0; col < term->maxcols && col < ROWBUF_MAX; len++, col++) {
			Cell cell = cells[col];

			if (!cell.ucs4) break;

			if (cell.attr & ATTR_INVERT) {
				SWAP(uint32, cell.bg, cell.fg);
			}

			font_load_codepoint(
				fonts[cell.attr & (ATTR_BOLD|ATTR_ITALIC)],
				cell.ucs4,
				&cmds[col].font,
				&cmds[col].glyph
			);
			cmds[col].bg = pack_xrgb(cell.bg, (cell.bg != term->color_bg) ? 0xff : 0);
			cmds[col].fg = pack_xrgb(cell.fg, 0xff);
		}

		draw_text_utf8(win, cmds, len, win->bw, win->bw + row * term->rowsize);
	}
#undef ROWBUF_MAX

	// draw cursor last
	client_draw_cursor(client);

	win_render_frame(win);
}

void
client_draw_cursor(const Client *client)
{
	const Win *win = client->win;
	const Term *term = client->term;

	Cursor cursor = term_get_cursor(term);

	if (cursor.visible) {
		Cell cell = term_get_cell(term, cursor.col, cursor.row);

		GlyphRender cmd = { 0 };

		font_load_codepoint(
			fonts[cell.attr & (ATTR_BOLD|ATTR_ITALIC)],
			cell.ucs4,
			&cmd.font,
			&cmd.glyph
		);

		draw_rect(
			client->win, pack_xrgb(term->color_bg, 0xff),
			win->bw + cursor.col * term->colsize,
			win->bw + cursor.row * term->rowsize,
			term->colsize,
			term->rowsize
		);

		switch (cursor.shape) {
		case CursorShapeDefault:
		case CursorShapeBar:
			cmd.bg = 0;
			cmd.fg = pack_xrgb(cell.fg, 0xff);
			draw_text_utf8(
				client->win, &cmd, 1,
				win->bw + cursor.col * term->colsize,
				win->bw + cursor.row * term->rowsize
			);
			draw_rect(
				client->win, pack_xrgb(cursor.color, 0xff),
				win->bw + cursor.col * term->colsize,
				win->bw + cursor.row * term->rowsize,
				2, term->rowsize
			);
			break;
		case CursorShapeBlock:
			cmd.bg = pack_xrgb(term->color_fg, 0xff);
			cmd.fg = pack_xrgb(term->color_bg, 0xff);
			draw_text_utf8(
				client->win, &cmd, 1,
				win->bw + cursor.col * term->colsize,
				win->bw + cursor.row * term->rowsize
			);
			break;
		case CursorShapeUnderscore:
			cmd.bg = 0;
			cmd.fg = pack_xrgb(cell.fg, 0xff);
			draw_text_utf8(
				client->win, &cmd, 1,
				win->bw + cursor.col * term->colsize,
				win->bw + cursor.row * term->rowsize
			);
			draw_rect(
				client->win, pack_xrgb(term->color_fg, 0xff),
				win->bw + cursor.col * term->colsize,
				win->bw + (cursor.row + 1) * term->rowsize - 2,
				term->colsize, 2
			);
			break;
		default:
			break;
		}
	}
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

	int cols = (width - 2 * win->bw) / term->colsize;
	int rows = (height - 2 * win->bw) / term->rowsize;

	win_resize_client(win, width, height);
	if (cols != term->maxcols || rows != term->maxrows) {
		term_resize(term, cols, rows);
	}
}

