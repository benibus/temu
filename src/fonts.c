#include "utils.h"
#include "x11.h"
#include "fonts.h"

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

#ifndef NDEBUG
  #define DEBUG_PRINT_GLYPHS  0
  #define DEBUG_PRINT_PATTERN 0
#else
  #define DEBUG_PRINT_GLYPHS  0
  #define DEBUG_PRINT_PATTERN 0
#endif

#define CONFIG_MAX_FONTS 16

#define FONTATTR_MONOSPACE (1 << 0)
#define FONTATTR_EMBOLDEN  (1 << 1)
#define FONTATTR_TRANSFORM (1 << 2)
#define FONTATTR_ANTIALIAS (1 << 3)

typedef struct {
	uint16 width, height;
	uint16 pitch;
	PixelFormat format:16;
	int16 x_advance, y_advance;
	int16 x_bearing, y_bearing;
	void *data;
} BitmapInfo;

typedef struct {
	Display *dpy; // TEMPORARY
	char *filepath;
	uint32 hashval;

	FcPattern *pattern;
	FcCharSet *charset;

	FT_FaceRec *face;
	FT_Matrix matrix;
	FT_Int loadflags;
	FT_Render_Mode rendermode;
	FT_LcdFilter lcdfilter;

	FontID parent;
	FontID *children;

	GlyphSet glyphset;
	XRenderPictFormat *picformat;

	uint64 *glyphmap;
	uint32 basehash;

	BitmapInfo *bitmaps;

	uint num_codepoints;
	uint num_glyphs;
	uint num_entries;

	uint16 attrs;
	int pixelformat;

	float pixelsize;
	float scale;
	float aspect;

	int width, height;
	int ascent, descent;
	int max_advance;
} FontData;

typedef struct {
	uint16 generation;
	FontData *data;
} FontSlot;

struct FontManager {
	FT_Library library;
	FontSlot *slots;
};

static struct FontManager instance;

typedef struct {
	FontID font;
	uint32 glyph;
} FontGlyph;

struct FontConfig {
	double pixelsize;
	double dpi;
	double aspect;
	int pixelformat;
	int spacing_style;
	int char_width;
	int hint_style;
	int pixel_fmt;
	int lcdfilter;
	bool use_hinting;
	bool use_autohint;
	bool use_embolden;
	bool use_vertlayout;
	bool use_antialias;
	FcCharSet *charset;
	FT_Matrix matrix;
};

enum FontHinting {
	FontHintingNone,
	FontHintingNormal,
	FontHintingLight,
	FontHintingFull,
	FontHintingAuto,
	FontHintingCount,
};

#define FCMATRIX_DFL ((FcMatrix){ 1, 0, 0, 1 })
#define FTMATRIX_DFL ((FT_Matrix){ 0x10000, 0, 0, 0x10000 })
#define FCRES_FOUND(res) ((res) == FcResultMatch)
#define FCRES_VALID(res) (FCRES_FOUND(res) || (res) == FcResultNoMatch)

// 64-bit packed value for codepoint-to-glyph mappings.
// From LSB to MSB...
//  - Bits 00-31: Glyph index extracted from the font file (32 bits)
//  - Bits 32-56: Unicode codepoint (24 bits)
//  - Bits 57-64: Hash table metadata, i.e. nonzero if occupied (8 bits)
//
#define PACK_MAPKEY(ucs4,gidx) (         \
  (~0UL << (32 + 24))|                   \
  ((uint64)((ucs4) & 0xffffffff) << 32)| \
  ((uint64)((gidx) & 0xffffffff) <<  0)  \
)
// Helper macros for extracting the codepoint and glyph index
#define MAPKEY_UCS4(key) (((key) & 0x00ffffff00000000) >> 32)
#define MAPKEY_GIDX(key) (((key) & 0x00000000ffffffff) >>  0)

