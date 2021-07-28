#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <locale.h>
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_LCD_FILTER_H
#include FT_SYNTHESIS_H
#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>

#include "utils.h"
#include "x11.h"

#define SUBPX 64L
#define SUBPX_MASK (~(SUBPX-1))
#define PX2SUBPX(n) ((n) * SUBPX)
#define SUBPX2PX(n) ((n) / SUBPX)
#define PX_FLOOR(n) ((n) & SUBPX_MASK)
#define PX_CEIL(n)  (((n)+(SUBPX-1)) & SUBPX_MASK)
#define PX_TRUNC(n) ((n) >> 6)
#define PX_ROUND_UP(n)   (((n)+(SUBPX/2)) & SUBPX_MASK)
#define PX_ROUND_DOWN(n) (((n)-(SUBPX/2)) & SUBPX_MASK)
#define PIXELS(subpx) PX_TRUNC(subpx)

struct SLLNode_;
struct DLLNode_;
typedef struct SLLNode_ {
	struct SLLNode_ *next;
} SLLNode;
typedef struct DLLNode_ {
	struct DLLNode_ *prev, *next;
} DLLNode;

typedef Glyph XGlyph;
typedef GlyphSet XGlyphSet;
#define Font FontFace

typedef struct FontSource_ FontSource;
typedef struct GlyphInfo_ GlyphInfo;
typedef struct GlyphTable_ GlyphTable;

struct GlyphInfo_ {
	uint32 ucs4;
	uint32 hash;
	ulong index;
	GlyphMetrics metrics;
};

struct GlyphTable_ {
	GlyphInfo *items;
	uint32 count, max;
};

struct FontSource_ {
	SLLNode node;
	Display *dpy;
	char *file;
	int index;
	FT_Face face;
	FT_Matrix matrix;
	struct { long x, y; } size_px;
	int num_glyphs;
	int refcount;
	bool locked;
};

struct FontFace_ {
	SLLNode node;
	FontSource *src;
	FT_Matrix matrix;
	FT_Int loadflags;
	FT_Render_Mode mode;
	XGlyphSet glyphset;
	XRenderPictFormat *picfmt;
	GlyphInfo *glyphs;
	GlyphTable table;
	FcPattern *pattern;
	FcCharSet *charset;
	int num_codepoints;
	struct { long x, y; } size_px;
	struct { float x, y; } size_pt;
	bool antialias, bold, transform, has_color;
	int subpx_order, lcd_filter, spacing;
	int fixed_width;
	int width, height;
	int ascent, descent;
	int max_advance;
};

static struct {
	FT_Library ft;
	FontFace *fonts;
	FontSource *font_sources;
} shared;

static const char ascii_[] =
    " !\"#$%&'()*+,-./0123456789:;<=>?"
    "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
    "`abcdefghijklmnopqrstuvwxyz{|}~";

static uint32 hash32(uint32);
static uint32 font_load_glyph(FontFace *, uint32);
static void font_load_glyphs(FontFace *);
static void font_free_glyphs(FontFace *);
static GlyphInfo *font_add_glyph(FontFace *, uint32, uint32);
static void font_destroy_source(FontSource *);

static GlyphMetrics *glyph_set_metrics(GlyphMetrics *, const FontFace *, const FT_GlyphSlotRec *);
static uint glyph_write_image(GlyphMetrics, FT_Bitmap, uchar **, uint);
static GlyphTable *glyph_table_init(FontFace *);
static uint32 glyph_get_ucs4(FontFace *, uint32);
static uint32 glyph_lookup_ucs4(FontFace *, uint32);
static GlyphInfo *glyph_insert(FontFace *, uint32, GlyphInfo *);

static void dbg_print_glyph_bitmap(const uchar *, GlyphMetrics, uint);

uint32
hash32(uint32 n)
{
	n = (n + 0x7ed55d16) + (n << 12);
	n = (n ^ 0xc761c23c) ^ (n >> 19);
	n = (n + 0x165667b1) + (n <<  5);
	n = (n + 0xd3a2646c) ^ (n <<  9);
	n = (n + 0xfd7046c5) + (n <<  3);
	n = (n ^ 0xb55a4f09) ^ (n >> 16);

	return n;
}

uint
glyph_write_image(GlyphMetrics metrics, FT_Bitmap src, uchar **buf, uint max)
{
	if (!buf) return 0;

	uint size = metrics.pitch * metrics.height;
	uchar *img = *buf;

	if (!img || size > max) {
		img = malloc(size);
	}
	memset(img, 0, size);

	uchar *p1 = src.buffer;
	uchar *p2 = img;

	for (uint y = 0; y < metrics.height; y++) {
#if 0
		memcpy(p2, p1, glyph.width);
#else
		for (uint x = 0; x < metrics.width; x++) {
			float c = p1[x];
			p2[x] = (uint8)(powf(c / 255.0f, 0.75f) * 255.0f);
			/* p2[x] = (uint8)(sqrtf(c / 255.0f) * 255.0f); */
		}
#endif
		p1 += src.pitch;
		p2 += metrics.pitch;
	}

	*buf = img;
	return size;
}

