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

#ifndef RENDERER_H__
#define RENDERER_H__

#include "common.h"
#include "cells.h"
#include "fonts.h"

void gfx_render_clear_1u(uint32 rgb);
void gfx_render_clear_3u(uint8 r, uint8 g, uint8 b);
void gfx_render_clear_3f(float r, float g, float b);
void gfx_render_frame(const Frame *, FontSet *);

#endif

