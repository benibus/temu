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

#ifndef TERM_OPCODES_H__
#define TERM_OPCODES_H__

#include "common.h"

/* C2 ----------------------------------
 *        1      0
 * 000000001111111 C20 [7]
 * 000011110000000 C21 [4]
 * 001100000000000 C22 [2] (status bits)
 *
 * C3 ----------------------------------
 *        1      0
 * 000000000111111 C30 [6]
 * 000001111000000 C31 [4]
 * 000110000000000 C32 [2]
 * 111000000000000 C33 [3] (status bits)
 */
enum {
    // Tag information starts at this bit
    OcShiftTag = 24,

    // Range mask shift values
    OcShiftC20 = 0,
    OcShiftC21 = OcShiftC20 + 7,
    OcShiftC22 = OcShiftC21 + 4,
    OcShiftC30 = 0,
    OcShiftC31 = OcShiftC30 + 6,
    OcShiftC32 = OcShiftC31 + 4,
    OcShiftC33 = OcShiftC32 + 2,

    // Range masks (unshifted)
    OcBitsC20  = (1 << (OcShiftC21 - OcShiftC20)) - 1,
    OcBitsC21  = (1 << (OcShiftC22 - OcShiftC21)) - 1,
    OcBitsC30  = (1 << (OcShiftC31 - OcShiftC30)) - 1,
    OcBitsC31  = (1 << (OcShiftC32 - OcShiftC31)) - 1,
    OcBitsC32  = (1 << (OcShiftC33 - OcShiftC32)) - 1,

    // Minimum characters (added to range mask)
    OcMinC20   = '0',
    OcMinC21   = ' ',
    OcMinC30   = '@',
    OcMinC31   = ' ',
    OcMinC32   = '<',

    // Maximum characters
    OcMaxC20   = OcMinC20 + OcBitsC20,
    OcMaxC21   = OcMinC21 + OcBitsC21,
    OcMaxC30   = OcMinC30 + OcBitsC30,
    OcMaxC31   = OcMinC31 + OcBitsC31,
    OcMaxC32   = OcMinC32 + OcBitsC32,
};

#define OPCODE_TAG(oc)    ((oc) >> OcShiftTag)
#define OPCODE_NO_TAG(oc) ((oc) & ((1 << OcShiftTag) - 1))

