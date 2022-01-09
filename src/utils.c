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

#include "utils.h"

#include <stdarg.h>
#include <math.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

static struct timespec time_get_ts(const TimeRec *);
static struct timeval time_get_tv(const TimeRec *);
static TimeRec time_conv_ts(const struct timespec *);
static TimeRec time_conv_tv(const struct timeval *);

void *
xmalloc(size_t count, size_t stride)
{
    void *ptr = malloc(count * stride);

    if (!ptr) {
        fprintf(stderr, "%s() failure: aborting...\n", "malloc");
        abort();
    }

    return ptr;
}

void *
xcalloc(size_t count, size_t stride)
{
    void *ptr = calloc(count, stride);

    if (!ptr) {
        fprintf(stderr, "%s() failure: aborting...\n", "calloc");
        abort();
    }

    return ptr;
}

void *
xrealloc(void *ptr, size_t count, size_t stride)
{
    ptr = realloc(ptr, count * stride);

    if (!ptr) {
        fprintf(stderr, "%s() failure: aborting...\n", "realloc");
        abort();
    }

    return ptr;
}

uint64
round_pow2(uint64 n)
{
    n |= (n >> 1);
    n |= (n >> 2);
    n |= (n >> 4);
    n |= (n >> 8);
    n |= (n >> 16);
    n |= (n >> 32);

    return n;
}

bool
isprime(int32 n)
{
    const int32 min = 2;

    if (n > min && n & (min - 1)) {
        const int32 max = sqrt(n);
        for (int32 i = min + 1; i <= max; i += min) {
            if (n % i == 0) {
                return false;
            }
        }
        return true;
    }

    return (n == min);
}