#if 1
GlyphTable *
glyph_table_init(FontFace *font)
{
	assert(font && font->num_codepoints);
	GlyphTable *table = &font->table;

	assert(font->num_codepoints >= 128 - 32);
	assert(font->num_codepoints < 0x10ffff);
	memset(table, 0, sizeof(*table));
	table->max = MIN(font->num_codepoints / 2 * 3, 0x10ffff);
	table->items = calloc(table->max, sizeof(*table->items));

	return table;
}

uint32
glyph_get_ucs4(FontFace *font, uint32 addr)
{
	GlyphTable *table = &font->table;

	if (addr < table->max) {
		return table->items[addr].ucs4;
	}

	return 0;
}

GlyphMetrics
glyph_get_metrics(FontFace *font, uint32 addr)
{
	GlyphTable *table = &font->table;

	if (addr < table->max) {
		return table->items[addr].metrics;
	}

	return (GlyphMetrics){ 0 };
}

ulong
glyph_get_index(FontFace *font, uint32 addr)
{
	GlyphTable *table = &font->table;

	if (addr < table->max) {
		return table->items[addr].index;
	}

	return 0;
}

GlyphInfo *
glyph_get_data(FontFace *font, uint32 addr)
{
	GlyphTable *table = &font->table;

	if (addr < table->max) {
		return table->items + addr;
	}

	return NULL;
}

uint32
glyph_lookup_ucs4(FontFace *font, uint32 ucs4)
{
	if (!ucs4) return 0;

	GlyphTable *table = &font->table;

	uint32 hash = hash32(ucs4);
	if (!hash) return 0;

	uint32 start = (hash % table->max);
	uint32 addr = start;
	uint32 result = 0;

	do {
		GlyphInfo *data = &table->items[addr];
		if (!data->ucs4 || ucs4 == data->ucs4) {
			assert(data->ucs4 || (!data->ucs4 && !data->index));
			result = addr;
			break;
		}
		if (++addr == table->max) {
			addr = 1;
		}
	} while (addr != start);

	return result;
}

GlyphInfo *
glyph_insert(FontFace *font, uint32 addr, GlyphInfo *src)
{
	GlyphTable *table = &font->table;
	GlyphInfo *dst = &table->items[addr];

	table->count += !dst->ucs4;
	memcpy(dst, src, sizeof(*dst));

	return dst;
}
#endif

bool
font_get_face_metrics(FontFace *font, FontMetrics *ret)
{
	if (font && ret) {
		memset(ret, 0, sizeof(*ret));

		ret->embolden  = !!font->bold;
		ret->transform = !!font->transform;

		ret->width       = font->width;
		ret->height      = font->height;
		ret->ascent      = font->ascent;
		ret->descent     = font->descent;
		ret->max_advance = font->max_advance;

		ret->size_px.x = font->size_px.x;
		ret->size_px.y = font->size_px.y;
		ret->size_pt.x = font->size_pt.x;
		ret->size_pt.y = font->size_pt.y;

		return true;
	}

	return false;
}

void
font_destroy_source(FontSource *target)
{
	SLLNode **p = (SLLNode **)&shared.font_sources;

	while (*p && *p != (SLLNode *)target) {
		p = &(*p)->next;
	}
	if (*p) {
		FontSource *obj = (FontSource *)*p;
		*p = (*p)->next;

		FT_Done_Face(obj->face);
		free(obj->file);
		free(obj);
	}
}

void
font_destroy_face(FontFace *target)
{
	SLLNode **p = (SLLNode **)&shared.fonts;

	while (*p && *p != (SLLNode *)target) {
		p = &(*p)->next;
	}
	if (*p) {
		FontFace *obj = (FontFace *)*p;
		*p = (*p)->next;

		FcPatternDestroy(obj->pattern);
		FcCharSetDestroy(obj->charset);
		XRenderFreeGlyphSet(obj->src->dpy, obj->glyphset);
		if (!--obj->src->refcount) {
			font_destroy_source(obj->src);
		}
		free(obj->glyphs);
		free(obj);
	}
}

void
font_free_glyphs(FontFace *target)
{
	if (target) {
		FcCharSetDestroy(target->charset);
		XRenderFreeGlyphSet(target->src->dpy, target->glyphset);
		free(target->glyphs);
		target->num_codepoints = 0;
		target->glyphs = NULL;
	}
}

