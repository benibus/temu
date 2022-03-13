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
        "#1b1c1e", // Background
        "#a5a8a6", // Foreground

        "#34373c", // Black
        "#b25449", // Red
        "#698754", // Green
        "#d88e61", // Yellow
        "#547991", // Blue
        "#887190", // Magenta
        "#578d85", // Cyan
        "#8e929b", // White

        "#56575f", // Bright Black
        "#cb695c", // Bright Red
        "#749c61", // Bright Green
        "#e3ac72", // Bright Yellow
        "#6494af", // Bright Blue
        "#a085a6", // Bright Magenta
        "#6aa9a5", // Bright Cyan
        "#c5c8c6"  // Bright White
    }
};

#endif
