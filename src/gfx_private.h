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

#ifndef GFX_PRIVATE_H__
#define GFX_PRIVATE_H__

#include "common.h"
#include "opengl.h"

typedef struct GfxImage_ GfxImage;
typedef struct GfxInstance_ GfxInstance; // NOTE(ben): temporary

struct GfxImage_ {
    int width;
    int height;
    int colpx;
    int rowpx;
    int borderpx;

    GLuint prog;
    GLuint vao;
    GLuint vbo;
    GfxInstance *instances;

    struct {
        GLuint projection;
        GLuint cellpx;
        GLuint borderpx;
        GLuint screenpx;
    } uniforms;
};

GfxImage *gfx_image_create(void);
void gfx_image_destroy(GfxImage *img);
bool gfx_image_init(GfxImage *img);
void gfx_image_set_size(GfxImage *img,
                        int width, int height,
                        int inc_width, int inc_height,
                        int border);

#endif

