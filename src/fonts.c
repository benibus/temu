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

#ifndef NDEBUG
  #define DEBUG_PRINT_GLYPHS  0
  #define DEBUG_PRINT_PATTERN 0
#else
  #define DEBUG_PRINT_GLYPHS  0
  #define DEBUG_PRINT_PATTERN 0
#endif

#define FONTATTR_MONOSPACE (1 << 0)
#define FONTATTR_EMBOLDEN  (1 << 1)
#define FONTATTR_TRANSFORM (1 << 2)
#define FONTATTR_ANTIALIAS (1 << 3)
#define FONTATTR_COLOR     (1 << 4)

struct FontData {
	void *generic;

	FT_FaceRec *face;
	FT_Matrix matrix;
	FT_Int loadflags;
	FT_Int loadtarget;
	FT_Render_Mode rendermode;
	FT_LcdFilter lcdfilter;

	FcPattern *pattern;
	FcCharSet *charset;

	uint64 *glyphmap;
	uint32 basehash;
	Bitmap *bitmaps;
	int depth;

	uint num_codepoints;
	uint num_glyphs;
	uint num_entries;

	uint16 attrs;
	float pixsize;
	float scale;
	float aspect;
	int width;
	int height;
	int ascent;
	int descent;
	int max_advance;
};

struct FontDesc {
	FcPattern *pattern;
	float pixsize;
	float aspect;
	int depth;
	FT_Matrix matrix;
	FT_LcdFilter lcdfilter;
	FT_Int loadtarget;
	FT_Int loadflags;
	FT_Render_Mode rendermode;
	int hintstyle;
	FcCharSet *charset;
	uint16 attrs;
};

struct FontSet {
	FcFontSet *fcset;
	FontData fonts[4];
};

struct FontManager {
	FT_Library library;
	double dpi;
	struct FontSet sets[2];
	struct {
		FontHookCreate create;
		FontHookAdd add;
		FontHookDestroy destroy;
	} hooks;
};

static struct FontManager instance;

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

static void font__create_from_desc(FontData *font, struct FontDesc desc);
static uint32 font__create_glyphmap(FontData *font, uint);
static uint32 font__compute_glyph_index(const FontData *, uint32);
static void font__set_glyph_mapping(FontData *font, uint32 hash, uint32 ucs4, uint32 index);
static uint32 font__lookup_glyph_index(const FontData *font, uint32 ucs4);
static uint32 font__hash_codepoint(const FontData *font, uint32 ucs4);
static void *font__cache_glyph(FontData *font, uint32 glyph);
static int font__compute_nominal_width(const FontData *font);
static bool get_pattern_desc(FcPattern *pat, struct FontDesc *desc_);
static void dbg_print_freetype_bitmap(const FT_FaceRec *face);

bool
fontmgr_configure(double dpi, FontHookCreate  hook_create,
                              FontHookAdd     hook_add,
                              FontHookDestroy hook_destroy)
{
	if (instance.library) {
		return true;
	}

	if (FT_Init_FreeType(&instance.library)) {
		dbgprint("Failed to initialize FreeType");
		return false;
	}

	instance.dpi = dpi;
	instance.hooks.create  = hook_create;
	instance.hooks.add     = hook_add;
	instance.hooks.destroy = hook_destroy;

	return true;
}

