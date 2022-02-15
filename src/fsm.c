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
#include "utf8.h"

struct Atom {
    StateCode state;
    ActionCode action;
};

struct StateInfo {
    const char *name;
    struct Atom (*emit)(uchar);
    ActionCode on_enter;
    ActionCode on_exit;
};

static struct Atom emit_ground(uchar);
static struct Atom emit_esc1(uchar);
static struct Atom emit_esc2(uchar);
static struct Atom emit_csi1(uchar);
static struct Atom emit_csi2(uchar);
static struct Atom emit_csi_param(uchar);
static struct Atom emit_csi_ignore(uchar);
static struct Atom emit_osc(uchar);
static struct Atom emit_dcs1(uchar);
static struct Atom emit_dcs2(uchar);
static struct Atom emit_dcs_param(uchar);
static struct Atom emit_dcs_ignore(uchar);
static struct Atom emit_dcs_pass(uchar);
static struct Atom emit_sos_pm_apc(uchar);
static struct Atom emit_utf8b1(uchar);
static struct Atom emit_utf8b2(uchar);
static struct Atom emit_utf8b3(uchar);

static const struct StateInfo state_table[StateCount] = {
    [StateGround]    = { "Ground",    emit_ground,     ActionNone,     ActionNone },
    [StateEsc1]      = { "Esc1",      emit_esc1,       ActionClear,    ActionNone },
    [StateEsc2]      = { "Esc2",      emit_esc2,       ActionNone,     ActionNone },
    [StateCsi1]      = { "Csi1",      emit_csi1,       ActionClear,    ActionNone },
    [StateCsi2]      = { "Csi2",      emit_csi2,       ActionNone,     ActionNone },
    [StateCsiParam]  = { "CsiParam",  emit_csi_param,  ActionNone,     ActionNone },
    [StateCsiIgnore] = { "CsiIgnore", emit_csi_ignore, ActionNone,     ActionNone },
    [StateOsc]       = { "Osc",       emit_osc,        ActionOscStart, ActionOscEnd },
    [StateDcs1]      = { "Dcs1",      emit_dcs1,       ActionClear,    ActionNone },
    [StateDcs2]      = { "Dcs2",      emit_dcs2,       ActionNone,     ActionNone },
    [StateDcsParam]  = { "DcsParam",  emit_dcs_param,  ActionNone,     ActionNone },
    [StateDcsIgnore] = { "DcsIgnore", emit_dcs_ignore, ActionNone,     ActionNone },
    [StateDcsPass]   = { "DcsPass",   emit_dcs_pass,   ActionHook,     ActionUnhook },
    [StateSosPmApc]  = { "SosPmApc",  emit_sos_pm_apc, ActionNone,     ActionNone },
    [StateUtf8B1]    = { "Utf8B1",    emit_utf8b1,     ActionNone,     ActionNone },
    [StateUtf8B2]    = { "Utf8B2",    emit_utf8b2,     ActionNone,     ActionNone },
    [StateUtf8B3]    = { "Utf8B3",    emit_utf8b3,     ActionNone,     ActionNone }
};

#define ACTION_ENTRIES \
  X_(None)             \
  X_(Ignore)           \
  X_(Print)            \
  X_(Exec)             \
  X_(Clear)            \
  X_(Collect)          \
  X_(Param)            \
  X_(EscDispatch)      \
  X_(CsiDispatch)      \
  X_(Hook)             \
  X_(Unhook)           \
  X_(Put)              \
  X_(OscStart)         \
  X_(OscPut)           \
  X_(OscEnd)           \
  X_(Utf8Start)        \
  X_(Utf8Cont)         \
  X_(Utf8Fail)

static const char *action_names[ActionCount] = {
#define X_(suffix) [Action##suffix] = #suffix,
    ACTION_ENTRIES
#undef X_
};
#undef ACTION_ENTRIES

#define BEL 0x07
#define CAN 0x18
#define SUB 0x1a
#define ESC 0x1b

