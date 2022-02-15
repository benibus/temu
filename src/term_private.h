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

#ifndef TERM_PRIVATE_H__
#define TERM_PRIVATE_H__

#include "common.h"
#include "term.h"
#include "term_ring.h"
#include "cells.h"

static_assert(FontStyleRegular == ATTR_NONE, "Bitmask mismatch.");
static_assert(FontStyleBold == ATTR_BOLD, "Bitmask mismatch.");
static_assert(FontStyleItalic == ATTR_ITALIC, "Bitmask mismatch.");
static_assert(FontStyleBoldItalic == (ATTR_BOLD|ATTR_ITALIC), "Bitmask mismatch.");

typedef struct TermCallbacks_ TermCallbacks;
typedef struct TermColors_ TermColors;
typedef struct TermParser_ TermParser;

typedef struct { void *param; TermFuncSetTitle *func; } TermCallbackSetTitle;
typedef struct { void *param; TermFuncSetIcon  *func; } TermCallbackSetIcon;
typedef struct { void *param; TermFuncSetProp  *func; } TermCallbackSetProp;

struct TermCallbacks_ {
    TermCallbackSetTitle settitle;
    TermCallbackSetIcon  seticon;
    TermCallbackSetProp  setprop;
};

struct TermColors_ {
    union {
        uint32 base16[16];
        uint32 base256[256];
    };
    uint32 bg;
    uint32 fg;
};

struct TermParser_ {
    uint state;      // Current FSM state
    uint32 ucs4;     // Current codepoint being decoded
    uchar *data;     // Dynamic buffer for OSC/DCS/APC string sequences
    uchar tokens[2]; // Stashed intermediate tokens
    int depth;       // Intermediate token index
    int argv[16];    // Numeric parameters
    int argi;        // Numeric parameter index
    bool overflow;   // Numeric parameter overflowed
    uchar chars[4+1];
};

#define IOBUF_MAX (4096)

struct Term_ {
    int pid; // PTY PID
    int mfd; // PTY master file descriptor
    int sfd; // PTY slave file descriptor
    uchar input[IOBUF_MAX]; // PTY input buffer

    FontSet *fonts;

    Ring *rings[2];  // Primary/alternate screen buffers
    Ring *ring;      // Current screen buffer

    uint8 *tabstops; // Current tabstop columns
    int tabcols;     // Columns per horizontal tab

    int cols;      // Current screen columns
    int rows;      // Current screen rows
    int max_cols;  // Maximum prior screen columns
    int max_rows;  // Maximum prior screen rows
    int colpx;     // Horizontal cell size in pixels
    int rowpx;     // Vertical cell size in pixels
    int histlines; // Maximum lines in scrollback history

    int x; // Current cursor column
    int y; // Current cursor row

    bool wrapnext;   // Wrap cursor before next write
    bool hidecursor; // Cursor is manually hidden
    bool altscreen;  // Alternate screen is active

    Frame frame;
    Cell cell;
    CursorStyle crs_style;
    uint32 crs_color;
    CursorDesc saved_crs;

    TermColors colors;

    TermParser parser;

    TermCallbacks callbacks;
};

#endif

