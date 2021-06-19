#ifndef CONFIG_H__
#define CONFIG_H__

static Config cfg = {
	.wm_class = "Temu",
	.wm_name = "win_floating",
	.wm_title = "temu",
	.geometry = NULL,
	.font = "monospace:size=9",
	.colors = {
		[COLOR_BG] = "black",
		[COLOR_FG] = "white"
	},
	.shell = "/bin/dash",
	.tabstop = 8,
	.border_px = 0,
	.history_size = 120,
	.columns = 120,
	.rows = 40,
	.position = { .x = 0, .y = 0 },
	.latency = { .min = 8, .max = 33 }
};

#define CFG(var_) CFG_##var_

// number constants
#define CFG_TABSTOP     8
#define CFG_NUM_COLS    80
#define CFG_NUM_ROWS    24
#define CFG_BORDER_PX   0
#define CFG_MAX_HIST    120
#define CFG_MIN_LATENCY 8
#define CFG_MAX_LATENCY 33
#define CFG_RGB_BG      0,0,0
#define CFG_RGB_FG      255,255,255
#define CFG_RGB_BACKBUF 255,0,255

// string constants
#define CFG_FONT_NAME "monospace:size=9"
#define CFG_WM_NAME   "win_floating"
#define CFG_WM_CLASS  "Temu"
#define CFG_WM_TITLE  "temu"

// booleans
#define CFG_LOG_EVENTS   0
#define CFG_LOG_KEYPRESS 0
#define CFG_LOG_DRAWING  0

#endif
