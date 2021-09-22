#include <math.h>
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
#define PIXELS(subpx) ((subpx) >> 6)

struct SLLNode_;
struct DLLNode_;
typedef struct SLLNode_ {
	struct SLLNode_ *next;
} SLLNode;

typedef Glyph XGlyph;
typedef GlyphSet XGlyphSet;
#define Font FontFace

typedef struct FontSource_ FontSource;

struct FontSource_ {
	SLLNode node;
	Display *dpy;
	char *file;
	int index;
	FT_Face face;
	int num_glyphs;
	int refcount;
	bool locked;
};

#define FONTATTR_MONOSPACE (1 << 0)
#define FONTATTR_EMBOLDEN  (1 << 1)
#define FONTATTR_TRANSFORM (1 << 2)
#define FONTATTR_ANTIALIAS (1 << 3)
#define FONTATTR_COLOR     (1 << 4)
#define FONTATTR_HINTING   (1 << 5)
#define FONTATTR_AUTOHINT  (1 << 6)

struct FontFace_ {
	SLLNode node;
	FontSource *src;

	FT_Matrix matrix;
	FT_Int loadflags;
	FT_Render_Mode mode;

	FcPattern *pattern;
	FcCharSet *charset;

	XGlyphSet glyphset;
	XRenderPictFormat *picfmt;

	bool antialias, bold, transform, has_color;
	struct { int64 x, y; } size_px;
	int subpx_order, lcd_filter, spacing;
	int width, height;
	int ascent, descent;
	int max_advance;

	struct GlyphCache_ {
		uint32 num_ents;
		uint32 max_ents;

		struct GlyphMapping_ {
			uint32 key;
			uint32 index;
		} *map;

		struct GlyphData_ {
			uint32 index;
			struct GlyphMetrics_ {
				int width, height;
				int size;
				struct { int x, y; } bearing;
				struct { int x, y; } advance;
			} metrics;
		} *glyphs;
	} cache;
};

typedef struct GlyphCache_   GlyphCache;
typedef struct GlyphMapping_ GlyphMapping;
typedef struct GlyphData_    GlyphData;
typedef struct GlyphMetrics_ GlyphMetrics;

typedef struct GlyphMeta_ {
	FontFace *font;
	GlyphData *data;
} GlyphMeta;

typedef enum {
	GlyphStateMissing  = -1,
	GlyphStateUncached =  0,
	GlyphStateCached   = +1
} GlyphState;

static struct {
	FT_Library ft;
	FontFace *fonts;
	FontSource *font_sources;
} shared;

struct FontConfig {
	double pixel_size;
	double dpi;
	double aspect;
	int subpixel_order;
	int spacing_style;
	int char_width;
	int hint_style;
	int pixel_fmt;
	int lcd_filter;
	bool use_hinting;
	bool use_autohint;
	bool use_embolden;
	bool use_vertlayout;
	bool use_antialias;
	bool use_color;
	FcCharSet *charset;
	FT_Matrix matrix;
};

#define FCMATRIX_DFL ((FcMatrix){ 1, 0, 0, 1 })
#define FTMATRIX_DFL ((FT_Matrix){ 0x10000, 0, 0, 0x10000 })
#define FCRES_FOUND(res) ((res) == FcResultMatch)
#define FCRES_VALID(res) (FCRES_FOUND(res) || (res) == FcResultNoMatch)

#define MASK_UCS4 ((1 << 21) - 1)
#define MASK_META (~MASK_UCS4)
#define ENT_PACK(u) ((u) | MASK_META)
#define ENT_UCS4(c) ((c) & MASK_UCS4)

#define DUMMY__(c) ((uint8)(powf((c) / 255.0f, 0.75f) * 255.0f))

static uint32 font_init_cache(GlyphCache *, uint, uint);
static GlyphMeta font_load_glyph(FontFace *, uint32);
static GlyphState font_query_glyph(FontFace *, uint32, uint32 *);
static GlyphData *font_cache_glyph(FontFace *, uint32);
static bool font_extract_prop(const FcPattern *, const char *, void *);
static uint32 font_get_glyph_index(const FontFace *, uint32);
static GlyphMetrics font_get_glyph_metrics(const FontFace *, const FT_FaceRec *);
static void font_free_glyphs(FontFace *);
static void font_destroy_source(FontSource *);

