#include "utils.h"
#include "cells.h"
#include "ring.h"

#define LINE_DEFAULT    (0)
#define LINE_WRAPPED    (1 << 0)
#define LINE_HASTABS    (1 << 1)
#define LINE_HASMULTI   (1 << 2)
#define LINE_HASCOMPLEX (1 << 3)

typedef struct {
    union {
        uint16 flags;
        char pad_[16];
    };
    Cell cells[];
} Line;

struct Ring {
    uchar *data;
    int base;
    int head;
    int max;
    int cols;
    int rows;
    int offset;
};

#define LINESIZE(n) (offsetof(Line, cells) + sizeof(Cell) * (n))
#define LINE(r,n)   ((Line *)((r)->data + ((n) * LINESIZE((r)->cols))))

Ring *ring_rewrap(const Ring *ring, int cols, int rows, const uint8 *tabstops);

static inline int
get_index(const Ring *ring, int row)
{
    int result = uwrap(ring->head + row, ring->max + 1);

    return result;
}

static inline int
get_writeable_index(const Ring *ring, int row)
{
    row = CLAMP(row, 0, ring->rows - 1);

    int result = uwrap(ring->head + row, ring->max + 1);

    return result;
}

static inline int
get_visible_index(const Ring *ring, int row)
{
    row = CLAMP(row, 0, ring->rows - 1);

    int result = uwrap(ring->head - ring->offset + row, ring->max + 1);

    return result;
}

Ring *
ring_create(int histlines, int cols, int rows)
{
    static_assert(LINESIZE(0) == 16, "Bad member alignment");

    Ring *ring = xcalloc(1, sizeof(*ring));

    size_t size = LINESIZE(cols) * (histlines + 1);

    ring->data = xcalloc(size, 1);
    ring->cols = cols;
    ring->rows = rows;
    ring->base = 1;
    ring->head = 1;
    ring->max = histlines;

    return ring;
}

void
ring_destroy(Ring *ring)
{
    ASSERT(ring);
    if (ring->data) {
        free(ring->data);
    }
    free(ring);
}

void
ring_set_dimensions(Ring *ring, int cols, int rows)
{
    if (cols != ring->cols) {
        uchar *data = xcalloc(LINESIZE(cols) * (ring->max + 1), 1);
        const int count = ring_histlines(ring) + ring->rows;

        for (int n = 0; n < count; n++) {
            const int idx = (ring->base + n) % (ring->max + 1);
            Line *src = LINE(ring, idx);
            Line *dst = (Line *)(data + (LINESIZE(cols) * idx));
            memcpy(dst, src, LINESIZE(MIN(cols, ring->cols)));
        }

        free(ring->data);
        ring->data = data;
    }

    ring->cols = cols;
    ring->rows = rows;
}

int
ring_histlines(const Ring *ring)
{
    int count = ring->head - ring->base;

    if (count < 0) {
        count += ring->max + 1;
    }

    return count;
}

void
ring_move_screen_offset(Ring *ring, int delta)
{
    const int max_offset = imax(0, ring_histlines(ring));
    ring->offset = CLAMP(ring->offset + delta, 0, max_offset);
}

void
ring_reset_screen_offset(Ring *ring)
{
    ring->offset = 0;
}

void
ring_move_screen_head(Ring *ring, int delta)
{
    if (delta < 0) {
        while (delta++ < 0) {
            if (ring->head == ring->base) {
                break;
            }
            ring->head -= (ring->head != 0) ? 1 : -ring->max;
        }
    } else {
        while (delta-- > 0) {
            ring->head += (ring->head != ring->max) ? 1 : -ring->max;
            const int botidx = get_index(ring, ring->rows);
            if (botidx == ring->base) {
                ring->base += (ring->base != ring->max) ? 1 : -ring->max;
                memset(LINE(ring, botidx), 0, LINESIZE(ring->cols));
            }
        }
    }
}

void
ring_copy_framebuffer(const Ring *ring, Cell *frame)
{
    Cell *dst = frame;
    int idx = get_visible_index(ring, 0);

    for (int n = 0; n < ring->rows; n++) {
        const Cell *src = LINE(ring, idx)->cells;
        memcpy(dst, src, sizeof(*dst) * ring->cols);
        dst += ring->cols;
        idx += (idx != ring->max) ? 1 : -idx;
    }
}

