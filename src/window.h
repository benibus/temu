#ifndef WINDOW_H__
#define WINDOW_H__

#include "defs.h"

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

typedef uint64 ColorID;

typedef struct { uint8 r, g, b;    } RGB;
typedef struct { uint8 r, g, b, a; } RGBA;

typedef uint32 RGB32;
typedef uint32 RGBA32;

#define PACK_RGB(r,g,b)    (((r)<<16)|((g)<<8)|((b)<<0))
#define PACK_RGBA(r,g,b,a) (((r)<<24)|((g)<<16)|((b)<<8)|((a)<<0))

typedef struct {
	ColorID id;
	struct {
		uint8 r;
		uint8 g;
		uint8 b;
		uint8 a;
	};
} Color;

typedef struct {
	uint32 ucs4;
	FontFace *font;
	Color *foreground;
	Color *background;
	uint32 flags;
} GlyphRender;

typedef struct {
	Win *win;
	uint16 mode;
	FontFace *font;
	struct {
		Color *fg;
		Color *bg;
	} color;
} RC;

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
ColorID win_alloc_color(const RC *, RGBA);
void win_free_color(const RC *, ColorID);
bool win_parse_color_string(const RC *, const char *, RGBA *rgba);

u64 timer_current_ns(void);
double timer_elapsed_s(u64, u64 *);

FontFace *font_create_face(RC *, const char *);
FontFace *font_create_derived_face(FontFace *, uint);
bool font_get_face_metrics(FontFace *, FontMetrics *);
bool font_init_face(FontFace *);
void font_destroy_face(FontFace *);
void draw_rect_solid(const RC *, const Color *, int, int, int, int);
void draw_text_utf8(const RC *, const GlyphRender *, uint, int, int);

#endif
