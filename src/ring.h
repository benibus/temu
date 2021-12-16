#ifndef RING_H__
#define RING_H__

#include "common.h"
#include "cells.h"

typedef struct Ring Ring;

Ring *ring_create(int max_rows, int cols, int rows);
void ring_destroy(Ring *);
int ring_histlines(const Ring *ring);
int ring_get_scroll(const Ring *ring);
int ring_adjust_scroll(Ring *ring, int delta);
int ring_reset_scroll(Ring *ring);
void ring_adjust_head(Ring *ring, int delta);
void ring_copy_framebuffer(const Ring *ring, Cell *frame);
void ring_set_dimensions(Ring *ring, int cols, int rows);
Cell *cells_get(const Ring *ring, int col, int row);
Cell *cells_get_visible(const Ring *ring, int col, int row);
void cells_set(Ring *ring, Cell cell, int col, int row, int count);
void cells_clear(Ring *ring, int col, int row, int count);
void cells_delete(Ring *ring, int col, int row, int count);
void cells_insert(Ring *ring, Cell cell, int col, int row, int count);
void row_set_wrap(Ring *ring, int row, bool enable);
void rows_clear(Ring *ring, int row, int count);
void rows_delete(Ring *ring, int row, int count);
void rows_move(Ring *ring, int row, int count, int shift);
bool check_visible(const Ring *ring, int col, int row);
void dbg_print_ring(const Ring *ring);

#endif

