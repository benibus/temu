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
#include "term_parser.h"
#include "term_opcodes.h"
#include "term_fsm.h"

static uint32 do_action(TermParser *ctx, uint16 pair, uchar c);

static struct {
    FSM fsm;
} globals;

bool
parser_init(TermParser *ctx)
{
    ASSERT(ctx);

    fsm_generate(&globals.fsm);
#if 0
    fsm_print(&globals.fsm);
#endif
    arr_reserve(ctx->data, 4);

    return true;
}

void
parser_fini(TermParser *ctx)
{
    ASSERT(ctx);

    arr_free(ctx->data);
}

uint32
parser_emit(TermParser *ctx, const uchar *data, size_t max, size_t *adv)
{
    uint32 opcode = 0;
    size_t idx;
    uint8 prev_state = ctx->state;

    for (idx = 0; !opcode && idx < max; idx++) {
        const uchar c = data[idx];
        const uint16 pair = globals.fsm.table[c][ctx->state];

        opcode = do_action(ctx, pair, c);
        prev_state = ctx->state;
        ctx->state = GET_STATE(pair);
    }

    SETPTR(adv, idx);

    // TODO(ben): Temporary
#if 0
    dbgprint("len:[%zu] state:[%s->%s] opcode:[%s|0x%08x]",
             idx,
             fsm_state_to_string(prev_state),
             fsm_state_to_string(ctx->state),
             opcode_to_string(opcode),
             opcode);
    for (size_t n = 0; n < idx; n++) {
        fprintf(stderr, "%c%s", n ? ' ' : '\t', charstring(data[n]));
    }
    fprintf(stderr, "\n");
#else
    UNUSED(prev_state);
#endif

    return opcode;
}

static inline void
clear_context(TermParser *ctx)
{
    memset(ctx->chars, 0, sizeof(ctx->chars));
    memset(ctx->argv, 0, sizeof(ctx->argv));
    ctx->argi = 0;
    ctx->overflow = false;
    arr_clear(ctx->data);
}

static inline int
add_digit(TermParser *ctx, int digit)
{
    ASSERT(digit >= 0 && digit < 10);

    int *const argp = &ctx->argv[ctx->argi];

    if (!ctx->overflow) {
        if (INT_MAX / 10 - digit > *argp) {
            *argp = *argp * 10 + digit;
        } else {
            ctx->overflow = true;
            *argp = 0; // TODO(ben): Fallback to default param or abort the sequence?
            dbgprint("warning: parameter integer overflow");
        }
    }

    return *argp;
}

static inline bool
next_param(TermParser *ctx)
{
    if (ctx->argi + 1 >= (int)LEN(ctx->argv)) {
        dbgprint("warning: ignoring excess parameter");
        return false;
    }

    ctx->argv[++ctx->argi] = 0;
    ctx->overflow = false;

    return true;
}

uint32
do_action(TermParser *ctx, uint16 pair, uchar c)
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
        ctx->chars[3] = c;
        const uint32 ucs4 = TO_UCS4(ctx->chars);
        opcode = opcode_encode(OPTAG_WRITE, ucs4);
        memset(ctx->chars, 0, sizeof(ctx->chars));
        break;
#undef TO_UCS4
    }
    case ActionUtf8GetB2:
        ctx->chars[2] = c;
        break;
    case ActionUtf8GetB3:
        ctx->chars[1] = c;
        break;
    case ActionUtf8GetB4:
        ctx->chars[0] = c;
        break;
    case ActionUtf8Error:
        dbgprint("discarding malformed UTF-8 sequence");
        memset(ctx->chars, 0, sizeof(ctx->chars));
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
        arr_push(ctx->data, c);
        break;
    case ActionOscStart:
        memset(ctx->argv, 0, sizeof(ctx->argv));
        ctx->argi = 0;
        arr_clear(ctx->data);
        break;
    case ActionOscPut:
        // All OSC sequences take a leading numeric parameter, which we consume in the arg buffer.
        // Semicolon-separated string parameters are handled by the OSC parser itself
        if (ctx->argi == 0) {
            if (c >= '0' && c <= '9') {
                add_digit(ctx, c - '0');
            } else {
                // NOTE(ben): Assuming OSC sequences take a default '0' parameter
                if (c != ';') {
                    ctx->argv[ctx->argi] = 0;
                }
                next_param(ctx);
            }
        } else {
            arr_push(ctx->data, c);
        }
        break;
    case ActionOscEnd:
        arr_push(ctx->data, 0);
        opcode = opcode_encode_3c(OPTAG_OSC, ctx->chars[2], ctx->chars[1], ctx->chars[0]);
        memset(ctx->chars, 0, sizeof(ctx->chars));
        break;
    case ActionGetPrivMarker:
        ASSERT(c >= OcMinC32);
        ASSERT(c <= OcMaxC32);
        ctx->chars[2] = c;
        break;
    case ActionGetIntermediate:
        ASSERT((GET_STATE(pair) == StateEsc2)
               ? (c >= OcMinC21 && c <= OcMaxC21)
               : (c >= OcMinC31 && c <= OcMaxC31));
        ctx->chars[1] = c;
        break;
    case ActionParam:
        if (c == ';') {
            next_param(ctx);
        } else {
            add_digit(ctx, c - '0');
        }
        break;
    case ActionClear:
        clear_context(ctx);
        break;
    case ActionEscDispatch: {
        ctx->chars[0] = c;
        opcode = opcode_encode_2c(OPTAG_ESC, ctx->chars[1], ctx->chars[0]);
        memset(ctx->chars, 0, sizeof(ctx->chars));
        break;
    }
    case ActionCsiDispatch: {
        ctx->chars[0] = c;
        opcode = opcode_encode_3c(OPTAG_CSI, ctx->chars[2], ctx->chars[1], ctx->chars[0]);
        memset(ctx->chars, 0, sizeof(ctx->chars));
        break;
    }
    default:
        break;
    }

    return opcode;
}

