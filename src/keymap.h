#ifndef KEYS_H__
#define KEYS_H__

#define KEYCODE_TABLE \
    X_(Escape,      256) \
    X_(Return,      257) \
    X_(Tab,         258) \
    X_(Backspace,   259) \
    X_(Insert,      260) \
    X_(Delete,      261) \
    X_(Right,       262) \
    X_(Left,        263) \
    X_(Down,        264) \
    X_(Up,          265) \
    X_(PageUp,      266) \
    X_(PageDown,    267) \
    X_(Home,        268) \
    X_(End,         269) \
    X_(F1,          290) \
    X_(F2,          291) \
    X_(F3,          292) \
    X_(F4,          293) \
    X_(F5,          294) \
    X_(F6,          295) \
    X_(F7,          296) \
    X_(F8,          297) \
    X_(F9,          298) \
    X_(F10,         299) \
    X_(F11,         300) \
    X_(F12,         301) \
    X_(F13,         302) \
    X_(F14,         303) \
    X_(F15,         304) \
    X_(F16,         305) \
    X_(F17,         306) \
    X_(F18,         307) \
    X_(F19,         308) \
    X_(F20,         309) \
    X_(F21,         310) \
    X_(F22,         311) \
    X_(F23,         312) \
    X_(F24,         313) \
    X_(F25,         314) \
    X_(KP0,         320) \
    X_(KP1,         321) \
    X_(KP2,         322) \
    X_(KP3,         323) \
    X_(KP4,         324) \
    X_(KP5,         325) \
    X_(KP6,         326) \
    X_(KP7,         327) \
    X_(KP8,         328) \
    X_(KP9,         329) \
    X_(KPDecimal,   330) \
    X_(KPDivide,    331) \
    X_(KPMultiply,  332) \
    X_(KPSubtract,  333) \
    X_(KPAdd,       334) \
    X_(KPSeparator, 335) \
    X_(KPEnter,     336) \
    X_(KPEqual,     337) \
    X_(KPTab,       338) \
    X_(KPSpace,     339) \
    X_(KPInsert,    340) \
    X_(KPDelete,    341) \
    X_(KPRight,     342) \
    X_(KPLeft,      343) \
    X_(KPDown,      344) \
    X_(KPUp,        345) \
    X_(KPPageUp,    346) \
    X_(KPPageDown,  347) \
    X_(KPHome,      348) \
    X_(KPEnd,       349)

enum {
	KeyNone = -1,
#define X_(key,val) Key##key = val,
	KEYCODE_TABLE
#undef  X_
	KeyCount
};

static char *keyname[] = {
#define X_(key,val) [Key##key] = #key,
	KEYCODE_TABLE
#undef  X_
};

#undef KEYCODE_TABLE

enum {
	ModNone  = (0),
	ModShift = (1 << 0),
	ModAlt   = (1 << 1),
	ModCtrl  = (1 << 2),
	ModAny   = (1 << 3),
	ModCount
};

#endif
