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

typedef struct Term_ Term;

typedef void TermFuncSetTitle(void *, const char *, size_t);
typedef void TermFuncSetIcon(void *, const char *, size_t);
typedef void TermFuncSetProp(void *, const char *, size_t);

Term *term_create(uint16 histlines, uint8 tabcols);
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
void term_set_default_colors(Term *term, uint32 bg, uint32 fg);
void term_set_background_color(Term *term, uint32 color);
void term_set_foreground_color(Term *term, uint32 color);
void term_set_base16_color(Term *term, uint8 idx, uint32 color);
void term_set_base256_color(Term *term, uint8 idx, uint32 color);
void term_set_display(Term *term, FontSet *fonts, uint width, uint height);
void term_callback_settitle(Term *term, void *param, TermFuncSetTitle *func);
void term_callback_seticon(Term *term, void *param, TermFuncSetIcon *func);
void term_callback_setprop(Term *term, void *param, TermFuncSetProp *func);
void term_print_history(const Term *);
void term_print_stream(const Term *);

#endif
