#ifndef RENDER_H__
#define RENDER_H__

#include "defs.h"
#include "window.h"

typedef struct {
	PixelFormat format;
	int width, height;
	int pitch;
	int x_advance, y_advance;
	int x_bearing, y_bearing;
} GlyphInfo;

typedef struct GlyphCache GlyphCache;

struct GlyphCache *glyphcache_create(int, PixelFormat, int, int);
void glyphcache_destroy(struct GlyphCache *);
GlyphInfo *glyphcache_submit_bitmap(struct GlyphCache *, uint32, const uchar *, GlyphInfo);

#endif

