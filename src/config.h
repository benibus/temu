#ifndef CONFIG_H__
#define CONFIG_H__

static Config config = {
	.wm_class = "Temu",
	.wm_instance = "win_floating",
	.wm_title = "temu",
	.geometry = NULL,
	.font = "DejaVu Sans Mono:size=12.0",
	.colors = {
		"#0F0F13", // default background
		"#989898", // default foreground

		"#07070A", // user colors (0-16)
		"#803A41",
		"#38574D",
		"#785C46",
		"#3A4D5C",
		"#5F495A",
		"#406262",
		"#888888",

		"#4D4D4D",
		"#93484F",
		"#3D6354",
		"#7A6E52",
		"#495E68",
		"#725463",
		"#537676",
		"#A4A4A4"
	},
	.shell = "/bin/bash",
	.tablen = 8,
	.border_px = 0,
	.histsize = 120,
	.columns = 140,
	.rows = 40,
	.scrollinc = 1,
	.position = { .x = 0, .y = 0 },
	.latency = { .min = 8, .max = 33 }
};

#endif
