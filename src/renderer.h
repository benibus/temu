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

#ifndef RENDERER_H__
#define RENDERER_H__

#include "common.h"
#include "cells.h"
#include "fonts.h"

typedef union Vec2F_ Vec2F;
typedef union Vec2I_ Vec2I;
typedef union Vec2U_ Vec2U;
typedef union Vec3F_ Vec3F;
typedef union Vec3I_ Vec3I;
typedef union Vec3U_ Vec3U;
typedef union Vec4F_ Vec4F;
typedef union Vec4I_ Vec4I;
typedef union Vec4U_ Vec4U;

void renderer_set_dimensions(int, int, int, int, int, int, int);
bool renderer_init(void);
void renderer_shutdown(void);
void renderer_draw_frame(const Frame *, FontSet *);

union Vec2F_ {
    float arr[2];
    struct { float x, y; };
    struct { float s, t; };
    struct { float u, v; };
};

union Vec2I_ {
    int arr[2];
    struct { int x, y; };
    struct { int s, t; };
    struct { int u, v; };
};

union Vec2U_ {
    uint arr[2];
    struct { uint x, y; };
    struct { uint s, t; };
    struct { uint u, v; };
};

union Vec3F_ {
    float arr[3];
    struct { float x, y, z; };
    struct { float r, g, b; };
};

union Vec3I_ {
    int arr[3];
    struct { int x, y, z; };
    struct { int r, g, b; };
};

union Vec3U_ {
    uint arr[3];
    struct { uint x, y, z; };
    struct { uint r, g, b; };
};

union Vec4F_ {
    float arr[4];
    struct { float x, y, z, w; };
    struct { float s, t, u, v; };
    struct { float r, g, b, a; };
};

union Vec4I_ {
    int arr[4];
    struct { int x, y, z, w; };
    struct { int s, t, u, v; };
    struct { int r, g, b, a; };
};

union Vec4U_ {
    uint arr[4];
    struct { uint x, y, z, w; };
    struct { uint s, t, u, v; };
    struct { uint r, g, b, a; };
};

#if STD_C11
  #define VEC__(N,T,...) ((Vec##N##T){{ __VA_ARGS__ }})
#else
  #define VEC__(N,T,...) ((Vec##N##T){ __VA_ARGS__ })
#endif

#define VEC2F(x,y) VEC__(2,F,x,y)
#define VEC2I(x,y) VEC__(2,I,x,y)
#define VEC2U(x,y) VEC__(2,U,x,y)

#define VEC3F(x,y,z) VEC__(3,F,x,y,z)
#define VEC3I(x,y,z) VEC__(3,I,x,y,z)
#define VEC3U(x,y,z) VEC__(3,U,x,y,z)

#define VEC4F(x,y,z,w) VEC__(4,F,x,y,z,w)
#define VEC4I(x,y,z,w) VEC__(4,I,x,y,z,w)
#define VEC4U(x,y,z,w) VEC__(4,U,x,y,z,w)

#endif

