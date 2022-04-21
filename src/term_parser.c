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
static size_t *arg_set(Parser *parser, size_t val);
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
    uint32 op = 0;
    size_t idx;

    for (idx = 0; !op && idx < max; idx++) {
        const uchar c = data[idx];
        const uint16 pair = globals.fsm.table[c][parser->state];

        op = do_action(parser, pair, c);
        parser->state = GET_STATE(pair);
    }

    SETPTR(adv, idx);

    return op;
}

void
reset_string(Parser *parser)
{
    arr_clear(parser->data);
}

void
reset_sequence(Parser *parser)
{
    memset(&parser->seq, 0, sizeof(parser->seq));
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
    size_t *arg = NULL;

    if (parser->nargs_ <= MAX_ARGS) {
        arg = (parser->nargs > 0) ? &parser->args[parser->nargs-1] : arg_next(parser);
    }

    return arg;
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

// Sets arg count to 1 and sets the first arg to the specified value
size_t *
arg_set(Parser *parser, size_t val)
{
    reset_args(parser);
    size_t *arg = &parser->args[0];
    *arg = val;
    parser->nargs = 1;

    return arg;
}

uint32
do_action(Parser *parser, uint16 pair, uchar c)
{
    uint32 op = 0;

    switch (GET_ACTION(pair)) {
    case ACTION_NONE:
    case ACTION_IGNORE:
        break;
    case ACTION_PRINT:
        arg_set(parser, c);
        op = OP_WRITE;
        break;
    case ACTION_PRINTWIDE:
        parser->seq.type = 0;
        parser->seq.chars[3] = c;
        arg_set(parser, sequence_encode(&parser->seq));
        op = OP_WRITE;
        reset_sequence(parser);
        break;
    case ACTION_UTF8GETB2:
        parser->seq.chars[2] = c;
        break;
    case ACTION_UTF8GETB3:
        parser->seq.chars[1] = c;
        break;
    case ACTION_UTF8GETB4:
        parser->seq.chars[0] = c;
        break;
    case ACTION_UTF8ERROR:
        err_printf("Discarding malformed UTF-8 sequence\n");
        // memset(parser->chars, 0, sizeof(parser->chars));
        reset_sequence(parser);
        break;
    case ACTION_HOOK: // sets handler based on DCS parameters, intermediates, and new char
        break;
    case ACTION_UNHOOK:
        break;
    case ACTION_PUT:
        arr_push(parser->data, c);
        break;
    case ACTION_OSCDISPATCH:
        arr_push(parser->data, 0);
        parser->seq.type = SEQ_OSC;
        op = sequence_to_opcode(&parser->seq);
        reset_sequence(parser);
        break;
    case ACTION_GETPRIVMARKER:
        parser->seq.chars[2] = c;
        break;
    case ACTION_GETINTERMEDIATE:
        parser->seq.chars[1] = c;
        break;
    case ACTION_PARAM:
        if (c == ';') {
            arg_next(parser);
        } else {
            arg_accum(parser, c - '0');
        }
        break;
    case ACTION_CLEAR:
        reset(parser);
        break;
    case ACTION_ESCDISPATCH:
        parser->seq.type = SEQ_ESC;
        parser->seq.chars[0] = c;
        op = sequence_to_opcode(&parser->seq);
        reset_sequence(parser);
        break;
    case ACTION_CSIDISPATCH:
        parser->seq.type = SEQ_CSI;
        parser->seq.chars[0] = c;
        op = sequence_to_opcode(&parser->seq);
        reset_sequence(parser);
        break;
    default:
        break;
    }

    return op;
}

