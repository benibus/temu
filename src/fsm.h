#ifndef FSM_H__
#define FSM_H__

#include "defs.h"

typedef enum {
	ActionNone,
	ActionIgnore,
	ActionPrint,
	ActionExec,
	ActionClear,
	ActionCollect,
	ActionParam,
	ActionEscDispatch,
	ActionCsiDispatch,
	ActionHook,
	ActionUnhook,
	ActionPut,
	ActionOscStart,
	ActionOscPut,
	ActionOscEnd,
	ActionUtf8Start,
	ActionUtf8Cont,
	ActionUtf8Fail,
	ActionCount
} ActionCode;

typedef enum {
	StateGround,
	StateEsc1,
	StateEsc2,
	StateCsi1,
	StateCsi2,
	StateCsiParam,
	StateCsiIgnore,
	StateOsc,
	StateDcs1,
	StateDcs2,
	StateDcsParam,
	StateDcsIgnore,
	StateDcsPass,
	StateSosPmApc,
	StateUtf8B1,
	StateUtf8B2,
	StateUtf8B3,
	StateCount,
} StateCode;

typedef struct {
	StateCode state;       // next state to transition to
	ActionCode actions[3]; // actions to complete before transition (in order)
} StateTrans;

StateTrans fsm_next_state(StateCode, uchar);

// return a human-readable debug string
const char *fsm_get_state_string(StateCode);
const char *fsm_get_action_string(ActionCode);

#endif

