#include "utils.h"
#include "fonts.h"
#include "opengl.h"

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

#define PIXEL_ALIGN 4
#define MIN_PADDING 1

struct GlyphMapping {
	bool status;
	uint32 idx;
	uint32 ucs4;
};

typedef struct {
	FT_FaceRec *face;
	FT_Matrix matrix;
	FT_Int loadflags;
	FT_Int loadtarget;
	FT_Render_Mode rendermode;
	FT_LcdFilter lcdfilter;

	FcPattern *pattern;
	FcCharSet *charset;

	struct GlyphMapping *glyphmap;
	uint32 basehash;
	int depth;
	uchar *bitmap;
	Glyph *glyphs;

	uint num_codepoints;
	uint num_glyphs;
	uint num_mapped;

	uint16 attrs;
	float pixsize;
	float scale;
	float aspect;
	int pitch;
	int width;
	int height;
	int ascent;
	int descent;
	int max_advance;
	int max_width;
	int max_height;

	uint texid;
	int tex_width;
	int tex_height;
	int tile_width;
	int tile_height;
	int square;
	int lpad;
	int rpad;
	int vpad;
} Font;

struct FontDesc {
	FcPattern *pattern;
	float pixsize;
	float aspect;
	FT_Matrix matrix;
	FT_LcdFilter lcdfilter;
	FT_Render_Mode rendermode;
	int hintstyle;
	FcCharSet *charset;
	uint16 attrs;
};

struct FontSet_ {
	FcFontSet *fcset;
	Font fonts[FontStyleCount];
};

struct FontManager {
	FT_Library library;
	double dpi;
	FontSet set;
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

static void font_create_from_desc(Font *font, struct FontDesc desc);
static uint32 font_create_glyphmap(Font *font, uint);
static Glyph *font_render_glyph(Font *font, uint32 glyph);
static int font_compute_nominal_width(const Font *font);
static bool get_pattern_desc(FcPattern *pat, struct FontDesc *desc_);
static uint32 hash_codepoint(const struct GlyphMapping *, uint32, uint32);
static uint32 query_file_glyph_index(FT_FaceRec *, const FcCharSet *, uint32);
static void dbg_print_freetype_bitmap(const FT_FaceRec *face);

bool
fontmgr_init(double dpi)
{
	if (instance.library) {
		return true;
	}

	if (FT_Init_FreeType(&instance.library)) {
		dbgprint("Failed to initialize FreeType");
		return false;
	}

	instance.dpi = dpi;

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

	FcPattern *pats[FontStyleCount];

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

	FcPatternAddInteger(pats[FontStyleRegular],    FC_SLANT,  FC_SLANT_ROMAN);
	FcPatternAddInteger(pats[FontStyleRegular],    FC_WEIGHT, FC_WEIGHT_REGULAR);
	FcPatternAddInteger(pats[FontStyleBold],       FC_SLANT,  FC_SLANT_ROMAN);
	FcPatternAddInteger(pats[FontStyleBold],       FC_WEIGHT, FC_WEIGHT_BOLD);
	FcPatternAddInteger(pats[FontStyleItalic],     FC_SLANT,  FC_SLANT_ITALIC);
	FcPatternAddInteger(pats[FontStyleItalic],     FC_WEIGHT, FC_WEIGHT_REGULAR);
	FcPatternAddInteger(pats[FontStyleBoldItalic], FC_SLANT,  FC_SLANT_ITALIC);
	FcPatternAddInteger(pats[FontStyleBoldItalic], FC_WEIGHT, FC_WEIGHT_BOLD);

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

	FontSet *set = &instance.set;

	set->fcset = fcset;

	for (int i = 0; i < fcset->nfont; i++) {
		struct FontDesc desc = { 0 };
		if (get_pattern_desc(fcset->fonts[i], &desc)) {
			Font *font = &set->fonts[i];
			font_create_from_desc(font, desc);
		}
	}

	return set;
}

bool
fontset_init(FontSet *set)
{
	glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glPixelStorei(GL_UNPACK_ALIGNMENT, PIXEL_ALIGN);

	for (int i = 0; i < set->fcset->nfont; i++) {
		Font *font = &set->fonts[i];

		if (!font_create_glyphmap(font, font->num_codepoints)) {
			return false;
		}
		font->glyphs = xcalloc(font->num_glyphs, sizeof(*font->glyphs));
		font->bitmap = xcalloc(MAX(font->pitch, font->max_width) * font->max_height, font->depth);

		glGenTextures(1, &font->texid);
		ASSERT(font->texid);
		dbgprintf("Generated texture: %u\n", font->texid);

		glBindTexture(GL_TEXTURE_2D, font->texid);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_R8,
			font->tex_width,
			font->tex_height,
			0,
			GL_RED,
			GL_UNSIGNED_BYTE,
			NULL
		);

		font_render_glyph(font, 0);
	}

