#ifndef WINDOW_H__
#define WINDOW_H__

#include "defs.h"

#include "keymap.h"

typedef enum {
	PixelFormatNone,
	PixelFormatA8,
	PixelFormatA1,
	PixelFormatRGB24,
	PixelFormatBGR24,
	PixelFormatRGBA32,
	PixelFormatARGB32,
	PixelFormatBGRA32,
	PixelFormatABGR32,
	PixelFormatHRGB24,
	PixelFormatHBGR24,
	PixelFormatVRGB24,
	PixelFormatVBGR24,
	PixelFormatUnknown,
	PixelFormatCount
} PixelFormat;

#define WINATTR_NONE      (0)
#define WINATTR_FLOATING  (1 << 0)
#define WINATTR_RESIZABLE (1 << 1)
#define WINATTR_OPENGL    (1 << 2)
#define WINATTR_DEFAULT   (1 << 3)

typedef struct {
	void *ref;
	pid_t pid;
	bool state;
	char *title, *instance, *class;
	int fd;
	int x, y;
	uint w, h;
	uint iw, ih;
	uint bw;
	uint minw, minh;
	uint maxw, maxh;
	uint32 flags;
	struct {
		void (*resize)(void *, int, int);
		void (*key_press)(void *, int, int, char *, int);
		void (*visible)(void *);
	} events;
} Win;

#define SETPTR(p,v) do { if (p) { *(p) = (v); } } while (0)

typedef struct {
	uint32 ucs4;
	uint32 font;
	uint32 fg;
	uint32 bg;
} GlyphRender;

Win *win_create_client(void);
bool win_init_client(Win *);
void win_show_client(Win *);
void win_resize_client(Win *, uint, uint);
int win_process_events(Win *, double);
void win_get_size(Win *, uint *, uint *);
void win_get_coords(Win *, int *, int *);
void win_render_frame(Win *);
int win_events_pending(Win *);
int64 win_get_color_handle(Win *, uint32);
bool win_parse_color_string(const Win *, const char *, uint32 *);

PixelFormat platform_get_pixel_format(const Win *);

u64 timer_current_ns(void);
double timer_elapsed_s(u64, u64 *);

void draw_rect(const Win *, uint32, int, int, int, int);
void draw_text_utf8(const Win *, const GlyphRender *, uint, int, int);

#endif
