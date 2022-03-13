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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

static inline size_t
safe_mul(size_t a, size_t b, size_t max)
{
    if (a && b && b > max / a) {
        err_printf("%s(%zu, %zu): %s\n", __func__, a, b, strerror(EOVERFLOW));
        exit(EOVERFLOW);
    }
    return a * b;
}

void *
xmalloc(size_t count, size_t stride)
{
    void *ptr = malloc(safe_mul(count, stride, SIZE_MAX));

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
    ptr = realloc(ptr, safe_mul(count, stride, SIZE_MAX));

    if (!ptr) {
        fprintf(stderr, "%s() failure: aborting...\n", "realloc");
        abort();
    }

    return ptr;
}

uint64
round_pow2(uint64 n)
{
    if (IS_POW2(n)) {
        return n;
    } else if (n > (1UL << 63)) {
        err_printf("%s(0x%016lx): %s\n", __func__, n, strerror(EOVERFLOW));
        exit(EOVERFLOW);
    }

    n |= (n >> 1);
    n |= (n >> 2);
    n |= (n >> 4);
    n |= (n >> 8);
    n |= (n >> 16);
    n |= (n >> 32);

    return n + 1;
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

void
assert_fail(const char *file,
            uint line,
            const char *func,
            const char *expr)
{
    trace_fprintf__(file, line, func, stderr, "Assertion '%s' failed\n", expr);
}

static inline int
get_basename(const char *path)
{
    const char *sep = strrchr(path, '/');
    return sep ? &sep[1] - path : 0;
}

int
trace_fprintf__(const char *restrict file,
                uint line,
                const char *restrict func,
                FILE *restrict fp,
                const char *restrict fmt,
                ...)
{
    int n = 0;
    file += get_basename(file);
    n += fprintf(fp, TRACEPRINTF_FMT, file, line, func);
    va_list args;
    va_start(args, fmt);
    n += vfprintf(fp, fmt, args);
    va_end(args);

    return n;
}

int
err_fprintf(FILE *restrict fp, const char *restrict fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = err_vfprintf(fp, fmt, args);
    va_end(args);

    return n;
}

int
err_vfprintf(FILE *restrict fp, const char *restrict fmt, va_list args)
{
    static const char *heading = "[error]";

    int n = 0;
    int res;
#if ERRPRINTF_ENABLE_COLOR
    if (isatty(fileno(fp))) {
        res = fprintf(fp, "\033[%d;%dm%s\033[m ", 1, 31, heading);
    } else
#endif
    {
        res = fprintf(fp, "%s ", heading);
    }
    if (res >= 0) {
        n += res;
        res = vfprintf(fp, fmt, args);
    }

    return (res < 0) ? res : res + n;
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

static inline TimeRec
from_timespec(const struct timespec *ts)
{
    TimeRec t = { 0 };

    size_t nsec = ts->tv_sec * 1e9 + ts->tv_nsec;

    t.sec  = nsec * 1e-9, nsec -= t.sec  * 1e9;
    t.msec = nsec * 1e-6, nsec -= t.msec * 1e6;
    t.usec = nsec * 1e-3, nsec -= t.usec * 1e3;
    t.nsec = nsec;

    return t;
}

static inline TimeRec
from_timeval(const struct timeval *tv)
{
    TimeRec t = { 0 };

    size_t nsec = tv->tv_sec * 1e9 + tv->tv_usec * 1e3;

    t.sec  = nsec * 1e-9, nsec -= t.sec  * 1e9;
    t.msec = nsec * 1e-6, nsec -= t.msec * 1e6;
    t.usec = nsec * 1e-3, nsec -= t.usec * 1e3;
    t.nsec = nsec;

    return t;
}

uint32
timer_msec(TimeRec *ret)
{
    struct timespec ts;
    uint32 t = 0;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t += ts.tv_sec * 1e3;
    t += ts.tv_nsec * 1e-6;

    if (ret) *ret = from_timespec(&ts);

    return t;
}

uint32
timer_usec(TimeRec *ret)
{
    struct timespec ts;
    uint32 t = 0;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t += ts.tv_sec * 1e6;
    t += ts.tv_nsec * 1e-3;

    if (ret) *ret = from_timespec(&ts);

    return t;
}

uint64
timer_nsec(TimeRec *ret)
{
    struct timespec ts;
    uint32 t = 0;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t += ts.tv_sec * 1e9;
    t += ts.tv_nsec;

    if (ret) *ret = from_timespec(&ts);

    return t;
}

