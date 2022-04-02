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

#ifndef COLOR_H__
#define COLOR_H__

#include "common.h"

typedef struct Palette Palette;
typedef struct Color Color;

enum {
    BLACK,
    RED,
    GREEN,
    YELLOW,
    BLUE,
    MAGENTA,
    CYAN,
    WHITE,
    LBLACK,
    LRED,
    LGREEN,
    LYELLOW,
    LBLUE,
    LMAGENTA,
    LCYAN,
    LWHITE,

    BACKGROUND = 256,
    FOREGROUND = 257,

    NUM_COLORS,
};

struct Color {
    bool resolved;
    union {
        uint16 key; // Unresolved palette index
        uint32 val; // Resolved color value
    };
};

struct Palette {
    union {
        uint32 table[256+2];
        struct {
            union {
                uint32 base16[16];
                uint32 base256[256];
            };
            union {
                uint32 defaults[2];
                struct {
                    uint32 bg;
                    uint32 fg;
                };
            };
        };
    };
};

static_assert(offsetof(Palette, bg) == BACKGROUND * sizeof(uint32), "Invalid offset");
static_assert(offsetof(Palette, fg) == FOREGROUND * sizeof(uint32), "Invalid offset");

void palette_init(Palette *palette, bool explicit_alpha);
uint32 palette_resolve_color(const Palette *palette, Color *color);
uint32 palette_query_color(const Palette *palette, Color color);

Color color_from_key(uint16 key);
Color color_from_argb_1u(uint32 argb);
Color color_from_argb_4u(uint8 a, uint8 r, uint8 g, uint8 b);
Color color_from_rgb_1u(uint32 rgb);
Color color_from_rgb_3u(uint8 r, uint8 g, uint8 b);

const char *color_key_to_string(uint16 key);

#endif