#define PACK_FID(idx) ((instance.slots[(idx)].generation << 16)|((idx) & 0xffff))
#define FID_IDX(fid)  ((fid) & 0xffff)
#define FID_GEN(fid)  ((fid) >> 16)

#define DUMMY__(c) ((uint8)(powf((c) / 255.0f, 0.75f) * 255.0f))

static FontData *font_object(FontID);
static FontSlot *font_slot(FontID);

static FontID font_insert(FcPattern *);
static uint32 font_init_glyphmap(FontData *, uint);
static void *font_cache_glyph(FontData *, uint32);
static void font_free_glyphs(FontData *);
static uint32 font_hash_codepoint(const FontData *, uint32);
static void font_set_mapping(FontData *, uint32, uint32, uint32);
static uint32 font_lookup_glyph_index(const FontData *, uint32);

static bool extract_property(const FcPattern *, const char *, void *);
static uint32 compute_glyph_index(const FontData *, uint32);

static void dbg_print_freetype_bitmap(const FT_FaceRec *face);
static void dbg_print_font_object(const FontData *);

inline FontSlot *
font_slot(FontID font)
{
	if (!font) return NULL;

	ASSERT(FID_IDX(font) < arr_count(instance.slots));
	FontSlot *slot = instance.slots + FID_IDX(font);
	ASSERT(!slot->data || slot->generation == FID_GEN(font));

	return slot;
}

inline FontData *
font_object(FontID font)
{
	FontSlot *slot = font_slot(font);

	return (slot) ? slot->data : NULL;
}

uint32
font_hash_codepoint(const FontData *self, uint32 ucs4)
{
	const uint32 basehash = self->basehash;
	uint32 hash = ucs4 % basehash;
	uint32 offset = 0;

	for (;;) {
		const uint64 key = self->glyphmap[hash];

		if (!key || ucs4 == MAPKEY_UCS4(key)) {
			return hash;
		}

		if (!offset) {
			offset = ucs4 % (basehash - 2);
			offset += !offset;
		}
		hash += offset;
		hash -= (hash >= basehash) ? basehash : 0;
	}

	return 0;
}

uint32
font_lookup_glyph_index(const FontData *self, uint32 ucs4)
{
	uint32 hash = font_hash_codepoint(self, ucs4);

	if (self->glyphmap[hash]) {
		return MAPKEY_GIDX(self->glyphmap[hash]);
	}

	return 0;
}

void
font_set_mapping(FontData *self, uint32 hash, uint32 ucs4, uint32 index)
{
	if (!self->glyphmap[hash]) {
		self->num_entries++;
	}

	self->glyphmap[hash] = PACK_MAPKEY(ucs4, index);
}

uint32
compute_glyph_index(const FontData *self, uint32 ucs4)
{
	if (FcCharSetHasChar(self->charset, ucs4)) {
		return FcFreeTypeCharIndex(self->face, ucs4);
	}

	return 0;
}

