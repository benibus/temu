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
#define INCLUDE_ESCSEQS 1
#include "opcodes.h"

inline uint8
opcode_type(uint32 op)
{
#define X_(type,name,...) [OP_##name] = SEQ_##type,
    static const uint8 types[NUM_OPCODES] = { XTABLE_ESCSEQS };
#undef X_

    ASSERT(op < NUM_OPCODES);
    return types[op];
}

const char *
opcode_name(uint32 op)
{
    switch (op) {
    case OP_NONE:    return "NONE";
    case OP_UNKNOWN: return "UNKNOWN";
    case OP_WRITE:   return "WRITE";
#define X_(type,name,...) case OP_##name: return #type "::" #name;
    XTABLE_ESCSEQS
#undef X_
    default: return "OTHER";
    }
}

uint32
sequence_encode(const Sequence *seq)
{
    uint32 code = 0;

    switch (seq->type) {
    case SEQ_ESC:
    case SEQ_CSI:
    case SEQ_OSC:
    case SEQ_DCS:
    case SEQ_APC: {
        // Pack as a 3-byte escape sequence with the specific type in the highest byte
        code = ESCSEQ(seq->type, seq->chars[2],
                                 seq->chars[1],
                                 seq->chars[0]);
        break;
    }
    case 0:
        // Pack as a UTF-32 wide char.
        // The source sequence is assumed to be valid UTF-8 with 0-3 leading zero-bytes
        code |= (seq->chars[0] & 0x07) << 18;
        code |= (seq->chars[1] & 0x0f) << 12;
        code |= (seq->chars[2] & 0x1f) <<  6;
        code |= (seq->chars[3] & 0x7f) <<  0;
        break;
    default:
        return 0;
    }

    return code;
}

void
sequence_decode(uint32 code, Sequence *r_seq)
{
    Sequence seq;
    memset(&seq, 0, sizeof(seq));

    seq.type = code >> 24;

    switch (seq.type) {
    case SEQ_ESC:
    case SEQ_CSI:
    case SEQ_OSC:
    case SEQ_DCS:
    case SEQ_APC: {
        // Interpret the lower 24 bits as an encoded escape sequence.
        // Convert back to original representation
        seq.chars[0] = (code >> (0 * 8)) & 0xff;
        seq.chars[1] = (code >> (1 * 8)) & 0xff;
        seq.chars[2] = (code >> (2 * 8)) & 0xff;
        break;
    }
    case 0:
        // Interpret the lower 24 bits as a UTF-32 codepoint.
        // TODO(ben): Conversion back to UTF-8
        break;
    default:
        break;
    }

    SETPTR(r_seq, seq);
}

uint32
sequence_to_opcode(const Sequence *seq)
{
    const uint32 code = sequence_encode(seq);

    // If there's no type information, the lower 3 bytes are assumed to an arbitrary
    // UTF-32 codepoint, which always translates to a WRITE operation
    if (!(code >> 24)) {
        return OP_WRITE;
    }

    // If a type is specified, the sequence code maps to an escape sequence operation
    switch (code) {
#define X_(type,name,...) case ESCSEQ_##name: return OP_##name;
    XTABLE_ESCSEQS
#undef X_
    // If nothing matches, the sequence is valid but unrecognized
    default: return OP_UNKNOWN;
    }
}