StateTrans
fsm_next_state(StateCode state, uchar c)
{
    StateTrans result = { 0 };
    struct Atom atom;

    switch (c) {
    case CAN:
    case SUB: atom = (struct Atom){ StateGround, ActionExec }; break;
    case ESC: atom = (struct Atom){ StateEsc1,   ActionNone }; break;
    default:  atom = state_table[state].emit(c); break;
    }

    result.state = atom.state;
    result.actions[0] = atom.action;

    if (result.state != state) {
        result.actions[1] = state_table[state].on_exit;
        result.actions[2] = state_table[result.state].on_enter;
    }

    return result;
}

const char *
fsm_get_state_string(StateCode state)
{
    return state_table[state].name;
}

const char *
fsm_get_action_string(ActionCode action)
{
    return action_names[action];
}

#define EMIT(state,action) return (struct Atom){ (state), (action) }

struct Atom
emit_ground(uchar c)
{
    static const StateCode same = StateGround;

    // expected codepoint length based on leading byte
    switch (utf8_check_first(c)) {
    case 4: EMIT(StateUtf8B3, ActionUtf8Start);
    case 3: EMIT(StateUtf8B2, ActionUtf8Start);
    case 2: EMIT(StateUtf8B1, ActionUtf8Start);
    case 1: EMIT(same, (c < ' ') ? ActionExec : ActionPrint);
    }

    // invalid codepoint
    EMIT(StateGround, ActionUtf8Fail);
}

struct Atom
emit_utf8b1(uchar c)
{
    if (utf8_check_cont(c)) {
        EMIT(StateGround, ActionPrint);
    }

    EMIT(StateGround, ActionUtf8Fail);
}

struct Atom
emit_utf8b2(uchar c)
{
    if (utf8_check_cont(c)) {
        EMIT(StateUtf8B1, ActionUtf8Cont);
    }

    EMIT(StateGround, ActionUtf8Fail);
}

struct Atom
emit_utf8b3(uchar c)
{
    if (utf8_check_cont(c)) {
        EMIT(StateUtf8B2, ActionUtf8Cont);
    }

    EMIT(StateGround, ActionUtf8Fail);
}

struct Atom
emit_esc1(uchar c)
{
    static const StateCode same = StateEsc1;

    if (c < ' ') {
        EMIT(same, ActionExec);
    } else if (c < '0') {
        EMIT(StateEsc2, ActionCollect);
    } else if (c < 'P' || c == 'Y' || c == 'Z') {
        EMIT(StateGround, ActionEscDispatch);
    } else if (c < 0x7f) {
        switch (c) {
            case '[': EMIT(StateCsi1, 0);
            case ']': EMIT(StateOsc, 0);
        }
        EMIT(StateGround, ActionEscDispatch);
    }

    EMIT(same, 0);
}

struct Atom
emit_esc2(uchar c)
{
    static const StateCode same = StateEsc2;

    if (c < ' ') {
        EMIT(same, ActionExec);
    } else if (c < '0') {
#if 1
        EMIT(StateGround, 0);
#else
        EMIT(same, ActionCollect);
#endif
    } else if (c < 0x7f) {
        EMIT(StateGround, ActionEscDispatch);
    }

    EMIT(same, 0);
}

struct Atom
emit_csi1(uchar c)
{
    static const StateCode same = StateCsi1;

    if (c < ' ') {
        EMIT(same, ActionExec);
    } else if (c < '0') {
        EMIT(StateCsi2, ActionExec);
    } else if (c < '<') {
        if (c == ':') {
            EMIT(StateCsiIgnore, 0);
        }
        EMIT(StateCsiParam, ActionParam);
    } else if (c < 0x7f) {
        if (c < '@') {
            EMIT(StateCsiParam, ActionCollect);
        }
        EMIT(StateGround, ActionCsiDispatch);
    }

    EMIT(same, 0);
}

struct Atom
emit_csi2(uchar c)
{
    static const StateCode same = StateCsi2;

    if (c < ' ') {
        EMIT(same, ActionExec);
#if 0
    } else if (c < '0') {
        EMIT(same, ActionCollect);
#endif
    } else if (c < '@') {
        EMIT(StateCsiIgnore, 0);
    } else if (c < 0x7f) {
        EMIT(StateGround, ActionCsiDispatch);
    }

    EMIT(same, 0);
}