	return true;
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
		.rendermode = 0,
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
		}
	}
	if (FcPatternGet(pat, FC_CHARSET, 0, &fcval) == FcResultMatch) {
		if (fcval.u.c) {
			desc.charset = FcCharSetCopy((FcCharSet *)fcval.u.c);
		}
	}

	if (desc_) {
		*desc_ = desc;
	}

	return true;
}

void
font_create_from_desc(Font *font_, struct FontDesc desc)
{
	ASSERT(font_);

	if (!instance.library) {
		if (!!FT_Init_FreeType(&instance.library)) {
			dbgprint("Failed to initialize FreeType");
			return;
		}
	}

	Font font = { 0 };

	font.attrs     = desc.attrs;
	font.pixsize   = desc.pixsize;
	font.aspect    = desc.aspect;
	font.matrix    = desc.matrix;
	font.lcdfilter = desc.lcdfilter;
	font.pattern   = desc.pattern;

	FcChar8 *filepath;
	if (FcPatternGetString(desc.pattern, FC_FILE, 0, &filepath) != FcResultMatch) {
		filepath = NULL;
	}
	if (!filepath) {
		dbgprint("Failed to extract font file from FcPattern");
		exit(EXIT_FAILURE);
	}

	FT_Error fterr;
	fterr = FT_New_Face(instance.library, (char *)filepath, 0, &font.face);
	if (fterr) {
		dbgprintf("Failed to initialize font file: %s\n", (char *)filepath);
		exit(EXIT_FAILURE);
	}
	FT_Set_Char_Size(font.face, desc.pixsize * 64, 0, 72 * desc.aspect, 72);
	FT_Set_Transform(font.face, &desc.matrix, NULL);

	font.loadflags  = 0;
	font.loadtarget = FT_LOAD_TARGET_LIGHT;
	font.rendermode = FT_RENDER_MODE_NORMAL;

	if ((desc.attrs & FONTATTR_COLOR) || FT_HAS_COLOR(font.face)) {
		ASSERT(!desc.hintstyle);
		font.loadflags = FT_LOAD_COLOR;
		font.rendermode = FT_RENDER_MODE_LCD;
	} else if (desc.hintstyle) {
		if (desc.hintstyle == FontHintingAuto) {
			font.loadflags = FT_LOAD_FORCE_AUTOHINT;
		} else if (desc.rendermode == FT_RENDER_MODE_LCD) {
			font.loadtarget = FT_LOAD_TARGET_LCD;
			font.rendermode = FT_RENDER_MODE_LCD;
		} else if (desc.hintstyle != FontHintingLight) {
			font.loadtarget = FT_LOAD_TARGET_NORMAL;
		}
	} else {
		font.loadflags = FT_LOAD_NO_HINTING|FT_LOAD_NO_BITMAP|FT_LOAD_MONOCHROME;
		font.loadtarget = FT_LOAD_TARGET_MONO;
		font.rendermode = FT_RENDER_MODE_MONO;
	}

#if 1
	font.depth = 1;
#else
	if (font.rendermode == FT_RENDER_MODE_LCD) {
		font.depth = sizeof(uint32);
	} else {
		font.depth = sizeof(uint8);
	}
#endif

	FT_Size_Metrics metrics = font.face->size->metrics;
	if (desc.attrs & FONTATTR_TRANSFORM) {
		FT_Vector v[4] = {
			{ .x = 0, .y = metrics.ascender  },
			{ .x = 0, .y = metrics.descender },
			{ .x = 0, .y = metrics.height    },
			{ .x = metrics.max_advance, .y = 0 }
		};
		FT_Vector_Transform(&v[0], &desc.matrix), font.ascent  = +(v[0].y >> 6);
		FT_Vector_Transform(&v[1], &desc.matrix), font.descent = -(v[1].y >> 6);
		FT_Vector_Transform(&v[2], &desc.matrix), font.height  = +(v[2].y >> 6);
		FT_Vector_Transform(&v[3], &desc.matrix), font.max_advance = +(v[3].x >> 6);
	} else {
		font.ascent  = +(metrics.ascender >> 6);
		font.descent = -(metrics.descender >> 6);
		font.height  = +(metrics.height >> 6);
		font.max_advance = +(metrics.max_advance >> 6);
	}

	font.width       = font_compute_nominal_width(&font);
	font.height      = MAX(font.height, font.ascent + font.descent);
	font.max_width   = (font.face->bbox.xMax - font.face->bbox.xMin) >> 6;
	font.max_height  = (font.face->bbox.yMax - font.face->bbox.yMin) >> 6;
	font.pitch       = ALIGN_UP(font.width + 2 * MIN_PADDING, PIXEL_ALIGN);
	font.vpad        = MIN_PADDING;
	font.lpad        = MIN_PADDING;
	font.rpad        = font.pitch - font.width - MIN_PADDING;

	font.num_glyphs  = font.face->num_glyphs + 1;
	font.square      = floor(sqrt(font.num_glyphs)) + 1;
	font.tile_width  = font.pitch * font.depth;
	font.tile_height = (font.height + 2 * font.vpad) * font.depth;
	font.tex_width   = font.square * font.tile_width;
	font.tex_height  = font.square * font.tile_height;

	// TODO(ben): The actual glyph cache.
	if (font.tex_width / font.depth > GL_MAX_TEXTURE_SIZE ||
	    font.tex_height / font.depth > GL_MAX_TEXTURE_SIZE)
	{
		dbgprintf(
			"Atlas dimensions (%dx%d) exceed GL_MAX_TEXTURE_SIZE (%dx%d)\n",
			font.tex_width / font.depth, font.tex_height / font.depth,
			GL_MAX_TEXTURE_SIZE, GL_MAX_TEXTURE_SIZE
		);
		abort();
	}

	if (!desc.charset) {
		font.charset = FcFreeTypeCharSet(font.face, NULL);
	} else {
		font.charset = desc.charset;
	}
	font.num_codepoints = FcCharSetCount(font.charset);

	*font_ = font;
}

