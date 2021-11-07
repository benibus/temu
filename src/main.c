#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <unistd.h>

#include "utils.h"
#include "terminal.h"
#include "window.h"
#include "fonts.h"
#include "renderer.h"

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
	Win *win;
	Term *term;
	int scrollinc;
	int width;
	int height;
	int borderpx;
	int cols;
	int rows;
	int colpx;
	int rowpx;
} Client;

static Client client_;

FontSet *fontset = NULL;
static struct TermConfig termcfg;

static void run(Client *);
static void event_keypress(void *, int, int, char *, int);
static void event_resize(void *, int, int);

int
main(int argc, char **argv)
{
	static_assert(FontStyleRegular == ATTR_NONE, "Bitmask mismatch.");
	static_assert(FontStyleBold == ATTR_BOLD, "Bitmask mismatch.");
	static_assert(FontStyleItalic == ATTR_ITALIC, "Bitmask mismatch.");
	static_assert(FontStyleBoldItalic == (ATTR_BOLD|ATTR_ITALIC), "Bitmask mismatch.");

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

	if (!server_setup()) {
		return 2;
	}

	if (!fontmgr_init(server_get_dpi())) {
		dbgprint("Failed to initialize font manager");
		return false;
	}
	if (!(fontset = fontmgr_create_fontset(config.font))) {
		dbgprint("Failed to instantiate fonts");
		return false;
	}

	{
		int width, height, ascent, descent;

		fontset_get_metrics(fontset, &width, &height, &ascent, &descent);
		termcfg.colsize = width;
		termcfg.rowsize = ascent + descent;
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
			dbgprintf("Failed to parse RGB string: %s\n", config.colors[i]);
			exit(EXIT_FAILURE);
		}
	}

	Win *win = server_create_window(
		(struct WinConfig){
			.param = &client_,
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
				.resize   = event_resize,
				.keypress = event_keypress,
				.expose   = NULL
			}
		}
	);

	if (!win || !window_show(win)) {
		exit(EXIT_FAILURE);
	}
	window_get_dimensions(win, &client_.width, &client_.height, &client_.borderpx);

	if (!fontset_init(fontset)) {
		dbgprint("Failed to initialize FontSet");
		exit(EXIT_FAILURE);
	}

	client_.colpx  = termcfg.colsize;
	client_.rowpx  = termcfg.rowsize;
	client_.cols   = (client_.width - 2 * client_.borderpx) / client_.colpx;
	client_.rows   = (client_.height - 2 * client_.borderpx) / client_.rowpx;
	ASSERT(client_.cols == (int)config.columns);
	ASSERT(client_.rows == (int)config.rows);

	if (!renderer_init()) {
		dbgprint("Failed to initialize renderer");
		exit(EXIT_FAILURE);
	}
	renderer_set_dimensions(
		client_.width, client_.height,
		client_.cols, client_.rows,
		client_.colpx, client_.rowpx,
		client_.borderpx
	);

	termcfg.cols = client_.cols;
	termcfg.rows = client_.rows;
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

	window_make_current(client->win);

	if (!window_online(client->win)) {
		return;
	}

	const int srvfd = server_get_fileno();
	const int ptyfd = term_exec(term, config.shell);

	do {
		renderer_draw_frame(term_generate_frame(term));
		window_update(client->win);
	} while (window_poll_events(client->win));

	struct pollfd pollset[] = {
		{ .fd = ptyfd, .events = POLLIN, .revents = 0 },
		{ .fd = srvfd, .events = POLLIN, .revents = 0 }
	};

	static_assert(LEN(pollset) == 2, "Unexpected pollset size");

	// Target polling rate
	static const uint32 msec = 16;

	while (window_online(client->win)) {
		const uint32 basetime = time_get_mono_msec(NULL);
		bool draw = true;

		switch (poll(pollset, LEN(pollset), msec)) {
		case -1:
			if (!errno) {
				return;
			}
			perror("poll()");
			exit(errno);
		case 0:
			if (!window_poll_events(client->win)) {
				draw = false;
			}
			break;
		case LEN(pollset) - 1:
			if (pollset[0].revents & POLLIN) {
				const int timeout = msec - (time_get_mono_msec(NULL) - basetime);
				term_pull(term, MAX(timeout, 0));
			} else {
				if (!window_poll_events(client->win)) {
					draw = false;
				}
			}
			break;
		case LEN(pollset):
			break;
		}

		if (draw) {
			renderer_draw_frame(term_generate_frame(client->term));
			window_update(client->win);
		}
	}
}

void
event_resize(void *param, int width, int height)
{
	Client *client = param;

	if (width == client->width && height == client->height) {
		return;
	}

	const int cols = (width - 2 * client->borderpx) / client->colpx;
	const int rows = (height - 2 * client->borderpx) / client->rowpx;

	if (cols != client->term->cols || rows != client->term->rows) {
		term_resize(client->term, cols, rows);
	}

	renderer_set_dimensions(
		width, height,
		cols, rows,
		client->colpx, client->rowpx,
		client->borderpx
	);

	client->width  = width;
	client->height = height;
	client->cols   = cols;
	client->rows   = rows;
}

void
event_keypress(void *param, int key, int mod, char *buf, int len)
{
	Client *client = param;
	Term *term = client->term;

	char seq[64];
	int seqlen = 0;

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

	if ((seqlen = term_make_key_string(term, key, mod, seq, LEN(seq)))) {
		term_push(term, seq, seqlen);
	} else if (len == 1) {
		term_reset_scroll(term);
		term_push(term, buf, len);
	}
}