FontSource *
font_insert_source(FcPattern *pattern)
{
	SLLNode **p;
	FontSource *obj;
	FcChar8 *file;
	int index;

	FcPatternGetString(pattern, FC_FILE, 0, &file);
	FcPatternGetInteger(pattern, FC_INDEX, 0, &index);

	for (p = (SLLNode **)&shared.font_sources; *p; p = &(*p)->next) {
		obj = (FontSource *)*p;
		// As far as I can tell, this is the canonical way to check for uniqueness
		if (!strcmp((char *)file, obj->file) && index == obj->index) {
			assert(obj->refcount && obj->file);
			obj->refcount++;
			return obj;
		}
	}

	assert(!*p);
	*p = calloc(1, sizeof(*obj));
	obj = (FontSource *)*p;
	obj->file = strdup((char *)file);
	obj->index = index;
	obj->refcount++;

	return obj;
}

FontFace *
font_insert_face(FcPattern *pattern)
{
	SLLNode **p;
	FontFace *obj;

	for (p = (SLLNode **)&shared.fonts; *p; p = &(*p)->next) {
		obj = (FontFace *)*p;
		if (FcPatternEqual(pattern, obj->pattern)) {
			FcPatternDestroy(pattern);
			return obj;
		}
	}

	assert(!*p);
	*p = calloc(1, sizeof(*obj));
	obj = (FontFace *)*p;
	obj->src = font_insert_source(pattern);
	obj->pattern = pattern;

	return obj;
}

FontFace *
font_create_face(RC *rc, const char *name)
{
	WinData *win = (WinData *)rc->win;
	assert(win);

	struct { FcPattern *base, *conf, *match; } pattern;
	FcResult res;
	FcValue dummy;

	if (!(pattern.base = FcNameParse((FcChar8 *)name))) {
		return NULL;
	}
	pattern.conf = FcPatternDuplicate(pattern.base);
	assert(pattern.conf);
	FcConfigSubstitute(NULL, pattern.conf, FcMatchPattern);

#define ADDVAL(T_,obj,val) \
if (FcPatternGet(pattern.conf, (obj), 0, &dummy) == FcResultNoMatch) { \
	FcPatternAdd##T_(pattern.conf, (obj), (val));                  \
}
	ADDVAL(Bool,    FC_ANTIALIAS,  FcTrue);
	ADDVAL(Bool,    FC_EMBOLDEN,   FcFalse);
	ADDVAL(Bool,    FC_HINTING,    FcTrue);
	ADDVAL(Integer, FC_HINT_STYLE, FC_HINT_FULL);
	ADDVAL(Bool,    FC_AUTOHINT,   FcFalse);
	ADDVAL(Integer, FC_LCD_FILTER, FC_LCD_DEFAULT);
	ADDVAL(Bool,    FC_MINSPACE,   False);
	ADDVAL(Double,  FC_DPI,        win->x11->dpi);
	ADDVAL(Integer, FC_SPACING,    FC_MONO);
	ADDVAL(Bool,    FC_VERTICAL_LAYOUT, FcFalse);

	if (FcPatternGet(pattern.conf, FC_RGBA, 0, &dummy) == FcResultNoMatch) {
		int subpx = FC_RGBA_UNKNOWN;
		int order = XRenderQuerySubpixelOrder(win->x11->dpy, win->x11->screen);
		switch (order) {
			case SubPixelNone:          subpx = FC_RGBA_NONE; break;
			case SubPixelHorizontalRGB: subpx = FC_RGBA_RGB;  break;
			case SubPixelHorizontalBGR: subpx = FC_RGBA_BGR;  break;
			case SubPixelVerticalRGB:   subpx = FC_RGBA_VRGB; break;
			case SubPixelVerticalBGR:   subpx = FC_RGBA_VBGR; break;
		}
		FcPatternAddInteger(pattern.conf, FC_RGBA, subpx);
	}
#undef ADDVAL
	FcDefaultSubstitute(pattern.conf);
	pattern.match = FcFontMatch(NULL, pattern.conf, &res);

	FcPatternDestroy(pattern.base);
	FcPatternDestroy(pattern.conf);
	FcPatternPrint(pattern.match);

	FontFace *font = font_insert_face(pattern.match);
	if (font && !font->src->dpy) {
		font->src->dpy = win->x11->dpy;
	}

	return font;
}

