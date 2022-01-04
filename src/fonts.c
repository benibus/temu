/*------------------------------------------------------------------------------*
 * This file is part of temu
 * Copyright (C) 2021-2022 Benjamin Harkins
 *
 * temu is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 *------------------------------------------------------------------------------*/

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

#define ATLAS_WIDTH  2048
#define ATLAS_HEIGHT 2048

#if (ATLAS_WIDTH > GL_MAX_TEXTURE_SIZE) || (ATLAS_HEIGHT > GL_MAX_TEXTURE_SIZE)
  #error "Atlas texture size exceeds OpenGL ES limits"
#endif

typedef struct AtlasNode_ AtlasNode;

typedef struct {
    AtlasNode *node;
    uint32 idx;
    int width;
    int height;
    int hbearing;
    int vbearing;
} Glyph;

typedef struct {
    bool status;
    uint32 idx;
    uint32 ucs4;
} GlyphMapping;

struct AtlasNode_ {
    AtlasNode *prev;
    AtlasNode *next;
    Glyph *glyph;
    float u;
    float v;
    float du;
    float dv;
};

typedef struct {
    GLuint tex;       // GPU texture ID
    AtlasNode *nodes; // Tile data
    AtlasNode *head;  // LRU tile in queue (pointer into nodes array)
    AtlasNode *tail;  // MRU tile in queue (pointer into nodes array)
    int count;        // Current number of used tiles
    int max;          // Max number of tiles
    int depth;        // Pixel depth of texture
    int nx, ny;       // Atlas dimension (in tiles)
    int dx, dy;       // Tile width (in pixels)
    int lpad;         // Left tile padding
    int rpad;         // Right tile padding
    int vpad;         // Top/Bottom tile padding
} Atlas;

typedef struct {
    FontSet *set;

    FT_FaceRec *face;
    FT_Matrix matrix;
    FT_Int loadflags;
    FT_Int loadtarget;
    FT_Render_Mode rendermode;
    FT_LcdFilter lcdfilter;

    char *filepath;
    FcPattern *pattern;
    FcCharSet *charset;

    Glyph *glyphs;
    GlyphMapping *glyphmap;
    uint32 basehash;

    uint num_codepoints;
    uint num_glyphs;
    uint num_mapped;

    byte *bitmap;

    uint16 attrs;
    float pixsize;
    float scale;
    float aspect;
    int width;
    int height;
    int ascent;
    int descent;
    int max_advance;
    int max_width;
    int max_height;
} Font;

