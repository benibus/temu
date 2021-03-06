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
#include "term_ring.h"

#define LINE_DEFAULT    (0)
#define LINE_WRAPPED    (1 << 0)
#define LINE_HASTABS    (1 << 1)
#define LINE_HASMULTI   (1 << 2)
#define LINE_HASCOMPLEX (1 << 3)

typedef struct {
    uint16 flags;
    Cell cells[];
} Line;

struct Ring {
    uchar *data;
    int base;
    int head;
    int max;
    int cols;
    int rows;
    int scroll;
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

    int result = uwrap(ring->head - ring->scroll + row, ring->max + 1);

    return result;
}

Ring *
ring_create(int histlines, int cols, int rows)
{
    ASSERT(histlines > 0);
    ASSERT(histlines > 1);

    Ring *ring = xcalloc(1, sizeof(*ring));

    size_t size = LINESIZE(cols) * histlines;

    ring->data = xcalloc(size, 1);
    ring->cols = cols;
    ring->rows = rows;
    ring->base = 1;
    ring->head = 1;
    ring->max = histlines - 1;

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
    if (!ring) return;

    if (cols != ring->cols || rows > ring->max) {
        ring->max = MAX(ring->max, rows);

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

int
ring_get_scroll(const Ring *ring)
{
    return ring->scroll;
}

int
ring_adjust_scroll(Ring *ring, int delta)
{
    const int max_scroll = imax(0, ring_histlines(ring));

    ring->scroll = CLAMP(ring->scroll + delta, 0, max_scroll);

    return ring->scroll;
}

int
ring_reset_scroll(Ring *ring)
{
    ring->scroll = 0;

    return ring->scroll;
}

void
ring_adjust_head(Ring *ring, int delta)
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

void
rows_move(Ring *ring, int row, int count, int shift)
{
    if (!ring || count <= 0 || shift <= 0) {
        return;
    }

    const int beg = CLAMP(row, 0, ring->rows);
    const int end = MIN(beg + (int)count, ring->rows);

    for (int at = end - 1; at >= beg; at--) {
        if (at + (int)shift >= ring->rows) {
            continue;
        }

        const int srcln = get_writeable_index(ring, at);
        const int dstln = get_writeable_index(ring, at + shift);

        memmove(LINE(ring, dstln), LINE(ring, srcln), LINESIZE(ring->cols));
        memset(LINE(ring, srcln), 0, LINESIZE(ring->cols));
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
    bool result = (col < ring->cols && row < ring->rows - ring->scroll);

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

