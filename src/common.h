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

#if defined(NDEBUG) && (defined(BUILD_DEBUG) || !defined(BUILD_RELEASE))
  #error "Build-type definition mismatch"
#endif
#if defined(BUILD_RELEASE) && !defined(NDEBUG)
  #define NDEBUG 1
#endif

#if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
  #define attribute__ __attribute__
#else
  #define attribute__(...)
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#include <limits.h>

typedef long long llong;
typedef long double ldouble;

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned long long ullong;

typedef int8_t    int8;
typedef int16_t   int16;
typedef int32_t   int32;
typedef int64_t   int64;
typedef uint8_t   uint8;
typedef uint16_t  uint16;
typedef uint32_t  uint32;
typedef uint64_t  uint64;
typedef int8_t    i8;
typedef int16_t   i16;
typedef int32_t   i32;
typedef int64_t   i64;
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;

typedef uint32_t UTF8;
typedef uint32_t UCS4;

typedef struct {
    char *data;
    uint32 len;
    uint32 max;
} String;

#define MIN(a,b)        (((a) < (b)) ? (a) : (b))
#define MAX(a,b)        (((a) > (b)) ? (a) : (b))
#define CLAMP(n,a,b)    (((n) < (a)) ? (a) : (((n) > (b)) ? (b) : (n)))
#define BETWEEN(n,a,b)  (((n) >= (a)) && ((n) <= (b)))
#define ABS(n)          (((n) < 0) ? -(n) : (n))
#define LEN(arr)        (sizeof((arr)) / sizeof((arr)[0]))
#define DEFAULT(v1,v2)  ((!!(v1)) ? (v1) : (v2))
#define ALIGN_DOWN(n,a) ((n) & ~((a) - 1))
#define ALIGN_UP(n,a)   (((n) + ((a) - 1)) & ~((a) - 1))

#if STD_C11
  #define vecx(...) {{ __VA_ARGS__ }}
#else
  #define vecx(...) { __VA_ARGS__ }
#endif

#define vec2(x_,y_)       vecx((x_), (y_))
#define vec3(x_,y_,z_)    vecx((x_), (y_), (z_))
#define vec4(x_,y_,z_,w_) vecx((x_), (y_), (z_), (w_))

#define pack_rgb(r,g,b)    ((((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff))
#define pack_bgr(r,g,b)    ((((b)&0xff)<<16)|(((g)&0xff)<<8)|((r)&0xff))
#define pack_rgba(r,g,b,a) ((((r)&0xff)<<24)|(((g)&0xff)<<16)|(((b)&0xff)<<8)|((a)&0xff))
#define pack_argb(r,g,b,a) ((((a)&0xff)<<24)|(((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff))
#define pack_abgr(r,g,b,a) ((((a)&0xff)<<24)|(((b)&0xff)<<16)|(((g)&0xff)<<8)|((r)&0xff))
#define pack_rgbx(rgb,a)   ((((rgb)&0x00ffffff)<<8)|((a)&0xff))
#define pack_xrgb(rgb,a)   (((rgb)&0x00ffffff)|(((a)&0xff)<<24))

#endif

