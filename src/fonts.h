#ifndef FONTS_H__
#define FONTS_H__

#include "defs.h"

#define STYLE_REGULAR (0)
#define STYLE_BOLD    (1 << 0)
#define STYLE_ITALIC  (1 << 1)
#define STYLE_OBLIQUE (1 << 2)
#define STYLE_MAX     (1 << 3)

typedef struct {
	int width;
	int height;
	int pitch;
	int x_advance;
	int y_advance;
	int x_bearing;
	int y_bearing;
	int ascent;
	int descent;
	void *data;
} Bitmap;

typedef struct FontSet FontSet;
typedef struct FontData FontData;

typedef struct {
	FontData *font;
	uint32 glyph;
} FontGlyph;

typedef void *(*FontHookCreate)(int depth);
typedef void (*FontHookAdd)(void *generic, uint32 glyph, const Bitmap *);
typedef void (*FontHookDestroy)(void *generic);

bool fontmgr_configure(double, FontHookCreate, FontHookAdd, FontHookDestroy);
FontSet *fontmgr_create_fontset(const char *);
FontGlyph fontset_get_codepoint(FontSet *, uint, uint32);
bool fontset_get_metrics(const FontSet *, int *, int *, int *, int *);
void *font_get_generic(const FontData *);

#endif