FontFace *
font_create_derived_face(FontFace *font, uint style)
{
	struct { FcPattern *conf, *match; } pattern;
	FcValue dummy;
	FcResult res;

	pattern.conf = FcPatternDuplicate(font->pattern);
	assert(pattern.conf);
	FcConfigSubstitute(NULL, pattern.conf, FcMatchPattern);

	if (style & (STYLE_ITALIC|STYLE_OBLIQUE)) {
		FcPatternDel(pattern.conf, FC_SLANT);
		FcPatternAddInteger(pattern.conf,
		    FC_SLANT, (style & STYLE_OBLIQUE) ? FC_SLANT_OBLIQUE : FC_SLANT_ITALIC);
	}
	if (style & STYLE_BOLD) {
		FcPatternDel(pattern.conf, FC_WEIGHT);
		FcPatternAddInteger(pattern.conf, FC_WEIGHT, FC_WEIGHT_BOLD);
	}

	FcDefaultSubstitute(pattern.conf);
	pattern.match = FcFontMatch(NULL, pattern.conf, &res);
	FcPatternDestroy(pattern.conf);
	FcPatternPrint(pattern.match);

	FontFace *new_font = font_insert_face(pattern.match);

	return new_font;
}

Color *
color_create_name(RC *rc, Color *output, const char *name)
{
	WinData *win = (WinData *)rc->win;
	XColor screen, channels;
	Color *color = output;

	if (XAllocNamedColor(win->x11->dpy, win->colormap, name, &screen, &channels)) {
		if (!color) {
			color = calloc(1, sizeof(*color));
		}
		color->pixel = screen.pixel;
		color->values.red   = channels.red;
		color->values.green = channels.green;
		color->values.blue  = channels.blue;
		color->values.alpha = 0xffff;

		color->fill = XRenderCreateSolidFill(win->x11->dpy, &color->values);

		return color;
	}

	return NULL;
}

void
color_free_data(RC *rc, Color *color)
{
	WinData *win = (WinData *)rc->win;
	assert(color);

	if (win->x11->vis->visual->class != TrueColor) {
		XFreeColors(win->x11->dpy, win->colormap, &color->pixel, 1, 0);
	}
	XRenderFreePicture(win->x11->dpy, color->fill);
	color->fill = 0;
}

void
draw_rect_solid(const RC *rc, const Color *color_, int x_, int y_, int w_, int h_)
{
	WinData *win = (WinData *)rc->win;

	int x = CLAMP(x_, 0, (int)win->pub.w);
	int y = CLAMP(y_, 0, (int)win->pub.h);
	int w = CLAMP(x + w_, x, (int)win->pub.w) - x;
	int h = CLAMP(y + h_, y, (int)win->pub.h) - y;

	const Color *color = (color_) ? color_ : rc->color.fg;
	if (!color) return;
	XRenderFillRectangle(win->x11->dpy,
	                     PictOpOver,
	                     win->pic,
	                     &color->values,
	                     x, y, w, h);
}

void
draw_string(const RC *rc, int x, int y, const void *str, uint len, uint width)
{
	WinData *win = (WinData *)rc->win;
	struct { Color *fg, *bg; } color;

	if (rc->mode & RC_MODE_INVERT) {
		color.fg = rc->color.bg;
		color.bg = rc->color.fg;
	} else {
		color.fg = rc->color.fg;
		color.bg = rc->color.bg;
	}
	if (!color.fg) return;
	if (rc->mode & RC_MODE_FILL) {
		draw_rect_solid(rc,
		                color.bg,
		                x, y,
		                rc->font->width * len,
		                rc->font->ascent + rc->font->descent);
	}
#define RENDER_STRING(T_,sz) \
XRenderCompositeString##sz(win->x11->dpy,                        \
                           PictOpOver, color.fg->fill, win->pic, \
                           rc->font->picfmt, rc->font->glyphset, \
                           0, 0, x, y + rc->font->ascent,        \
                           (const T_ *)str, len)
	switch (width) {
		case 1: RENDER_STRING(char, 8); break;
		case 2: RENDER_STRING(uint16, 16); break;
		case 4: RENDER_STRING(uint32, 32); break;
	}
#undef RENDER_STRING
}

void
draw_string8(const RC *rc, int x, int y, const char *str, uint len)
{
	draw_string(rc, x, y, str, len, sizeof(*str));
}

void
draw_string16(const RC *rc, int x, int y, const uint16 *str, uint len)
{
	draw_string(rc, x, y, str, len, sizeof(*str));
}

void
draw_string32(const RC *rc, int x, int y, const uint32 *str, uint len)
{
	draw_string(rc, x, y, str, len, sizeof(*str));
}

