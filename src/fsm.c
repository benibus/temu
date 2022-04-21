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
#include "fsm.h"

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
    static const TableDesc descs[NUM_STATES] = {
        [STATE_GROUND] = {
            .ranges = {
                { 0xf0, 0xf7, STATE_UTF8B3, ACTION_UTF8GETB4 },
                { 0xe0, 0xef, STATE_UTF8B2, ACTION_UTF8GETB3 },
                { 0xc0, 0xdf, STATE_UTF8B1, ACTION_UTF8GETB2 },
                { 0x00, 0x7f, -1, ACTION_PRINT },
                { 0x00, 0xff, -1, ACTION_UTF8ERROR },
            }
        },

        [STATE_UTF8B1] = {
            .ranges = {
                { 0x80, 0xff, STATE_GROUND, ACTION_PRINTWIDE },
                { 0x00, 0x3f, STATE_GROUND, ACTION_PRINTWIDE },
                { 0x00, 0xff, STATE_GROUND, ACTION_UTF8ERROR },
            }
        },

        [STATE_UTF8B2] = {
            .ranges = {
                { 0x80, 0xff, STATE_UTF8B1, ACTION_UTF8GETB2 },
                { 0x00, 0x3f, STATE_UTF8B1, ACTION_UTF8GETB2 },
                { 0x00, 0xff, STATE_GROUND, ACTION_UTF8ERROR },
            }
        },

        [STATE_UTF8B3] = {
            .ranges = {
                { 0x80, 0xff, STATE_UTF8B2, ACTION_UTF8GETB3 },
                { 0x00, 0x3f, STATE_UTF8B2, ACTION_UTF8GETB3 },
                { 0x00, 0xff, STATE_GROUND, ACTION_UTF8ERROR },
            }
        },

        [STATE_ESC1] = {
            .ranges = {
                { ']',  ']',  STATE_OSC, 0 },
                { '[',  '[',  STATE_CSI1, 0 },
                { '0',  0x7e, STATE_GROUND, ACTION_ESCDISPATCH },
                { ' ',  '/',  STATE_ESC2, ACTION_GETINTERMEDIATE },
                { 0x00, 0x1f, -1, ACTION_PRINT },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [STATE_ESC2] = {
            .ranges = {
                { '0',  0x7e, STATE_GROUND, ACTION_ESCDISPATCH },
                { ' ',  '/',  STATE_GROUND, 0 },
                { 0x00, 0x1f, -1, ACTION_PRINT },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [STATE_CSI1] = {
            .ranges = {
                { '@',  0x7e, STATE_GROUND, ACTION_CSIDISPATCH },
                { '<',  '?',  STATE_CSIPARAM, ACTION_GETPRIVMARKER },
                { ':',  ':',  STATE_CSIIGNORE, 0 },
                { '0',  ';',  STATE_CSIPARAM, ACTION_PARAM },
                { ' ',  '/',  STATE_CSI2, ACTION_GETINTERMEDIATE },
                { 0x00, 0x1f, -1, ACTION_PRINT },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [STATE_CSI2] = {
            .ranges = {
                { '@',  0x7e,  STATE_GROUND, ACTION_CSIDISPATCH },
                { ' ',  '?',   STATE_CSIIGNORE, 0 },
                { 0x00, 0x1f,  -1, ACTION_PRINT },
                { 0x00, 0xff,  -1, 0 },
            }
        },

        [STATE_CSIIGNORE] = {
            .ranges = {
                { '@',  0x7e, STATE_GROUND, 0 },
                { 0x00, 0x1f, -1, ACTION_PRINT },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [STATE_CSIPARAM] = {
            .ranges = {
                { '@',  0x7e, STATE_GROUND, ACTION_CSIDISPATCH },
                { '<',  '?',  STATE_CSIIGNORE, 0 },
                { ':',  ':',  STATE_CSIIGNORE, 0 },
                { '0',  ';',  -1, ACTION_PARAM },
                { ' ',  '/',  STATE_CSI2, ACTION_GETINTERMEDIATE },
                { 0x00, 0x1f, -1, ACTION_PRINT },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [STATE_DCS1] = {
            .ranges = {
                { '@',  0x7e, STATE_DCSPASS, 0 },
                { '<',  '?',  STATE_DCSPARAM, ACTION_GETPRIVMARKER },
                { ':',  ':',  STATE_DCSIGNORE, 0 },
                { '0',  ';',  STATE_DCSPARAM, ACTION_PARAM },
                { ' ',  '/',  STATE_DCS2, ACTION_GETINTERMEDIATE },
                { 0x00, 0x1f, -1, ACTION_PRINT },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [STATE_DCS2] = {
            .ranges = {
                { '@',  0x7e, STATE_DCSPASS, 0 },
                { ' ',  '?',  STATE_DCSIGNORE, 0 },
                { 0x00, 0x1f, -1, ACTION_PRINT },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [STATE_DCSIGNORE] = {
            .ranges = {
                { 0x9c, 0x9c, STATE_GROUND, 0 },
                { 0x00, 0x1f, -1, ACTION_PRINT },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [STATE_DCSPARAM] = {
            .ranges = {
                { '@',  0x7e, STATE_DCSPASS, 0 },
                { '<',  '?',  STATE_DCSIGNORE, 0 },
                { ':',  ':',  STATE_DCSIGNORE, 0 },
                { '0',  ';',  -1, ACTION_PARAM },
                { ' ',  '/',  STATE_DCS2, ACTION_GETINTERMEDIATE },
                { 0x00, 0x1f, -1, ACTION_PRINT },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [STATE_DCSPASS] = {
            .ranges = {
                { 0x9c, 0x9c, STATE_GROUND, 0 },
                { 0x00, 0x7e, -1, ACTION_PUT },
                { 0x00, 0xff, -1, 0 },
            }
        },

        [STATE_OSC] = {
            .ranges = {
                { 0x07, 0x07, STATE_GROUND, ACTION_OSCDISPATCH },
                { 0x00, 0x1f, -1, 0 },
                { 0x00, 0xff, -1, ACTION_PUT },
            }
        },

        [STATE_SOSPMAPC] = {
            .ranges = {
                { 0x9c, 0x9c, STATE_GROUND, 0 },
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
        for (int s = 0; s < NUM_STATES; s++) {
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
    for (int s = 0; s < NUM_STATES; s++) {
        switch (s) {
        // 0x00-0x3f are valid UTF-8 continuation bytes, so we exclude these
        case STATE_UTF8B3:
        case STATE_UTF8B2:
        case STATE_UTF8B1:
            break;
        default:
            fsm->table[0x1b][s] = PAIR(STATE_ESC1, ACTION_CLEAR);
            fsm->table[0x1a][s] = PAIR(STATE_GROUND, ACTION_PRINT);
            fsm->table[0x18][s] = PAIR(STATE_GROUND, ACTION_PRINT);
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
    for (int s = 0; s < NUM_STATES; s++) {
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