// Helpers for header definitions
#define PACK_TAG(tag) ((tag) << OcShiftTag)
#define PACK_C(x,y,c)                               \
    (((c) >= OcMinC##x##y && (c) <= OcMaxC##x##y) ? \
    (                                               \
        (1 << (OcShiftC##x##x + (y))) |             \
        (((c) - OcMinC##x##y) << OcShiftC##x##y)    \
    ) : 0)
#define PACK_1(t,n)         \
    (                       \
        OPCODE_NO_TAG(n) |  \
        PACK_TAG(t)         \
    )
#define PACK_2(t,c1,c0)     \
    (                       \
        PACK_C(2, 0, c0) |  \
        PACK_C(2, 1, c1) |  \
        PACK_TAG(t)         \
    )
#define PACK_3(t,c2,c1,c0)  \
    (                       \
        PACK_C(3, 0, c0) |  \
        PACK_C(3, 1, c1) |  \
        PACK_C(3, 2, c2) |  \
        PACK_TAG(t)         \
    )

// Expansion table for opcodes that encode multiple characters
#define XTABLE_ESC_SEQS \
    X_(  ESC,       IND, 2,   0,  'D') \
    X_(  ESC,       NEL, 2,   0,  'E') \
    X_(  ESC,       HTS, 2,   0,  'H') \
    X_(  ESC,        RI, 2,   0,  'M') \
    X_(  ESC,       SS2, 2,   0,  'N') \
    X_(  ESC,       SS3, 2,   0,  'O') \
    X_(  ESC,       SPA, 2,   0,  'V') \
    X_(  ESC,       EPA, 2,   0,  'W') \
    X_(  ESC,     DECID, 2,   0,  'Z') \
    X_(  ESC,     S7CIT, 2, ' ',  'F') \
    X_(  ESC,     S8CIT, 2, ' ',  'G') \
    X_(  ESC,     ANSI1, 2, ' ',  'L') \
    X_(  ESC,     ANSI2, 2, ' ',  'M') \
    X_(  ESC,     ANSI3, 2, ' ',  'N') \
    X_(  ESC,   DECDHLT, 2, '#',  '3') \
    X_(  ESC,   DECDHLB, 2, '#',  '4') \
    X_(  ESC,    DECSWL, 2, '#',  '5') \
    X_(  ESC,    DECDWL, 2, '#',  '6') \
    X_(  ESC,    DECALN, 2, '#',  '8') \
    X_(  ESC,     CSDFL, 2, '%',  '@') \
    X_(  ESC,    CSUTF8, 2, '%',  'G') \
    X_(  ESC,       G0A, 2, '(',  'C') \
    X_(  ESC,       G1A, 2, ')',  'C') \
    X_(  ESC,       G2A, 2, '*',  'C') \
    X_(  ESC,       G3A, 2, '+',  'C') \
    X_(  ESC,       G1B, 2, '-',  'C') \
    X_(  ESC,       G2B, 2, '.',  'C') \
    X_(  ESC,       G3B, 2, '/',  'C') \
    X_(  ESC,     DECBI, 2,   0,  '6') \
    X_(  ESC,     DECSC, 2,   0,  '7') \
    X_(  ESC,     DECRC, 2,   0,  '8') \
    X_(  ESC,     DECFI, 2,   0,  '9') \
    X_(  ESC,   DECKPAM, 2,   0,  '=') \
    X_(  ESC,     HPCLL, 2,   0,  'F') \
    X_(  ESC,       RIS, 2,   0,  'c') \
    X_(  ESC,   HPMEMLK, 2,   0,  'l') \
    X_(  ESC,  HPMEMULK, 2,   0,  'm') \
    X_(  ESC,       LS2, 2,   0,  'n') \
    X_(  ESC,       LS3, 2,   0,  'o') \
    X_(  ESC,      LS3R, 2,   0,  '|') \
    X_(  ESC,      LS2R, 2,   0,  '}') \
    X_(  ESC,      LS1R, 2,   0,  '~') \
    X_(  CSI,       ICH, 3,   0,    0,  '@') \
    X_(  CSI,       CUU, 3,   0,    0,  'A') \
    X_(  CSI,       CUD, 3,   0,    0,  'B') \
    X_(  CSI,       CUF, 3,   0,    0,  'C') \
    X_(  CSI,       CUB, 3,   0,    0,  'D') \
    X_(  CSI,       CNL, 3,   0,    0,  'E') \
    X_(  CSI,       CPL, 3,   0,    0,  'F') \
    X_(  CSI,       CHA, 3,   0,    0,  'G') \
    X_(  CSI,       CUP, 3,   0,    0,  'H') \
    X_(  CSI,       CHT, 3,   0,    0,  'I') \
    X_(  CSI,        ED, 3,   0,    0,  'J') \
    X_(  CSI,        EL, 3,   0,    0,  'K') \
    X_(  CSI,        IL, 3,   0,    0,  'L') \
    X_(  CSI,        DL, 3,   0,    0,  'M') \
    X_(  CSI,       DCH, 3,   0,    0,  'P') \
    X_(  CSI,        SU, 3,   0,    0,  'S') \
    X_(  CSI,        SD, 3,   0,    0,  'T') \
    X_(  CSI,       ECH, 3,   0,    0,  'X') \
    X_(  CSI,       CBT, 3,   0,    0,  'Z') \
    X_(  CSI,       HPA, 3,   0,    0,  '`') \
    X_(  CSI,       HPR, 3,   0,    0,  'a') \
    X_(  CSI,       REP, 3,   0,    0,  'b') \
    X_(  CSI,       VPA, 3,   0,    0,  'd') \
    X_(  CSI,       VPR, 3,   0,    0,  'e') \
    X_(  CSI,       HVP, 3,   0,    0,  'f') \
    X_(  CSI,       TBC, 3,   0,    0,  'g') \
    X_(  CSI,        SM, 3,   0,    0,  'h') \
    X_(  CSI,        MC, 3,   0,    0,  'i') \
    X_(  CSI,        RM, 3,   0,    0,  'l') \
    X_(  CSI,       SGR, 3,   0,    0,  'm') \
    X_(  CSI,       DSR, 3,   0,    0,  'n') \
    X_(  CSI,   DECSTBM, 3,   0,    0,  'r') \
    X_(  CSI,        DA, 3,   0,    0,  'c') \
    X_(  CSI,   DECSLRM, 3,   0,    0,  's') \
    X_(  CSI,  XTWINOPS, 3,   0,    0,  't') \
    X_(  CSI,  DECSCUSR, 3,   0,  ' ',  'q') \
    X_(  CSI,    DECSTR, 3,   0,  '!',  'p') \
    X_(  CSI,    DECSCL, 3,   0,  '"',  'p') \
    X_(  CSI,   DECCARA, 3,   0,  '$',  't') \
    X_(  CSI,    DECCRA, 3,   0,  '$',  'v') \
    X_(  CSI,    DECFRA, 3,   0,  '$',  'x') \
    X_(  CSI,    DECERA, 3,   0,  '$',  'z') \
    X_(  CSI,     DECIC, 3,   0, '\'',  '}') \
    X_(  CSI,     DECDC, 3,   0, '\'',  '~') \
    X_(  CSI,    DECEFR, 3, '>',    0,  'w') \
    X_(  CSI,    DECELR, 3, '>',    0,  'z') \
    X_(  CSI,    DECSLE, 3, '>',    0,  '{') \
    X_(  CSI,   DECRQLP, 3, '>',    0,  '|') \
    X_(  CSI,    DECSED, 3, '?',    0,  'J') \
    X_(  CSI,    DECSEL, 3, '?',    0,  'K') \
    X_(  CSI,    DECSET, 3, '?',    0,  'h') \
    X_(  CSI,     DECMC, 3, '?',    0,  'i') \
    X_(  CSI,    DECRST, 3, '?',    0,  'l') \
    X_(  CSI,    DECDSR, 3, '?',    0,  'n') \
    X_(  OSC,       OSC, 1,   0) \
    X_(  DCS,    DECUDK, 3,   0,    0,  '|') \
    X_(  DCS,   DECRQSS, 3,   0,  '$',  'q') \
    X_(  DCS,   DECRSPS, 3,   0,  '$',  't') \
    X_(  DCS, XTGETXRES, 3,   0,  '+',  'Q') \
    X_(  DCS, XTSETTCAP, 3,   0,  '+',  'p') \
    X_(  DCS, XTGETTCAP, 3,   0,  '+',  'q') \
    X_(  DCS,  DECSIXEL, 3,   0,    0,  'q') \
    X_(  DCS,  DECREGIS, 3,   0,    0,  'p') \

// Opcode tags stored in the upper 8 bits. Used to interpret the lower 24 bits
enum {
    OPTAG_NONE,
    OPTAG_WRITE,
    OPTAG_ESC,
    OPTAG_CSI,
    OPTAG_OSC,
    OPTAG_DCS,
    OPTAG_APC
};

// Enumerate the opcodes for escape sequences. Opcodes with the WRITE tag don't have
// associated symbols because they store arbitrary codepoints in their lower 24 bits
enum {
#define X_(t,x,n,...) OP_##x = PACK_##n(OPTAG_##t, __VA_ARGS__),
    XTABLE_ESC_SEQS
#undef X_
};

// Map opcodes to parallel indices for table lookups
enum {
    OPIDX_NONE,
    OPIDX_WRITE,
#define X_(t,x,...) OPIDX_##x,
    XTABLE_ESC_SEQS
#undef X_
    NumOpcodes
};

const char *opcode_to_string(uint32 opcode);
uint opcode_to_index(uint32 opcode);
void opcode_get_chars(uint32 opcode, char *buf);

static inline uint32
opcode_encode(uint8 tag, uint32 val)
{
    return PACK_1(tag, val);
}

static inline uint32
opcode_encode_2c(uint8 tag, uchar c0, uchar c1)
{
    return PACK_2(tag, c0, c1);
}

static inline uint32
opcode_encode_3c(uint8 tag, uchar c0, uchar c1, uchar c3)
{
    return PACK_3(tag, c0, c1, c3);
}

#undef PACK_TAG
#undef PACK_C
#undef PACK_1
#undef PACK_2
#undef PACK_3

#endif