void
draw_text_utf8(const RC *rc, const GlyphRender *glyphs, uint max, int x, int y)
{
	assert(rc);
	WinData *win = (WinData *)rc->win;

	if (!max || !glyphs) return;

	struct {
		FontFace *font;
		Color *foreground;
		Color *background;
	} brush = { 0 };
	struct {
		int x0, y0, x1, y1;
		int dx, dy;
		int kx, ky;
	} pos = { 0 };
	struct { uint32 buf[2048]; uint n; } text = { 0 };
	struct { XGlyphElt32 buf[32]; uint n; } elts = { 0 };

	brush.foreground = glyphs[0].foreground;
	brush.background = glyphs[0].background;
	brush.font = glyphs[0].font;

	pos.x1 = pos.x0 = x;
	pos.y1 = pos.y0 = y;
	pos.dx = pos.x1 - pos.x0;
	pos.dy = pos.y1 - pos.y0;

	bool flushed = true;

	for (uint i = 0; i < max; i++) {
		uint32 ucs4 = glyphs[i].ucs4;

		uint32 entry = font_load_glyph(brush.font, ucs4);
		/* uint32 index = glyph_get_index(brush.font, entry); */
		GlyphMetrics metrics = glyph_get_metrics(brush.font, entry);

		int chdx = metrics.advance.x;
		int chdy = metrics.advance.y;
		int adjx = 0;
		int adjy = 0;

		// We keep accumulating elts until the brush changes, then "flush".
		// Glyphs with different metrics can be drawn in bulk, but
		// using a different font/color requires a separate draw call
		if (flushed || pos.dx != chdx || pos.dy != chdy) {
			// get next elt if new metrics
			elts.n += !flushed;
			elts.buf[elts.n].chars = &text.buf[text.n];
			pos.x0 = pos.x1;
			pos.y0 = pos.y1;
			pos.kx = 0;
			pos.ky = brush.font->ascent;
		}
		// Elt positions have to specified relative to the previous elt in the array.
		// This is why only the first elt (per draw call) receives the offset from
		// the drawing orgin
		if (pos.x1 != pos.x0 || pos.y1 != pos.y0) {
			adjx = chdx - pos.dx;
			adjy = chdy - pos.dy;
		}

		elts.buf[elts.n].xOff = pos.x0 + pos.kx + adjx;
		elts.buf[elts.n].yOff = pos.y0 + pos.ky + adjy;
		elts.buf[elts.n].glyphset = brush.font->glyphset;
		elts.buf[elts.n].nchars++;
		text.buf[text.n++] = ucs4;

		flushed = false;
		// accumulate position so we can set the new leading elt after drawing
		pos.x1 += (pos.dx = chdx);
		pos.y1 += (pos.dy = chdy);

		// If none of the local buffers are at capacity, keep going.
		// Otherwise, just draw now and reset everything at the new orgin
		if (i + 1 < max && elts.n < LEN(elts.buf) && text.n < LEN(text.buf)) {
			if (glyphs[i+1].foreground == brush.foreground &&
			    glyphs[i+1].background == brush.background &&
			    glyphs[i+1].font == brush.font)
			{
				continue;
			}
		}

		if (brush.background && brush.background != rc->color.bg) {
			draw_rect_solid(rc, brush.background,
			                    pos.x0, pos.y0,
			                    pos.x1 - pos.x0,
			                    brush.font->ascent + brush.font->descent);
		}
		// render elt buffer
		XRenderCompositeText32(win->x11->dpy,
			               PictOpOver,
			               brush.foreground->fill,
			               win->pic,
			               brush.font->picfmt,
			               0, 0,
			               elts.buf[0].xOff,
			               elts.buf[0].yOff,
			               elts.buf, elts.n + 1);

		// finalize draw, reset buffers
		if (i + 1 < max) {
			memset(elts.buf, 0, sizeof(*elts.buf) * (elts.n + 1));
			text.n = elts.n = 0, flushed = true;
			brush.foreground = glyphs[i+1].foreground;
			brush.background = glyphs[i+1].background;
			brush.font = glyphs[i+1].font;
		}
	}
}

