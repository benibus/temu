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

#include "utils.h"
#include "term_opcodes.h"

#define HAS_C(x,y,n) (((n) >> (OcShiftC##x##x + (y))) & 1)
#define GET_C(x,y,n) \
    (HAS_C(x, y, n) * ((((n) >> OcShiftC##x##y) & OcBitsC##x##y) + OcMinC##x##y))

const char *
opcode_to_string(uint32 opcode)
{
    switch (OPCODE_TAG(opcode)) {
    case OPTAG_WRITE: return "WRITE";
    }

    switch (opcode) {
#define X_(t,x,...) case OP_##x: return #t "::" #x;
    XTABLE_ESC_SEQS
#undef X_
    default: return "NONE";
    }
}

uint
opcode_to_index(uint32 opcode)
{
    switch (OPCODE_TAG(opcode)) {
    case OPTAG_WRITE: return OPIDX_WRITE;
    }

    switch (opcode) {
#define X_(t,x,...) case OP_##x: return OPIDX_##x;
    XTABLE_ESC_SEQS
#undef X_
    default: return OPIDX_NONE;
    }
}

void
opcode_get_chars(uint32 opcode, char *buf)
{
    switch (OPCODE_TAG(opcode)) {
    case OPTAG_CSI:
    case OPTAG_DCS:
        buf[0] = GET_C(3, 0, opcode);
        buf[1] = GET_C(3, 1, opcode);
        buf[2] = GET_C(3, 2, opcode);
        break;
    case OPTAG_ESC:
        buf[0] = GET_C(2, 0, opcode);
        buf[1] = GET_C(2, 1, opcode);
        buf[2] = 0;
        break;
    }
}

