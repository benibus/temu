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
#include "opcodes.h"

static_assert(FontStyleRegular == ATTR_NONE, "Bitmask mismatch.");
static_assert(FontStyleBold == ATTR_BOLD, "Bitmask mismatch.");
static_assert(FontStyleItalic == ATTR_ITALIC, "Bitmask mismatch.");
static_assert(FontStyleBoldItalic == (ATTR_BOLD|ATTR_ITALIC), "Bitmask mismatch.");

typedef struct Parser Parser;

enum { MAX_ARGS = 16 };

struct Parser {
    uint state;            // Current FSM state
    uchar *data;           // Dynamic buffer for OSC/DCS/APC string sequences
    size_t args[MAX_ARGS]; // Integer args
    uint16 nargs;          // Number of integer args, capped at MAX_ARGS
    size_t nargs_;         // Number of integer args, uncapped (internal use only)
    Sequence seq;          // Current UTF-8/escape sequence
};

enum { MAX_READ = 4096 };

struct Term {
    App *app; // Global application handle
    Palette *palette;

    int pid; // PTY PID
    int mfd; // PTY master file descriptor
    int sfd; // PTY slave file descriptor
    uchar input[MAX_READ]; // PTY input buffer

    FontSet *fonts;

    Ring *rings[2];  // Primary/alternate screen buffers
    Ring *ring;      // Current screen buffer

    uint8 *tabstops; // Current tabstop columns
    int tabcols;     // Columns per horizontal tab

    int cols;      // Current screen columns
    int rows;      // Current screen rows
    int max_cols;  // Maximum prior screen columns
    int max_rows;  // Maximum prior screen rows
    int width;
    int height;
    int border;
    int colpx;     // Horizontal cell size in pixels
    int rowpx;     // Vertical cell size in pixels
    int histlines; // Maximum lines in scrollback history

    Cursor cur;
    struct {
        Cursor cur;
    } saved;

    Frame frame;
    Cell cell;

    Parser parser;
    bool tracing;
};

#endif