void
row_set_wrap(Ring *ring, int row, bool enable)
{
    const int idx = get_writeable_index(ring, row);
    Line *line = LINE(ring, idx);

    BSET(line->flags, LINE_WRAPPED, enable);
}

void
rows_clear(Ring *ring, int row, int count)
{
    const int beg = MIN(row, ring->rows);
    const int end = MIN(beg + count, ring->rows);

    for (int at = beg; at < end; at++) {
        const int dstidx = get_writeable_index(ring, at);
        memset(LINE(ring, dstidx), 0, LINESIZE(ring->cols));
    }
}

void
rows_delete(Ring *ring, int row, int count)
{
    const int beg = MIN(row, ring->rows);
    const int end = MIN(beg + count, ring->rows);

    for (int at = beg; at < end; at++) {
        const int dstidx = get_writeable_index(ring, at);
        if (at + (end - beg) < ring->rows) {
            const int srcidx = get_writeable_index(ring, at + (end - beg));
            memmove(LINE(ring, dstidx), LINE(ring, srcidx), LINESIZE(ring->cols));
        } else {
            memset(LINE(ring, dstidx), 0, LINESIZE(ring->cols));
        }
    }
}

Cell *
cells_get(const Ring *ring, int col, int row)
{
    const int idx = get_writeable_index(ring, row);
    Cell *cells = LINE(ring, idx)->cells + col;

    return cells;
}

Cell *
cells_get_visible(const Ring *ring, int col, int row)
{
    const int idx = get_visible_index(ring, row);
    Cell *cells = LINE(ring, idx)->cells + col;

    return cells;
}

void
cells_set(Ring *ring, Cell cell, int col, int row, int count)
{
    const int beg = MIN(col, ring->cols);
    const int end = MIN(beg + count, ring->cols);
    const int idx = get_writeable_index(ring, row);
    Cell *cells = LINE(ring, idx)->cells;

    for (int at = beg; at < end; at++) {
        cells[at] = cell;
    }
}

void
cells_clear(Ring *ring, int col, int row, int count)
{
    const int beg = MIN(col, ring->cols);
    const int end = MIN(beg + count, ring->cols);
    const int idx = get_writeable_index(ring, row);
    Cell *cells = LINE(ring, idx)->cells;

    memset(&cells[beg], 0, (end - beg) * sizeof(*cells));
}

void
cells_delete(Ring *ring, int col, int row, int count)
{
    const int beg = MIN(col, ring->cols);
    const int end = MIN(beg + count, ring->cols);
    const int idx = get_writeable_index(ring, row);
    Cell *cells = LINE(ring, idx)->cells;

    memmove(&cells[beg], &cells[end], (ring->cols - end) * sizeof(Cell));

    cells_clear(ring, end, row, ring->cols);
}

void
cells_insert(Ring *ring, Cell cell, int col, int row, int count)
{
    const int beg = MIN(col, ring->cols);
    const int end = MIN(beg + count, ring->cols);
    const int idx = get_writeable_index(ring, row);
    Cell *cells = LINE(ring, idx)->cells;

    memmove(&cells[end], &cells[beg], (ring->cols - end) * sizeof(Cell));

    cells_set(ring, cell, beg, row, end - beg);
}

bool
check_visible(const Ring *ring, int col, int row)
{
    bool result = (col < ring->cols && row < ring->rows - ring->offset);

    return result;
}

void
dbg_print_ring(const Ring *ring)
{
    const int screen_end = get_index(ring, ring->rows);

    for (int idx = 0; idx < ring->max + 1; idx++) {
        const Line *line = LINE(ring, idx);

        fprintf(
            stderr,
            "[%03d] 0x%.2x (%c:%c:%c) | ",
            idx,
            line->flags,
            (idx == ring->base) ? '@' : ' ',
            (idx == ring->head) ? 'T' : ' ',
            (idx == screen_end) ? 'B' : ' '
        );
        for (int col = 0; col < ring->cols; col++) {
            fprintf(
                stderr,
                "%lc%s",
                DEFAULT(line->cells[col].ucs4, ' '),
                (col + 1 == ring->cols) ? "|\n" : ""
            );
        }
    }
}

