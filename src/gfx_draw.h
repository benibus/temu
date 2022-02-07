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

#ifndef GFX_DRAW_H__
#define GFX_DRAW_H__

#include "common.h"
#include "cells.h"
#include "fonts.h"

void gfx_clear_rgb1u(uint32 rgb);
void gfx_clear_rgb3u(uint8 r, uint8 g, uint8 b);
void gfx_clear_rgb3f(float r, float g, float b);
void gfx_draw_frame(const Frame *, FontSet *);

#endif

