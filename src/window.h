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

#ifndef WINDOW_H__
#define WINDOW_H__

#include "common.h"
#include "keycodes.h"

typedef struct Win Win;

typedef struct {
    char *wm_name;
    char *wm_class;
    char *wm_title;
    uint width;
    uint height;
    uint inc_width;
    uint inc_height;
    uint min_width;
    uint min_height;
} WinConfig;

Win *window_create(void);
bool window_configure(Win *win, WinConfig cfg);
void window_destroy(Win *win);
bool window_open(Win *win, int *width, int *height);
bool window_make_current(const Win *win);
bool window_online(const Win *win);
float window_get_dpi(const Win *win);
int window_get_fileno(const Win *win);
bool window_query_color(const Win *, const char *name, uint32 *color);
int window_events_pending(const Win *win);
void window_get_size(const Win *win, int *width, int *height);
void window_set_title(Win *win, const char *name, size_t len);
void window_set_icon(Win *win, const char *name, size_t len);
void window_refresh(const Win *win);
int window_width(const Win *win);
int window_height(const Win *win);

#endif
