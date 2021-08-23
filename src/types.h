#ifndef UTILS_TYPES_H__
#define UTILS_TYPES_H__

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
typedef intptr_t  intptr;
typedef uint8_t   uint8;
typedef uint16_t  uint16;
typedef uint32_t  uint32;
typedef uint64_t  uint64;
typedef uintptr_t uintptr;
typedef int8_t    i8;
typedef int16_t   i16;
typedef int32_t   i32;
typedef int64_t   i64;
typedef intptr_t  iptr;
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef uintptr_t uptr;

typedef uint32_t UTF8;
typedef uint32_t UCS4;

typedef struct {
	char *str;
	uint len;
} String;

#if (__STDC_VERSION__ >= 201112L)
#define VEC2T(T_)                  \
union {                            \
  T_ arr[2];                       \
  struct { T_ x, y; };             \
  struct { T_ w, h; };             \
  struct { T_ s, t; };             \
  struct { T_ u, v; };             \
  struct { T_ a, b; };             \
  struct { T_ i, j; };             \
  struct { T_ dx, dy; };           \
  struct { T_ t1, t2; };           \
  struct { T_ min, max; };         \
  struct { T_ beg, end; };         \
  struct { T_ top, bot; };         \
  struct { T_ col, row; };         \
  struct { T_ head, tail; };       \
  struct { T_ width, height; };    \
}

#define VEC3T(T_)                  \
union {                            \
  T_ arr[3];                       \
  struct { T_ x, y, z; };          \
  struct { T_ x_, y_, w; };        \
  struct { T_ r, g, b; };          \
  struct { T_ l, m, h; };          \
  struct { T_ i, j, k; };          \
  struct { T_ t0, t1, t2; };       \
  struct { T_ min, mid, max; };    \
  struct { T_ beg, cur, end; };    \
  struct { T_ col, row, len; };    \
  struct { T_ red, green, blue; }; \
}

#define VEC4T(T_)                  \
union {                            \
  T_ arr[4];                       \
  struct { T_ x, y, z, w; };       \
  struct { T_ r, g, b, a; };       \
  struct { T_ i, j, k, l; };       \
  struct { T_ x1, y1, x2, y2; };   \
  struct { T_ x0, y0, xi, yi; };   \
  struct { T_ nx, ny, dx, dy; };   \
  struct { T_ xx, xy, yx, yy; };   \
  struct { T_ t0, t1, t2, t3; };   \
  struct { T_ xmin, ymin, xmax, ymax;  }; \
  struct { T_ col, row, width, height; }; \
  struct { T_ red, green, blue, alpha; }; \
}

typedef VEC2T(int)    Vec2I;
typedef VEC2T(uint)   Vec2U;
typedef VEC2T(float)  Vec2F;
typedef VEC2T(double) Vec2LF;
typedef VEC2T(int8)   Vec2I8;
typedef VEC2T(uint8)  Vec2U8;
typedef VEC2T(int16)  Vec2I16;
typedef VEC2T(uint16) Vec2U16;
typedef VEC2T(int32)  Vec2I32;
typedef VEC2T(uint32) Vec2U32;
typedef VEC2T(int64)  Vec2I64;
typedef VEC2T(uint64) Vec2U64;

typedef VEC3T(int)    Vec3I;
typedef VEC3T(uint)   Vec3U;
typedef VEC3T(float)  Vec3F;
typedef VEC3T(double) Vec3LF;
typedef VEC3T(int8)   Vec3I8;
typedef VEC3T(uint8)  Vec3U8;
typedef VEC3T(int16)  Vec3I16;
typedef VEC3T(uint16) Vec3U16;
typedef VEC3T(int32)  Vec3I32;
typedef VEC3T(uint32) Vec3U32;
typedef VEC3T(int64)  Vec3I64;
typedef VEC3T(uint64) Vec3U64;

typedef VEC4T(int)    Vec4I;
typedef VEC4T(uint)   Vec4U;
typedef VEC4T(float)  Vec4F;
typedef VEC4T(double) Vec4LF;
typedef VEC4T(int8)   Vec4I8;
typedef VEC4T(uint8)  Vec4U8;
typedef VEC4T(int16)  Vec4I16;
typedef VEC4T(uint16) Vec4U16;
typedef VEC4T(int32)  Vec4I32;
typedef VEC4T(uint32) Vec4U32;
typedef VEC4T(int64)  Vec4I64;
typedef VEC4T(uint64) Vec4U64;

#define vec_(...) { .arr = { __VA_ARGS__ } }
#define vec2(x_,y_)       vec_((x_), (y_))
#define vec3(x_,y_,z_)    vec_((x_), (y_), (z_))
#define vec4(x_,y_,z_,w_) vec_((x_), (y_), (z_), (w_))

#define VECLEN(v) (sizeof((v).arr)/sizeof((v).arr[0]))

#define vec2v(v) vec2((VECLEN(v) > 0) ? (v).arr[0] : 0, \
                      (VECLEN(v) > 1) ? (v).arr[1] : 0)

#define vec3v(v) vec3((VECLEN(v) > 0) ? (v).arr[0] : 0, \
                      (VECLEN(v) > 1) ? (v).arr[1] : 0, \
                      (VECLEN(v) > 2) ? (v).arr[2] : 0)

#define vec4v(v) vec4((VECLEN(v) > 0) ? (v).arr[0] : 0, \
                      (VECLEN(v) > 1) ? (v).arr[1] : 0, \
                      (VECLEN(v) > 2) ? (v).arr[2] : 0, \
                      (VECLEN(v) > 3) ? (v).arr[3] : 0)

#endif

#endif

