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

#ifndef COMMON_H__
#define COMMON_H__

#if defined(__GNUC__)
  #define COMPILER_GCC 1
#elif defined(__clang__)
  #define COMPILER_CLANG 1
#elif defined(__TINYC__)
  #define COMPILER_TCC 1
#elif defined(_MSC_VER)
  #define COMPILER_MSVC 1
#elif defined(__MINGW32__) || defined(__MINGW64__)
  #define COMPILER_MINGW 1
#endif

#if (__STDC_VERSION__ < 199901L)
  #define STD_C89 1
  #define STD_C99 0
  #define STD_C11 0
  #define STD_C18 0
  #define STDC 1989L
#elif (__STDC_VERSION__ < 201112L)
  #define STD_C89 1
  #define STD_C99 1
  #define STD_C11 0
  #define STD_C18 0
  #define STDC 1999L
#elif (__STDC_VERSION__ < 201710L)
  #define STD_C89 1
  #define STD_C99 1
  #define STD_C11 1
  #define STD_C18 0
  #define STDC 2011L
#else
  #define STD_C89 1
  #define STD_C99 1
  #define STD_C11 1
  #define STD_C18 1
  #define STDC 2018L
#endif

#if defined(NDEBUG) && (BUILD_DEBUG || !BUILD_RELEASE)
    #error "Build-type definition mismatch"
#endif
#if BUILD_RELEASE && !defined(NDEBUG)
    #define NDEBUG 1
#endif

#if !defined(COMPILER_GCC) && !defined(COMPILER_CLANG)
    #ifndef __attribute__
    #define __attribute__(x)
    #endif
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef long long llong;
typedef unsigned long long ullong;

typedef int8_t   int8;
typedef int16_t  int16;
typedef int32_t  int32;
typedef int64_t  int64;
typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

#define XPASTE__(a,b) a##b
#define XPASTE(a,b)   XPASTE__(a,b)
#define XSTRING__(s)  #s
#define XSTRING(s)    XSTRING__(s)

#define MIN(a,b)        (((a) < (b)) ? (a) : (b))
#define MAX(a,b)        (((a) > (b)) ? (a) : (b))
#define CLAMP(n,a,b)    (((n) < (a)) ? (a) : (((n) > (b)) ? (b) : (n)))
#define BETWEEN(n,a,b)  (((n) >= (a)) && ((n) <= (b)))
#define ABS(n)          (((n) < 0) ? -(n) : (n))
#define LEN(arr)        (sizeof((arr)) / sizeof((arr)[0]))
#define DEFAULT(v1,v2)  ((!!(v1)) ? (v1) : (v2))
#define IS_POW2(n)      ((n) > 0 && ((n) & ((n) - 1)) == 0)
#define ALIGN_DOWN(n,a) ((n) & ~((a) - 1))
#define ALIGN_UP(n,a)   (((n) + ((a) - 1)) & ~((a) - 1))
#define BITMASK(n)      ((1ULL << (n)) - 1)

#define PACK_4xN(n,a,b,c,d) \
    (                                       \
        (((a) & BITMASK(n)) << ((n) * 3)) | \
        (((b) & BITMASK(n)) << ((n) * 2)) | \
        (((c) & BITMASK(n)) << ((n) * 1)) | \
        (((d) & BITMASK(n)) << ((n) * 0))   \
    )
#define PACK_4x8(a,b,c,d) PACK_4xN(8, a, b, c, d)

#endif

