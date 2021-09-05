#ifndef UTILS_TYPES_H__
#define UTILS_TYPES_H__

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

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
	char *str;
	uint len;
} String;

#if (__STDC_VERSION__ >= 201112L)
#include <assert.h>

#define VLEN(v) (sizeof((v).arr)/sizeof((v).arr[0]))

#define vecx(...) { .arr = { __VA_ARGS__ } }
#define vec2(x_,y_)       vecx((x_), (y_))
#define vec3(x_,y_,z_)    vecx((x_), (y_), (z_))
#define vec4(x_,y_,z_,w_) vecx((x_), (y_), (z_), (w_))

#define Vec2(T_,x,y)     \
  union {                \
    T_ arr[2];           \
    struct {             \
      T_ x;              \
      T_ y;              \
    };                   \
  }

#define Vec3(T_,x,y,z)   \
  union {                \
    T_ arr[3];           \
    struct {             \
      T_ x;              \
      T_ y;              \
      T_ z;              \
    };                   \
  }

#define Vec4(T_,x,y,z,w) \
  union {                \
    T_ arr[4];           \
    struct {             \
      T_ x;              \
      T_ y;              \
      T_ z;              \
      T_ w;              \
    };                   \
  }

#define V2(...) Vec2(__VA_ARGS__)
#define V3(...) Vec3(__VA_ARGS__)
#define V4(...) Vec4(__VA_ARGS__)
#endif

#endif

