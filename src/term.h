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
#include "fonts.h"
#include "app.h"

enum {
    MIN_HISTLINES = (1 << 8), MAX_HISTLINES = (1 << 15),
    MIN_COLS      = (1),      MAX_COLS      = INT16_MAX,
    MIN_ROWS      = (1),      MAX_ROWS      = INT16_MAX,
    MIN_TABCOLS   = (1),      MAX_TABCOLS   = (32),
};

typedef struct Term Term;

Term *term_create(App *app);
void term_destroy(Term *term);
void term_resize(Term *term, uint width, uint height);
int term_exec(Term *term, const char *shell, int argc, const char *const *argv);
void term_draw(Term *term);
size_t term_pull(Term *term);
size_t term_push(Term *term, const void *data, size_t len);
size_t term_push_input(Term *term, uint key, uint mod, const uchar *text, size_t len);
void term_scroll(Term *term, int lines);
void term_reset_scroll(Term *term);
int term_cols(const Term *term);
int term_rows(const Term *term);
void term_print_history(const Term *term);
void term_print_stream(const Term *term);
bool term_toggle_trace(Term *term);

#endif