static void dbg_print_glyph_bitmap(const uchar *, GlyphMetrics, uint);

GlyphState
font_query_glyph(FontFace *font, uint32 ucs4, uint32 *index)
{
	if (!font->cache.max_ents) {
		return 0;
	}

	const uint32 limit = font->cache.max_ents;
	uint32 hash = ucs4 % limit;
	uint32 offset = 0;
	GlyphState state = GlyphStateUncached;

	GlyphMapping *ent = font->cache.map + hash;

	for (;;) {
		if (!ent->key) {
			uint32 index = font_get_glyph_index(font, ucs4);
			if (!index) {
				return GlyphStateMissing;
			}
			ent->key = ENT_PACK(ucs4);
			ent->index = index;
			break;
		} else if (ucs4 == ENT_UCS4(ent->key)) {
			state = GlyphStateCached;
			break;
		}

		if (!offset) {
			offset = ucs4 % (limit - 2);
			offset += !offset;
		}
		hash += offset;
		hash -= (hash >= limit) ? limit : 0;

		ent = font->cache.map + hash;
	}

	if (index) {
		*index = ent->index;
	}
	if (!state) {
		font->cache.num_ents++;
	}

	return state;
}

uint32
font_get_glyph_index(const FontFace *font, uint32 ucs4)
{
	if (FcCharSetHasChar(font->charset, ucs4)) {
		return FcFreeTypeCharIndex(font->src->face, ucs4);
	}

	return 0;
}

