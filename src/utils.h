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
#define SETPTR(p,...) do {   \
  if (p) *(p) = __VA_ARGS__; \
while (0)

#define strequal(s1,s2)    (strcmp((s1), (s2)) == 0)
#define strnequal(s1,s2,n) (strncmp((s1), (s2), (n)) == 0)
#define memequal(s1,s2,n)  (memcmp((s1), (s2), (n)) == 0)
#define memclear(p,n,sz)   (memset((p), 0, (n) * (sz)))

#define printout(...) fprintf(stdout, __VA_ARGS__)
#define printerr(...) fprintf(stderr, __VA_ARGS__)

int dbgprint__(const char *, int, const char *, const char *, ...)
  attribute__((format(printf, 4, 5)));
#define DBGPRINT_ARG __FILE__, __LINE__, __func__

#ifdef BUILD_DEBUG
  #define dbgprint(...) dbgprint__(DBGPRINT_ARG, __VA_ARGS__)
  #define dbgbreak(...) raise(SIGTRAP)
  #define ASSERT(cond) do {                      \
    if (!(cond)) {                               \
        dbgprint("assertion failed: %s", #cond); \
        dbgbreak();                              \
    }                                            \
  } while (0)
#else
  #define dbgprint(...)
  #define dbgbreak(...)
  #define ASSERT(...)
#endif

void *xmalloc(size_t, size_t);
void *xcalloc(size_t, size_t);
void *xrealloc(void *, size_t, size_t);
void *memshift(void *, ptrdiff_t, size_t, size_t);
void *memcshift(void *, ptrdiff_t, size_t, size_t);
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

typedef struct {
    TimeRec t0;
    TimeRec t1;
    TimeRec t2;
} TimeBlock;

uint32 timer_msec(TimeRec *);
uint32 timer_usec(TimeRec *);
uint64 timer_nsec(TimeRec *);
int64 timediff_msec(const TimeRec *, const TimeRec *);
int64 timediff_usec(const TimeRec *, const TimeRec *);
void timeblk_update(TimeBlock *);

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
