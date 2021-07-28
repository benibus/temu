#ifndef CONFIG_H__
#define CONFIG_H__

static Config config = {
	.wm_class = "Temu",
	.wm_instance = "win_floating",
	.wm_title = "temu",
	.geometry = NULL,
	.font = "monospace:size=16.0",
	.colors = {
		[0]  = "#07070A",
		[1]  = "#803A41",
		[2]  = "#38574D",
		[3]  = "#785C46",
		[4]  = "#3A4D5C",
		[5]  = "#5F495A",
		[6]  = "#406262",
		[7]  = "#989898",

		[8]  = "#4D4D4D",
		[9]  = "#93484F",
		[10] = "#3D6354",
		[11] = "#7A6E52",
		[12] = "#495E68",
		[13] = "#725463",
		[14] = "#537676",
		[15] = "#A4A4A4",
	},
	.shell = "/bin/dash",
	.tabstop = 8,
	.border_px = 0,
	.history_size = 120,
	.columns = 80,
	.rows = 24,
	.position = { .x = 0, .y = 0 },
	.latency = { .min = 8, .max = 33 }
};

#endif
