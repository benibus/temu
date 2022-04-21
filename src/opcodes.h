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

#ifndef OPCODES_H__
#define OPCODES_H__

#include "common.h"

typedef struct Sequence Sequence;

// Sequence type ID
typedef enum {
    SEQ_DEFAULT,
    SEQ_OSC,
    SEQ_ESC,
    SEQ_CSI,
    SEQ_DCS,
    SEQ_APC,
} SeqType;

// Generalized representation of a 3/4-byte parsed sequence.
// Interpreted as a UTF-8 string or an escape sequence depending on type
struct Sequence {
    SeqType type : 8;
    union {
        uchar data[9];
        struct {
            uchar chars[4]; // Used data
            uchar pad__[5]; // Padding to ensure >2x buffer length
        };
    };
};

// Pack a well-formed escape sequence as a 32-bit integer
//
// From least to most significant byte...
//   1: Mandatory finalizer byte
//   2: Optional intermediate byte (occurs before finalizer)
//   3: Optional private marker byte (occurs after initializer)
//   4: Mandatory sequence type (ESC, CSI, OSC, etc.)
//
// Technically, ECMA permits any number of intermediates (which isn't supported by the
// encoding above). However, since no terminal vendor has ever defined a function with
// more than one, such sequences get filtered out by the parser beforehand.
//
#define ESCSEQ(type,c2,c1,c0) PACK_4x8(type, c2, c1, c0)

