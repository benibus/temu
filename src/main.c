#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <unistd.h>

#include "utils.h"
#include "terminal.h"
#include "window.h"
#include "render.h"
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
} Config;
// NOTE: must include *after* definitions
#include "config.h"

typedef struct Client_ {
	/* Win *win; */
	WinClient *win;
	Term *term;
	int scrollinc;
	int width;
	int height;
	int border;
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
	for (int opt; (opt = getopt(argc, argv, "T:N:C:S:f:c:r:x:y:b:m:s:")) != -1; ) {
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

	struct TermConfig termcfg = { 0 };

	// temporary
	srand(time_get_mono_msec(NULL));

	if (!server_setup()) {
		return 2;
	}

	fonts[0] = font_create(config.font);
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
		case 0:  dst = &termcfg.color_bg;  break;
		case 1:  dst = &termcfg.color_fg;  break;
		default: dst = &termcfg.colors[i-2]; break;
		}
		if (!server_parse_color_string(config.colors[i], dst)) {
			dbgprintf("failed to parse RGB string: %s\n", config.colors[i]);
			exit(EXIT_FAILURE);
		}
	}

	{
		int width, height, ascent, descent;

		font_get_extents(fonts[0], &width, &height, &ascent, &descent);
		termcfg.colsize = width;
		termcfg.rowsize = ascent + descent;
	}

	WinClient *win = server_create_window(
		(struct WinConfig){
			.smooth_resize = false,
			.wm_title    = config.wm_title,
			.wm_instance = config.wm_instance,
			.wm_class    = config.wm_class,
			.cols        = config.columns,
			.rows        = config.rows,
			.colpx       = termcfg.colsize,
			.rowpx       = termcfg.rowsize,
			.border      = config.border_px,
			.callbacks = {
				.generic  = &client_,
				.resize   = event_resize,
				.keypress = event_key_press,
				.expose   = NULL
			}
		}
	);

	if (!win || !window_show(win)) {
		exit(EXIT_FAILURE);
	}
	window_get_dimensions(win, &client_.width, &client_.height, &client_.border);

	termcfg.cols = (client_.width - 2 * client_.border) / termcfg.colsize;
	termcfg.rows = (client_.height - 2 * client_.border) / termcfg.rowsize;

	ASSERT(termcfg.cols == (int)config.columns);
	ASSERT(termcfg.rows == (int)config.rows);

	termcfg.shell = config.shell;
	termcfg.histlines = MAX(config.histsize, termcfg.rows);
	termcfg.tabcols = DEFAULT(config.tablen, 8);

	if (!(client_.term = term_create(termcfg))) {
		return 6;
	}

	client_.win = win;
	client_.scrollinc = DEFAULT(config.scrollinc, 1);

	run(&client_);

	term_destroy(client_.term);

	return 0;
}

void
run(Client *client)
{
	Term *term = client->term;

	do {
		client_draw_screen(client);
	} while (window_poll_events(client->win));

	const int srvfd = server_get_fileno();
	const int ptyfd = term_exec(term, config.shell);

	struct pollfd pollset[] = {
		{ .fd = ptyfd, .events = POLLIN, .revents = 0 },
		{ .fd = srvfd, .events = POLLIN, .revents = 0 }
	};
	static_assert(LEN(pollset) == 2, "Unexpected pollset size.");

	// Target polling rate
	static const int msec = 16;

	while (window_online(client->win)) {
		switch (poll(pollset, 2, msec)) {
		case -1:
			if (errno) {
				perror("poll()");
				exit(errno);
			}
			return;
		case 0:
			if (!window_poll_events(client->win)) {
				continue;
			}
			break;
		case 1:
			if (pollset[0].revents & POLLIN) {
				term_pull(term);
			} else if (!window_poll_events(client->win)) {
				continue;
			}
			break;
		case 2:
			break;
		}

		client_draw_screen(client);
	}
}

