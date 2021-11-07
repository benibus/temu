#ifndef FONTS_H__
#define FONTS_H__

#include "defs.h"

#define FONTSTYLE_REGULAR (0)
#define FONTSTYLE_BOLD    (1 << 0)
#define FONTSTYLE_ITALIC  (1 << 1)
#define FONTSTYLE_MASK    ((1 << 2)-1)

typedef enum {
	FontStyleRegular    = FONTSTYLE_REGULAR,
	FontStyleBold       = FONTSTYLE_BOLD,
	FontStyleItalic     = FONTSTYLE_ITALIC,
	FontStyleBoldItalic = FONTSTYLE_BOLD|FONTSTYLE_ITALIC,
	FontStyleCount
} FontStyle;

typedef struct FontSet_ FontSet;

typedef struct {
	uint32 idx;
	uint texid;
	float x;
	float y;
	float width;
	float height;
	float hbearing;
	float vbearing;
} Glyph;

bool fontmgr_init(double);
FontSet *fontmgr_create_fontset(const char *);
bool fontset_init(FontSet *);
Glyph *fontset_get_glyph(FontSet *, FontStyle, uint32);
bool fontset_get_metrics(const FontSet *, int *, int *, int *, int *);

#endif