bool
font_init(FontID font)
{
	FontData *self = font_object(font);
	if (!self) {
		return false;
	}
	ASSERT(self->dpy);

	struct FontConfig cfg = { 0 };

	extract_property(self->pattern, FC_PIXEL_SIZE, &cfg.pixelsize);
	extract_property(self->pattern, FC_DPI,        &cfg.dpi);
	extract_property(self->pattern, FC_ASPECT,     &cfg.aspect);
	extract_property(self->pattern, FC_RGBA,       &cfg.pixelformat);
	extract_property(self->pattern, FC_LCD_FILTER, &cfg.lcdfilter);
	extract_property(self->pattern, FC_MATRIX,     &cfg.matrix);
	extract_property(self->pattern, FC_EMBOLDEN,   &cfg.use_embolden);
	extract_property(self->pattern, FC_SPACING,    &cfg.spacing_style);
	extract_property(self->pattern, FC_HINTING,    &cfg.use_hinting);
	extract_property(self->pattern, FC_AUTOHINT,   &cfg.use_autohint);
	extract_property(self->pattern, FC_HINT_STYLE, &cfg.hint_style);
	extract_property(self->pattern, FC_CHAR_WIDTH, &cfg.char_width);
	extract_property(self->pattern, FC_VERTICAL_LAYOUT, &cfg.use_vertlayout);
	extract_property(self->pattern, FC_ANTIALIAS,  &cfg.use_antialias);
	extract_property(self->pattern, FC_CHARSET,    &cfg.charset);

	if (!self->face) {
		if (!instance.library) {
			if (!!FT_Init_FreeType(&instance.library)) {
				dbgprint("Failed to initialize FreeType");
				return false;
			}
		}
		FT_New_Face(instance.library, self->filepath, 0, &self->face);
		self->num_glyphs = self->face->num_glyphs + 1;
	}

	self->pixelsize = cfg.pixelsize;
	self->scale = 1.f;
	self->aspect = cfg.aspect;
	FT_Set_Char_Size(self->face, self->pixelsize * 64L, 0, 72U * self->aspect, 72U);

	FT_Set_Transform(self->face, &cfg.matrix, NULL);

	if (!(self->face->face_flags & FT_FACE_FLAG_SCALABLE)) {
		cfg.use_antialias = false;
	}
	if (cfg.hint_style == FC_HINT_NONE) {
		cfg.use_hinting = false;
	}

	int hintstyle;

	if (!cfg.use_antialias) {
		hintstyle = 0;
	} else if (cfg.use_autohint) {
		hintstyle = FontHintingAuto;
	} else if (cfg.use_hinting) {
		if (cfg.hint_style == FC_HINT_SLIGHT) {
			hintstyle = FontHintingLight;
		} else {
			hintstyle = FontHintingNormal;
		}
	}

	FT_Render_Mode rendermode;

	switch (cfg.pixelformat) {
	case FC_RGBA_RGB:
	case FC_RGBA_BGR:
		rendermode = FT_RENDER_MODE_LCD;
		break;
	case FC_RGBA_VRGB:
	case FC_RGBA_VBGR:
		rendermode = FT_RENDER_MODE_LCD_V;
		break;
	default:
		rendermode = FT_RENDER_MODE_NORMAL;
		break;
	}

	int target = FT_LOAD_TARGET_NORMAL;
	int loadflags = 0;
	int picformat = PictStandardA8;

	if (hintstyle == FontHintingLight) {
		if (rendermode == FT_RENDER_MODE_LCD) {
			picformat = PictStandardARGB32;
			target = FT_LOAD_TARGET_LCD;
		} else if (rendermode == FT_RENDER_MODE_LCD_V) {
			picformat = PictStandardARGB32;
			target = FT_LOAD_TARGET_LCD_V;
		} else {
			target = FT_LOAD_TARGET_LIGHT;
		}
	} else if (hintstyle == FontHintingAuto) {
		loadflags |= FT_LOAD_FORCE_AUTOHINT;
	} else if (!hintstyle) {
		loadflags |= FT_LOAD_NO_HINTING;
	}
	if (FT_HAS_COLOR(self->face)) {
		picformat = PictStandardARGB32;
		loadflags |= FT_LOAD_COLOR;
	} else if (!hintstyle) {
		loadflags |= FT_LOAD_NO_BITMAP|FT_LOAD_MONOCHROME;
		picformat = PictStandardA1;
		target = FT_LOAD_TARGET_MONO;
		rendermode = FT_RENDER_MODE_MONO;
	}

	FT_Size_Metrics mtx = self->face->size->metrics;
	if (memequal(&cfg.matrix, &FTMATRIX_DFL, sizeof(FT_Matrix))) {
		self->ascent  = +(mtx.ascender >> 6);
		self->descent = -(mtx.descender >> 6);
		self->height  = +(mtx.height >> 6);
		self->max_advance = DEFAULT(cfg.char_width, mtx.max_advance >> 6);

		BSET(self->attrs, FONTATTR_TRANSFORM, false);
	} else { // Transform the default metrics
		FT_Vector v = { 0 };

		v.x = 0, v.y = mtx.ascender;
		FT_Vector_Transform(&v, &cfg.matrix);
		self->ascent = +(v.y >> 6);

		v.x = 0, v.y = mtx.descender;
		FT_Vector_Transform(&v, &cfg.matrix);
		self->descent = -(v.y >> 6);

		v.x = 0, v.y = mtx.height;
		FT_Vector_Transform(&v, &cfg.matrix);
		self->height = +(v.y >> 6);

		v.x = mtx.max_advance, v.y = 0;
		FT_Vector_Transform(&v, &cfg.matrix);
		self->max_advance = +(v.x >> 6);

		self->loadflags |= FT_LOAD_NO_BITMAP;
		BSET(self->attrs, FONTATTR_TRANSFORM, true);
	}

	BSET(self->attrs, FONTATTR_EMBOLDEN, cfg.use_embolden);
	BSET(self->attrs, FONTATTR_MONOSPACE, (cfg.spacing_style == FC_MONO));
	BSET(self->attrs, FONTATTR_ANTIALIAS, cfg.use_antialias);

	if (!(self->attrs & FONTATTR_MONOSPACE)) {
		fprintf(stderr, "Warning: non-monospace fonts are not currently supported.\n");
	}

	self->loadflags = loadflags|target;
	self->rendermode = rendermode;
	self->pixelformat = cfg.pixelformat;
	self->lcdfilter = cfg.lcdfilter;
	self->matrix = cfg.matrix;

	self->picformat = XRenderFindStandardFormat(self->dpy, picformat);

	if (!cfg.charset) {
		cfg.charset = FcFreeTypeCharSet(self->face, NULL);
		if (!cfg.charset) {
			return false;
		}
	}
	self->charset = FcCharSetCopy(cfg.charset);
	self->num_codepoints = FcCharSetCount(self->charset);

	if (!font_init_glyphmap(self, self->num_codepoints)) {
		return false;
	}
	self->glyphset = XRenderCreateGlyphSet(self->dpy, self->picformat);
	self->bitmaps = xcalloc(self->num_glyphs, sizeof(*self->bitmaps));

	// override everything and pre-render the "missing glyph" glyph
	font_cache_glyph(self, 0);

	return true;
}

