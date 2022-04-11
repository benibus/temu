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
#include "opcodes.h"
#include "fsm.h"
#include "term_parser.h"

static void reset_string(Parser *parser);
static void reset_sequence(Parser *parser);
static void reset_args(Parser *parser);
static void reset(Parser *parser);
static size_t *arg_curr(Parser *parser);
static size_t *arg_next(Parser *parser);
static size_t *arg_accum(Parser *parser, uint8 digit);
static uint32 do_action(Parser *parser, uint16 pair, uchar c);

static struct {
    FSM fsm;
} globals;

bool
parser_init(Parser *parser)
{
    ASSERT(parser);

    fsm_generate(&globals.fsm);
#if 0
    fsm_print(&globals.fsm);
#endif
    arr_reserve(parser->data, 4);

    return true;
}

void
parser_fini(Parser *parser)
{
    ASSERT(parser);

    arr_free(parser->data);
}

uint32
parser_emit(Parser *parser, const uchar *data, size_t max, size_t *adv)
{
    uint32 opcode = 0;
    size_t idx;

    for (idx = 0; !opcode && idx < max; idx++) {
        const uchar c = data[idx];
        const uint16 pair = globals.fsm.table[c][parser->state];

        opcode = do_action(parser, pair, c);
        parser->state = GET_STATE(pair);
    }

    SETPTR(adv, idx);

    return opcode;
}

void
reset_string(Parser *parser)
{
    arr_clear(parser->data);
}

void
reset_sequence(Parser *parser)
{
    memset(parser->chars, 0, sizeof(parser->chars));
}

void
reset_args(Parser *parser)
{
    memset(parser->args, 0, sizeof(parser->args[0]) * parser->nargs);
    parser->nargs = parser->nargs_ = 0;
}

void
reset(Parser *parser)
{
    reset_string(parser);
    reset_sequence(parser);
    reset_args(parser);
}

// If the arg limit hasn't been exceeded, returns a pointer to the current arg, otherwise
// returns NULL. If the arg count is 0, an arg is added automatically
size_t *
arg_curr(Parser *parser)
{
    size_t *arg = &parser->args[parser->nargs-1];

    if (parser->nargs_ <= MAX_ARGS) {
        return (parser->nargs > 0) ? arg : arg_next(parser);
    }

    return NULL;
}

// If the arg limit hasn't been reached, returns a pointer to a new zero-initialized arg,
// otherwise returns NULL. If the arg count is 0, two args are added and the second one
// is returned
size_t *
arg_next(Parser *parser)
{
    size_t *arg = NULL;

    parser->nargs_ += (parser->nargs_ < SIZE_MAX);

    if (parser->nargs_ <= MAX_ARGS) {
        parser->nargs = parser->nargs_;
        if ((arg = arg_curr(parser))) {
            *arg = 0;
        }
    }

    return arg;
}

// Add a base-10 digit to the most recent arg if the arg limit hasn't been exceeded.
// Handles integer overflow by clamping to SIZE_MAX
size_t *
arg_accum(Parser *parser, uint8 digit)
{
    ASSERT(digit < 10);
    size_t *arg = arg_curr(parser);

    if (arg && digit < 10) {
        if (*arg <= SIZE_MAX / 10) {
            *arg *= 10;
            *arg += MIN(digit, SIZE_MAX - *arg);
        } else {
            *arg = SIZE_MAX;
        }
    }

    return arg;
}

uint32
do_action(Parser *parser, uint16 pair, uchar c)
{
    uint32 opcode = 0;

    switch (GET_ACTION(pair)) {
    case ActionNone:
    case ActionIgnore:
        break;
    case ActionPrint:
        opcode = opcode_encode(OPTAG_WRITE, c);
        break;
    case ActionPrintWide: {
        // UTF-8 bytes get placed as they would appear in memory, but the sequence doesn't
        // necessarily start at index 0. This means that the sequence can be re-parsed
        // after seeking past any null bytes, which allows for deferred error reporting
        // if the state machine indicates a malformed sequence.
#define TO_UCS4(c)               \
    (                            \
        (((c)[0] & 0x07) << 18)| \
        (((c)[1] & 0x0f) << 12)| \
        (((c)[2] & 0x1f) <<  6)| \
        (((c)[3] & 0x7f) <<  0)  \
    )
        parser->chars[3] = c;
        const uint32 ucs4 = TO_UCS4(parser->chars);
        opcode = opcode_encode(OPTAG_WRITE, ucs4);
        reset_sequence(parser);
        break;
#undef TO_UCS4
    }
    case ActionUtf8GetB2:
        parser->chars[2] = c;
        break;
    case ActionUtf8GetB3:
        parser->chars[1] = c;
        break;
    case ActionUtf8GetB4:
        parser->chars[0] = c;
        break;
    case ActionUtf8Error:
        err_printf("Discarding malformed UTF-8 sequence\n");
        // memset(parser->chars, 0, sizeof(parser->chars));
        reset_sequence(parser);
        break;
    case ActionExec:
        opcode = opcode_encode(OPTAG_WRITE, c);
        break;
    case ActionHook:
        // sets handler based on DCS parameters, intermediates, and new char
        break;
    case ActionUnhook:
        break;
    case ActionPut:
        arr_push(parser->data, c);
        break;
    case ActionOscStart:
        reset_args(parser);
        reset_string(parser);
        break;
    case ActionOscPut:
        arr_push(parser->data, c);
        break;
    case ActionOscEnd:
        arr_push(parser->data, 0);
        opcode = opcode_encode_3c(OPTAG_OSC, parser->chars[2],
                                             parser->chars[1],
                                             parser->chars[0]);
        reset_sequence(parser);
        break;
    case ActionGetPrivMarker:
        ASSERT(c >= OcMinC32);
        ASSERT(c <= OcMaxC32);
        parser->chars[2] = c;
        break;
    case ActionGetIntermediate:
        ASSERT((GET_STATE(pair) == StateEsc2)
               ? (c >= OcMinC21 && c <= OcMaxC21)
               : (c >= OcMinC31 && c <= OcMaxC31));
        parser->chars[1] = c;
        break;
    case ActionParam:
        if (c == ';') {
            arg_next(parser);
        } else {
            arg_accum(parser, c - '0');
        }
        break;
    case ActionClear:
        reset(parser);
        break;
    case ActionEscDispatch: {
        parser->chars[0] = c;
        opcode = opcode_encode_2c(OPTAG_ESC, parser->chars[1],
                                             parser->chars[0]);
        reset_sequence(parser);
        break;
    }
    case ActionCsiDispatch: {
        parser->chars[0] = c;
        opcode = opcode_encode_3c(OPTAG_CSI, parser->chars[2],
                                             parser->chars[1],
                                             parser->chars[0]);
        reset_sequence(parser);
        break;
    }
    default:
        break;
    }

    return opcode;
}

