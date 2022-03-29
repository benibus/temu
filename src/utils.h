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

#ifndef UTILS_H__
#define UTILS_H__

#include "common.h"

#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define UNUSED(v) (void)v
#define FREE(p) ((p) ? (free(p), ((p) = NULL)) : NULL)
#define BSET(n,m,c) ((!!(c)) ? ((n) |= (m)) : ((n) &= ~(m)))
#define SWAP(T,v1,v2) do {  \
  T vt__ = (v1);            \
  (v1) = (v2), (v2) = vt__; \
} while (0)
#define SETPTR(p,...) (((p) != NULL) ? (*(p) = (__VA_ARGS__), (p)) : NULL)

#define strequal(s1,s2)    (strcmp((s1), (s2)) == 0)
#define strnequal(s1,s2,n) (strncmp((s1), (s2), (n)) == 0)
#define memequal(s1,s2,n)  (memcmp((s1), (s2), (n)) == 0)

#ifndef TRACEPRINTF_FMT
#define TRACEPRINTF_FMT "[%s:%u][%s] "
#endif
#ifndef TRACEPRINTF_ARGS
#define TRACEPRINTF_ARGS __FILE__, __LINE__, __func__
#endif

int trace_fprintf__(const char *restrict file,
                    uint line,
                    const char *restrict func,
                    FILE *restrict fp,
                    const char *restrict fmt,
                    ...)
    __attribute__((format(printf, 5, 6)));
#define trace_fprintf(...) trace_fprintf__(TRACEPRINTF_ARGS, __VA_ARGS__)
#define trace_printf(...)  trace_fprintf(stderr, __VA_ARGS__)

#ifndef ERRPRINTF_ENABLE_COLOR
#define ERRPRINTF_ENABLE_COLOR 1
#endif

int err_vfprintf(FILE *restrict fp, const char *restrict fmt, va_list args);
int err_fprintf(FILE *restrict fp, const char *restrict fmt, ...)
    __attribute__((format(printf, 2, 3)));
#define err_printf(...) err_fprintf(stderr, __VA_ARGS__)

#if BUILD_DEBUG
#define dbg_printf(...) trace_printf(__VA_ARGS__)
#else
#define dbg_printf(...)
#endif

void assert_fail(const char *file, uint line, const char *func, const char *expr);

#if BUILD_DEBUG
#define ASSERT(expr) \
    (!(expr) ? (void)(assert_fail(TRACEPRINTF_ARGS, #expr), raise(SIGTRAP)) : (void)0)
#else
#define ASSERT(expr)
#endif

void *xmalloc(size_t, size_t);
void *xcalloc(size_t, size_t);
void *xrealloc(void *, size_t, size_t);
uint64 round_pow2(uint64);
bool isprime(int32);
const char *charstring(uint32);

typedef struct {
    char *data;
    size_t size;
    int fd;
} FileBuf;

void *file_load(FileBuf *, const char *);
void file_unload(FileBuf *);

typedef union {
    uint32 units[4];
    struct {
        uint32 sec;
        uint32 msec;
        uint32 usec;
        uint32 nsec;
    };
} TimeRec;

uint32 timer_msec(TimeRec *);
uint32 timer_usec(TimeRec *);
uint64 timer_nsec(TimeRec *);

static inline bool strempty(const char *str) { return !(str && str[0]); }

static inline int64 imin(int64 a, int64 b) { return MIN(a, b); }
static inline int64 imax(int64 a, int64 b) { return MAX(a, b); }
static inline int64 iclamp(int64 a, int64 b, int64 c) { return CLAMP(a, b, c); }
static inline int64 umin(int64 a, int64 b) { return MAX(MIN(a, b), 0); }
static inline int64 umax(int64 a, int64 b) { return MAX(MAX(a, b), 0); }
static inline int64 uclamp(int64 a, int64 b, int64 c) { return CLAMP(a, MAX(b, 0), c); }
static inline int uwrap(int n, int m) { return ((n %= m) < 0) ? m + n : n; }

struct ArrHdr_ {
    size_t count;
    size_t max;
    uchar data[];
};

#define arr__(p) ((struct ArrHdr_ *)((uchar *)(p) - offsetof(struct ArrHdr_, data)))

#define arr_count(p)     ((p) ? arr__(p)->count : 0)
#define arr_max(p)       ((p) ? arr__(p)->max : 0)
#define arr_size(p)      ((p) ? arr_count(p) * sizeof(*(p)) : 0)
#define arr_head(p)      ((p) + 0)
#define arr_tail(p)      ((p) + arr_count(p))

#define arr_grow(p,n)    ((n) > arr_max(p) ? ((p) = arr__resize((p), (n), sizeof(*(p)))) : 0)
#define arr_reserve(p,n) (arr_grow((p), arr_count(p) + (n)))
#define arr_push(p,...)  (arr_reserve((p), 1), (p)[arr__(p)->count++] = (__VA_ARGS__))
#define arr_pop(p)       ((p) ? (p)[(arr__(p)->count -= (arr_count(p) > 0) ? 1 : 0)] : 0)
#define arr_set(p,i,...) ((i) < arr_max(p) ? ((p)[(i)] = (__VA_ARGS__)) : 0)
#define arr_get(p,i)     ((i) < arr_max(p) ? (p) + (i) : NULL)
#define arr_free(p)      ((p) ? (free(arr__(p)), (p) = NULL) : 0)
#define arr_clear(p)     ((p) ? arr__(p)->count = 0 : 0)

static inline void *
arr__resize(const void *data, size_t count, size_t stride)
{
    static_assert(offsetof(struct ArrHdr_, data) == 16, "Bad member alignment");

    struct ArrHdr_ *ptr;
    size_t max, size;

    max = MAX(2 * arr_max(data), MAX(count, offsetof(struct ArrHdr_, data)));
    ASSERT(count <= max);
    size = offsetof(struct ArrHdr_, data) + max * stride;

    if (data) {
        ptr = xrealloc(arr__(data), size, 1);
    } else {
        ptr = xcalloc(size, 1);
        ptr->count = 0;
    }
    ptr->max = max;

    return ptr->data;
}

#endif