FontSet *
fontmgr_create_fontset(const char *name)
{
	FcPattern *pat_base = FcPatternDuplicate(FcNameParse((FcChar8 *)name));
	FcPatternPrint(pat_base);

	FcConfigSubstitute(NULL, pat_base, FcMatchPattern);
	FcPatternPrint(pat_base);

	FcValue fcval;

	if (FcPatternGet(pat_base, FC_DPI, 0, &fcval) != FcResultMatch) {
		FcPatternAddDouble(pat_base, FC_DPI, instance.dpi);
	}
	if (FcPatternGet(pat_base, FC_RGBA, 0, &fcval) != FcResultMatch) {
		FcPatternAddInteger(pat_base, FC_RGBA, FC_RGBA_UNKNOWN);
	}

	FcDefaultSubstitute(pat_base);

	FcPattern *pats[4];

	pats[0] = FcPatternDuplicate(pat_base);

	if (!pats[0]) return 0;

	if (FcPatternGet(pats[0], FC_SLANT, 0, &fcval) == FcResultMatch) {
		FcPatternDel(pats[0], FC_SLANT);
	}
	if (FcPatternGet(pats[0], FC_WEIGHT, 0, &fcval) == FcResultMatch) {
		FcPatternDel(pats[0], FC_WEIGHT);
	}

	pats[1] = FcPatternDuplicate(pats[0]);
	pats[2] = FcPatternDuplicate(pats[0]);
	pats[3] = FcPatternDuplicate(pats[0]);

	FcPatternAddInteger(pats[0], FC_SLANT,  FC_SLANT_ROMAN);
	FcPatternAddInteger(pats[0], FC_WEIGHT, FC_WEIGHT_REGULAR);
	FcPatternAddInteger(pats[1], FC_SLANT,  FC_SLANT_ROMAN);
	FcPatternAddInteger(pats[1], FC_WEIGHT, FC_WEIGHT_BOLD);
	FcPatternAddInteger(pats[2], FC_SLANT,  FC_SLANT_ITALIC);
	FcPatternAddInteger(pats[2], FC_WEIGHT, FC_WEIGHT_REGULAR);
	FcPatternAddInteger(pats[3], FC_SLANT,  FC_SLANT_ITALIC);
	FcPatternAddInteger(pats[3], FC_WEIGHT, FC_WEIGHT_BOLD);

	FcFontSet *fcset = FcFontSetCreate();

	for (uint i = 0; i < LEN(pats); i++) {
		if (pats[i]) {
			FcResult result;
			FcPattern *pat = FcFontMatch(NULL, pats[i], &result);
			if (pat || (pat = pats[0])) {
				FcFontSetAdd(fcset, pat);
			}
			FcPatternDestroy(pats[i]);
		}
	}

	FcFontSetPrint(fcset);

	FcPatternDestroy(pat_base);

	if (!fcset->nfont) {
		FcFontSetDestroy(fcset);
		return NULL;
	}

	struct FontSet *set = &instance.sets[0];

	for (int i = 0; i < fcset->nfont; i++) {
		struct FontDesc desc = {0};
		if (get_pattern_desc(fcset->fonts[i], &desc)) {
			FontData *font = &set->fonts[i];

			font__create_from_desc(font, desc);
			font->width = font__compute_nominal_width(font);
			if (!font__create_glyphmap(font, font->num_codepoints)) {
				return 0;
			}
			if (instance.hooks.create) {
				font->generic = instance.hooks.create(font->depth);
			}
			font->bitmaps = xcalloc(font->num_glyphs, sizeof(*font->bitmaps));
			font__cache_glyph(font, 0);
		}
	}

	return set;
}

bool
fontset_get_metrics(const FontSet *fontset, int *width, int *height, int *ascent, int *descent)
{
	if (fontset) {
		if (width)   *width   = fontset->fonts[0].width;
		if (height)  *height  = fontset->fonts[0].height;
		if (ascent)  *ascent  = fontset->fonts[0].ascent;
		if (descent) *descent = fontset->fonts[0].descent;

		return true;
	}

	return false;
}

FontData *
fontset_get_font(FontSet *fontset, uint idx)
{
	return &fontset->fonts[idx];
}

FontGlyph
fontset_get_codepoint(FontSet *fontset, uint idx, uint32 ucs4)
{
	ASSERT(fontset);

	FontData *font = &fontset->fonts[idx];
	uint32 glyph = 0;
	uint32 hash = font__hash_codepoint(font, ucs4);

	if (!font->glyphmap[hash]) {
		glyph = font__compute_glyph_index(font, ucs4);
		if (glyph && font__cache_glyph(font, glyph)) {
			font__set_glyph_mapping(font, hash, ucs4, glyph);
		}
	} else {
		ASSERT(ucs4 == MAPKEY_UCS4(font->glyphmap[hash]));
		glyph = MAPKEY_GIDX(font->glyphmap[hash]);
	}

	return (FontGlyph){ .font = font, .glyph = glyph };
}

