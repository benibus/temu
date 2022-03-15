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

#ifndef GFX_CONTEXT_H__
#define GFX_CONTEXT_H__

#include "common.h"

#include <EGL/egl.h>

typedef struct Gfx Gfx;

Gfx *gfx_create_context(EGLNativeDisplayType dpy);
void gfx_destroy_context(Gfx *gfx);
EGLint gfx_get_native_visual(Gfx *gfx);
bool gfx_bind_surface(Gfx *gfx, EGLNativeWindowType win);
bool gfx_get_size(const Gfx *gfx, int *r_width, int *r_height);
void gfx_resize(Gfx *gfx, uint width, uint height);
void gfx_swap_buffers(const Gfx *gfx);
void gfx_print_info(const Gfx *gfx);
void gfx_set_debug_object(const void *obj);
void gfx_set_vsync(const Gfx *gfx, bool enable);

#endif