GlyphInfo *
font_add_glyph(FontFace *font, uint32 addr, uint32 ucs4)
{
	uchar local[4096];
	uchar *buf = local;
	uint size = 0;
	GlyphInfo glyph = { 0 };
	GlyphInfo *data = NULL;

	FT_FaceRec *face = font->src->face;
	FT_GlyphSlotRec *slot = face->glyph;

	FT_Library_SetLcdFilter(shared.ft, font->lcd_filter);
	{
		glyph.index = FcFreeTypeCharIndex(face, ucs4);
		glyph.ucs4 = ucs4;

		FT_Error err;
		err = FT_Load_Glyph(face, glyph.index, font->loadflags);
		if (err) return data;

		if (!font->width) {
			font->width = PIXELS(slot->metrics.horiAdvance);
		}
		if (font->bold) {
			FT_GlyphSlot_Embolden(slot);
		}
		if (slot->format != FT_GLYPH_FORMAT_BITMAP) {
			err = FT_Render_Glyph(slot, font->mode);
			if (err) return data;
			slot = face->glyph;
		}
	}
	FT_Library_SetLcdFilter(shared.ft, FT_LCD_FILTER_NONE);

	if (!glyph_set_metrics(&glyph.metrics, font, face->glyph)) {
		return data;
	}

	size = glyph_write_image(glyph.metrics,
	                         slot->bitmap,
	                         &buf,
	                         sizeof(local));
#if 0
	dbg_print_glyph_bitmap(buf, glyph.metrics, 1);
#endif
	XGlyphInfo info = { 0 };
	info.width  = glyph.metrics.width;
	info.height = glyph.metrics.height;
	info.x      = glyph.metrics.bearing.x;
	info.y      = glyph.metrics.bearing.y;
	info.xOff   = glyph.metrics.advance.x;
	info.yOff   = glyph.metrics.advance.y;

	XRenderAddGlyphs(font->src->dpy,
		         font->glyphset,
		         (XGlyph *)&glyph.ucs4,
		         &info, 1,
		         (char *)buf, size);

	data = glyph_insert(font, addr, &glyph);

	if (buf != local) free(buf);
	return data;
}

uint32
font_load_glyph(FontFace *font, uint32 ucs4)
{
	uint32 addr = glyph_lookup_ucs4(font, ucs4);

	if (addr && !glyph_get_ucs4(font, addr)) {
		font_add_glyph(font, addr, ucs4);
	}

	return addr;
}

void
font_load_glyphs(FontFace *font)
{
	if (!font->glyphset) {
		font->glyphset = XRenderCreateGlyphSet(font->src->dpy, font->picfmt);
	}
	if (!font->table.items) {
		glyph_table_init(font);
		font_add_glyph(font, 0, 0);
	}

	for (uint ucs4 = ' '; ucs4 < 127; ucs4++) {
		font_load_glyph(font, ucs4);
	}
}

GlyphMetrics *
glyph_set_metrics(GlyphMetrics *metrics, const FontFace *font, const FT_GlyphSlotRec *slot)
{
	GlyphMetrics tmp = *metrics;

	if (slot->format != FT_GLYPH_FORMAT_BITMAP)
		return NULL;

	if (font->spacing == FC_MONO) {
		if (font->loadflags & FT_LOAD_VERTICAL_LAYOUT) {
			tmp.advance.x = 0;
			tmp.advance.y = -font->width;
		} else {
			tmp.advance.x = font->width;
			tmp.advance.y = 0;
		}
	} else {
		return NULL;
	}

	tmp.width  = slot->bitmap.width;
	tmp.height = slot->bitmap.rows;
	tmp.pitch  = (tmp.width + 3) & ~3;
	tmp.bearing.x = -slot->bitmap_left;
	tmp.bearing.y = slot->bitmap_top;

	switch (slot->bitmap.pixel_mode) {
		case FT_PIXEL_MODE_MONO: {
			if (font->mode == FT_RENDER_MODE_MONO) {
				tmp.pitch = (((tmp.width + 31) & ~31) >> 3);
				break;
			}
		}
		// fallthrough
		case FT_PIXEL_MODE_GRAY: {
			switch (font->mode) {
				case FT_RENDER_MODE_LCD:
				case FT_RENDER_MODE_LCD_V: {
					tmp.pitch = tmp.width * sizeof(uint32);
					break;
				}
				default: {
					break;
				}
			}
			break;
		}
		default: {
			return NULL;
		}
	}

	memcpy(metrics, &tmp, sizeof(tmp));

	return metrics;
}

