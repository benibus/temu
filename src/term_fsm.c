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
#include "term_fsm.h"

typedef struct {
    uchar beg;
    uchar end;
    int8 state;
    int8 action;
} TableRange;

typedef struct {
    TableRange ranges[16];
} TableDesc;

static void build_table(FSM *fsm, const TableDesc *descs);
static const TableRange *find_range(uchar c, TableDesc desc);

void
fsm_generate(FSM *fsm)
{
    /* Character ranges for filling the state machine's transition table.
     * Each state's range list is searched linearly for each byte. The first matching
     * range is selected and its state/action pair is written to the table.
     * A negative state value is used as a self-reference (i.e. no change) and
     * expands to the appropriate state in the actual table
     */
    static const TableDesc descs[NumStates] = {
        [StateGround] = {
            .ranges = {
                { 0xf0, 0xf7, StateUtf8B3, ActionUtf8GetB4 },
                { 0xe0, 0xef, StateUtf8B2, ActionUtf8GetB3 },
                { 0xc0, 0xdf, StateUtf8B1, ActionUtf8GetB2 },
                { 0x20, 0x7f, -1, ActionPrint },
                { 0x00, 0x1f, -1, ActionExec },
                { 0x00, 0xff, -1, ActionUtf8Error },
            }
        },

        [StateUtf8B1] = {
            .ranges = {
                { 0x80, 0xff, StateGround, ActionPrintWide },
                { 0x00, 0x3f, StateGround, ActionPrintWide },
                { 0x00, 0xff, StateGround, ActionUtf8Error },
            }
        },

        [StateUtf8B2] = {
            .ranges = {
                { 0x80, 0xff, StateUtf8B1, ActionUtf8GetB2 },
                { 0x00, 0x3f, StateUtf8B1, ActionUtf8GetB2 },
                { 0x00, 0xff, StateGround, ActionUtf8Error },
            }
        },

        [StateUtf8B3] = {
            .ranges = {
                { 0x80, 0xff, StateUtf8B2, ActionUtf8GetB3 },
                { 0x00, 0x3f, StateUtf8B2, ActionUtf8GetB3 },
                { 0x00, 0xff, StateGround, ActionUtf8Error },
            }
        },

        [StateEsc1] = {
            .ranges = {
                { ']',  ']',  StateOsc, 0 },
                { '[',  '[',  StateCsi1, 0 },
                { '0',  0x7e, StateGround, ActionEscDispatch },
                { ' ',  '/',  StateEsc2, ActionGetIntermediate },
                { 0x00, 0x1f, -1, ActionExec },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [StateEsc2] = {
            .ranges = {
                { '0',  0x7e, StateGround, ActionEscDispatch },
                { ' ',  '/',  StateGround, 0 },
                { 0x00, 0x1f, -1, ActionExec },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [StateCsi1] = {
            .ranges = {
                { '@',  0x7e, StateGround, ActionCsiDispatch },
                { '<',  '?',  StateCsiParam, ActionGetPrivMarker },
                { ':',  ':',  StateCsiIgnore, 0 },
                { '0',  ';',  StateCsiParam, ActionParam },
                { ' ',  '/',  StateCsi2, ActionGetIntermediate },
                { 0x00, 0x1f, -1, ActionExec },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [StateCsi2] = {
            .ranges = {
                { '@',  0x7e,  StateGround, ActionCsiDispatch },
                { ' ',  '?',   StateCsiIgnore, 0 },
                { 0x00, 0x1f,  -1, ActionExec },
                { 0x00, 0xff,  -1, 0 },
            }
        },

        [StateCsiIgnore] = {
            .ranges = {
                { '@',  0x7e, StateGround, 0 },
                { 0x00, 0x1f, -1, ActionExec },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [StateCsiParam] = {
            .ranges = {
                { '@',  0x7e, StateGround, ActionCsiDispatch },
                { '<',  '?',  StateCsiIgnore, 0 },
                { ':',  ':',  StateCsiIgnore, 0 },
                { '0',  ';',  -1, ActionParam },
                { ' ',  '/',  StateCsi2, ActionGetIntermediate },
                { 0x00, 0x1f, -1, ActionExec },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [StateDcs1] = {
            .ranges = {
                { '@',  0x7e, StateDcsPass, 0 },
                { '<',  '?',  StateDcsParam, ActionGetPrivMarker },
                { ':',  ':',  StateDcsIgnore, 0 },
                { '0',  ';',  StateDcsParam, ActionParam },
                { ' ',  '/',  StateDcs2, ActionGetIntermediate },
                { 0x00, 0x1f, -1, ActionExec },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [StateDcs2] = {
            .ranges = {
                { '@',  0x7e, StateDcsPass, 0 },
                { ' ',  '?',  StateDcsIgnore, 0 },
                { 0x00, 0x1f, -1, ActionExec },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [StateDcsIgnore] = {
            .ranges = {
                { 0x9c, 0x9c, StateGround, 0 },
                { 0x00, 0x1f, -1, ActionExec },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [StateDcsParam] = {
            .ranges = {
                { '@',  0x7e, StateDcsPass, 0 },
                { '<',  '?',  StateDcsIgnore, 0 },
                { ':',  ':',  StateDcsIgnore, 0 },
                { '0',  ';',  -1, ActionParam },
                { ' ',  '/',  StateDcs2, ActionGetIntermediate },
                { 0x00, 0x1f, -1, ActionExec },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [StateDcsPass] = {
            .ranges = {
                { 0x9c, 0x9c, StateGround, 0 },
                { 0x00, 0x7e, -1, ActionPut },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [StateOsc] = {
            .ranges = {
                { 0x07, 0x07, StateGround, ActionOscEnd },
                { 0x00, 0x1f, -1, 0 },
                { 0x00, 0xff, -1, ActionOscPut },
            }
        },

        [StateSosPmApc] = {
            .ranges = {
                { 0x9c, 0x9c, StateGround, 0 },
                { 0x00, 0xff, -1, 0 },
            }
        },
    };

    build_table(fsm, descs);
}

void
build_table(FSM *fsm, const TableDesc *descs)
{
    // First pass over each state/character
    for (int c = 255; c >= 0; c--) {
        for (int s = 0; s < NumStates; s++) {
            uint8 state  = s;
            uint8 action = 0;
            const TableRange *range = find_range(c, descs[s]);

            if (range) {
                state  = (range->state >= 0) ? range->state : state;
                action = range->action;
            }

            fsm->table[c][s] = PAIR(state, action);
        }
    }

    // Second pass for control characters that are [mostly] state-independent
    for (int s = 0; s < NumStates; s++) {
        switch (s) {
        // 0x00-0x3f are valid UTF-8 continuation bytes, so we exclude these
        case StateUtf8B3:
        case StateUtf8B2:
        case StateUtf8B1:
            break;
        default:
            fsm->table[0x1b][s] = PAIR(StateEsc1, ActionClear);
            fsm->table[0x1a][s] = PAIR(StateGround, ActionExec);
            fsm->table[0x18][s] = PAIR(StateGround, ActionExec);
            break;
        }
    }
}

const TableRange *
find_range(uchar c, TableDesc desc)
{
    const TableRange *result = NULL;

    // Linear traversal of the state's ranges. Pick the first one that matches
    for (uint i = 0; i < LEN(desc.ranges); i++) {
        const TableRange *range = &desc.ranges[i];

        if (!range->beg && !range->end) {
            break;
        }

        ASSERT(range->beg >= 0 && range->end > 0);

        if (c >= range->beg && c <= range->end) {
            result = range;
            break;
        }
    }

    return result;
}

void
fsm_print(FILE *fp, const FSM *fsm)
{
    for (int s = 0; s < NumStates; s++) {
        fprintf(fp, "STATE(%s):\n", fsm_state_to_string(s));
        for (int c = 0; c < 256; c++) {
            const uint16 pair = fsm->table[c][s];
            fprintf(fp, "\t[0x%02x] = ( %s, %s )\n",
                    c,
                    fsm_state_to_string(GET_STATE(pair)),
                    fsm_action_to_string(GET_ACTION(pair)));
        }
        fprintf(fp, "\n");
    }
}

