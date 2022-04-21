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

#ifndef FSM_H__
#define FSM_H__

#include "common.h"

#define XTABLE_STATES \
    X_(GROUND)        \
    X_(UTF8B1)        \
    X_(UTF8B2)        \
    X_(UTF8B3)        \
    X_(ESC1)          \
    X_(ESC2)          \
    X_(CSI1)          \
    X_(CSI2)          \
    X_(CSIPARAM)      \
    X_(CSIIGNORE)     \
    X_(OSC)           \
    X_(DCS1)          \
    X_(DCS2)          \
    X_(DCSPARAM)      \
    X_(DCSIGNORE)     \
    X_(DCSPASS)       \
    X_(SOSPMAPC)

#define X_(x) STATE_##x,
typedef enum { XTABLE_STATES NUM_STATES } FSMState;
#undef X_

static inline const char *
fsm_state_to_string(uint8 state)
{
#define X_(x) [STATE_##x] = #x,
    static const char *strings[NUM_STATES] = { XTABLE_STATES };
#undef X_
    ASSERT(state < NUM_STATES);
    return strings[state];
}

#define XTABLE_ACTIONS  \
    X_(NONE)            \
    X_(IGNORE)          \
    X_(PRINT)           \
    X_(PRINTWIDE)       \
    X_(CLEAR)           \
    X_(GETINTERMEDIATE) \
    X_(GETPRIVMARKER)   \
    X_(PARAM)           \
    X_(ESCDISPATCH)     \
    X_(CSIDISPATCH)     \
    X_(HOOK)            \
    X_(UNHOOK)          \
    X_(PUT)             \
    X_(OSCDISPATCH)     \
    X_(UTF8GETB2)       \
    X_(UTF8GETB3)       \
    X_(UTF8GETB4)       \
    X_(UTF8ERROR)

#define X_(x) ACTION_##x,
typedef enum { XTABLE_ACTIONS NUM_ACTIONS } FSMAction;
#undef X_

static inline const char *
fsm_action_to_string(uint8 action)
{
#define X_(x) [ACTION_##x] = #x,
    static const char *strings[NUM_ACTIONS] = { XTABLE_ACTIONS };
#undef X_
    ASSERT(action < NUM_ACTIONS);
    return strings[action];
}

#undef XTABLE_STATES
#undef XTABLE_ACTIONS

typedef struct {
    uint16 table[256][NUM_STATES];
} FSM;

#define PAIR(state,action) ((((state) & 0xff) << 8)|((action) & 0xff))
#define GET_STATE(pair)    (((pair) >> 8) & 0xff)
#define GET_ACTION(pair)   (((pair) >> 0) & 0xff)

void fsm_generate(FSM *fsm);
void fsm_print(FILE *fp, const FSM *fsm);

#endif