uint32
font_create_glyphmap(Font *font, uint num_codepoints)
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
font_compute_nominal_width(const Font *font)
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
hash_codepoint(const struct GlyphMapping *glyphmap, uint32 capacity, uint32 ucs4)
{
	uint32 hash = ucs4 % capacity;
	uint32 offset = 0;

	for (;;) {
		if (!glyphmap[hash].status || glyphmap[hash].ucs4 == ucs4) {
			return hash;
		}
		if (!offset) {
			offset = ucs4 % (capacity - 2);
			offset += !offset;
		}
		hash += offset;
		hash -= (hash >= capacity) ? capacity : 0;
	}

	return 0;
}

uint32
query_file_glyph_index(FT_FaceRec *face, const FcCharSet *charset, uint32 ucs4)
{
	if (FcCharSetHasChar(charset, ucs4)) {
		return FcFreeTypeCharIndex(face, ucs4);
	}

	return 0;
}

Glyph *
fontset_get_glyph(FontSet *set, FontStyle style, uint32 ucs4)
{
	Font *font = &set->fonts[style];

	Glyph *glyph = &font->glyphs[0];
	uint32 hash = hash_codepoint(font->glyphmap, font->num_glyphs, ucs4);

	if (font->glyphmap[hash].status) {
		glyph = &font->glyphs[font->glyphmap[hash].idx];
	} else {
		uint idx = query_file_glyph_index(font->face, font->charset, ucs4);
		if (idx) {
			glyph = font_render_glyph(font, idx);
			font->glyphmap[hash].status = true;
			font->glyphmap[hash].idx  = idx;
			font->glyphmap[hash].ucs4 = ucs4;
			font->num_mapped++;
		}
	}

	return glyph;
}

