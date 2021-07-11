#ifndef WINDOW_H__
#define WINDOW_H__

#include "keymap.h"

enum {
	WINATTR_NONE,
	WINATTR_FLOATING  = (1 << 0),
	WINATTR_RESIZABLE = (1 << 1),
	WINATTR_OPENGL    = (1 << 2),
	WINATTR_DEFAULT   = (1 << 3),
	WINATTR_MAX
};

typedef struct {
	bool state;
	char *title, *instance, *class;
	int x, y;
	uint w, h;
	uint iw, ih;
	uint bw;
	uint minw, minh;
	uint maxw, maxh;
	u32 flags;
	struct {
		void (*resize)(int, int);
		void (*key_press)(int, int, char *, int);
		void (*visible)(void);
	} events;
} Win;

enum {
	FACE_REGULAR,
	FACE_ITALIC,
	FACE_BOLD_ITALIC,
	FACE_BOLD,
	NUM_FACE
};

typedef struct RC_ RC;

#define MAX_FONTS  2
#define MAX_COLORS 16
#define XW_API_X11 1

#define ID_NULL (-1)

Win *ws_init_window(void);
bool ws_create_window(Win *);
double ws_process_events(Win *, double);
void ws_get_win_size(Win *, uint *, uint *);
void ws_get_win_pos(Win *, int *, int *);
void ws_show_window(Win *);
void ws_swap_buffers(Win *);

RC *wsr_init_context(Win *);
bool wsr_set_font(RC *, int, int);
int wsr_load_font(Win *, const char *);
bool wsr_get_avg_font_size(int, int, int *, int *);
bool wsr_set_colors(RC *, int, int);
int wsr_load_color_name(RC *, const char *);
void wsr_fill_region(RC *, uint, uint, uint, uint, uint, uint);
void wsr_clear_region(RC *, uint, uint, uint, uint, uint, uint);
void wsr_clear_screen(RC *);
void wsr_draw_string(RC *, const char *, uint, uint, uint, bool);

u64 timer_current_ns(void);
double timer_elapsed_s(u64, u64 *);

#endif