struct FontDesc {
    FcPattern *pattern;
    char *filepath;
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
    Atlas atlas;
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

static float norm_x(int);
static float norm_y(int);
static int denorm_x(float);
static int denorm_y(float);

FontSet *fontset_create(FcPattern *pat);
static void font_create_from_desc(Font *font, struct FontDesc desc);
static uint32 font_create_glyphmap(Font *font, uint);
static Glyph *font_render_glyph(Font *font, uint32 glyph);
static int font_compute_nominal_width(const Font *font);

static FcPattern *pattern_create_from_name(const char *);
static FcPattern *pattern_create_from_file(const char *);
static void pattern_set_defaults(FcPattern *);
static FcFontSet *pattern_expand_set(FcPattern *);
static bool pattern_extract_desc(FcPattern *pat, struct FontDesc *desc_);

static uint32 hash_codepoint(const GlyphMapping *, uint32, uint32);
static uint32 query_file_glyph_index(FT_FaceRec *, const FcCharSet *, uint32);
static void dbg_print_freetype_bitmap(const FT_FaceRec *face);

static Atlas *atlas_match_pixelmode(FT_Pixel_Mode pixelmode);
static Atlas *atlas_match_depth(int depth);
static AtlasNode *atlas_cache_glyph_bitmap(Atlas *atlas, Glyph *glyph, byte *bitmap);
static AtlasNode *atlas_reference_glyph(Atlas *atlas, Glyph *glyph);

inline float norm_x(int x) { return x / (float)ATLAS_WIDTH;  }
inline float norm_y(int y) { return y / (float)ATLAS_HEIGHT; }
inline int denorm_x(float x) { return x * ATLAS_WIDTH;  }
inline int denorm_y(float y) { return y * ATLAS_HEIGHT; }

bool
fontmgr_init(double dpi)
{
    if (instance.library) {
        return true;
    }

    if (FT_Init_FreeType(&instance.library)) {
        dbgprint("Failed to initialize freetype");
        return false;
    }

    instance.dpi = dpi;

    dbgprint(
        "Font manager initialized\n"
        "    DPI = %.02f\n"
        "    GL_MAX_TEXTURE_SIZE = %d",
        instance.dpi,
        GL_MAX_TEXTURE_SIZE
    );

    return true;
}

FcPattern *
pattern_create_from_file(const char *filepath)
{
    // Fontconfig stores the path as-is, but we normalize it for predictability
    ASSERT(!filepath || filepath[0] == '/');

    FcPattern *result = NULL;

    dbgprint("Opening font from path: %s", filepath);

    if (filepath && FcConfigAppFontAddFile(NULL, (const FcChar8 *)filepath)) {
        FcFontSet *fcset = FcConfigGetFonts(NULL, FcSetApplication);
        for (int i = 0; !result && fcset && i < fcset->nfont; i++) {
            FcValue fcval;
            // There's probably a better way to retreive the pattern...
            if (FcPatternGet(fcset->fonts[i], FC_FILE, 0, &fcval) == FcResultMatch) {
                if (strequal(filepath, (const char *)fcval.u.s)) {
                    result = FcPatternDuplicate(fcset->fonts[i]);
                    ASSERT(result);
                }
            }
        }
    }

    if (result) {
        pattern_set_defaults(result);
    } else {
        dbgprint("Failed to open font... falling back to defaults");
        result = pattern_create_from_name(NULL);
    }

    return result;
}

FcPattern *
pattern_create_from_name(const char *name)
{
    FcPattern *result = NULL;
    name = DEFAULT(name, FONT_DEFAULT);

    dbgprint("Opening font from name: \"%s\"", name);

    FcPattern *pat = FcNameParse((FcChar8 *)name);
    if (pat) {
        result = FcPatternDuplicate(pat);
    } else if (!strequal(name, FONT_DEFAULT)) {
        dbgprint("Failed to open font... falling back to defaults");
        result = pattern_create_from_name(NULL);
    }
    if (result) {
        pattern_set_defaults(result);
        FcPatternDestroy(pat);
    }

    return result;
}

void
pattern_set_defaults(FcPattern *pat)
{
    ASSERT(pat);

    FcValue fcval;

    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    if (FcPatternGet(pat, FC_DPI, 0, &fcval) != FcResultMatch) {
        FcPatternAddDouble(pat, FC_DPI, instance.dpi);
    }
    if (FcPatternGet(pat, FC_RGBA, 0, &fcval) != FcResultMatch) {
        FcPatternAddInteger(pat, FC_RGBA, FC_RGBA_UNKNOWN);
    }

    FcDefaultSubstitute(pat);
    if (FcPatternGet(pat, FC_SLANT, 0, &fcval) == FcResultMatch) {
        FcPatternDel(pat, FC_SLANT);
    }
    if (FcPatternGet(pat, FC_WEIGHT, 0, &fcval) == FcResultMatch) {
        FcPatternDel(pat, FC_WEIGHT);
    }
    if (FcPatternGet(pat, FC_SCALABLE, 0, &fcval) == FcResultMatch) {
        FcPatternDel(pat, FC_SCALABLE);
    }
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
}

FcFontSet *
pattern_expand_set(FcPattern *pat)
{
    ASSERT(pat);

    FcPattern *pats[FontStyleCount];

    FcFontSet *fcset = FcFontSetCreate();
    if (!fcset) {
        dbgprint("Failed to create FcFontSet\n");
        goto done;
    }

    for (int i = 0; i < FontStyleCount; i++) {
        pats[i] = FcPatternDuplicate(pat);

        switch (i) {
        case FontStyleRegular:
            FcPatternAddInteger(pats[i], FC_SLANT,  FC_SLANT_ROMAN);
            FcPatternAddInteger(pats[i], FC_WEIGHT, FC_WEIGHT_REGULAR);
            break;
        case FontStyleBold:
            FcPatternAddInteger(pats[i], FC_SLANT,  FC_SLANT_ROMAN);
            FcPatternAddInteger(pats[i], FC_WEIGHT, FC_WEIGHT_BOLD);
            break;
        case FontStyleItalic:
            FcPatternAddInteger(pats[i], FC_SLANT,  FC_SLANT_ITALIC);
            FcPatternAddInteger(pats[i], FC_WEIGHT, FC_WEIGHT_REGULAR);
            break;
        case FontStyleBoldItalic:
            FcPatternAddInteger(pats[i], FC_SLANT,  FC_SLANT_ITALIC);
            FcPatternAddInteger(pats[i], FC_WEIGHT, FC_WEIGHT_BOLD);
            break;
        }

        FcResult result;
        FcPattern *pat_match = FcFontMatch(NULL, pats[i], &result);
        if (pat_match) {
            FcFontSetAdd(fcset, pat_match);
        }
        FcPatternDestroy(pats[i]);
#if DEBUG_PRINT_PATTERN
        FcPatternPrint(pat_match);
#endif

        if (fcset->nfont < i + 1) {
            dbgprint("Failed to find matching FcPattern for style %d", i);
            FcFontSetDestroy(fcset);
            fcset = NULL;
            goto done;
        }
    }

#if 0
    FcFontSetPrint(fcset);
#endif

done:
    FcPatternDestroy(pat);

    return fcset;
}

FontSet *
fontmgr_create_fontset(const char *name)
{
    return fontset_create(pattern_create_from_name(name));
}

FontSet *
fontmgr_create_fontset_from_file(const char *filepath)
{
    return fontset_create(pattern_create_from_file(filepath));
}

FontSet *
fontset_create(FcPattern *pat)
{
    if (!pat) {
        return NULL;
    }

    FontSet *set = &instance.set;
    set->fcset = pattern_expand_set(pat);
    ASSERT(set->fcset->nfont == FontStyleCount);

    for (int i = 0; i < FontStyleCount; i++) {
        struct FontDesc desc = { 0 };
        if (pattern_extract_desc(set->fcset->fonts[i], &desc)) {
            Font *font = &set->fonts[i];
            font_create_from_desc(font, desc);
            font->set = set;
        }
    }

    return set;
}

bool
fontset_init(FontSet *set)
{
    Atlas *atlas = &set->atlas;
    Font *basefont = &set->fonts[0];

    // Setup the atlas metrics and nodes
    {
        const int pitch = ALIGN_UP(basefont->width + 2 * MIN_PADDING, PIXEL_ALIGN);

        atlas->vpad = MIN_PADDING;
        atlas->lpad = MIN_PADDING;
        atlas->rpad = pitch - basefont->width - MIN_PADDING;

        atlas->depth = 1;

        atlas->dx = basefont->width + atlas->lpad + atlas->rpad;
        atlas->dy = basefont->height + 2 * atlas->vpad;
        atlas->nx = ATLAS_WIDTH / atlas->dx;
        atlas->ny = ATLAS_HEIGHT / atlas->dy;

        atlas->max = atlas->nx * atlas->ny;
        atlas->nodes = xcalloc(atlas->max, sizeof(*atlas->nodes));

        const float du = norm_x(atlas->dx);
        const float dv = norm_y(atlas->dy);
        const float pl = norm_x(atlas->lpad);
        const float pr = norm_x(atlas->rpad);
        const float pv = norm_y(atlas->vpad);

        for (int y = 0, i = 0; y < atlas->ny; y++) {
            for (int x = 0; x < atlas->nx; x++, i++) {
                atlas->nodes[i].u = x * du + pl;
                atlas->nodes[i].v = y * dv + pv;
                atlas->nodes[i].du = du - pl - pr;
                atlas->nodes[i].dv = dv - pv - pv;
            }
        }
    }

    // Setup the atlas texture
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, PIXEL_ALIGN);

