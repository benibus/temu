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

#ifndef CELLS_H__
#define CELLS_H__

#include "common.h"

#define ATTR_NONE      (0)
#define ATTR_BOLD      (1 << 0)
#define ATTR_ITALIC    (1 << 1)
#define ATTR_UNDERLINE (1 << 2)
#define ATTR_BLINK     (1 << 3)
#define ATTR_INVERT    (1 << 4)
#define ATTR_INVISIBLE (1 << 5)
#define ATTR_MAX       (1 << 6)
#define ATTR_MASK      (ATTR_MAX-1)

typedef enum {
    CellTypeBlank,
    CellTypeNormal,
    CellTypeComplex,
    CellTypeTab,
    CellTypeDummyTab,
    CellTypeDummyWide,
    CellTypeCount
} CellType;

typedef struct {
    uint32 ucs4;
    uint32 bg;
    uint32 fg;
    CellType type:8;
    uint8 width;
    uint16 attrs;
} Cell;

typedef enum {
    CursorStyleDefault,
    CursorStyleBlock       = 2,
    CursorStyleUnderscore  = 4,
    CursorStyleBar         = 5,
    CursorStyleOutline     = 7
} CursorStyle;

typedef struct {
    int col, row;
    CursorStyle style;
    uint32 color;
    bool visible;
} CursorDesc;

typedef struct {
    Cell *cells;
    int cols, rows;
    int width, height;
    CursorDesc cursor;
    uint32 time;
    uint32 default_bg;
    uint32 default_fg;
} Frame;

#endif