bool
font_init_face(FontFace *font)
{
	if (!font) return false;

	FontSource *root = font->src;
	FT_FaceRec *face = font->src->face;

	struct FontConfig cfg = { 0 };

	int load_target = FT_LOAD_TARGET_NORMAL;
	int load_flags = 0;
	int render_mode = FT_RENDER_MODE_NORMAL;
	int pixel_fmt = PictStandardA8;

#define font_extract_prop(...) ASSERT(font_extract_prop(__VA_ARGS__))
	font_extract_prop(font->pattern, FC_PIXEL_SIZE, &cfg.pixel_size);
	font_extract_prop(font->pattern, FC_DPI,        &cfg.dpi);
	font_extract_prop(font->pattern, FC_ASPECT,     &cfg.aspect);
	font_extract_prop(font->pattern, FC_RGBA,       &cfg.subpixel_order)
	font_extract_prop(font->pattern, FC_LCD_FILTER, &cfg.lcd_filter);
	font_extract_prop(font->pattern, FC_MATRIX,     &cfg.matrix);
	font_extract_prop(font->pattern, FC_EMBOLDEN,   &cfg.use_embolden);
	font_extract_prop(font->pattern, FC_SPACING,    &cfg.spacing_style);
	font_extract_prop(font->pattern, FC_HINTING,    &cfg.use_hinting);
	font_extract_prop(font->pattern, FC_AUTOHINT,   &cfg.use_autohint);
	font_extract_prop(font->pattern, FC_HINT_STYLE, &cfg.hint_style);
	font_extract_prop(font->pattern, FC_CHAR_WIDTH, &cfg.char_width);
	font_extract_prop(font->pattern, FC_VERTICAL_LAYOUT, &cfg.use_vertlayout);
	font_extract_prop(font->pattern, FC_ANTIALIAS,  &cfg.use_antialias);
	font_extract_prop(font->pattern, FC_CHARSET,    &cfg.charset);
#undef font_extract_prop

	if (!face) {
		if (!shared.ft) {
			ASSERT(!FT_Init_FreeType(&shared.ft));
		}
		FT_New_Face(shared.ft, root->file, root->index, &root->face);
		face = root->face;
		root->num_glyphs = face->num_glyphs + 1;
	}

	font->size_px.x = (int64)(cfg.pixel_size * SUBPX);
	font->size_px.y = (int64)(cfg.pixel_size * SUBPX * cfg.aspect);

	FT_Set_Char_Size(face, font->size_px.x, font->size_px.y, 0, 0);
	FT_Set_Transform(face, &cfg.matrix, NULL);

	if (FT_HAS_COLOR(face)) {
		cfg.use_color = true;
	}
	if (!(face->face_flags & FT_FACE_FLAG_SCALABLE)) {
		cfg.use_antialias = false;
	}

	// TODO(ben): Make this less confusing
	if (cfg.use_autohint) {
		load_flags |= FT_LOAD_FORCE_AUTOHINT;
	}
	if (cfg.use_color) {
		pixel_fmt = PictStandardARGB32;
		load_flags |= FT_LOAD_COLOR;
	} else if (!cfg.use_antialias) {
		if (cfg.hint_style == FC_HINT_NONE) {
			load_flags |= FT_LOAD_NO_HINTING;
		} else {
			load_target = FT_LOAD_TARGET_MONO;
		}
		pixel_fmt = PictStandardA1;
		load_flags |= FT_LOAD_MONOCHROME;
		render_mode = FT_RENDER_MODE_MONO;
	} else if (!cfg.use_hinting) {
		load_flags |= FT_LOAD_NO_BITMAP;
		load_flags |= FT_LOAD_NO_HINTING;
	} else {
		load_flags |= FT_LOAD_NO_BITMAP;
		switch (cfg.hint_style) {
		case FC_HINT_NONE:
			load_flags |= FT_LOAD_NO_HINTING;
			break;
		case FC_HINT_SLIGHT:
			load_target = FT_LOAD_TARGET_LIGHT;
			break;
		case FC_HINT_MEDIUM:
			break;
		default:
			switch (cfg.subpixel_order)
			case FC_RGBA_RGB:
			case FC_RGBA_BGR: {
				pixel_fmt = PictStandardARGB32;
				load_target = FT_LOAD_TARGET_LCD;
				render_mode = FT_RENDER_MODE_LCD;
				break;
			case FC_RGBA_VRGB:
			case FC_RGBA_VBGR:
				pixel_fmt = PictStandardARGB32;
				load_target = FT_LOAD_TARGET_LCD_V;
				render_mode = FT_RENDER_MODE_LCD_V;
				break;
			default:
				break;
			}
		}
	}

	FT_Size_Metrics mtx = face->size->metrics;
	if (memequal(&cfg.matrix, &FTMATRIX_DFL, sizeof(FT_Matrix))) {
		font->ascent  = +(mtx.ascender >> 6);
		font->descent = -(mtx.descender >> 6);
		font->height  = +(mtx.height >> 6);
		font->max_advance = DEFAULT(cfg.char_width, mtx.max_advance >> 6);

		font->transform = false;
	} else {
		// transform the default metrics
		FT_Vector v = { 0 };

		v.x = 0, v.y = mtx.ascender;
		FT_Vector_Transform(&v, &cfg.matrix);
		font->ascent = +(v.y >> 6);

		v.x = 0, v.y = mtx.descender;
		FT_Vector_Transform(&v, &cfg.matrix);
		font->descent = -(v.y >> 6);

		v.x = 0, v.y = mtx.height;
		FT_Vector_Transform(&v, &cfg.matrix);
		font->height = +(v.y >> 6);

		v.x = mtx.max_advance, v.y = 0;
		FT_Vector_Transform(&v, &cfg.matrix);
		font->max_advance = +(v.x >> 6);

		font->transform = true;
		font->loadflags |= FT_LOAD_NO_BITMAP;
	}

	font->loadflags = load_flags|load_target;
	font->mode = render_mode;
	font->subpx_order = cfg.subpixel_order;
	font->has_color = cfg.use_color;
	font->lcd_filter = cfg.lcd_filter;
	font->bold = cfg.use_embolden;
	font->antialias = cfg.use_antialias;
	font->spacing = cfg.spacing_style;
	font->matrix = cfg.matrix;
	font->picfmt = XRenderFindStandardFormat(root->dpy, pixel_fmt);
	font->charset = (cfg.charset) ? FcCharSetCopy(cfg.charset) : NULL;
	if (cfg.charset) {
		font->charset = FcCharSetCopy(cfg.charset);
	} else {
		font->charset = FcFreeTypeCharSet(font->src->face, NULL);
	}

	ASSERT(font_init_cache(&font->cache, FcCharSetCount(font->charset), root->num_glyphs));
	font->glyphset = XRenderCreateGlyphSet(font->src->dpy, font->picfmt);

	// override everything and pre-render the "missing glyph" glyph
	font_cache_glyph(font, 0);
	// pre-cache the ASCII range
#if 0
	for (int c = 1; c < 128; c++) {
		font_load_glyph(font, c);
	};
#endif

	return true;
}