    glGenTextures(1, &atlas->tex);
    ASSERT(atlas->tex);
    dbgprint("Generated texture: %u", atlas->tex);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas->tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R8,
        ATLAS_WIDTH,
        ATLAS_HEIGHT,
        0,
        GL_RED,
        GL_UNSIGNED_BYTE,
        NULL
    );

    // Finalize initialization for the fonts
    for (int i = 0; i < FontStyleCount; i++) {
        Font *font = &set->fonts[i];
        font_create_glyphmap(font, font->num_codepoints);
        font->glyphs = xcalloc(font->num_glyphs, sizeof(*font->glyphs));
        font->bitmap = xcalloc(font->max_width * font->max_height, 1);

    }

    // Render the "missing glyph" glyph first.
    {
        Glyph *glyph = font_render_glyph(&set->fonts[0], 0);
        ASSERT(glyph);

        AtlasNode *node = atlas_cache_glyph_bitmap(atlas, glyph, set->fonts[0].bitmap);
        ASSERT(node && node->glyph == glyph);
        ASSERT(atlas->count == 1);
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

void
fontset_destroy(FontSet *set)
{
    for (int i = 0; i < FontStyleCount; i++) {
        Font *font = &set->fonts[i];
        FREE(font->bitmap);
        FREE(font->glyphs);
        FREE(font->glyphmap);
        FcCharSetDestroy(font->charset);
        FT_Done_Face(font->face);
    }

    FREE(set->atlas.nodes);
    FcFontSetDestroy(set->fcset);
    FT_Done_FreeType(instance.library);
    FcFini();
}

bool
pattern_extract_desc(FcPattern *pat, struct FontDesc *desc_)
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

    if (FcPatternGetString(pat, FC_FILE, 0, (FcChar8 **)&desc.filepath) != FcResultMatch) {
        dbgprint("Failed to extract font file from FcPattern");
        return false;
    }
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
    ASSERT(instance.library);

    Font font = { 0 };

    font.filepath  = desc.filepath;
    font.attrs     = desc.attrs;
    font.pixsize   = desc.pixsize;
    font.aspect    = desc.aspect;
    font.matrix    = desc.matrix;
    font.lcdfilter = desc.lcdfilter;
    font.pattern   = desc.pattern;

    FT_Error fterr;
    fterr = FT_New_Face(instance.library, desc.filepath, 0, &font.face);
    if (fterr) {
        dbgprint("Failed to initialize font file: %s", desc.filepath);
        exit(EXIT_FAILURE);
    }

    dbgprint("Opened freetype face for %s", desc.filepath);

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

#if 0
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

    font.width  = font_compute_nominal_width(&font);
    font.height = MAX(font.height, font.ascent + font.descent);
    font.max_width  = (font.face->bbox.xMax - font.face->bbox.xMin) >> 6;
    font.max_height = (font.face->bbox.yMax - font.face->bbox.yMin) >> 6;

    if (!desc.charset) {
        font.charset = FcFreeTypeCharSet(font.face, NULL);
    } else {
        font.charset = desc.charset;
    }
    font.num_codepoints = FcCharSetCount(font.charset);
    font.num_glyphs = font.face->num_glyphs + 1;

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
hash_codepoint(const GlyphMapping *glyphmap, uint32 capacity, uint32 ucs4)
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

Texture
fontset_get_glyph_texture(FontSet *set, FontStyle style, uint32 ucs4)
{
    // Sanity checks. The missing glyph *must* be canonicalized by this point
    ASSERT(set->fonts[0].glyphs[0].idx == 0);
    ASSERT(set->fonts[0].glyphs[0].node);
    ASSERT(set->atlas.nodes[0].glyph == &set->fonts[0].glyphs[0]);

    Font *font = &set->fonts[style];

    uint32 hash = hash_codepoint(font->glyphmap, font->num_glyphs, ucs4);
    ASSERT(hash < font->basehash);

    Glyph *glyph;
    Atlas *atlas = &set->atlas;
    AtlasNode *node;

    // A valid codepoint -> glyph mapping already exists
    if (font->glyphmap[hash].status) {
        ASSERT(font->glyphmap[hash].idx < font->num_glyphs);
        glyph = &font->glyphs[font->glyphmap[hash].idx];
        // The glyph is already allocated, so we set it to MRU
        // The cache skips referencing index 0 automatically
        if (glyph->node) {
            node = atlas_reference_glyph(atlas, glyph);
        // The glyph texture was paged out, so re-render/cache the bitmap
        } else {
            glyph = font_render_glyph(font, glyph->idx);
            node = atlas_cache_glyph_bitmap(atlas, glyph, font->bitmap);
        }
    // A glyph has never been mapped to this codepoint
    } else {
        // Extract the codepoint's glyph index from the font (the expensive operation)
        uint32 idx = query_file_glyph_index(font->face, font->charset, ucs4);
        // No glyph found in the font, use the (already existing) missing glyph
        if (!idx) {
            glyph = &font->glyphs[0];
        // Glyph found, add a new mapping and a new bitmap entry
        } else {
            font->glyphmap[hash].status = true;
            font->glyphmap[hash].idx  = idx;
            font->glyphmap[hash].ucs4 = ucs4;
            font->num_mapped++;
            glyph = font_render_glyph(font, idx);
        }
        // If glyph->idx == 0, the cache resolves to the missing glyph. The bitmap isn't read.
        node = atlas_cache_glyph_bitmap(atlas, glyph, font->bitmap);
    }

    return (Texture){
        .id = atlas->tex,
        .u  = node->u,
        .v  = node->v,
        .w  = node->du,
        .h  = node->dv
    };
}

Glyph *
font_render_glyph(Font *font, uint32 idx)
{
    FT_FaceRec *face = font->face;

    ASSERT(font->set);
    ASSERT(idx < font->num_glyphs);

    Glyph *glyph = NULL;

    FT_Library_SetLcdFilter(instance.library, font->lcdfilter);
    {
        FT_Error err;
        err = FT_Load_Glyph(face, idx, font->loadflags|font->loadtarget);
        if (err) {
            dbgprint(
                "Failed to load glyph (%u) from %s... "
                "style: %ld flags: %#x target: %#x",
                idx,
                font->filepath,
                font - font->set->fonts,
                font->loadflags,
                font->loadtarget
            );
        } else if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
            if (font->attrs & FONTATTR_EMBOLDEN) {
                FT_GlyphSlot_Embolden(face->glyph);
            }
            err = FT_Render_Glyph(face->glyph, font->rendermode);
            if (!err) {
                glyph = &font->glyphs[idx];
            } else {
                dbgprint(
                    "Failed to render glyph (%u) from %s... "
                    "style: %ld flags: %#x target: %#x mode: %d",
                    idx,
                    font->filepath,
                    font - font->set->fonts,
                    font->loadflags,
                    font->loadtarget,
                    font->rendermode
                );
            }
        }
    }
    FT_Library_SetLcdFilter(instance.library, FT_LCD_FILTER_NONE);

    const FT_GlyphSlotRec *slot = face->glyph;

    // NOTE(ben): Currently, only scalable alpha-only glyphs are safe to load
    if (!glyph || slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
#if 1
        if (idx && !font->glyphs[0].node) {
            return font_render_glyph(font, 0);
        } else {
            return &font->glyphs[0];
        }
#else
        return &font->glyphs[0];
#endif
    }

    ASSERT(slot->format == FT_GLYPH_FORMAT_BITMAP);

#if DEBUG_PRINT_GLYPHS
    dbg_print_freetype_bitmap(face);
#endif

    const Atlas *atlas = &font->set->atlas;

    int xsrc = 0, ysrc = 0;
    int xdst = 0, ydst = 0;

    // Right now, we have to pre-clamp the bitmaps at load time since the atlas doesn't support
    // variable-sized rectangles yet. This has a negative effect on transformed glyphs.
    if (slot->bitmap_left < 0) {
        xsrc = imin(-slot->bitmap_left, font->width);
    } else {
        xdst = imin(slot->bitmap_left, font->width);
    }
    if (slot->bitmap_top > font->ascent) {
        ysrc = imin(slot->bitmap_top - font->ascent, font->height);
    } else {
        ydst = imin(font->ascent - slot->bitmap_top, font->height);
    }

    const int width  = imin(slot->bitmap.width - xsrc, font->width - xdst);
    const int height = imin(slot->bitmap.rows - ysrc, font->height - ydst);
    ASSERT(width >= 0 && height >= 0);

    memset(font->bitmap, 0, font->max_width * font->max_height);

    const byte *srcptr = slot->bitmap.buffer + ysrc * slot->bitmap.pitch;
    byte *dstptr = font->bitmap + (ydst + atlas->vpad) * atlas->dx;

    // Do the copy (alpha-to-alpha for now)
    for (int y = ydst; y - ydst < height; y++) {
        memcpy(dstptr + (atlas->lpad + xdst) * atlas->depth, srcptr + xsrc, width);
        srcptr += slot->bitmap.pitch;
        dstptr += atlas->dx;
    }

    glyph->idx      = idx;
    glyph->width    = slot->bitmap.width;
    glyph->height   = slot->bitmap.rows;
    glyph->hbearing = -slot->bitmap_left;
    glyph->vbearing = slot->bitmap_top;
    glyph->node     = NULL;

    return glyph;
}

