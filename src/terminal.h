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

#ifndef TERM_H__
#define TERM_H__

#include "common.h"
#include "cells.h"
#include "ring.h"
#include "fonts.h"

typedef void TermFuncSetTitle(void *, const char *, size_t);
typedef void TermFuncSetIcon(void *, const char *, size_t);
typedef void TermFuncSetProp(void *, const char *, size_t);

typedef struct { void *param; TermFuncSetTitle *func; } TermCallbackSetTitle;
typedef struct { void *param; TermFuncSetIcon  *func; } TermCallbackSetIcon;
typedef struct { void *param; TermFuncSetProp  *func; } TermCallbackSetProp;

typedef struct {
    TermCallbackSetTitle settitle;
    TermCallbackSetIcon  seticon;
    TermCallbackSetProp  setprop;
} TermCallbacks;

typedef struct {
    union {
        uint32 base16[16];
        uint32 base256[256];
    };
    uint32 bg;
    uint32 fg;
} TermColors;

#define IOBUF_MAX (4096)

typedef struct {
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

    struct Parser {
        uint state;      // Current FSM state
        uint32 ucs4;     // Current codepoint being decoded
        uchar *data;     // Dynamic buffer for OSC/DCS/APC string sequences
        uchar tokens[2]; // Stashed intermediate tokens
        int depth;       // Intermediate token index
        int argv[16];    // Numeric parameters
        int argi;        // Numeric parameter index
        bool overflow;   // Numeric paramater overflowed
    } parser;

    TermCallbacks callbacks;
} Term;

Term *term_create(uint16 histlines, uint8 tabcols);
void term_resize(Term *term, uint width, uint height);

void term_destroy(Term *term);
int term_exec(Term *term, const char *shell, int argc, const char *const *argv);
size_t term_pull(Term *, uint32);
size_t term_push(Term *, const void *, size_t);
size_t term_push_input(Term *term, uint key, uint mod, const uchar *text, size_t len);
size_t term_consume(Term *, const uchar *, size_t);
void term_scroll(Term *, int);
void term_reset_scroll(Term *);
int term_cols(const Term *term);
int term_rows(const Term *term);
size_t term_make_key_string(const Term *, uint, uint, char *, size_t);
Cell *term_get_framebuffer(Term *);
Frame *term_generate_frame(Term *);

void term_set_background_color(Term *term, uint32 color);
void term_set_foreground_color(Term *term, uint32 color);
void term_set_default_colors(Term *term, uint32 bg_color, uint32 fg_color);
void term_set_base16_color(Term *term, uint8 idx, uint32 color);
void term_set_base256_color(Term *term, uint8 idx, uint32 color);
void term_set_display(Term *term, FontSet *fonts, uint width, uint height);

void term_callback_settitle(Term *term, void *param, TermFuncSetTitle *func);
void term_callback_seticon(Term *term, void *param, TermFuncSetIcon *func);
void term_callback_setprop(Term *term, void *param, TermFuncSetProp *func);

void term_print_history(const Term *);
void term_print_stream(const Term *);

#endif