bool
font_load_glyph(FontID font, uint32 ucs4, FontID *out_font, uint32 *out_glyph)
{
	FontData *self = font_object(font);
	ASSERT(self);

	uint32 glyph = 0;
	uint32 hash = font_hash_codepoint(self, ucs4);
	bool result = false;

	if (!self->glyphmap[hash]) {
		glyph = compute_glyph_index(self, ucs4);
		if (glyph && font_cache_glyph(self, glyph)) {
			font_set_mapping(self, hash, ucs4, glyph);
			result = true;
		}
	} else {
		ASSERT(ucs4 == MAPKEY_UCS4(self->glyphmap[hash]));
		glyph = MAPKEY_GIDX(self->glyphmap[hash]);
	}

	SETPTR(out_font, font);
	SETPTR(out_glyph, glyph);

	return result;
}

void *
font_cache_glyph(FontData *self, uint32 index)
{
	FT_FaceRec *face = self->face;
	FT_GlyphSlotRec *active = face->glyph;

	static uchar local[4096];
	uchar *data = local;

	FT_Library_SetLcdFilter(instance.library, self->lcdfilter);
	{
		FT_Error err;
		err = FT_Load_Glyph(face, index, self->loadflags);
		if (!!err) {
			return NULL;
		}
		if (!self->width) {
			self->width = active->metrics.horiAdvance >> 6;
		}
		if (self->attrs & FONTATTR_EMBOLDEN) {
			FT_GlyphSlot_Embolden(active);
		}
		if (active->format != FT_GLYPH_FORMAT_BITMAP) {
			err = FT_Render_Glyph(active, self->rendermode);
			if (!!err) {
				return NULL;
			}
			active = face->glyph;
		}
	}
	FT_Library_SetLcdFilter(instance.library, FT_LCD_FILTER_NONE);

	ASSERT(face->glyph->format == FT_GLYPH_FORMAT_BITMAP);

	BitmapInfo bitmap = { 0 };

	bitmap.x_advance = self->width; // NOTE(ben): Monospace dependent
	bitmap.y_advance = 0; // NOTE(ben): Monospace dependent
	bitmap.x_bearing = -active->bitmap_left;
	bitmap.y_bearing = +active->bitmap_top;
	bitmap.width = active->bitmap.width;
	bitmap.height = active->bitmap.rows;
	bitmap.pitch = active->bitmap.pitch;

	switch (active->bitmap.pixel_mode) {
	case FT_PIXEL_MODE_MONO:
		bitmap.format = PixelFormatA1;
		bitmap.pitch = ALIGN_UP(bitmap.width, 32) >> 3;
		break;
	case FT_PIXEL_MODE_GRAY:
		bitmap.format = PixelFormatA8;
		bitmap.pitch = ALIGN_UP(bitmap.width, 4);
		break;
	case FT_PIXEL_MODE_BGRA:
		bitmap.format = PixelFormatBGRA32;
		bitmap.pitch = bitmap.width;
	}

	const size_t size = bitmap.pitch * bitmap.height;

	if (size) {
		if (size > sizeof(local)) {
			data = xmalloc(size, 1);
		}

		const uchar *src = active->bitmap.buffer;
		uchar *dst = data;

		switch (bitmap.format) {
		case PixelFormatA8:
			for (int y = 0; y < bitmap.height; y++) {
				memcpy(dst, src, bitmap.width);
				src += active->bitmap.pitch;
				dst += bitmap.pitch;
			}
			break;
		default:
			// TODO(ben): Figure out endianness and buffer formats for multi/fractional-byte bitmaps
			if (data != local) {
				free(data);
			}
			return NULL;
		}
	}

#if DEBUG_PRINT_GLYPHS
	dbg_print_freetype_bitmap(face);
#endif

	XGlyphInfo info = {
		.width  = bitmap.width,
		.height = bitmap.height,
		.x      = bitmap.x_bearing,
		.y      = bitmap.y_bearing,
		.xOff   = bitmap.x_advance,
		.yOff   = bitmap.y_advance
	};
	XRenderAddGlyphs(self->dpy, self->glyphset, (Glyph *)&index, &info, 1, (char *)data, size);

	if (data != local) {
		free(data);
	}

	memcpy(self->bitmaps + index, &bitmap, sizeof(*self->bitmaps));

	return self->bitmaps + index;
}

