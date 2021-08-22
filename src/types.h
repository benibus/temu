#ifndef UTILS_TYPES_H__
#define UTILS_TYPES_H__

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

typedef ssize_t ssize;
typedef size_t  usize;

typedef uint32_t UTF8;
typedef uint32_t UCS4;

typedef struct {
	char *str;
	uint len;
} String;

#endif

