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

#ifndef VECTOR_H__
#define VECTOR_H__

#include "common.h"

// Convenience aliases for making code-generation easier
#ifndef Vec1F
    #define Vec1F float
#endif
#ifndef Vec1I
    #define Vec1I int
#endif
#ifndef Vec1U
    #define Vec1U uint
#endif

#define XDEF_VEC2(t)                    \
    union {                             \
        Vec1##t arr[2];                 \
        struct { Vec1##t x, y; };       \
        struct { Vec1##t s, t; };       \
        struct { Vec1##t u, v; };       \
    }
#define XDEF_VEC3(t)                    \
    union {                             \
        Vec1##t arr[3];                 \
        struct { Vec1##t x, y, z; };    \
        struct { Vec1##t r, g, b; };    \
    }
#define XDEF_VEC4(t)                    \
    union {                             \
        Vec1##t arr[4];                 \
        struct { Vec1##t x, y, z, w; }; \
        struct { Vec1##t s, t, u, v; }; \
        struct { Vec1##t r, g, b, a; }; \
    }

// The typedef itself doesn't get expanded so as to not elude grep, ctags, etc...
typedef XDEF_VEC2(F) Vec2F;
typedef XDEF_VEC2(I) Vec2I;
typedef XDEF_VEC2(U) Vec2U;
typedef XDEF_VEC3(F) Vec3F;
typedef XDEF_VEC3(I) Vec3I;
typedef XDEF_VEC3(U) Vec3U;
typedef XDEF_VEC4(F) Vec4F;
typedef XDEF_VEC4(I) Vec4I;
typedef XDEF_VEC4(U) Vec4U;

#undef XDEF_VEC2
#undef XDEF_VEC3
#undef XDEF_VEC4

#define VEC__(n,t,...) ((Vec##n##t){{ __VA_ARGS__ }})

#define VEC2F(x,y)     VEC__(2,F,x,y)
#define VEC2I(x,y)     VEC__(2,I,x,y)
#define VEC2U(x,y)     VEC__(2,U,x,y)
#define VEC3F(x,y,z)   VEC__(3,F,x,y,z)
#define VEC3I(x,y,z)   VEC__(3,I,x,y,z)
#define VEC3U(x,y,z)   VEC__(3,U,x,y,z)
#define VEC4F(x,y,z,w) VEC__(4,F,x,y,z,w)
#define VEC4I(x,y,z,w) VEC__(4,I,x,y,z,w)
#define VEC4U(x,y,z,w) VEC__(4,U,x,y,z,w)

#endif