GlyphMeta
font_load_glyph(FontFace *font, uint32 ucs4)
{
	uint32 index = 0;
	GlyphState state = font_query_glyph(font, ucs4, &index);

	if (state == GlyphStateUncached) {
		font_cache_glyph(font, index);
	}

	return (GlyphMeta){
		.font = font,
		.data = font->cache.glyphs + index
	};
}

GlyphData *
font_cache_glyph(FontFace *font, uint32 index)
{
	FT_FaceRec *face = font->src->face;
	FT_GlyphSlotRec *slot = face->glyph;

	static uchar local[4096];
	uchar *data = local;
	uint size;

	GlyphData glyph = { .index = index };

	FT_Library_SetLcdFilter(shared.ft, font->lcd_filter);
	{
		FT_Error err;
		err = FT_Load_Glyph(face, index, font->loadflags);
		ASSERT(!err);
		if (!font->width) {
			font->width = PIXELS(slot->metrics.horiAdvance);
		}
		if (font->bold) {
			FT_GlyphSlot_Embolden(slot);
		}
		if (slot->format != FT_GLYPH_FORMAT_BITMAP) {
			err = FT_Render_Glyph(slot, font->mode);
			ASSERT(!err);
			slot = face->glyph;
		}
	}
	FT_Library_SetLcdFilter(shared.ft, FT_LCD_FILTER_NONE);

	ASSERT(face->glyph->format == FT_GLYPH_FORMAT_BITMAP);
	glyph.metrics = font_get_glyph_metrics(font, face);

	if (glyph.metrics.size > (int)sizeof(local)) {
		data = xmalloc(glyph.metrics.size, sizeof(*data));
	}
	if (glyph.metrics.size > 0) {
		uchar *src = slot->bitmap.buffer;
		uchar *dst = data;
		uint pitch = glyph.metrics.size / glyph.metrics.height;

		for (int y = 0; y < glyph.metrics.height; y++) {
			memcpy(dst, src, glyph.metrics.width);
			src += slot->bitmap.pitch;
			dst += pitch;
		}
	}
#if 0
	dbg_print_glyph_bitmap(data, glyph.metrics, 1);
#endif

	XGlyphInfo info = {
		.width  = glyph.metrics.width,
		.height = glyph.metrics.height,
		.x      = glyph.metrics.bearing.x,
		.y      = glyph.metrics.bearing.y,
		.xOff   = glyph.metrics.advance.x,
		.yOff   = glyph.metrics.advance.y
	};
	XRenderAddGlyphs(font->src->dpy, font->glyphset,
	                 (XGlyph *)&index, &info, 1,
	                 (char *)data, glyph.metrics.size);

	if (data != local) free(data);

	return memcpy(font->cache.glyphs + glyph.index, &glyph, sizeof(glyph));
}