bool
extract_property(const FcPattern *pattern, const char *name, void *data)
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

	bool result = (FCRES_VALID(res) && FCRES_FOUND(res));
	// We set missing defaults ourselves in the injection phase so they should always be
	// succesfully extracted if this function is being called.
	ASSERT(result);

	return result;
}

uint32
font_init_glyphmap(FontData *self, uint num_codepoints)
{
	ASSERT(self);
	if (!num_codepoints) return 0;

	uint basehash = num_codepoints + (num_codepoints >> 1);
	basehash = ALIGN_UP(basehash, 2) + 1;
	while (!isprime(basehash)) {
		basehash += 2;
	}

	self->glyphmap = xcalloc(basehash, sizeof(*self->glyphmap));
	self->basehash = basehash;

	return basehash;
}

bool
font_get_extents(FontID font, int *width, int *height, int *ascent, int *descent)
{
	const FontData *self = font_object(font);

	if (self) {
		if (width)   *width   = self->width;
		if (height)  *height  = self->height;
		if (ascent)  *ascent  = self->ascent;
		if (descent) *descent = self->descent;

		return true;
	}

	return false;
}

FontID
font_insert(FcPattern *pattern)
{
	uint32 hashval = FcPatternHash(pattern);
	uint32 idx = arr_count(instance.slots);

	for (uint32 i = 0; i < arr_count(instance.slots); i++) {
		if (!instance.slots[i].data) {
			if (idx > i) {
				idx = i;
			}
		} else if (hashval == instance.slots[i].data->hashval) {
			FcPatternDestroy(pattern);
			return PACK_FID(i);
		}
	}
	if (idx == arr_count(instance.slots)) {
		arr_push(instance.slots, (FontSlot){ 1, NULL });
		ASSERT(idx == arr_count(instance.slots) - 1);
	}

	FontData *self = instance.slots[idx].data = xcalloc(1, sizeof(*self));

	FcChar8 *file;
	FcPatternGetString(pattern, FC_FILE, 0, &file);

	self->filepath = strdup((char *)file);
	self->pattern = pattern;
	self->hashval = hashval;

	return PACK_FID(idx);
}

