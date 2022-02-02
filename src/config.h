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

static const AppPrefs default_prefs = {
    .wm_class = "Temu",
    .wm_name = "temu",
    .wm_title = "temu",
    .geometry = NULL,
    .font = NULL,
    .fontpath = NULL,
    .shell = NULL,
    .cols = 140,
    .rows = 40,
    .tabcols = 8,
    .border = 0,
    .histlines = 128,
    .colors = {
        "#18191B", // Background
        "#BCBFBD", // Foreground

        "#282A2E", // Black
        "#A54242", // Red
        "#448D65", // Green
        "#DE935F", // Yellow
        "#4A7096", // Blue
        "#85678F", // Magenta
        "#558D86", // Cyan
        "#747C84", // White

        "#41464D", // Bright Black
        "#CC6666", // Bright Red
        "#55A679", // Bright Green
        "#F0B674", // Bright Yellow
        "#638EB3", // Bright Blue
        "#A884BB", // Bright Magenta
        "#70AFAB", // Bright Cyan
        "#C5C8C6"  // Bright White
    }
};

#endif