bool
get_pattern_desc(FcPattern *pat, struct FontDesc *desc_)
{
	struct FontDesc desc = {
		.pattern    = pat,
		.pixsize    = 16.f,
		.aspect     = 1.f,
		.matrix     = FTMATRIX_DFL,
		.hintstyle  = FontHintingLight,
		.lcdfilter  = FT_LCD_FILTER_DEFAULT,
		.loadflags  = 0,
		.loadtarget = FT_LOAD_TARGET_LIGHT,
		.rendermode = FT_RENDER_MODE_NORMAL,
		.depth      = 8,
		.attrs      = FONTATTR_ANTIALIAS
	};

	FcResult fcres;
	FcValue fcval;

	if (FcPatternGet(pat, FC_PIXEL_SIZE, 0, &fcval) == FcResultMatch) {
		desc.pixsize = fcval.u.d;
	}
	if (FcPatternGet(pat, FC_ASPECT, 0, &fcval) == FcResultMatch) {
		desc.aspect = fcval.u.d;
	}
	if (FcPatternGet(pat, FC_MATRIX, 0, &fcval) == FcResultMatch) {
		if (!memequal(fcval.u.m, &FCMATRIX_DFL, sizeof(FcMatrix))) {
			desc.attrs |= FONTATTR_TRANSFORM;
			desc.matrix.xx = 0x10000 * fcval.u.m->xx;
			desc.matrix.xy = 0x10000 * fcval.u.m->xy;
			desc.matrix.yx = 0x10000 * fcval.u.m->yx;
			desc.matrix.yy = 0x10000 * fcval.u.m->yy;
		}
	}
	if (FcPatternGet(pat, FC_EMBOLDEN, 0, &fcval) == FcResultMatch) {
		if (fcval.u.b) {
			desc.attrs |= FONTATTR_EMBOLDEN;
		}
	}
	if (FcPatternGet(pat, FC_HINTING, 0, &fcval) == FcResultMatch) {
		if (fcval.u.b) {
			if (FcPatternGet(pat, FC_HINT_STYLE, 0, &fcval) == FcResultMatch) {
				if (fcval.u.i == FC_HINT_NONE) {
					desc.hintstyle = 0;
				} else if (fcval.u.i != FC_HINT_SLIGHT) {
					desc.hintstyle = FontHintingFull;
				}
			}
		} else {
			desc.hintstyle = 0;
		}
		if (desc.hintstyle && FcPatternGet(pat, FC_AUTOHINT, 0, &fcval) == FcResultMatch) {
			if (fcval.u.b) {
				desc.hintstyle = FontHintingAuto;
			}
		}
	}
	if (FcPatternGet(pat, FC_ANTIALIAS, 0, &fcval) == FcResultMatch) {
		if (!fcval.u.b) {
			desc.attrs &= ~FONTATTR_ANTIALIAS;
			desc.hintstyle = 0;
		}
	}
	if (FcPatternGet(pat, FC_LCD_FILTER, 0, &fcval) == FcResultMatch) {
		if (desc.hintstyle && fcval.u.i != FC_LCD_NONE) {
			if (fcval.u.i == FC_LCD_LIGHT) {
				desc.lcdfilter = FT_LCD_FILTER_LIGHT;
			} else if (fcval.u.i == FC_LCD_LEGACY) {
				desc.lcdfilter = FT_LCD_FILTER_LEGACY;
			}
		} else {
			desc.lcdfilter = FT_LCD_FILTER_NONE;
		}
	}
	if (FcPatternGet(pat, FC_RGBA, 0, &fcval) == FcResultMatch) {
		if (fcval.u.i != FC_RGBA_UNKNOWN && fcval.u.i != FC_RGBA_NONE) {
			desc.rendermode = FT_RENDER_MODE_LCD;
		}
	}
	if (FcPatternGet(pat, FC_COLOR, 0, &fcval) == FcResultMatch) {
		if (fcval.u.b) {
			desc.attrs |= FONTATTR_COLOR;
			desc.hintstyle = 0;
			desc.rendermode = FT_RENDER_MODE_LCD;
		}
	}
	if (FcPatternGet(pat, FC_CHARSET, 0, &fcval) == FcResultMatch) {
		if (fcval.u.c) {
			desc.charset = FcCharSetCopy((FcCharSet *)fcval.u.c);
		}
	}

	if (desc.attrs & FONTATTR_COLOR) {
		ASSERT(!desc.hintstyle);
		desc.loadflags = FT_LOAD_COLOR;
	} else if (desc.hintstyle) {
		if (desc.hintstyle == FontHintingAuto) {
			desc.loadflags = FT_LOAD_FORCE_AUTOHINT;
		} else if (desc.rendermode == FT_RENDER_MODE_LCD) {
			desc.loadtarget = FT_LOAD_TARGET_LCD;
		} else if (desc.hintstyle != FontHintingLight) {
			desc.loadtarget = FT_LOAD_TARGET_NORMAL;
		}
	} else {
		desc.loadflags = FT_LOAD_NO_HINTING|FT_LOAD_NO_BITMAP|FT_LOAD_MONOCHROME;
		desc.loadtarget = FT_LOAD_TARGET_MONO;
		desc.rendermode = FT_RENDER_MODE_MONO;
	}
	if (desc.rendermode == FT_RENDER_MODE_MONO) {
		desc.depth = 1;
	} else if (desc.rendermode == FT_RENDER_MODE_LCD) {
		desc.depth = 32;
	} else {
		desc.depth = 8;
	}

	if (desc_) {
		*desc_ = desc;
	}

	return true;
}