GlyphMetrics
font_get_glyph_metrics(const FontFace *font, const FT_FaceRec *face)
{
	GlyphMetrics metrics = { 0 };
	const FT_GlyphSlotRec *slot = face->glyph;
	const FT_Bitmap bitmap = slot->bitmap;

	if (font->spacing == FC_MONO) {
		if (font->loadflags & FT_LOAD_VERTICAL_LAYOUT) {
			metrics.advance.x = 0;
			metrics.advance.y = -font->width;
		} else {
			metrics.advance.x = font->width;
			metrics.advance.y = 0;
		}
	} else {
		return (GlyphMetrics){ 0 };
	}

	metrics.width  = bitmap.width;
	metrics.height = bitmap.rows;
	metrics.bearing.x = -slot->bitmap_left;
	metrics.bearing.y = +slot->bitmap_top;

	int pitch = (metrics.width + 3) & ~3;

	switch (bitmap.pixel_mode) {
		case FT_PIXEL_MODE_MONO: {
			if (font->mode == FT_RENDER_MODE_MONO) {
				pitch = (((metrics.width + 31) & ~31) >> 3);
				break;
			}
		}
		// fallthrough
		case FT_PIXEL_MODE_GRAY: {
			switch (font->mode) {
				case FT_RENDER_MODE_LCD:
				case FT_RENDER_MODE_LCD_V: {
					pitch = metrics.width * sizeof(uint32);
					break;
				}
				default: {
					break;
				}
			}
			break;
		}
		default: {
			return (GlyphMetrics){ 0 };
		}
	}

	metrics.size = pitch * metrics.height;

	return metrics;
}

bool
font_extract_prop(const FcPattern *pattern, const char *name, void *data)
{
	ASSERT(pattern && data);

	if (!name) return false;

	FcResult res = FcResultNoMatch;
	const FcObjectType *ent = FcNameGetObjectType(name);

	switch (ent->type) {
		case FcTypeDouble: {
			res = FcPatternGetDouble(pattern, ent->object, 0, data);
			break;
		}
		case FcTypeInteger: {
			res = FcPatternGetInteger(pattern, ent->object, 0, data);
			break;
		}
		case FcTypeString: {
			res = FcPatternGetString(pattern, ent->object, 0, data);
			break;
		}
		case FcTypeBool: {
			FcBool src = 0;
			res = FcPatternGetBool(pattern, ent->object, 0, &src);
			*(bool *)data = !!src;
			break;
		}
		case FcTypeMatrix: {
			FcMatrix *matf = &FCMATRIX_DFL;
			FT_Matrix *mati = data;
			res = FcPatternGetMatrix(pattern, ent->object, 0, &matf);
			// convert to freetype's int64 format
			mati->xx = 0x10000L * matf->xx;
			mati->xy = 0x10000L * matf->xy;
			mati->yx = 0x10000L * matf->yx;
			mati->yy = 0x10000L * matf->yy;
			break;
		}
		case FcTypeCharSet: {
			res = FcPatternGetCharSet(pattern, ent->object, 0, data);
			break;
		}
		default: {
			break;
		}
	};

	ASSERT(FCRES_VALID(res));
	return FCRES_FOUND(res);
}

uint32
font_init_cache(GlyphCache *cache, uint num_chars, uint num_glyphs)
{
	uint32 max_ents = 0;

	if (num_chars) {
		max_ents = num_chars + (num_chars >> 1);
		max_ents = ((max_ents + 1) & ~1) + 1;
		while (!isprime(max_ents)) {
			max_ents += 2;
		}
	}

	if (!num_glyphs || !max_ents) {
		return 0;
	}

	memset(cache, 0, sizeof(*cache));
	cache->map = xcalloc(max_ents, sizeof(*cache->map));
	cache->glyphs = xcalloc(num_glyphs, sizeof(*cache->glyphs));
	cache->max_ents = max_ents;

	return max_ents;
}

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
		// TODO(ben): Maybe re-add this?
		ret->size_pt.x = 0;
		ret->size_pt.y = 0;

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
		font_free_glyphs(obj);
		if (!--obj->src->refcount) {
			font_destroy_source(obj->src);
		}
		free(obj);
	}
}

