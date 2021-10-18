#include "utils.h"
#include "fonts.h"
#include "window.h"
#include "render.h"

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

#define SETPTR(p,v) do { if (p) { *(p) = (v); } } while (0)

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

	GlyphCache *cache;

	uint64 *glyphmap;
	uint32 basehash;

	uint num_codepoints;
	uint num_glyphs;
	uint num_entries;

	uint16 attrs;
	PixelFormat pixelformat;

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
	int rgba;
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

	struct FontConfig cfg = { 0 };

	extract_property(self->pattern, FC_PIXEL_SIZE, &cfg.pixelsize);
	extract_property(self->pattern, FC_DPI,        &cfg.dpi);
	extract_property(self->pattern, FC_ASPECT,     &cfg.aspect);
	extract_property(self->pattern, FC_RGBA,       &cfg.rgba);
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

	switch (cfg.rgba) {
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
	PixelFormat pixelformat = PixelFormatAlpha;

	if (hintstyle == FontHintingLight) {
		if (rendermode == FT_RENDER_MODE_LCD) {
			pixelformat = PixelFormatLCDH;
			target = FT_LOAD_TARGET_LCD;
		} else if (rendermode == FT_RENDER_MODE_LCD_V) {
			pixelformat = PixelFormatLCDV;
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
		pixelformat = PixelFormatRGBA;
		loadflags |= FT_LOAD_COLOR;
	} else if (!hintstyle) {
		loadflags |= FT_LOAD_NO_BITMAP|FT_LOAD_MONOCHROME;
		pixelformat = PixelFormatMono;
		target = FT_LOAD_TARGET_MONO;
		rendermode = FT_RENDER_MODE_MONO;
	}

	FT_Size_Metrics metrics = self->face->size->metrics;
	if (memequal(&cfg.matrix, &FTMATRIX_DFL, sizeof(FT_Matrix))) {
		self->ascent  = +(metrics.ascender >> 6);
		self->descent = -(metrics.descender >> 6);
		self->height  = +(metrics.height >> 6);
		self->max_advance = DEFAULT(cfg.char_width, metrics.max_advance >> 6);

		BSET(self->attrs, FONTATTR_TRANSFORM, false);
	} else { // Transform the default metrics
		FT_Vector v = { 0 };

		v.x = 0, v.y = metrics.ascender;
		FT_Vector_Transform(&v, &cfg.matrix);
		self->ascent = +(v.y >> 6);

		v.x = 0, v.y = metrics.descender;
		FT_Vector_Transform(&v, &cfg.matrix);
		self->descent = -(v.y >> 6);

		v.x = 0, v.y = metrics.height;
		FT_Vector_Transform(&v, &cfg.matrix);
		self->height = +(v.y >> 6);

		v.x = metrics.max_advance, v.y = 0;
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
	self->pixelformat = pixelformat;
	self->lcdfilter = cfg.lcdfilter;
	self->matrix = cfg.matrix;

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
	self->cache = glyphcache_create(self->num_glyphs, pixelformat, self->ascent, self->descent);

	// override everything and pre-render the "missing glyph" glyph
	font_cache_glyph(self, 0);

	return true;
}

bool
font_load_codepoint(FontID font, uint32 ucs4, FontID *out_font, uint32 *out_glyph)
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

#if DEBUG_PRINT_GLYPHS
	dbg_print_freetype_bitmap(face);
#endif

	GlyphInfo info = { 0 };
	info.x_advance = self->width; // NOTE(ben): Monospace dependent
	info.y_advance = 0; // NOTE(ben): Monospace dependent
	info.x_bearing = -active->bitmap_left;
	info.y_bearing = +active->bitmap_top;
	info.width = active->bitmap.width;
	info.height = active->bitmap.rows;
	info.pitch = active->bitmap.pitch;

	switch (active->bitmap.pixel_mode) {
	case FT_PIXEL_MODE_MONO: info.format = PixelFormatMono;  break;
	case FT_PIXEL_MODE_BGRA: info.format = PixelFormatRGBA;  break;
	case FT_PIXEL_MODE_GRAY: info.format = PixelFormatAlpha; break;
	default: info.format = PixelFormatAlpha; break;
	}

	void *result = glyphcache_submit_bitmap(self->cache, index, active->bitmap.buffer, info);

	return result;
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
		FcCharSetDestroy(target->charset);
		FREE(target->glyphmap);
		glyphcache_destroy(target->cache);
	}
}

FontID
font_create(const char *name)
{
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
	// TODO(ben): Get DPI from the platform
	SETDFL(pattern.conf, Double,  FC_DPI, 96.f);
	if (FcPatternGet(pattern.conf, FC_RGBA, 0, &dummy) == FcResultNoMatch) {
#if 0
		int order;
		switch (platform_get_pixel_format(win)) {
			case PixelFormatNone: order = FC_RGBA_NONE; break;
			case PixelFormatLCDH: order = FC_RGBA_RGB;  break;
			case PixelFormatLCDV: order = FC_RGBA_VRGB; break;
			default: order = FC_RGBA_UNKNOWN;
		}
		FcPatternAddInteger(pattern.conf, FC_RGBA, order);
#else
		FcPatternAddInteger(pattern.conf, FC_RGBA, FC_RGBA_UNKNOWN);
#endif
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

#if DEBUG_PRINT_PATTERN
	font_print_debug(font);
#endif

	return font;
}

void *
font_get_render_data(FontID font)
{
	FontData *self = font_object(font);

	if (!self) return NULL;

	return self->cache;
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

