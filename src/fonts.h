#ifndef FONTS_H__
#define FONTS_H__

#include "defs.h"
#include "window.h"

#define STYLE_REGULAR (0)
#define STYLE_BOLD    (1 << 0)
#define STYLE_ITALIC  (1 << 1)
#define STYLE_OBLIQUE (1 << 2)
#define STYLE_MAX     (1 << 3)

typedef uint32 FontID;
typedef uint64 GlyphID;

FontID font_create(const Win *, const char *);
FontID font_create_derivative(FontID, uint);
void font_destroy(FontID);
bool font_get_extents(FontID, int *, int *, int *, int *);
void *font_get_render_data(FontID);
bool font_init(FontID);
bool font_query_glyph(FontID, uint32, FontID *, uint32 *);
bool font_load_codepoint(FontID, uint32, FontID *, uint32 *);
void font_print_debug(FontID);

#endif
