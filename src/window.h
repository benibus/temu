#ifndef WINDOW_H__
#define WINDOW_H__

#include "types.h"
#include "keymap.h"

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

#define STYLE_REGULAR (0)
#define STYLE_ITALIC  (1 << 0)
#define STYLE_OBLIQUE (1 << 1)
#define STYLE_BOLD    (1 << 2)
#define STYLE_MAX     (1 << 3)

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

struct Color32LE {
	uint64 id;
	uint64 pixel;
	// TODO(ben): Deep-dive into the C11 type-punning spec
	union {
		uint32 rgba;
		struct {
			uint8 a;
			uint8 b;
			uint8 g;
			uint8 r;
		};
	};
};

typedef struct Color32LE Color;

#define RGBA32_PACK(color) ( \
  ((color)->r << 24) | \
  ((color)->g << 16) | \
  ((color)->b << 8 ) | \
  ((color)->a << 0 )   \
)

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

#define RC_MODE_DEFAULT (0)
#define RC_MODE_FILL    (1 << 0)
#define RC_MODE_INVERT  (1 << 1)
#define RC_MODE_MAX     (1 << 2)

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