Glyph *
font_render_glyph(Font *font, uint32 glyph)
{
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

	int xsrc = 0, ysrc = 0;
	int xdst = 0, ydst = 0;

	if (active->bitmap_left < 0) {
		xsrc = imin(-active->bitmap_left, font->width);
	} else {
		xdst = imin(active->bitmap_left, font->width);
	}
	if (active->bitmap_top > font->ascent) {
		ysrc = imin(active->bitmap_top - font->ascent, font->height);
	} else {
		ydst = imin(font->ascent - active->bitmap_top, font->height);
	}

	const int width  = imin(active->bitmap.width - xsrc, font->width - xdst);
	const int height = imin(active->bitmap.rows - ysrc, font->height - ydst);
	ASSERT(width >= 0 && height >= 0);

	memset(font->bitmap, 0, font->tile_width * font->tile_height);

	const uchar *srcptr = active->bitmap.buffer + ysrc * active->bitmap.pitch;
	uchar *dstptr = font->bitmap + (ydst + font->vpad) * font->tile_width;

	for (int y = ydst; y - ydst < height; y++) {
		memcpy(dstptr + (font->lpad + xdst) * font->depth, srcptr + xsrc, width);
		srcptr += active->bitmap.pitch;
		dstptr += font->tile_width;
	}

	const int col = glyph % font->square;
	const int row = glyph / font->square;
	const float xdiv = font->tex_width;
	const float ydiv = font->tex_height;

	Glyph *entry = &font->glyphs[glyph];
	entry->idx      = glyph;
	entry->x        = (col * font->tile_width + font->lpad * font->depth) / xdiv;
	entry->y        = (row * font->tile_height + font->vpad * font->depth) / ydiv;
	entry->width    = (font->width * font->depth) / xdiv;
	entry->height   = (font->height * font->depth) / ydiv;
	entry->hbearing = (-active->bitmap_left * font->depth) / xdiv;
	entry->vbearing = (active->bitmap_top * font->depth) / ydiv;
	entry->texid    = font->texid;

	glBindTexture(GL_TEXTURE_2D, font->texid);
	glTexSubImage2D(
		GL_TEXTURE_2D,
		0,
		col * font->tile_width,
		row * font->tile_height,
		font->tile_width,
		font->tile_height,
		GL_RED,
		GL_UNSIGNED_BYTE,
		font->bitmap
	);

	return entry;
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
		"  FaceSizeMetrics\n"
		"    ascender    = %ld\n"
		"    descender   = %ld\n"
		"    height      = %ld\n"
		"    max_advance = %ld\n"
		"  GlyphBitmap\n"
		"    width       = %d\n"
		"    height      = %d\n"
		"    pitch       = %d\n"
		"    size        = %d\n"
		"    h_advance   = %d\n"
		"    v_advance   = %d\n"
		"    h_bearing   = %d\n"
		"    v_bearing   = %d\n"
		"  GlyphMetrics\n"
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
		face->size->metrics.ascender >> 6,
		face->size->metrics.descender >> 6,
		face->size->metrics.height >> 6,
		face->size->metrics.max_advance >> 6,
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

