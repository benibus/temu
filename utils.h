#ifndef UTILS_H__
#define UTILS_H__

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
typedef int8_t    sbool;

#define ARRLEN(arr_)       ( sizeof((arr_)) / sizeof((arr_)[0]) )
#define MEMLEN(pb_,pa_)    ( ((pb_) - (pa_)) / sizeof(*(pa_)) )

#define MIN(a_,b_)         ( ((a_) < (b_)) ? (a_) : (b_) )
#define MAX(a_,b_)         ( ((a_) > (b_)) ? (a_) : (b_) )
#define CLAMP(n_,a_,b_)    ( MIN(MAX((n_),(a_)),(b_)) )
#define BETWEEN(n_,a_,b_)  ( ((n_) >= (a_)) && ((n_) <= (b_)) )
#define XBETWEEN(n_,a_,b_) ( ((n_) >  (a_)) && ((n_) <  (b_)) )
#define WRAP(n_,m_)        ( ((n_) + 1 < (m_)) ? (n_) + 1 : 0 )
#define WRAP_INV(n_,m_)    ( ((n_) > 0) ? (n_) - 1 : (m_) - 1 )
#define ABS(n_)            ( ((n_) < 0) ? -(n_) : (n_) )

#define MALLOC(p_,n_)  ( xmalloc((n_), sizeof(*(p_))) )
#define CALLOC(p_,n_)  ( xcalloc((n_), sizeof(*(p_))) )
#define REALLOC(p_,n_) ( xrealloc((p_), (n_), sizeof(*(p_))) )
#define FREE(p_) do { if ((p_) != 0) { free((p_)); (p_) = 0; } } while (0)

#define MEMSET(p_,c_,n_) do { memset((p_), (c_), sizeof(*(p_)) * (n_)); } while (0)
#define MEMCLEAR(p_,n_)  do { memset((p_), 0, sizeof(*(p_)) * (n_)); } while (0)
#define MEMMOVE(p1_,p2_,n_) ( memmove((p1_), (p2_), (n_) * sizeof(*(p1_))) )
#define MEMSHIFT(p_,i_,n_)  ( memmove((p_)+(i_), (p_), (n_) * sizeof(*(p_))) )

#define SWAP(T_,v1_,v2_) do { \
	T_ tmp_ = (v1_); (v1_) = (v2_); (v2_) = tmp_; \
} while (0)
#define FALLBACK(p1_,p2_) ( ((p1_)) ? (p1_) : (p2_) )
// more parseable syntax for bracketed literals
#define OBJ(T_,...) ((T_){ __VA_ARGS__ })

#define msizeof(T_,memb_) (sizeof(((T_ *)0)->memb_))

typedef struct String {
	size_t len;
	char *buf;
} String;

typedef struct Buffer {
	char *data;
	int size;
	int max;
} Buffer;

struct FileBuf {
	char *data;
	size_t size;
	int fd;
};

typedef struct {
	uint8_t *data;
	size_t size, count, max;
} SBuf;
typedef struct {
	uint8_t data[1<<8];
	size_t size, count, max;
} SBuf8;
typedef struct {
	uint8_t data[1<<12];
	size_t size, count, max;
} SBuf12;
typedef struct {
	uint8_t data[1<<16];
	size_t size, count, max;
} SBuf16;
#define SBUF(n_) \
    ((SBuf##n_){ .size = msizeof(SBuf##n_, data[0]), .max = msizeof(SBuf##n_, data), })

void *xmalloc(size_t, size_t);
void *xcalloc(size_t, size_t);
void *xrealloc(void *, size_t, size_t);
void xfree(void *);

u64 bitround(u64, int);

void fputn(FILE *, int, int);

void *file_load(struct FileBuf *, const char *);
void file_unload(struct FileBuf *);

char *asciistr(int);

#define strmatch(s1_,s2_)     ( strcmp((s1_), (s2_)) == 0 )
#define strnmatch(s1_,s2_,n_) ( strncmp((s1_), (s2_), (n_)) == 0 )
#define memmatch(s1_,s2_,n_)  ( memcmp((s1_), (s2_), (n_)) == 0 )

#if !defined(BNB_UTILS_NO_INLINE)
static inline ptrdiff_t min(ptrdiff_t a, ptrdiff_t b) { return ((a < b) ? a : b); }
static inline ptrdiff_t max(ptrdiff_t a, ptrdiff_t b) { return ((a > b) ? a : b); }
static inline ptrdiff_t clamp(ptrdiff_t n, ptrdiff_t a, ptrdiff_t b) { return min(max(n, a), b); }
static inline bool      between(ptrdiff_t n, ptrdiff_t a, ptrdiff_t b) { return (n >= a && n <= b); }
static inline bool      xbetween(ptrdiff_t n, ptrdiff_t a, ptrdiff_t b) { return (n > a && n < b); }
static inline ptrdiff_t wrap(ptrdiff_t a, ptrdiff_t b) { return ((a + 1 < b) ? a + 1 : 0); }
static inline ptrdiff_t wrapinv(ptrdiff_t a, ptrdiff_t b) { return ((a > 0) ? a - 1 : b - 1); }
#endif

#define msg__begin(pre_,label_,suf_) do { \
	fprintf(stderr,"%s%s(%s/%s():%04d)%s", pre_, label_, __FILE__, __func__, __LINE__, suf_); \
} while (0)

#define msg_error(str_) do { \
	msg__begin("", "Error", " "); fprintf(stderr, "%s\n", str_); \
} while (0)

#define msg_log(label_,...) do { \
	if ((label_)[0]) {                         \
		msg__begin("\0", label_, " --- "); \
	}                                          \
	fprintf(stderr, __VA_ARGS__);              \
} while (0)

#define error_fatal(msg_,ret_) { msg_error(msg_); exit((ret_)); }

#if defined(DBGOPT_ASSERT_NO_SIGNAL)
 #define DBG_BREAK abort()
#else
 #include <signal.h>
 #define DBG_BREAK raise(SIGTRAP)
#endif

#define ASSERT(cond_) \
if (!(cond_)) { msg_error("assertion failed: " #cond_ ", SIGTRAP raised"); DBG_BREAK; }

#endif
