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

typedef void (*WinFuncResize)(void *, int, int);
typedef void (*WinFuncKeyPress)(void *, uint, uint, const byte *, int);
typedef void (*WinFuncKeyRelease)(void *, uint, uint);
typedef void (*WinFuncExpose)(void *);

typedef struct {
    WinFuncResize     resize;
    WinFuncKeyPress   key_press;
    WinFuncKeyRelease key_release;
    WinFuncExpose     expose;
} WinCallbacks;

typedef struct {
    void *param;
    char *wm_title;
    char *wm_instance;
    char *wm_class;
    uint16 cols;
    uint16 rows;
    uint16 colpx;
    uint16 rowpx;
    uint16 border;
    bool smooth_resize;
    WinCallbacks callbacks;
} WinConfig;

bool  platform_setup(void);
void  platform_shutdown(void);
float platform_get_dpi(void);
int   platform_get_fileno(void);
bool  platform_parse_color_string(const char *name, uint32 *color);
int   platform_events_pending(void);

Win*  window_create(WinConfig config);
void  window_destroy(Win *win);
bool  window_online(const Win *win);
void  window_get_dimensions(const Win *win, int *width, int *height, int *border);
void  window_set_title(Win *win, const char *name, size_t len);
void  window_set_icon(Win *win, const char *name, size_t len);
int   window_poll_events(Win *win);
bool  window_show(Win *win);
void  window_update(const Win *win);
bool  window_make_current(const Win *win);

#endif
