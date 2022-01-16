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

Gfx *gfx_create_context(EGLNativeDisplayType dpy);
void gfx_destroy_context(Gfx *gfx);
bool gfx_init_context(Gfx *gfx);
EGLint gfx_get_visual_id(Gfx *gfx);
GfxTarget *gfx_create_target(Gfx *gfx, EGLNativeWindowType win);
bool gfx_destroy_target(Gfx *gfx, GfxTarget *target);
bool gfx_query_target_size(const Gfx *gfx, GfxTarget *target, int *r_width, int *r_height);
void gfx_get_target_size(const Gfx *gfx, const GfxTarget *target, int *r_width, int *r_height);
void gfx_set_target_size(const Gfx *gfx, GfxTarget *target, uint width, uint height);
GfxTarget *gfx_get_target(const Gfx *gfx);
bool gfx_set_target(Gfx *gfx, GfxTarget *target);
void gfx_set_vsync(const Gfx *gfx, bool enable);
void gfx_post_target(const Gfx *gfx, const GfxTarget *target);
void gfx_print_info(const Gfx *gfx);
void gfx_set_debug_object(const void *obj);

#endif

