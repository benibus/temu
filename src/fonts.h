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

#ifndef FONTS_H__
#define FONTS_H__

#include "common.h"

#define FONT_DEFAULT "monospace:size=12.0"

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
    uint id;
    float u;
    float v;
    float w;
    float h;
} Texture;

bool fontmgr_init(double);
FontSet *fontmgr_create_fontset(const char *);
FontSet *fontmgr_create_fontset_from_file(const char *);
void fontset_destroy(FontSet *set);
bool fontset_init(FontSet *);
Texture fontset_get_glyph_texture(FontSet *, FontStyle, uint32);
bool fontset_get_metrics(const FontSet *, int *, int *, int *, int *);

#endif
