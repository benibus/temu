#ifndef CORE_DEFS_H__
#define CORE_DEFS_H__

#if defined(__GNUC__)
  #define CC_GCC 1
#elif defined(__clang__)
  #define CC_CLANG 1
#elif defined(__TINYC__)
  #define CC_TCC 1
#elif defined(_MSC_VER)
  #define CC_MSVC 1
#elif defined(__MINGW32__) || defined(__MINGW64__)
  #define CC_MINGW 1
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
	uint32 len;
	uint32 max;
	char *data;
} String;

#define MIN(a,b)       (((a) < (b)) ? (a) : (b))
#define MAX(a,b)       (((a) > (b)) ? (a) : (b))
#define CLAMP(n,a,b)   (((n) < (a)) ? (a) : (((n) > (b)) ? (b) : (n)))
#define BETWEEN(n,a,b) (((n) >= (a)) && ((n) <= (b)))
#define ABS(n)         (((n) < 0) ? -(n) : (n))
#define LEN(arr)       (sizeof((arr)) / sizeof((arr)[0]))
#define DEFAULT(v1,v2) ((!!(v1)) ? (v1) : (v2))
#define ALIGN(n,a)     ((n) & ~((a) - 1))
#define ALIGNUP(n,a)   (((n) + ((a) - 1)) & ~((a) - 1))
#define BSET(n,m,c)    ((!!(c)) ? ((n) |= (m)) : ((n) &= ~(m)))
#define RANGE(v1,o1,v2,o2,v3) (((v1) o1 (v2)) && ((v2) o2 (v3)))

#if STD_C11
  #define vecx(...) { { __VA_ARGS__ } }

  #define V2(T_,x,y)     \
  union {                \
    T_ arr[2];           \
    struct {             \
      T_ x;              \
      T_ y;              \
    };                   \
  }
  #define V3(T_,x,y,z)   \
  union {                \
    T_ arr[3];           \
    struct {             \
      T_ x;              \
      T_ y;              \
      T_ z;              \
    };                   \
  }
  #define V4(T_,x,y,z,w) \
  union {                \
    T_ arr[4];           \
    struct {             \
      T_ x;              \
      T_ y;              \
      T_ z;              \
      T_ w;              \
    };                   \
  }
#else
  #define vecx(...) { __VA_ARGS__ }

  #define V2(T_,x,y)     \
  struct {               \
    T_ x;                \
    T_ y;                \
  }
  #define V3(T_,x,y,z)   \
  struct {               \
    T_ x;                \
    T_ y;                \
    T_ z;                \
  }
  #define V4(T_,x,y,z,w) \
  struct {               \
    T_ x;                \
    T_ y;                \
    T_ z;                \
    T_ w;                \
  }
#endif

#define VLEN(v) (sizeof(v))

#define vec2(x_,y_)       vecx((x_), (y_))
#define vec3(x_,y_,z_)    vecx((x_), (y_), (z_))
#define vec4(x_,y_,z_,w_) vecx((x_), (y_), (z_), (w_))

#define pack_rgb(r,g,b)    ((((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff))
#define pack_bgr(r,g,b)    ((((b)&0xff)<<16)|(((g)&0xff)<<8)|((r)&0xff))
#define pack_rgba(r,g,b,a) ((((r)&0xff)<<24)|(((g)&0xff)<<16)|(((b)&0xff)<<8)|((a)&0xff))
#define pack_argb(r,g,b,a) ((((a)&0xff)<<24)|(((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff))
#define pack_abgr(r,g,b,a) ((((a)&0xff)<<24)|(((b)&0xff)<<16)|(((g)&0xff)<<8)|((r)&0xff))

#endif

