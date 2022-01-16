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

#ifndef X11_PLATFORM_H__
#define X11_PLATFORM_H__

#include "common.h"
#include "gfx_context.h"
#include "platform.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/Xatom.h>

typedef struct {
    struct { void *param; WinFuncResize     *func; } resize;
    struct { void *param; WinFuncKeyPress   *func; } keypress;
    struct { void *param; WinFuncKeyRelease *func; } keyrelease;
    struct { void *param; WinFuncExpose     *func; } expose;
} WinCallbacks;

typedef struct Server_ Server;

struct Win_ {
    Server *srv;
    Window xid;
    XIC ic;
    GC gc;
    GfxTarget *target;
    bool online;
    int pid;
    int xpos;
    int ypos;
    int width;
    int height;
    int border;
    WinCallbacks callbacks;
};

struct Server_ {
    Display *dpy;
    int screen;
    Window root;
    Visual *visual;
    XIM im;
    Colormap colormap;
    int fd;
    int dpy_width;
    int dpy_height;
    float dpi;
    int depth;
    Gfx *gfx;
    Win clients[1];
};

#endif

