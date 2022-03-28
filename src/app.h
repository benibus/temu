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

#ifndef APP_H__
#define APP_H__

#include "common.h"

typedef struct App App;
typedef struct Palette Palette;

// Accessors for window dimensions
int app_width(const App *app);
int app_height(const App *app);
int app_border(const App *app);
void app_get_dimensions(const App *app, int *width, int *height, int *border);
// Returns global font cache handle
void *app_fonts(const App *app);
// Accessors for font metrics
int app_font_width(const App *app);
int app_font_height(const App *app);
void app_get_font_metrics(const App *app,
                          int *width,
                          int *height,
                          int *ascent,
                          int *descent);
// Accessors for user-specified preferences
int app_histlines(const App *app);
int app_tabcols(const App *app);

struct Palette {
    union {
        uint32 base16[16];
        uint32 base256[256];
    };
    uint32 bg;
    uint32 fg;
};

void app_get_palette(const App *app, Palette *palette);

// Flags for setting window properties
enum {
    APPPROP_NONE  = (0),
    APPPROP_TITLE = (1 << 0),
    APPPROP_ICON  = (1 << 1),
};

// Set window properties using mask
void app_set_properties(App *app, uint8 props, const char *str, size_t len);

static inline void
app_set_icon(App *app, const char *str, size_t len)
{
    app_set_properties(app, APPPROP_ICON, str, len);
}

static inline void
app_set_title(App *app, const char *str, size_t len)
{
    app_set_properties(app, APPPROP_TITLE, str, len);
}

#endif

