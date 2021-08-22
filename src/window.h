#ifndef WINDOW_H__
#define WINDOW_H__

#include <X11/extensions/Xrender.h>

#include "types.h"
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
	void *ref;
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

enum {
	STYLE_REGULAR,
	STYLE_ITALIC  = (1 << 0),
	STYLE_OBLIQUE = (1 << 1),
	STYLE_BOLD    = (1 << 2),
	STYLE_MAX     = (1 << 3)
};

typedef struct FontFace_ FontFace;

typedef struct {
	bool embolden;
	bool transform;
	int width, height;
	int ascent, descent;
	int max_advance;
	struct { long  x, y; } size_px;
	struct { float x, y; } size_pt;
} FontMetrics;

typedef struct {
	Picture fill;
	ulong pixel;
	XRenderColor values;
} Color;

typedef struct {
	uint width, height;
	int pitch;
	struct { int x, y; } bearing;
	struct { int x, y; } advance;
} GlyphMetrics;

typedef struct {
	uint32 ucs4;
	FontFace *font;
	Color *foreground;
	Color *background;
	uint32 flags;
} GlyphRender;

enum {
	RC_MODE_DEFAULT,
	RC_MODE_FILL =   (1 << 0),
	RC_MODE_INVERT = (1 << 1),
	RC_MODE_MAX  =   (1 << 2)
};

typedef struct {
	Win *win;
	uint16 mode;
	FontFace *font;
	struct {
		Color *fg;
		Color *bg;
	} color;
} RC;

#define MAX_FONTS  2
#define MAX_COLORS 16
#define XW_API_X11 1

#define ID_NULL (-1)

Win *win_create_client(void);
bool win_init_client(Win *);
void win_show_client(Win *);
void win_resize_client(Win *, uint, uint);
int win_process_events(Win *, double);
void win_get_size(Win *, uint *, uint *);
void win_get_coords(Win *, int *, int *);
void win_render_frame(Win *);
bool win_init_render_context(Win *, RC *);
int win_events_pending(Win *);

u64 timer_current_ns(void);
double timer_elapsed_s(u64, u64 *);

FontFace *font_create_face(RC *, const char *);
FontFace *font_create_derived_face(FontFace *, uint);
bool font_get_face_metrics(FontFace *, FontMetrics *);
bool font_init_face(FontFace *);
void font_destroy_face(FontFace *);
Color *color_create_name(RC *, Color *, const char *);
void color_free_data(RC *, Color *);
void draw_rect_solid(const RC *, const Color *, int, int, int, int);
void draw_string8(const RC *, int, int, const char *, uint);
void draw_string16(const RC *, int, int, const uint16 *, uint);
void draw_string32(const RC *, int, int, const uint32 *, uint);
void draw_text_utf8(const RC *, const GlyphRender *, uint, int, int);

#endif