void *
file_load(FileBuf *fbp, const char *path)
{
    struct stat stats;
    int fd = open(path, O_RDONLY, S_IRUSR);

    if (fstat(fd, &stats) >= 0) {
        char *buf = mmap(NULL, stats.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (buf && buf != MAP_FAILED) {
            fbp->data = buf;
            fbp->size = stats.st_size;
            fbp->fd   = fd;
            return buf;
        }
    }

    return NULL;
}

void
file_unload(FileBuf *fbp)
{
    if (munmap(fbp->data, fbp->size) == 0)
        fbp->data = NULL;
    close(fbp->fd);
}

#ifndef DBGPRINT_FMT
  #define DBGPRINT_FMT "@ %s:%d->%s() :: "
#endif
#ifndef DBGPRINT_FULLPATH
  #define DBGPRINT_FULLPATH 0
#endif

int
dbgprint__(const char *file_, int line, const char *func, const char *fmt, ...)
{
    const char *file;
#if DBGPRINT_FULLPATH
    file = file_;
#else
    file = (file = strrchr(file_, '/')) ? file + 1 : file_;
#endif

    va_list args;
    va_start(args, fmt);

    int n = 0;
    n += fprintf(stderr, DBGPRINT_FMT, file, line, func);
    n += vfprintf(stderr, fmt, args);
    fputc('\n', stderr);

    va_end(args);

    return n + 1;
}

// For debug output only
const char *
charstring(uint32 ucs4)
{
    static const char *symbols[] = {
        [0x00] = "NUL", [0x01] = "SOH", [0x02] = "STX", [0x03] = "ETX",
        [0x04] = "EOT", [0x05] = "ENQ", [0x06] = "ACK", [0x07] = "BEL",
        [0x08] = "BS",  [0x09] = "HT",  [0x0a] = "LF",  [0x0b] = "VT",
        [0x0c] = "FF",  [0x0d] = "CR",  [0x0e] = "SO",  [0x0f] = "SI",
        [0x10] = "DLE", [0x11] = "DC1", [0x12] = "DC2", [0x13] = "DC3",
        [0x14] = "DC4", [0x15] = "NAK", [0x16] = "SYN", [0x17] = "ETB",
        [0x18] = "CAN", [0x19] = "EM",  [0x1a] = "SUB", [0x1b] = "ESC",
        [0x1c] = "FS" , [0x1d] = "GS",  [0x1e] = "RS",  [0x1f] = "US",
        [0x20] = "Space"
    };
    static char buf[32] = { 0 };

    if (ucs4 < 0x7f) {
        if (ucs4 > ' ') {
            buf[0] = ucs4;
            buf[1] = 0;
        } else {
            return symbols[ucs4];
        }
    } else if (ucs4 == 0x7f) {
        return "Delete";
    } else {
        if (snprintf(buf, sizeof(buf), "%#.2x", ucs4) >= (int)sizeof(buf)) {
            buf[sizeof(buf)-1] = 0;
        }
    }

    return buf;
}

struct timespec
time_get_ts(const TimeRec *t)
{
    struct timespec ts = { 0 };

    ts.tv_sec = t->sec;
    ts.tv_nsec += t->msec * 1E6;
    ts.tv_nsec += t->usec * 1E3;
    ts.tv_nsec += t->nsec;

    return ts;
}

struct timeval
time_get_tv(const TimeRec *t)
{
    struct timeval tv = { 0 };

    tv.tv_sec = t->sec;
    tv.tv_usec += t->msec * 1E3;
    tv.tv_usec += t->usec;
    tv.tv_usec += t->nsec / 1E3;

    return tv;
}

TimeRec
time__conv_ts(const struct timespec *ts)
{
    TimeRec t = { 0 };

    size_t nsec = ts->tv_sec * 1E9 + ts->tv_nsec;

    t.sec  = nsec / 1E9, nsec -= t.sec  * 1E9;
    t.msec = nsec / 1E6, nsec -= t.msec * 1E6;
    t.usec = nsec / 1E3, nsec -= t.usec * 1E3;
    t.nsec = nsec;

    return t;
}

TimeRec
time__conv_tv(const struct timeval *tv)
{
    TimeRec t = { 0 };

    size_t nsec = tv->tv_sec * 1E9 + tv->tv_usec * 1E3;

    t.sec  = nsec / 1E9, nsec -= t.sec  * 1E9;
    t.msec = nsec / 1E6, nsec -= t.msec * 1E6;
    t.usec = nsec / 1E3, nsec -= t.usec * 1E3;
    t.nsec = nsec;

    return t;
}

uint32
timer_msec(TimeRec *ret)
{
    struct timespec ts;
    uint32 t = 0;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t += ts.tv_sec * 1E3;
    t += ts.tv_nsec / 1E6;

    if (ret) *ret = time__conv_ts(&ts);

    return t;
}

uint32
timer_usec(TimeRec *ret)
{
    struct timespec ts;
    uint32 t = 0;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t += ts.tv_sec * 1E6;
    t += ts.tv_nsec / 1E3;

    if (ret) *ret = time__conv_ts(&ts);

    return t;
}

uint64
timer_nsec(TimeRec *ret)
{
    struct timespec ts;
    uint32 t = 0;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t += ts.tv_sec * 1E9;
    t += ts.tv_nsec;

    if (ret) *ret = time__conv_ts(&ts);

    return t;
}

#define DIFF_SIGNED(a,b,O_,m) (((int64)(a) - (int64)(b)) O_ (m))
int64
timediff_msec(const TimeRec *t0, const TimeRec *t1)
{
    int64 res = 0;

    res += DIFF_SIGNED(t0->sec,  t1->sec,  *, 1E3);
    res += DIFF_SIGNED(t0->msec, t1->msec, *, 1);
    res += DIFF_SIGNED(t0->usec, t1->usec, /, 1E3);
    res += DIFF_SIGNED(t0->nsec, t1->nsec, /, 1E6);

    return res;
}

int64
timediff_usec(const TimeRec *t0, const TimeRec *t1)
{
    int64 res = 0;

    res += DIFF_SIGNED(t0->sec,  t1->sec,  *, 1E6);
    res += DIFF_SIGNED(t0->msec, t1->msec, *, 1E3);
    res += DIFF_SIGNED(t0->usec, t1->usec, *, 1);
    res += DIFF_SIGNED(t0->nsec, t1->nsec, /, 1E6);

    return res;
}
#undef DIFF_SIGNED

#define TIME_ISSET(t) ((t)->sec || (t)->msec || (t)->usec || (t)->nsec)
void
timeblk_update(TimeBlock *blk)
{
    ASSERT(blk);

    TimeRec tmp = { 0 };
    timer_msec(&tmp);

    if (TIME_ISSET(&blk->t0)) {
        blk->t1 = blk->t2;
    } else {
        blk->t0 = tmp;
        blk->t1 = tmp;
    }
    blk->t2 = tmp;
}
#undef TIME_ISSET

void *
memshift(void *base, ptrdiff_t shift, size_t count, size_t stride)
{
    if (!(base && shift && count))
        return base;
    stride = DEFAULT(stride, 1);

    uchar *p1 = (uchar *)base;
    uchar *p2 = p1 + (shift * (ptrdiff_t)stride);

    return memmove(p2, p1, count * stride);
}

void *
memcshift(void *base, ptrdiff_t shift, size_t count, size_t stride)
{
    if (!(base && shift && count))
        return base;
    stride = DEFAULT(stride, 1);

    ptrdiff_t offset = shift * (ptrdiff_t)stride;
    size_t bytes = count * stride;

    uchar *p1 = (uchar *)base;
    uchar *p2 = p1 + offset;
    uchar *vacant = (offset < 0) ? p2 + bytes : p1;

    void *res = memmove(p2, p1, bytes);
    if (res != NULL) {
        memset(vacant, 0, ABS(offset));
    }

    return res;
}