void
font_free_glyphs(FontFace *target)
{
	if (target) {
		FcCharSetDestroy(target->charset);
		XRenderFreeGlyphSet(target->src->dpy, target->glyphset);
		FREE(target->cache.map);
		FREE(target->cache.glyphs);
		target->cache.num_ents = 0;
		target->cache.max_ents = 0;
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
		if (strequal((char *)file, obj->file) && index == obj->index) {
			assert(obj->refcount && obj->file);
			obj->refcount++;
			return obj;
		}
	}

	assert(!*p);
	*p = xcalloc(1, sizeof(*obj));
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
	*p = xcalloc(1, sizeof(*obj));
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

#define SETDFL(pat,T_,obj,val)                                  \
if (FcPatternGet((pat), (obj), 0, &dummy) == FcResultNoMatch) { \
	fprintf(stderr, "Font(%s) setting defaults ... %s\n", __FILE__, #obj); \
	FcPatternAdd##T_((pat), (obj), (val));                      \
}
	SETDFL(pattern.conf, Double,  FC_DPI,        win->x11->dpi);
	SETDFL(pattern.conf, Bool,    FC_HINTING,    FcTrue);
	SETDFL(pattern.conf, Integer, FC_HINT_STYLE, FC_HINT_FULL);
	SETDFL(pattern.conf, Bool,    FC_AUTOHINT,   FcFalse);
	SETDFL(pattern.conf, Integer, FC_LCD_FILTER, FC_LCD_DEFAULT);
	SETDFL(pattern.conf, Integer, FC_SPACING,    FC_MONO);
	SETDFL(pattern.conf, Bool,    FC_VERTICAL_LAYOUT, FcFalse);

	if (FcPatternGet(pattern.conf, FC_RGBA, 0, &dummy) == FcResultNoMatch) {
		int val = FC_RGBA_UNKNOWN;
		switch (XRenderQuerySubpixelOrder(win->x11->dpy, win->x11->screen)) {
			case SubPixelNone:          val = FC_RGBA_NONE; break;
			case SubPixelHorizontalRGB: val = FC_RGBA_RGB;  break;
			case SubPixelHorizontalBGR: val = FC_RGBA_BGR;  break;
			case SubPixelVerticalRGB:   val = FC_RGBA_VRGB; break;
			case SubPixelVerticalBGR:   val = FC_RGBA_VBGR; break;
		}
		FcPatternAddInteger(pattern.conf, FC_RGBA, val);
	}

	FcDefaultSubstitute(pattern.conf);
	pattern.match = FcFontMatch(NULL, pattern.conf, &res);

	SETDFL(pattern.match, Double,  FC_PIXEL_SIZE, 16.f);
	SETDFL(pattern.match, Double,  FC_ASPECT,     1.f);
	SETDFL(pattern.match, Bool,    FC_EMBOLDEN,   FcFalse);
	SETDFL(pattern.match, Bool,    FC_ANTIALIAS,  FcTrue);
	SETDFL(pattern.match, Integer, FC_CHAR_WIDTH, 0);
	SETDFL(pattern.match, Matrix,  FC_MATRIX,     &FCMATRIX_DFL);
	SETDFL(pattern.match, CharSet, FC_CHARSET,    NULL);
#undef SETDFL

	FcPatternDestroy(pattern.base);
	FcPatternDestroy(pattern.conf);
	/* FcPatternPrint(pattern.match); */

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
	/* FcPatternPrint(pattern.match); */

	FontFace *new_font = font_insert_face(pattern.match);

	return new_font;
}

void
draw_rect_solid(const RC *rc, Color color, int x_, int y_, int w_, int h_)
{
	WinData *win = (WinData *)rc->win;

	int x = CLAMP(x_, 0, (int)win->pub.w);
	int y = CLAMP(y_, 0, (int)win->pub.h);
	int w = CLAMP(x + w_, x, (int)win->pub.w) - x;
	int h = CLAMP(y + h_, y, (int)win->pub.h) - y;

	XRenderFillRectangle(win->x11->dpy,
	                     PictOpOver,
	                     win->pic,
	                     &XR_ARGB(color.argb),
	                     x, y, w, h);
}

void
draw_text_utf8(const RC *rc, const GlyphRender *glyphs, uint max, int x, int y)
{
	assert(rc);
	WinData *win = (WinData *)rc->win;

	if (!max || !glyphs) return;

	struct {
		FontFace *font;
		Color bg;
		Color fg;
	} brush = { 0 };
	struct {
		int x0, y0, x1, y1; // current region (in pixels)
		int dx, dy;         // advance of previous glyph
		int kx, ky;         // constant offset from start of region
	} pos = { 0 };
	struct { uint32 buf[2048]; uint n; } text = { 0 };
	struct { XGlyphElt32 buf[32]; uint n; } elts = { 0 };

	brush.bg = glyphs[0].bg;
	brush.fg = glyphs[0].fg;
	brush.font = glyphs[0].font;

	pos.x1 = pos.x0 = x;
	pos.y1 = pos.y0 = y;
	pos.dx = pos.x1 - pos.x0;
	pos.dy = pos.y1 - pos.y0;

	bool flushed = true;

	for (uint i = 0; i < max; i++) {
		GlyphMeta glyph = font_load_glyph(brush.font, glyphs[i].ucs4);

		int chdx = glyph.data->metrics.advance.x;
		int chdy = glyph.data->metrics.advance.y;
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
			pos.ky = glyph.font->ascent;
		}
		// Elt positions must be specified relative to the previous elt in the array.
		// This is why only the first elt (per draw call) receives the offset from
		// the drawing orgin
		if (pos.x1 != pos.x0 || pos.y1 != pos.y0) {
			adjx = chdx - pos.dx;
			adjy = chdy - pos.dy;
		}

		elts.buf[elts.n].xOff = pos.x0 + pos.kx + adjx;
		elts.buf[elts.n].yOff = pos.y0 + pos.ky + adjy;
		elts.buf[elts.n].glyphset = glyph.font->glyphset;
		elts.buf[elts.n].nchars++;
		text.buf[text.n++] = glyph.data->index;

		flushed = false;
		// accumulate position so we can set the new leading elt after drawing
		pos.x1 += (pos.dx = chdx);
		pos.y1 += (pos.dy = chdy);

		// If none of the local buffers are at capacity, keep going.
		// Otherwise, just draw now and reset everything at the new orgin
		if (i + 1 < max && elts.n < LEN(elts.buf) && text.n < LEN(text.buf)) {
			if (glyphs[i+1].fg.argb == brush.fg.argb &&
			    glyphs[i+1].bg.argb == brush.bg.argb &&
			    glyphs[i+1].font == glyph.font)
			{
				continue;
			}
		}

		if (brush.bg.argb != rc->color.bg.argb) {
			draw_rect_solid(rc, brush.bg,
			                    pos.x0, pos.y0,
			                    pos.x1 - pos.x0,
			                    glyph.font->ascent + glyph.font->descent);
		}

		ColorID handle = brush.fg.id;

		// if there's no allocated fill, create it
		if (!handle) {
			handle = win_alloc_color(rc, brush.fg.argb);
		}

		// render elt buffer
		XRenderCompositeText32(win->x11->dpy,
			               PictOpOver,
			               handle,
			               win->pic,
			               glyph.font->picfmt,
			               0, 0,
			               elts.buf[0].xOff,
			               elts.buf[0].yOff,
			               elts.buf, elts.n + 1);

		// TODO(ben): We immediately free any new handles and don't cache anything.
		// Not really optimal, but RGB literals are an uncommon case here.
		// ...still might be worth another look.
		if (handle != brush.fg.id && handle) {
			win_free_color(rc, handle);
		}

		// finalize draw, reset buffers
		if (i + 1 < max) {
			memset(elts.buf, 0, sizeof(*elts.buf) * (elts.n + 1));
			text.n = elts.n = 0, flushed = true;
			brush.fg = glyphs[i+1].fg;
			brush.bg = glyphs[i+1].bg;
			brush.font = glyphs[i+1].font;
		}
	}
}

void
dbg_print_glyph_bitmap(const uchar *data, GlyphMetrics metrics, uint hscale)
{
	static const char density[] = { " .:;?%@#" };
	const uchar *row = data;
	int pitch = (metrics.size) ? metrics.size / metrics.height : 0;

	printf("==> (%04u) w/h/p = ( %03u, %03u, %03d ) brg = ( %02d, %02d ) adv = ( %02d, %02d )\n",
	       metrics.size,
	       metrics.width, metrics.height, pitch,
	       metrics.bearing.x, metrics.bearing.y,
	       metrics.advance.x, metrics.advance.y);

	for (int y = 0; y < metrics.height; row += pitch, y++) {
		for (int x = 0; x < metrics.width; x++) {
			for (int n = hscale; n--; ) {
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

