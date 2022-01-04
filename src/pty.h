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

#ifndef PTY_H__
#define PTY_H__

#include "common.h"
#include "terminal.h"

int pty_init(const char *, int *, int *);
void pty_hangup(int pid);
size_t pty_read(int, byte *, size_t, uint32);
size_t pty_write(int, const byte *, size_t);
void pty_resize(int, int, int, int, int);

#endif

