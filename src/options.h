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

#ifndef OPTIONS_H__
#define OPTIONS_H__

#include "common.h"

typedef struct Options Options;

struct Options {
    char *wm_class;
    char *wm_name;
    char *wm_title;
    char *geometry;
    char *font;
    char *fontpath;
    char *shell;
    int cols;
    int rows;
    int border;
    int tabcols;
    int histlines;
};

#endif

