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

#ifndef PLATFORM_H__
#define PLATFORM_H__

#include "common.h"
#include "keymap.h"

typedef struct Win_ Win;

// TODO(ben): Roll this into window_destroy() under the hood
void platform_shutdown(void);

Win *window_create(void);
void window_destroy(Win *win);
bool window_init(Win *win);
bool window_make_current(const Win *win);
bool window_online(const Win *win);
float window_get_dpi(const Win *win);
int window_get_fileno(const Win *win);
bool window_query_color(const Win *, const char *name, uint32 *color);
int window_events_pending(const Win *win);
void window_get_size(const Win *win, int *width, int *height);
void window_set_title(Win *win, const char *name, size_t len);
void window_set_icon(Win *win, const char *name, size_t len);
bool window_set_size(Win *win, uint width, uint height);
void window_set_size_hints(Win *win, uint min_width, uint min_height, uint inc_width, uint inc_height);
void window_set_class_hints(Win *win, char *wm_name, char *wm_class);
int window_poll_events(Win *win);
void window_update(const Win *win);

typedef void WinFuncResize(void *, int, int);
typedef void WinFuncKeyPress(void *, uint, uint, const uchar *, int);
typedef void WinFuncKeyRelease(void *, uint, uint);
typedef void WinFuncExpose(void *);

void window_callback_resize(Win *win, void *param, WinFuncResize *func);
void window_callback_keypress(Win *win, void *param, WinFuncKeyPress *func);
void window_callback_keyrelease(Win *win, void *param, WinFuncKeyRelease *func);
void window_callback_expose(Win *win, void *param, WinFuncExpose *func);

#endif