struct Atom
emit_csi_ignore(uchar c)
{
    static const StateCode same = StateCsiIgnore;

    if (c < ' ') {
        EMIT(same, ActionExec);
    } else if (c >= '@' && c < 0x7f) {
        EMIT(StateGround, 0);
    }

    EMIT(same, 0);
}

struct Atom
emit_csi_param(uchar c)
{
    static const StateCode same = StateCsiParam;

    if (c < ' ') {
        EMIT(same, ActionExec);
    } else if (c < '0') {
        EMIT(StateCsi2, ActionCollect);
    } else if (c < '<') {
        if (c == ':') {
            EMIT(StateCsiIgnore, 0);
        }
        EMIT(same, ActionParam);
    } else if (c < 0x7f) {
        if (c < '@') {
            EMIT(StateCsiIgnore, 0);
        }
        EMIT(StateGround, ActionCsiDispatch);
    }

    EMIT(same, 0);
}

struct Atom
emit_dcs1(uchar c)
{
    static const StateCode same = StateDcs1;

    if (c < ' ') {
        EMIT(same, ActionExec);
    } else if (c < '0') {
        EMIT(StateDcs2, ActionCollect);
    } else if (c < '<') {
        if (c == ':') {
            EMIT(StateDcsIgnore, 0);
        }
        EMIT(StateDcsParam, ActionParam);
    } else if (c < 0x7f) {
        if (c < '@') {
            EMIT(StateDcsParam, ActionCollect);
        }
        EMIT(StateDcsPass, 0);
    }

    EMIT(same, 0);
}

struct Atom
emit_dcs2(uchar c)
{
    static const StateCode same = StateDcs2;

    if (c < ' ') {
        EMIT(same, ActionExec);
#if 0
    } else if (c < '0') {
        EMIT(same, ActionCollect);
#endif
    } else if (c < '@') {
        EMIT(StateDcsIgnore, 0);
    } else if (c < 0x7f) {
        EMIT(StateDcsPass, 0);
    }

    EMIT(same, 0);
}

struct Atom
emit_dcs_ignore(uchar c)
{
    static const StateCode same = StateDcsIgnore;

    if (c < ' ') {
        EMIT(same, ActionExec);
    } else if (c == 0x9c) {
        EMIT(StateGround, 0);
    }

    EMIT(same, 0);
}

struct Atom
emit_dcs_param(uchar c)
{
    static const StateCode same = StateDcsParam;

    if (c < ' ') {
        EMIT(same, ActionExec);
    } else if (c < '0') {
#if 1
        EMIT(StateDcsIgnore, 0);
#else
        EMIT(StateDcs2, ActionCollect);
#endif
    } else if (c < '<') {
        if (c == ':') {
            EMIT(StateDcsIgnore, 0);
        }
        EMIT(same, ActionParam);
    } else if (c < 0x7f) {
        if (c < '@') {
            EMIT(StateDcsIgnore, 0);
        }
        EMIT(StateDcsPass, 0);
    }

    EMIT(same, 0);
}

struct Atom
emit_dcs_pass(uchar c)
{
    static const StateCode same = StateDcsPass;

    if (c < 0x7f) {
        EMIT(same, ActionPut);
    } else if (c == 0x9c) {
        EMIT(StateGround, 0);
    }

    EMIT(same, 0);
}

struct Atom
emit_osc(uchar c)
{
    static const StateCode same = StateOsc;

    if (c < ' ') {
        if (c == BEL) {
            EMIT(StateGround, 0);
        }
        EMIT(same, 0);
    }

    EMIT(same, ActionOscPut);
}

struct Atom
emit_sos_pm_apc(uchar c)
{
    static const StateCode same = StateSosPmApc;

    if (c == 0x9c) {
        EMIT(StateGround, ActionNone);
    }

    EMIT(same, ActionNone);
}