// Expansion table for encoded escape sequences
#define XTABLE_ESCSEQS \
    X_(  OSC,       OSC,   0,    0,    0) \
    X_(  ESC,       IND,   0,    0,  'D') \
    X_(  ESC,       NEL,   0,    0,  'E') \
    X_(  ESC,       HTS,   0,    0,  'H') \
    X_(  ESC,        RI,   0,    0,  'M') \
    X_(  ESC,       SS2,   0,    0,  'N') \
    X_(  ESC,       SS3,   0,    0,  'O') \
    X_(  ESC,       SPA,   0,    0,  'V') \
    X_(  ESC,       EPA,   0,    0,  'W') \
    X_(  ESC,     DECID,   0,    0,  'Z') \
    X_(  ESC,     S7CIT,   0,  ' ',  'F') \
    X_(  ESC,     S8CIT,   0,  ' ',  'G') \
    X_(  ESC,     ANSI1,   0,  ' ',  'L') \
    X_(  ESC,     ANSI2,   0,  ' ',  'M') \
    X_(  ESC,     ANSI3,   0,  ' ',  'N') \
    X_(  ESC,   DECDHLT,   0,  '#',  '3') \
    X_(  ESC,   DECDHLB,   0,  '#',  '4') \
    X_(  ESC,    DECSWL,   0,  '#',  '5') \
    X_(  ESC,    DECDWL,   0,  '#',  '6') \
    X_(  ESC,    DECALN,   0,  '#',  '8') \
    X_(  ESC,     CSDFL,   0,  '%',  '@') \
    X_(  ESC,    CSUTF8,   0,  '%',  'G') \
    X_(  ESC,       G0A,   0,  '(',  'C') \
    X_(  ESC,       G1A,   0,  ')',  'C') \
    X_(  ESC,       G2A,   0,  '*',  'C') \
    X_(  ESC,       G3A,   0,  '+',  'C') \
    X_(  ESC,       G1B,   0,  '-',  'C') \
    X_(  ESC,       G2B,   0,  '.',  'C') \
    X_(  ESC,       G3B,   0,  '/',  'C') \
    X_(  ESC,     DECBI,   0,    0,  '6') \
    X_(  ESC,     DECSC,   0,    0,  '7') \
    X_(  ESC,     DECRC,   0,    0,  '8') \
    X_(  ESC,     DECFI,   0,    0,  '9') \
    X_(  ESC,   DECKPAM,   0,    0,  '=') \
    X_(  ESC,     HPCLL,   0,    0,  'F') \
    X_(  ESC,       RIS,   0,    0,  'c') \
    X_(  ESC,   HPMEMLK,   0,    0,  'l') \
    X_(  ESC,  HPMEMULK,   0,    0,  'm') \
    X_(  ESC,       LS2,   0,    0,  'n') \
    X_(  ESC,       LS3,   0,    0,  'o') \
    X_(  ESC,      LS3R,   0,    0,  '|') \
    X_(  ESC,      LS2R,   0,    0,  '}') \
    X_(  ESC,      LS1R,   0,    0,  '~') \
    X_(  CSI,       ICH,   0,    0,  '@') \
    X_(  CSI,       CUU,   0,    0,  'A') \
    X_(  CSI,       CUD,   0,    0,  'B') \
    X_(  CSI,       CUF,   0,    0,  'C') \
    X_(  CSI,       CUB,   0,    0,  'D') \
    X_(  CSI,       CNL,   0,    0,  'E') \
    X_(  CSI,       CPL,   0,    0,  'F') \
    X_(  CSI,       CHA,   0,    0,  'G') \
    X_(  CSI,       CUP,   0,    0,  'H') \
    X_(  CSI,       CHT,   0,    0,  'I') \
    X_(  CSI,        ED,   0,    0,  'J') \
    X_(  CSI,        EL,   0,    0,  'K') \
    X_(  CSI,        IL,   0,    0,  'L') \
    X_(  CSI,        DL,   0,    0,  'M') \
    X_(  CSI,       DCH,   0,    0,  'P') \
    X_(  CSI,        SU,   0,    0,  'S') \
    X_(  CSI,        SD,   0,    0,  'T') \
    X_(  CSI,       ECH,   0,    0,  'X') \
    X_(  CSI,       CBT,   0,    0,  'Z') \
    X_(  CSI,       HPA,   0,    0,  '`') \
    X_(  CSI,       HPR,   0,    0,  'a') \
    X_(  CSI,       REP,   0,    0,  'b') \
    X_(  CSI,       VPA,   0,    0,  'd') \
    X_(  CSI,       VPR,   0,    0,  'e') \
    X_(  CSI,       HVP,   0,    0,  'f') \
    X_(  CSI,       TBC,   0,    0,  'g') \
    X_(  CSI,        SM,   0,    0,  'h') \
    X_(  CSI,        MC,   0,    0,  'i') \
    X_(  CSI,        RM,   0,    0,  'l') \
    X_(  CSI,       SGR,   0,    0,  'm') \
    X_(  CSI,       DSR,   0,    0,  'n') \
    X_(  CSI,   DECSTBM,   0,    0,  'r') \
    X_(  CSI,        DA,   0,    0,  'c') \
    X_(  CSI,   DECSLRM,   0,    0,  's') \
    X_(  CSI,  XTWINOPS,   0,    0,  't') \
    X_(  CSI,  DECSCUSR,   0,  ' ',  'q') \
    X_(  CSI,    DECSTR,   0,  '!',  'p') \
    X_(  CSI,    DECSCL,   0,  '"',  'p') \
    X_(  CSI,   DECCARA,   0,  '$',  't') \
    X_(  CSI,    DECCRA,   0,  '$',  'v') \
    X_(  CSI,    DECFRA,   0,  '$',  'x') \
    X_(  CSI,    DECERA,   0,  '$',  'z') \
    X_(  CSI,     DECIC,   0, '\'',  '}') \
    X_(  CSI,     DECDC,   0, '\'',  '~') \
    X_(  CSI,    DECEFR, '>',    0,  'w') \
    X_(  CSI,    DECELR, '>',    0,  'z') \
    X_(  CSI,    DECSLE, '>',    0,  '{') \
    X_(  CSI,   DECRQLP, '>',    0,  '|') \
    X_(  CSI,    DECSED, '?',    0,  'J') \
    X_(  CSI,    DECSEL, '?',    0,  'K') \
    X_(  CSI,    DECSET, '?',    0,  'h') \
    X_(  CSI,     DECMC, '?',    0,  'i') \
    X_(  CSI,    DECRST, '?',    0,  'l') \
    X_(  CSI,    DECDSR, '?',    0,  'n') \
    X_(  DCS,    DECUDK,   0,    0,  '|') \
    X_(  DCS,   DECRQSS,   0,  '$',  'q') \
    X_(  DCS,   DECRSPS,   0,  '$',  't') \
    X_(  DCS, XTGETXRES,   0,  '+',  'Q') \
    X_(  DCS, XTSETTCAP,   0,  '+',  'p') \
    X_(  DCS, XTGETTCAP,   0,  '+',  'q') \
    X_(  DCS,  DECSIXEL,   0,    0,  'q') \
    X_(  DCS,  DECREGIS,   0,    0,  'p')

// Enumerate encoded escape sequences
#define X_(type,name,...) ESCSEQ_##name = ESCSEQ(SEQ_##type, __VA_ARGS__),
enum { XTABLE_ESCSEQS };
#undef X_

// Enumerate opcodes (sequential, ideal for indexing)
enum {
    OP_NONE,
    OP_UNKNOWN,
    OP_WRITE,
    // Map encoded escape sequences to opcodes
#define X_(type,name,...) OP_##name,
    XTABLE_ESCSEQS
#undef X_
    NUM_OPCODES
};

// Returns human-readable name of opcode (primarily for debug output)
const char *opcode_name(uint32 op);
// Returns the sequence type for a given opcode (primarily for debug output)
uint8 opcode_type(uint32 op);
// Converts a 3-4 character sequence to a sparse 32-bit integer
uint32 sequence_encode(const Sequence *seq);
// Converts a 32-bit integer back to a 3-4 character sequence
void sequence_decode(uint32 code, Sequence *seq);
// Returns an opcode from an unencoded sequence
uint32 sequence_to_opcode(const Sequence *seq);

#if !INCLUDE_ESCSEQS
#undef XTABLE_ESCSEQS
#undef ESCSEQ
#endif

#endif

