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
#define OPENGL_INCLUDE_PLATFORM 1
#include "opengl.h"
#include "platform.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

typedef struct Server_ Server;

struct Win_ {
    void *param;
    Server *server;
    Window xid;
    XIC ic;
    GC gc;
    EGLSurface surface;
    bool online;
    int pid;
    int xpos;
    int ypos;
    int width;
    int height;
    int border;
    struct {
        EventFuncResize   resize;
        EventFuncKeyPress keypress;
        EventFuncExpose   expose;
    } callbacks;
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
    struct {
        EGLDisplay dpy;
        EGLContext context;
        EGLConfig config;
        struct {
            EGLint major;
            EGLint minor;
        } version;
    } egl;
    Win clients[4];
};

#endif

