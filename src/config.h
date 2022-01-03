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

#ifndef CONFIG_H__
#define CONFIG_H__

static Config config = {
    .wm_class = "Temu",
    .wm_instance = "temu",
    .wm_title = "temu",
    .geometry = NULL,
    .font = NULL,
    .fontfile = NULL,
    .colors = {
        "#18191B", // background
        "#BCBFBD", // foreground

        // Normal colors
        "#282A2E", // black
        "#A54242", // red
        "#448D65", // green
        "#DE935F", // yellow
        "#4A7096", // blue
        "#85678F", // magenta
        "#558D86", // cyan
        "#747C84", // white

        // Bright colors
        "#41464D", // black
        "#CC6666", // red
        "#55A679", // green
        "#F0B674", // yellow
        "#638EB3", // blue
        "#A884BB", // magenta
        "#70AFAB", // cyan
        "#C5C8C6"  // white
    },
    .shell = NULL,
    .tablen = 8,
    .border_px = 0,
    .histsize = 128,
    .columns = 140,
    .rows = 40,
    .position = { .x = 0, .y = 0 }
};

#endif