void
font__create_from_desc(FontData *font, struct FontDesc desc)
{
	ASSERT(font);

	if (!instance.library) {
		if (!!FT_Init_FreeType(&instance.library)) {
			dbgprint("Failed to initialize FreeType");
			return;
		}
	}

	FcChar8 *filepath = NULL;
	if (FcPatternGetString(desc.pattern, FC_FILE, 0, &filepath) != FcResultMatch) {
		return;
	}
	FT_New_Face(instance.library, (char *)filepath, 0, &font->face);
	FT_Set_Char_Size(font->face, desc.pixsize * 64, 0, 72 * desc.aspect, 72);
	FT_Set_Transform(font->face, &desc.matrix, NULL);

	FT_Size_Metrics metrics = font->face->size->metrics;
	if (desc.attrs & FONTATTR_TRANSFORM) {
		FT_Vector v[4] = {
			{ .x = 0, .y = metrics.ascender  },
			{ .x = 0, .y = metrics.descender },
			{ .x = 0, .y = metrics.height    },
			{ .x = metrics.max_advance, .y = 0 }
		};
		FT_Vector_Transform(&v[0], &desc.matrix), font->ascent  = +(v[0].y >> 6);
		FT_Vector_Transform(&v[1], &desc.matrix), font->descent = -(v[1].y >> 6);
		FT_Vector_Transform(&v[2], &desc.matrix), font->height  = +(v[2].y >> 6);
		FT_Vector_Transform(&v[3], &desc.matrix), font->max_advance = +(v[3].x >> 6);
	} else {
		font->ascent  = +(metrics.ascender >> 6);
		font->descent = -(metrics.descender >> 6);
		font->height  = +(metrics.height >> 6);
		font->max_advance = +(metrics.max_advance >> 6);
	}

	if (!desc.charset) {
		font->charset = FcFreeTypeCharSet(font->face, NULL);
	} else {
		font->charset = desc.charset;
	}
	font->num_codepoints = FcCharSetCount(font->charset);
	font->num_glyphs = font->face->num_glyphs + 1;

	font->attrs      = desc.attrs;
	font->pixsize    = desc.pixsize;
	font->aspect     = desc.aspect;
	font->depth      = desc.depth;
	font->matrix     = desc.matrix;
	font->loadflags  = desc.loadflags;
	font->loadtarget = desc.loadtarget;
	font->rendermode = desc.rendermode;
	font->lcdfilter  = desc.lcdfilter;
	font->pattern    = desc.pattern;
}

uint32
font__create_glyphmap(FontData *font, uint num_codepoints)
{
	ASSERT(font);
	if (!num_codepoints) return 0;

	uint basehash = num_codepoints + (num_codepoints >> 1);
	basehash = ALIGN_UP(basehash, 2) + 1;
	while (!isprime(basehash)) {
		basehash += 2;
	}

	font->glyphmap = xcalloc(basehash, sizeof(*font->glyphmap));
	font->basehash = basehash;

	return basehash;
}

int
font__compute_nominal_width(const FontData *font)
{
	int result;

	FT_Library_SetLcdFilter(instance.library, font->lcdfilter);
	FT_Error fterr = FT_Load_Char(font->face, ' ', font->loadflags|font->loadtarget);
	if (fterr) {
		result = -1;
	} else {
		result = font->face->glyph->metrics.horiAdvance >> 6;
	}
	FT_Library_SetLcdFilter(instance.library, FT_LCD_FILTER_NONE);

	return result;
}

