#ifndef RENDER_H__
#define RENDER_H__

#include "defs.h"
#include "window.h"
#include "fonts.h"

typedef struct {
	PixelFormat format;
	int width, height;
	int pitch;
	int x_advance, y_advance;
	int x_bearing, y_bearing;
} GlyphInfo;

typedef struct {
	uint32 style;
	uint32 ucs4;
	uint32 fg;
	uint32 bg;
} GlyphRender;

typedef struct GlyphCache GlyphCache;

void *glyphcache_create(int);
void glyphcache_add_glyph(void *, uint32, const Bitmap *);
void glyphcache_destroy(void *);

void draw_rect(const WinClient *, uint32, int, int, int, int);
void draw_text_utf8(const WinClient *, FontSet *, const GlyphRender *, uint, int, int);

#endif

