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

#define X_STATES  \
    X_(Ground)    \
    X_(Utf8B1)    \
    X_(Utf8B2)    \
    X_(Utf8B3)    \
    X_(Esc1)      \
    X_(Esc2)      \
    X_(Csi1)      \
    X_(Csi2)      \
    X_(CsiParam)  \
    X_(CsiIgnore) \
    X_(Osc)       \
    X_(Dcs1)      \
    X_(Dcs2)      \
    X_(DcsParam)  \
    X_(DcsIgnore) \
    X_(DcsPass)   \
    X_(SosPmApc)

#define X_(x) State##x,
typedef enum { X_STATES NumStates } FSMState;
#undef X_

static inline const char *
fsm_state_to_string(uint8 state)
{
#define X_(x) [State##x] = #x,
    static const char *strings[NumStates] = { X_STATES };
#undef X_
    ASSERT(state < NumStates);
    return strings[state];
}

#define X_ACTIONS       \
    X_(None)            \
    X_(Ignore)          \
    X_(Print)           \
    X_(PrintWide)       \
    X_(Exec)            \
    X_(Clear)           \
    X_(GetIntermediate) \
    X_(GetPrivMarker)   \
    X_(Param)           \
    X_(EscDispatch)     \
    X_(CsiDispatch)     \
    X_(Hook)            \
    X_(Unhook)          \
    X_(Put)             \
    X_(OscStart)        \
    X_(OscPut)          \
    X_(OscEnd)          \
    X_(Utf8GetB2)       \
    X_(Utf8GetB3)       \
    X_(Utf8GetB4)       \
    X_(Utf8Error)

#define X_(x) Action##x,
typedef enum { X_ACTIONS NumActions } FSMAction;
#undef X_

static inline const char *
fsm_action_to_string(uint8 action)
{
#define X_(x) [Action##x] = #x,
    static const char *strings[NumActions] = { X_ACTIONS };
#undef X_
    ASSERT(action < NumActions);
    return strings[action];
}

#undef X_STATES
#undef X_ACTIONS

typedef struct {
    uint16 table[256][NumStates];
} FSM;

#define PAIR(state,action) ((((state) & 0xff) << 8)|((action) & 0xff))
#define GET_STATE(pair)    (((pair) >> 8) & 0xff)
#define GET_ACTION(pair)   (((pair) >> 0) & 0xff)

void fsm_generate(FSM *fsm);
void fsm_print(FILE *fp, const FSM *fsm);

#endif

