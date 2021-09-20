#ifndef CONFIG_H__
#define CONFIG_H__

static Config config = {
	.wm_class = "Temu",
	.wm_instance = "win_floating",
	.wm_title = "temu",
	.geometry = NULL,
	.font = "DejaVu Sans Mono:size=12.0",
	.colors = {
		[0x0] = "#07070A",
		[0x1] = "#803A41",
		[0x2] = "#38574D",
		[0x3] = "#785C46",
		[0x4] = "#3A4D5C",
		[0x5] = "#5F495A",
		[0x6] = "#406262",
		[0x7] = "#A4A4A4",

		[0x8] = "#4D4D4D",
		[0x9] = "#93484F",
		[0xa] = "#3D6354",
		[0xb] = "#7A6E52",
		[0xc] = "#495E68",
		[0xd] = "#725463",
		[0xe] = "#537676",
		[0xf] = "#A4A4A4"
	},
	.shell = "/bin/dash",
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