Atlas *
atlas_match_pixelmode(FT_Pixel_Mode pixelmode)
{
    (void)pixelmode;
    return &instance.set.atlas;
}

Atlas *
atlas_match_depth(int depth)
{
    (void)depth;
    return &instance.set.atlas;
}

AtlasNode *
atlas_reference_glyph(Atlas *atlas, Glyph *glyph)
{
    ASSERT(atlas->count);
    ASSERT(glyph && glyph->node);

    AtlasNode *const head = atlas->head;
    AtlasNode *const tail = atlas->tail;
    AtlasNode *const node = glyph->node;

    // Move the glyph's node to the back of the queue (unless it's the sentinel)
    if (node != &atlas->nodes[0] && node != tail) {
        ASSERT(head != tail);
        ASSERT(head->next);

        if (node == head) {
            atlas->head = head->next;
        }
        atlas->tail = node;

        if (node->prev) node->prev->next = node->next;
        if (node->next) node->next->prev = node->prev;
        node->prev = tail;
        node->next = NULL;
        tail->next = node;
    }

    return node;
}

AtlasNode *
atlas_cache_glyph_bitmap(Atlas *atlas, Glyph *glyph, byte *bitmap)
{
    AtlasNode *node = NULL;

    ASSERT(glyph);

    // A null glyph gets cached once and never gets paged out
    // The same node is provided to every font
    if (!glyph->idx) {
        node = &atlas->nodes[0];
        if (!node->glyph) {
            node->glyph = glyph;
            atlas->count++;
        } else {
            goto assign;
        }
    } else {
        ASSERT(!glyph->node);

        AtlasNode *const head = atlas->head;
        AtlasNode *const tail = atlas->tail;

        atlas->count += !atlas->count;

        if (atlas->count < atlas->max) {
            // Add a bitmap to the atlas, push its node to the back of the queue
            node = &atlas->nodes[atlas->count];
            if (atlas->count == 1) {
                // Setup the queue if this is the first "real" glyph
                ASSERT(!head);
                ASSERT(!tail);

                atlas->head = node;
                atlas->tail = node;

                node->prev = NULL;
                node->next = NULL;
            } else {
                // Otherwise, perform a normal DLL insertion
                atlas->tail = node;

                tail->next = node;
                node->prev = tail;
                node->next = NULL;
            }
            atlas->count++;
        } else {
            ASSERT(atlas->count == atlas->max);
            // Atlas is full, replace the LRU node and nullify its current forward-reference
            head->glyph->node = NULL;
            // Reuse the least recently used node
            node = head;

            if (head != tail) {
                ASSERT(atlas->count > 2);
                ASSERT(!head->prev && head->next);

                atlas->head = head->next;
                atlas->tail = head;

                head->prev = tail;
                head->next->prev = NULL;
                head->next = NULL;
                tail->next = head;
            }
        }
        // Set the new back-reference
        node->glyph = glyph;
    }

    glBindTexture(GL_TEXTURE_2D, atlas->tex);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        (denorm_x(node->u) - atlas->lpad) * atlas->depth,
        (denorm_y(node->v) - atlas->vpad) * atlas->depth,
        atlas->dx * atlas->depth,
        atlas->dy * atlas->depth,
        GL_RED,
        GL_UNSIGNED_BYTE,
        bitmap
    );

assign:
    ASSERT(node);
    // Set the new forward-reference
    glyph->node = node;

    return node;
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

    const byte *pixels = slot->bitmap.buffer;
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

