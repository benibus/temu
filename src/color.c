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
#include "color.h"

void
palette_init(Palette *palette, bool explicit_alpha)
{
    ASSERT(palette);

    // Default standard colors (0-15)
    static const uint32 base16[16] = {
        0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xc0c0c0,
        0x808080, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff, 0xffffff
    };

    memset(palette, 0, sizeof(*palette));

    uint32 *const table = palette->table;
    const uint32 alpha_mask = (explicit_alpha) ? 0xff000000 : 0;

    for (uint i = 0; i < 256; i++) {
        if (i < 16) {
            table[i] = base16[i];
        } else if (i < 232) {
            // 6x6x6 color cube (16-231)
            uint8 n;
            table[i] |= ((n = ((i - 16) / 36) % 6) ? 40 * n + 55 : 0) << 16;
            table[i] |= ((n = ((i - 16) /  6) % 6) ? 40 * n + 55 : 0) <<  8;
            table[i] |= ((n = ((i - 16) /  1) % 6) ? 40 * n + 55 : 0) <<  0;
        } else {
            // Grayscale from darkest to lightest (232-255)
            const uint8 k = (i - 232) * 10 + 8;
            table[i] |= k << 16;
            table[i] |= k <<  8;
            table[i] |= k <<  0;
        }

        table[i] |= alpha_mask;
    }

    palette->bg = table[0];
    palette->fg = table[7];
}

uint32
palette_resolve_color(const Palette *palette, Color *color)
{
    ASSERT(color);

    if (!color->resolved) {
        ASSERT(color->key < NUM_COLORS);
        color->val = palette->table[color->key];
        color->resolved = true;
    } else {
        ASSERT(palette);
    }

    return color->val;
}

uint32
palette_query_color(const Palette *palette, Color color)
{
    return palette_resolve_color(palette, &color);
}

Color
color_from_key(uint16 key)
{
    ASSERT(key < NUM_COLORS);
    return (Color){ .resolved = false, .key = key };
}

Color
color_from_argb_1u(uint32 val)
{
    return (Color){ .resolved = true, .val = val };
}

Color
color_from_argb_4u(uint8 a, uint8 r, uint8 g, uint8 b)
{
    return color_from_argb_1u(PACK_4x8(a, r, g, b));
}

Color
color_from_rgb_1u(uint32 val)
{
    return color_from_argb_1u(val & 0xffffff);
}

Color
color_from_rgb_3u(uint8 r, uint8 g, uint8 b)
{
    return color_from_argb_4u(0, r, g, b);
}

const char *
color_key_to_string(uint16 key)
{
    switch (key) {
    case BLACK:    return "Black";
    case RED:      return "Red";
    case GREEN:    return "Green";
    case YELLOW:   return "Yellow";
    case BLUE:     return "Blue";
    case MAGENTA:  return "Magenta";
    case CYAN:     return "Cyan";
    case WHITE:    return "White";
    case LBLACK:   return "LightBlack";
    case LRED:     return "LightRed";
    case LGREEN:   return "LightGreen";
    case LYELLOW:  return "LightYellow";
    case LBLUE:    return "LightBlue";
    case LMAGENTA: return "LightMagenta";
    case LCYAN:    return "LightCyan";
    case LWHITE:   return "LightWhite";

    case BACKGROUND: return "Background";
    case FOREGROUND: return "Foreground";
    }

    return "Other";
}


