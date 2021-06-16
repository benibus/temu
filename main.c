#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

#include "term.h"
#include "x.h"

enum {
	COLOR_A0, COLOR_A1, COLOR_A2, COLOR_A3, COLOR_A4, COLOR_A5, COLOR_A6, COLOR_A7,
	COLOR_B0, COLOR_B1, COLOR_B2, COLOR_B3, COLOR_B4, COLOR_B5, COLOR_B6, COLOR_B7,
	COLOR_BG = 256,
	COLOR_FG = 257,

	NUM_COLOR
};

typedef struct Config {
	char *wm_class;
	char *wm_name;
	char *wm_title;
	char *geometry;
	char *font;
	char *colors[NUM_COLOR];
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

static struct WinConfig config = {
	.x = 0,
	.y = 0,
	.cols = CFG_NUM_COLS,
	.rows = CFG_NUM_ROWS,
	.borderpx = CFG_BORDER_PX,
	.histsize = CFG_MAX_HIST,
	.fontstr = "monospace:size=9",
	.wmtitle = "temu",
	.wmname  = "win_floating",
	.wmclass = "Temu"
};

int histsize = CFG_MAX_HIST;
int tabstop = CFG_TABSTOP;
double min_latency = CFG_MIN_LATENCY;
double max_latency = CFG_MAX_LATENCY;
u32 log_flags = 0;

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
		case 'T': config.wmtitle = optarg; break;
		case 'N': config.wmname  = optarg; break;
		case 'C': config.wmclass = optarg; break;
		case 'f': config.fontstr = optarg; break;
		case 'c':
			arg.n = strtol(optarg, &errp, 10);
			if (!*errp) {
				if (arg.n > 0) {
					config.cols = arg.n;
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
			if (!*errp) config.x = arg.n;
			break;
		case 'y':
			arg.n = strtol(optarg, &errp, 10);
			if (!*errp) config.y = arg.n;
			break;
		case 'b':
			arg.n = strtol(optarg, &errp, 10);
			if (!*errp) config.borderpx = arg.n;
			break;
		case 'm':
			arg.n = strtol(optarg, &errp, 10);
			if (arg.n > 0 && !*errp)
				config.histsize = arg.n;
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

	histsize = (config.rows > config.histsize) ? config.rows : config.histsize;

	log_flags |= (CFG_LOG_EVENTS)   ? LOG_EVENTS   : 0;
	log_flags |= (CFG_LOG_KEYPRESS) ? LOG_KEYPRESS : 0;
	log_flags |= (CFG_LOG_DRAWING)  ? LOG_DRAWING  : 0;

	x_configure(config);

	if (!x_init_session())
		exit(1);
	/* if (!x_init_window()) */
	/* 	exit(2); */

	/* dbg__test_draw(); */
#if 1
	run();
#endif
	return 0;
}