// TODO(ben): Make this function not be the worst thing ever
bool
font_init_face(FontFace *font)
{
	if (!font) return false;

	if (font->glyphs) return true;

	FcResult res;
	struct {
		FcMatrix *matrix;
		FcCharSet *charset;
		FcBool antialias, bold;
		FcBool hint_enable, hint_auto;
		FcBool vert_layout;
		int target;
		int hint_style;
		int fmt_id;
		double pxsize, dpi, aspect;
	} tmp;

#define GETVAL(T_,obj,val) ((res = FcPatternGet##T_(font->pattern, (obj), 0, (val))) == FcResultMatch)
	font->loadflags = FT_LOAD_DEFAULT;

	// Font size metrics
	if (!GETVAL(Double, FC_PIXEL_SIZE, &tmp.pxsize))
		return false;
	if (!GETVAL(Double, FC_DPI, &tmp.dpi))
		return false;
	if (!GETVAL(Double, FC_ASPECT, &tmp.aspect))
		tmp.aspect = 1.0;

	font->size_px.x = (long)(tmp.pxsize * SUBPX);
	font->size_px.y = (long)(tmp.pxsize * SUBPX * tmp.aspect);
	font->size_pt.x = tmp.pxsize * 72.0 / tmp.dpi;
	font->size_pt.y = tmp.pxsize * 72.0 / tmp.dpi * tmp.aspect;

	// Subpixel ordering
	if (!GETVAL(Integer, FC_RGBA, &font->subpx_order)) {
		if (res != FcResultNoMatch) return false;
		font->subpx_order = FC_RGBA_UNKNOWN;
	}

	// LCD filter settings
	if (!GETVAL(Integer, FC_LCD_FILTER, &font->lcd_filter)) {
		if (res != FcResultNoMatch) return false;
		font->lcd_filter = FC_LCD_DEFAULT;
	}

	// Character spacing type
	if (!GETVAL(Integer, FC_SPACING, &font->spacing)) {
		if (res != FcResultNoMatch) return false;
		font->spacing = FC_PROPORTIONAL;
	}

	// Transformation matrix (for synthetic fonts)
	if (!GETVAL(Matrix, FC_MATRIX, &tmp.matrix)) {
		if (res != FcResultNoMatch) return false;
		font->matrix.xx = font->matrix.yy = 0x10000L;
		font->matrix.xy = font->matrix.yx = 0L;
	} else {
		// convert from fontconfig to freetype scaling
		font->matrix.xx = 0x10000L * tmp.matrix->xx;
		font->matrix.xy = 0x10000L * tmp.matrix->xy;
		font->matrix.yx = 0x10000L * tmp.matrix->yx;
		font->matrix.yy = 0x10000L * tmp.matrix->yy;
	}
	// Enable transform if this is not the default matrix
	font->transform = (font->matrix.xx != 0x10000L ||
	                   font->matrix.xy != 0L ||
	                   font->matrix.yx != 0L ||
	                   font->matrix.yy != 0x10000L);

	// Hinting settings
	if (!GETVAL(Bool, FC_HINTING, &tmp.hint_enable)) {
		if (res != FcResultNoMatch) return false;
		tmp.hint_enable = FcTrue;
	}
	if (!GETVAL(Integer, FC_HINT_STYLE, &tmp.hint_style)) {
		if (res != FcResultNoMatch) return false;
		tmp.hint_style = FC_HINT_FULL;
	}
	/* font->loadflags |= (!tmp.hint_enable || tmp.hint_style == FC_HINT_NONE) */
	/*     ? FT_LOAD_NO_HINTING : 0; */

	if (!GETVAL(Bool, FC_AUTOHINT, &tmp.hint_auto)) {
		if (res != FcResultNoMatch) return false;
		tmp.hint_auto = FcFalse;
	}
	font->loadflags |= (tmp.hint_auto) ? FT_LOAD_FORCE_AUTOHINT : 0;

	// Embolden before render
	if (!GETVAL(Bool, FC_EMBOLDEN, &tmp.bold)) {
		if (res != FcResultNoMatch) return false;
		tmp.bold = FcFalse;
	}
	font->bold = !!tmp.bold;

	// Global character width
	if (!GETVAL(Integer, FC_CHAR_WIDTH, &font->fixed_width)) {
		if (res == FcResultNoMatch) font->fixed_width = 0;
	}

	// Vertical/horizontal layout
	if (GETVAL(Bool, FC_VERTICAL_LAYOUT, &tmp.vert_layout)) {
		if (tmp.vert_layout == FcTrue) return false;
	}

	// Antialiasing settings
	if (!GETVAL(Bool, FC_ANTIALIAS, &tmp.antialias)) {
		if (res != FcResultNoMatch) return false;
		tmp.antialias = FcTrue;
	}

	// Finally load the Freetype face object
	if (!font->src->face) {
		if (!shared.ft) {
			if (FT_Init_FreeType(&shared.ft) != 0) exit(2);
		}
		FT_New_Face(shared.ft, font->src->file, font->src->index, &font->src->face);
		font->src->num_glyphs = font->src->face->num_glyphs + 1;
	}
	assert(font->src->face);

	// Get the character set and codepoint range
	if (GETVAL(CharSet, FC_CHARSET, &tmp.charset)) {
		font->charset = FcCharSetCopy(tmp.charset);
	} else {
		font->charset = FcFreeTypeCharSet(font->src->face, NULL);
	}
	if (!font->charset) return false;
	font->num_codepoints = FcCharSetCount(font->charset);

	// Set the pixel size that we previously extracted
	if (FT_Set_Char_Size(font->src->face, font->size_px.x, font->size_px.y, 0, 0) != 0) {
		return false;
	}

	// Set the transform with the provided matrix
	FT_Set_Transform(font->src->face, &font->matrix, NULL);
	font->src->size_px.x = font->size_px.x;
	font->src->size_px.y = font->size_px.y;
	font->src->matrix = font->matrix;

	// Set properties that we needed the face for
	if (!(font->src->face->face_flags & FT_FACE_FLAG_SCALABLE)) {
		tmp.antialias = FcFalse;
	}
	font->antialias = !!tmp.antialias;
	font->loadflags |= (font->antialias || font->transform) ? FT_LOAD_NO_BITMAP : 0;
	font->has_color = FT_HAS_COLOR(font->src->face);

	// defaults (?)
	tmp.fmt_id = PictStandardA8;
	tmp.target = FT_LOAD_TARGET_NORMAL;
	font->mode = FT_RENDER_MODE_NORMAL;

	if (font->has_color) {
		font->loadflags |= FT_LOAD_COLOR;
		tmp.fmt_id = PictStandardARGB32;
	} else if (!font->antialias) {
		if (tmp.hint_style == FC_HINT_NONE) {
			font->loadflags |= FT_LOAD_NO_HINTING;
		} else {
			tmp.target = FT_LOAD_TARGET_MONO;
		}
		tmp.fmt_id = PictStandardA1;
		font->loadflags |= FT_LOAD_MONOCHROME;
		font->mode = FT_RENDER_MODE_MONO;
	} else if (!tmp.hint_enable) {
		font->loadflags |= FT_LOAD_NO_HINTING;
	} else {
		switch (tmp.hint_style) {
			case FC_HINT_NONE:
				font->loadflags |= FT_LOAD_NO_HINTING;
				break;
			case FC_HINT_SLIGHT:
				tmp.target = FT_LOAD_TARGET_LIGHT;
				break;
			case FC_HINT_MEDIUM:
				break;
			default: {
				switch (font->subpx_order) {
					case FC_RGBA_RGB:
					case FC_RGBA_BGR: {
						font->mode = FT_RENDER_MODE_LCD;
						tmp.target = FT_LOAD_TARGET_LCD;
						tmp.fmt_id = PictStandardARGB32;
						break;
					}
					case FC_RGBA_VRGB:
					case FC_RGBA_VBGR: {
						tmp.target = FT_LOAD_TARGET_LCD_V;
						font->mode = FT_RENDER_MODE_LCD_V;
						tmp.fmt_id = PictStandardARGB32;
						break;
					}
					default: {
						break;
					}
				}
			}
		}
	}
	font->loadflags |= tmp.target;
	font->picfmt = XRenderFindStandardFormat(font->src->dpy, tmp.fmt_id);
	assert(font->picfmt);

	FT_Size_Metrics mtx = font->src->face->size->metrics;
	// Transform and set the public global metrics
	if (!font->transform) {
		font->ascent  = PIXELS(mtx.ascender);
		font->descent = -PIXELS(mtx.descender);
		font->height  = PIXELS(mtx.height);
		font->max_advance = (font->fixed_width)
		    ? font->fixed_width
		    : PIXELS(mtx.max_advance);
	} else {
		FT_Vector vec;

		vec.x = 0, vec.y = mtx.ascender;
		FT_Vector_Transform(&vec, &font->matrix);
		font->ascent = PIXELS(vec.y);

		vec.x = 0, vec.y = mtx.descender;
		FT_Vector_Transform(&vec, &font->matrix);
		font->descent = -PIXELS(vec.y);

		vec.x = 0, vec.y = mtx.height;
		FT_Vector_Transform(&vec, &font->matrix);
		font->height = PIXELS(vec.y);

		vec.x = mtx.max_advance, vec.y = 0;
		FT_Vector_Transform(&vec, &font->matrix);
		font->max_advance = PIXELS(vec.x);
	}
#undef GETVAL

	font_load_glyphs(font);

	return true;
}

void
dbg_print_glyph_bitmap(const uchar *data, GlyphMetrics metrics, uint hscale)
{
	static const char density[] = { " .:;?%@#" };
	const uchar *row = data;

	printf("==> (%04u) w/h/p = ( %03u, %03u, %03d ) brg = ( %02d, %02d ) adv = ( %02d, %02d )\n",
	       metrics.pitch * metrics.height,
	       metrics.width, metrics.height, metrics.pitch,
	       metrics.bearing.x, metrics.bearing.y,
	       metrics.advance.x, metrics.advance.y);

	for (uint y = 0; y < metrics.height; row += metrics.pitch, y++) {
		for (uint x = 0; x < metrics.width; x++) {
			for (uint n = hscale; n--; ) {
#if 0
				putchar(density[row[x]/32]);
			}
		}
		puts("|");
#else
				(void)density;
				if (row[x]) {
					printf("%03u ", (row[x] * 100) / 255);
				} else {
					printf("%3s ", "---");
				}
			}
		}
		putchar('\n');
#endif
	}
}

