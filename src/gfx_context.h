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

typedef struct Gfx_ Gfx;
typedef struct GfxTarget_ GfxTarget;

Gfx *gfx_context_create(EGLNativeDisplayType dpy);
void gfx_context_destroy(Gfx *gfx);
bool gfx_context_init(Gfx *gfx);
EGLint gfx_get_visual_id(Gfx *gfx);
GfxTarget *gfx_target_create(Gfx *gfx, EGLNativeWindowType win);
bool gfx_target_destroy(GfxTarget *target);
bool gfx_target_init(GfxTarget *target);
bool gfx_target_query_size(const GfxTarget *target, int *r_width, int *r_height);
void gfx_target_set_size(GfxTarget *target, uint width, uint height,
                         uint inc_width,
                         uint inc_height,
                         uint border);
GfxTarget *gfx_get_target(const Gfx *gfx);
bool gfx_set_target(Gfx *gfx, GfxTarget *target);
void gfx_target_post(const GfxTarget *target);
void gfx_set_vsync(const Gfx *gfx, bool enable);
void gfx_print_info(const Gfx *gfx);
void gfx_set_debug_object(const void *obj);

#endif