void
font_destroy(FontID font)
{
	ASSERT(font);

	FontSlot *slot = font_slot(font);
	ASSERT(slot && slot->data);

	FcPatternDestroy(slot->data->pattern);
	font_free_glyphs(slot->data);
	FT_Done_Face(slot->data->face);
	free(slot->data->filepath);
	free(slot->data);

	slot->data = NULL;
	slot->generation++;
}

void
font_free_glyphs(FontData *target)
{
	if (target) {
		XRenderFreeGlyphSet(target->dpy, target->glyphset);
		FcCharSetDestroy(target->charset);
		FREE(target->glyphmap);
		FREE(target->bitmaps);
	}
}

FontID
font_create(const Win *pub, const char *name)
{
	const WinData *win = (const WinData *)pub;
	ASSERT(win);

	struct { FcPattern *base, *conf, *match; } pattern;
	FcResult res;
	FcValue dummy;

	if (!(pattern.base = FcNameParse((FcChar8 *)name))) {
		return 0;
	}
	pattern.conf = FcPatternDuplicate(pattern.base);
	assert(pattern.conf);

	// Acquire the bare pattern with the user-specified options only
	FcConfigSubstitute(NULL, pattern.conf, FcMatchPattern);

#define SETDFL(pat,T_,obj,val)                                  \
if (FcPatternGet((pat), (obj), 0, &dummy) == FcResultNoMatch) { \
	dbgprintf("Setting %s = %s\n", #obj, #val);                 \
	FcPatternAdd##T_((pat), (obj), (val));                      \
}
	// Set the our overrides if the user didn't specify them
	SETDFL(pattern.conf, Double,  FC_DPI, win->x11->dpi);
	if (FcPatternGet(pattern.conf, FC_RGBA, 0, &dummy) == FcResultNoMatch) {
		int order;
		switch (platform_get_pixel_format(&win->pub)) {
			case PixelFormatNone:   order = FC_RGBA_NONE; break;
			case PixelFormatHRGB24: order = FC_RGBA_RGB;  break;
			case PixelFormatHBGR24: order = FC_RGBA_BGR;  break;
			case PixelFormatVRGB24: order = FC_RGBA_VRGB; break;
			case PixelFormatVBGR24: order = FC_RGBA_VBGR; break;
			default: order = FC_RGBA_UNKNOWN;
		}
		FcPatternAddInteger(pattern.conf, FC_RGBA, order);
	}

	// Acquire the full pattern using the configuration pattern
	FcDefaultSubstitute(pattern.conf);
	pattern.match = FcFontMatch(NULL, pattern.conf, &res);

	// Set default properties if the matching font doesn't specify them.
	// This is so a property will always exist when we extract them later. Otherwise,
	// the logic for setting defaults becomes very convoluted.
	SETDFL(pattern.match, Double,  FC_ASPECT,     1.f);
	SETDFL(pattern.match, Bool,    FC_HINTING,    FcTrue);
	SETDFL(pattern.match, Integer, FC_HINT_STYLE, FC_HINT_SLIGHT);
	SETDFL(pattern.match, Bool,    FC_AUTOHINT,   FcFalse);
	SETDFL(pattern.match, Integer, FC_LCD_FILTER, FC_LCD_DEFAULT);
	SETDFL(pattern.match, Integer, FC_SPACING,    FC_MONO);
	SETDFL(pattern.match, Bool,    FC_VERTICAL_LAYOUT, FcFalse);
	SETDFL(pattern.match, Double,  FC_PIXEL_SIZE, 16.f);
	SETDFL(pattern.match, Bool,    FC_EMBOLDEN,   FcFalse);
	SETDFL(pattern.match, Bool,    FC_ANTIALIAS,  FcTrue);
	SETDFL(pattern.match, Integer, FC_CHAR_WIDTH, 0);
	SETDFL(pattern.match, Matrix,  FC_MATRIX,     &FCMATRIX_DFL);
	SETDFL(pattern.match, CharSet, FC_CHARSET,    NULL);
#undef SETDFL

	FcPatternDestroy(pattern.base);
	FcPatternDestroy(pattern.conf);

	FontID font = font_insert(pattern.match);
	FontData *self = font_object(font);

	if (self && !self->dpy) {
		ASSERT(font);
		self->dpy = win->x11->dpy;
	}

#if DEBUG_PRINT_PATTERN
	font_print_debug(font);
#endif

	return font;
}

FontID
font_create_derivative(FontID basefont, uint style)
{
	FontData *data = font_object(basefont);
	if (!data) {
		return 0;
	}

	struct { FcPattern *conf, *match; } pattern;
	FcResult res;

	pattern.conf = FcPatternDuplicate(data->pattern);
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

	FontID font = font_insert(pattern.match);
	FontData *self = font_object(font);

	if (self && !self->dpy) {
		self->dpy = data->dpy;
	}

#if DEBUG_PRINT_PATTERN
	font_print_debug(font);
#endif

	return font;
}

void
draw_rect(const Win *pub, uint32 color, int x_, int y_, int w_, int h_)
{
	WinData *win = (WinData *)pub;

	int x = CLAMP(x_, 0, (int)win->pub.w);
	int y = CLAMP(y_, 0, (int)win->pub.h);
	int w = CLAMP(x + w_, x, (int)win->pub.w) - x;
	int h = CLAMP(y + h_, y, (int)win->pub.h) - y;

	XRenderFillRectangle(win->x11->dpy,
	                     PictOpOver,
	                     win->pic,
	                     &XRENDER_COLOR(color),
	                     x, y, w, h);
}

void
draw_text_utf8(const Win *pub, const GlyphRender *glyphs, uint max, int x, int y)
{
	ASSERT(pub);
	WinData *win = (WinData *)pub;

	if (!max || !glyphs) return;

	struct {
		FontID font;
		uint32 bg;
		uint32 fg;
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
		FontID font, glyph;
		font_load_glyph(brush.font, glyphs[i].ucs4, &font, &glyph);

		const FontData *self = font_object(font);
		ASSERT(self);

		const BitmapInfo *bitmap = self->bitmaps + glyph;

		int chdx = bitmap->x_advance;
		int chdy = bitmap->y_advance;
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
			pos.ky = self->ascent;
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
		elts.buf[elts.n].glyphset = self->glyphset;
		elts.buf[elts.n].nchars++;
		text.buf[text.n++] = glyph;

		flushed = false;
		// accumulate position so we can set the new leading elt after drawing
		pos.x1 += (pos.dx = chdx);
		pos.y1 += (pos.dy = chdy);

		// If none of the local buffers are at capacity, keep going.
		// Otherwise, just draw now and reset everything at the new orgin
		if (i + 1 < max && elts.n < LEN(elts.buf) && text.n < LEN(text.buf)) {
			if (glyphs[i+1].fg == brush.fg &&
			    glyphs[i+1].bg == brush.bg &&
			    glyphs[i+1].font == font)
			{
				continue;
			}
		}

		if (brush.bg >> 24) {
			draw_rect(&win->pub,
				brush.bg,
				pos.x0,
				pos.y0,
				pos.x1 - pos.x0,
				self->ascent + self->descent
			);
		}

		// render elt buffer
		XRenderCompositeText32(win->x11->dpy,
			               PictOpOver,
			               win_get_color_handle(&win->pub, brush.fg),
			               win->pic,
			               self->picformat,
			               0, 0,
			               elts.buf[0].xOff,
			               elts.buf[0].yOff,
			               elts.buf, elts.n + 1);

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
font_print_debug(FontID font)
{
	const FontData *self = font_object(font);

	if (self) {
		dbg_print_font_object(self);
	}
}

void
dbg_print_font_object(const FontData *self)
{
	dbgprintfl("FontData(%p) HashID: %#.08x", (void *)self, self->hashval);
	FcPatternPrint(self->pattern);
}

void
dbg_print_freetype_bitmap(const FT_FaceRec *face)
{
	const FT_GlyphSlotRec *slot = face->glyph;

	int x_advance = slot->advance.x >> 6;
	int y_advance = slot->advance.y >> 6;
	int x_bearing = slot->bitmap_left;
	int y_bearing = slot->bitmap_top;
	int width = slot->bitmap.width;
	int height = slot->bitmap.rows;
	int pitch = slot->bitmap.pitch;

	fprintf(stderr,
		"GLYPH(%u)\n"
		"{\n"
		"  Face\n"
		"    ascender    = %d\n"
		"    descender   = %d\n"
		"    height      = %d\n"
		"    max_advance_width  = %d\n"
		"    max_advance_height = %d\n"
		"  Bitmap\n"
		"    width       = %d\n"
		"    height      = %d\n"
		"    pitch       = %d\n"
		"    size        = %d\n"
		"    advance.x   = %d\n"
		"    advance.y   = %d\n"
		"    bearing.x   = %d\n"
		"    bearing.y   = %d\n"
		"  Metrics\n"
		"    width       = %ld\n"
		"    height      = %ld\n"
		"    h_advance   = %ld\n"
		"    v_advance   = %ld\n"
		"    h_bearing.x = %ld\n"
		"    h_bearing.y = %ld\n"
		"    v_bearing.x = %ld\n"
		"    v_bearing.y = %ld\n"
		"\n",
		slot->glyph_index,
		face->ascender >> 6,
		face->descender >> 6,
		face->height >> 6,
		face->max_advance_width >> 6,
		face->max_advance_height >> 6,
		width, height, pitch, pitch * height,
		x_advance, y_advance,
		x_bearing, y_bearing,
		slot->metrics.width >> 6,
		slot->metrics.height >> 6,
		slot->metrics.horiAdvance >> 6,
		slot->metrics.vertAdvance >> 6,
		slot->metrics.horiBearingX >> 6,
		slot->metrics.horiBearingY >> 6,
		slot->metrics.vertBearingX >> 6,
		slot->metrics.vertBearingY >> 6
	);

	const uchar *pixels = slot->bitmap.buffer;
	const uint8 startcolor = 235;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < pitch; x++) {
			fprintf(stderr, "%s\033[48;5;%d;38;5;66m.\033[00m%s",
				(x == 0) ? "    " : "",
				(x < width) ? startcolor + (int)((255 - startcolor) * (pixels[x] / 255.f)) : 232,
				(x + 1 == pitch) ? "\n" : ""
			);
		}
		pixels += pitch;
	}
	fprintf(stderr, "}\n\n");
}