uint32
font__hash_codepoint(const FontData *font, uint32 ucs4)
{
	const uint32 basehash = font->basehash;
	uint32 hash = ucs4 % basehash;
	uint32 offset = 0;

	for (;;) {
		const uint64 key = font->glyphmap[hash];

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
font__lookup_glyph_index(const FontData *font, uint32 ucs4)
{
	uint32 hash = font__hash_codepoint(font, ucs4);

	if (font->glyphmap[hash]) {
		return MAPKEY_GIDX(font->glyphmap[hash]);
	}

	return 0;
}

void
font__set_glyph_mapping(FontData *font, uint32 hash, uint32 ucs4, uint32 glyph)
{
	if (!font->glyphmap[hash]) {
		font->num_entries++;
	}

	font->glyphmap[hash] = PACK_MAPKEY(ucs4, glyph);
}

uint32
font__compute_glyph_index(const FontData *font, uint32 ucs4)
{
	if (FcCharSetHasChar(font->charset, ucs4)) {
		return FcFreeTypeCharIndex(font->face, ucs4);
	}

	return 0;
}

void *
font_get_generic(const FontData *font)
{
	if (font) {
		return font->generic;
	}

	return NULL;
}

Bitmap *
font_get_glyph_bitmap(const FontData *font, uint32 glyph)
{
	Bitmap *result = &font->bitmaps[glyph];

	return result;
}

void *
font__cache_glyph(FontData *font, uint32 glyph)
{
	ASSERT(font);

	if (font->bitmaps[glyph].data) {
		return &font->bitmaps[glyph];
	}

	FT_FaceRec *face = font->face;
	FT_GlyphSlotRec *active = face->glyph;

	FT_Library_SetLcdFilter(instance.library, font->lcdfilter);
	{
		FT_Error err;
		err = FT_Load_Glyph(face, glyph, font->loadflags|font->loadtarget);
		if (err) {
			return NULL;
		}
		if (font->attrs & FONTATTR_EMBOLDEN) {
			FT_GlyphSlot_Embolden(active);
		}
		if (active->format != FT_GLYPH_FORMAT_BITMAP) {
			err = FT_Render_Glyph(active, font->rendermode);
			if (err) {
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
	Bitmap src = {
		.ascent    = font->ascent,
		.descent   = font->descent,
		.x_advance = font->width,
		.y_advance = 0,
		.x_bearing = -active->bitmap_left,
		.y_bearing = active->bitmap_top,
		.width     = active->bitmap.width,
		.height    = active->bitmap.rows,
		.pitch     = active->bitmap.pitch,
		.data      = active->bitmap.buffer
	};
	Bitmap dst = src;

	switch (active->bitmap.pixel_mode) {
	case FT_PIXEL_MODE_MONO:
		dst.pitch = ALIGN_UP(src.width, 32) >> 3;
		break;
	case FT_PIXEL_MODE_BGRA:
		dst.pitch = src.width * 4;
		break;
	case FT_PIXEL_MODE_GRAY:
	default:
		dst.pitch = ALIGN_UP(src.width, 4);
		break;
	}

	static uchar local[4096] = { 0 };
	uchar *buf = local;

	if (dst.pitch * dst.height > (int)sizeof(local)) {
		buf = xmalloc(dst.pitch * dst.height, 1);
	}
	dst.data = buf;

	uchar *ps = src.data;
	uchar *pd = buf;

	Bitmap *bitmap = NULL;

	switch (active->bitmap.pixel_mode) {
	case FT_PIXEL_MODE_MONO:
		goto cleanup;
	case FT_PIXEL_MODE_GRAY:
		switch (font->depth) {
		// grayscale to grayscale
		case 8:
			for (int y = 0; y < dst.height; y++) {
				memcpy(pd, ps, dst.width);
				ps += src.pitch;
				pd += dst.pitch;
			}
			break;
		// grayscale to ARGB
		case 32:
			for (int y = 0; y < dst.height; y++) {
				for (int x = 0; x < dst.width; x++) {
					((uint32 *)pd)[x] = (
						(ps[x] << 24)|
						(ps[x] << 16)|
						(ps[x] <<  8)|
						(ps[x] <<  0)
					);
				}
				ps += src.pitch;
				pd += dst.pitch;
			}
			break;
		default:
			goto cleanup;
		}
		break;
	case FT_PIXEL_MODE_BGRA:
		switch (font->depth) {
		// BGRA to grayscale
		case 8:
			goto cleanup;
		// BGRA to ARGB
		case 32:
			// TODO(ben): Almost certainly wrong. Need to test with color glyphs.
			for (int y = 0; y < dst.height; y++) {
				for (int x = 0; x < dst.width; x += 4) {
					uint32 pixel = (
						(ps[x+3] << 24)|
						(ps[x+2] << 16)|
						(ps[x+1] <<  8)|
						(ps[x+0] <<  0)
					);
					*((uint32 *)(pd + x)) = pixel;
				}
				ps += src.pitch * 4;
				pd += dst.pitch;
			}
			break;
		default:
			goto cleanup;
		}
		break;
	default:
		goto cleanup;
	}

	bitmap = &font->bitmaps[glyph];
	bitmap[0] = dst;

	if (instance.hooks.add) {
		instance.hooks.add(font->generic, glyph, bitmap);
	}
cleanup:
	if (buf != local) {
		free(buf);
	}

	return bitmap;
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