void
client_draw_screen(Client *client)
{
	/* Win *win = client->win; */
	Term *term = client->term;
	int width = client->width;
	int height = client->height;
	int border = client->border;

#define ROWBUF_MAX 256
	GlyphRender cmds[ROWBUF_MAX] = { 0 };

	draw_rect(
		client->win,
		pack_xrgb(term->color_bg, 0xff),
		0, 0,
		width,
		height
	);

	const Cell *framebuf = term_get_framebuffer(term);
	ASSERT(framebuf);

	for (int row = 0; row < term->rows; row++) {
		const Cell *cells = framebuf + row * term->cols;
		int len = 0;

		for (int col = 0; col < term->cols && col < ROWBUF_MAX; len++, col++) {
			Cell cell = cells[col];

			if (!cell.ucs4) break;

			if (cell.attrs & ATTR_INVERT) {
				SWAP(uint32, cell.bg, cell.fg);
			}

			font_load_codepoint(
				fonts[cell.attrs & (ATTR_BOLD|ATTR_ITALIC)],
				cell.ucs4,
				&cmds[col].font,
				&cmds[col].glyph
			);
			cmds[col].bg = pack_xrgb(cell.bg, (cell.bg != term->color_bg) ? 0xff : 0);
			cmds[col].fg = pack_xrgb(cell.fg, 0xff);
		}

		if (len) {
			draw_text_utf8(client->win, cmds, len, border, border + row * term->rowsize);
		}
	}
#undef ROWBUF_MAX

	// draw cursor last
	client_draw_cursor(client);

	window_render(client->win);
}

void
client_draw_cursor(const Client *client)
{
	const WinClient *win = client->win;
	const Term *term = client->term;
	int width = client->width;
	int height = client->height;
	int border = client->border;

	Cursor cursor = { 0 };

	if (term_get_cursor(term, &cursor)) {
		Cell cell = term_get_cell(term, cursor.col, cursor.row);

		GlyphRender cmd = { 0 };

		font_load_codepoint(
			fonts[cell.attrs & (ATTR_BOLD|ATTR_ITALIC)],
			cell.ucs4,
			&cmd.font,
			&cmd.glyph
		);

		draw_rect(
			win,
			pack_xrgb(term->color_bg, 0xff),
			border + cursor.col * term->colsize,
			border + cursor.row * term->rowsize,
			term->colsize,
			term->rowsize
		);

		switch (cursor.style) {
		case CursorStyleDefault:
		case CursorStyleBar:
			cmd.bg = 0;
			cmd.fg = pack_xrgb(cell.fg, 0xff);
			draw_text_utf8(
				client->win, &cmd, 1,
				border + cursor.col * term->colsize,
				border + cursor.row * term->rowsize
			);
			draw_rect(
				client->win, pack_xrgb(cursor.color, 0xff),
				border + cursor.col * term->colsize,
				border + cursor.row * term->rowsize,
				2, term->rowsize
			);
			break;
		case CursorStyleBlock:
			cmd.bg = pack_xrgb(term->color_fg, 0xff);
			cmd.fg = pack_xrgb(term->color_bg, 0xff);
			draw_text_utf8(
				client->win, &cmd, 1,
				border + cursor.col * term->colsize,
				border + cursor.row * term->rowsize
			);
			break;
		case CursorStyleUnderscore:
			cmd.bg = 0;
			cmd.fg = pack_xrgb(cell.fg, 0xff);
			draw_text_utf8(
				client->win, &cmd, 1,
				border + cursor.col * term->colsize,
				border + cursor.row * term->rowsize
			);
			draw_rect(
				client->win, pack_xrgb(term->color_fg, 0xff),
				border + cursor.col * term->colsize,
				border + (cursor.row + 1) * term->rowsize - 2,
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
	Term *term = client->term;

	int cols = (width - 2 * client->border) / term->colsize;
	int rows = (height - 2 * client->border) / term->rowsize;

	/* win_resize_client(win, width, height); */
	if (cols != term->cols || rows != term->rows) {
		term_resize(term, cols, rows);
	}
	client->width = width;
	client->height = height;
}

