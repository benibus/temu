#ifndef KEYS_H__
#define KEYS_H__

#define KEYCODE_TABLE \
    expand__(KEY_ESCAPE,        256) \
    expand__(KEY_RETURN,        257) \
    expand__(KEY_TAB,           258) \
    expand__(KEY_BACKSPACE,     259) \
    expand__(KEY_INSERT,        260) \
    expand__(KEY_DELETE,        261) \
    expand__(KEY_RIGHT,         262) \
    expand__(KEY_LEFT,          263) \
    expand__(KEY_DOWN,          264) \
    expand__(KEY_UP,            265) \
    expand__(KEY_PAGE_UP,       266) \
    expand__(KEY_PAGE_DOWN,     267) \
    expand__(KEY_HOME,          268) \
    expand__(KEY_END,           269) \
    expand__(KEY_F1,            290) \
    expand__(KEY_F2,            291) \
    expand__(KEY_F3,            292) \
    expand__(KEY_F4,            293) \
    expand__(KEY_F5,            294) \
    expand__(KEY_F6,            295) \
    expand__(KEY_F7,            296) \
    expand__(KEY_F8,            297) \
    expand__(KEY_F9,            298) \
    expand__(KEY_F10,           299) \
    expand__(KEY_F11,           300) \
    expand__(KEY_F12,           301) \
    expand__(KEY_F13,           302) \
    expand__(KEY_F14,           303) \
    expand__(KEY_F15,           304) \
    expand__(KEY_F16,           305) \
    expand__(KEY_F17,           306) \
    expand__(KEY_F18,           307) \
    expand__(KEY_F19,           308) \
    expand__(KEY_F20,           309) \
    expand__(KEY_F21,           310) \
    expand__(KEY_F22,           311) \
    expand__(KEY_F23,           312) \
    expand__(KEY_F24,           313) \
    expand__(KEY_F25,           314) \
    expand__(KEY_KP_0,          320) \
    expand__(KEY_KP_1,          321) \
    expand__(KEY_KP_2,          322) \
    expand__(KEY_KP_3,          323) \
    expand__(KEY_KP_4,          324) \
    expand__(KEY_KP_5,          325) \
    expand__(KEY_KP_6,          326) \
    expand__(KEY_KP_7,          327) \
    expand__(KEY_KP_8,          328) \
    expand__(KEY_KP_9,          329) \
    expand__(KEY_KP_DECIMAL,    330) \
    expand__(KEY_KP_DIVIDE,     331) \
    expand__(KEY_KP_MULTIPLY,   332) \
    expand__(KEY_KP_SUBTRACT,   333) \
    expand__(KEY_KP_ADD,        334) \
    expand__(KEY_KP_SEPARATOR,  335) \
    expand__(KEY_KP_ENTER,      336) \
    expand__(KEY_KP_EQUAL,      337) \
    expand__(KEY_KP_TAB,        338) \
    expand__(KEY_KP_SPACE,      339) \
    expand__(KEY_KP_INSERT,     340) \
    expand__(KEY_KP_DELETE,     341) \
    expand__(KEY_KP_RIGHT,      342) \
    expand__(KEY_KP_LEFT,       343) \
    expand__(KEY_KP_DOWN,       344) \
    expand__(KEY_KP_UP,         345) \
    expand__(KEY_KP_PAGE_UP,    346) \
    expand__(KEY_KP_PAGE_DOWN,  347) \
    expand__(KEY_KP_HOME,       348) \
    expand__(KEY_KP_END,        349)

enum {
	KEY_NONE = -1,
#define expand__(key,val) key = val,
	KEYCODE_TABLE
#undef  expand__
	NUM_KEY
};

static char *keyname[] = {
#define expand__(key,val) [key] = #key,
	KEYCODE_TABLE
#undef  expand__
};

#undef KEYCODE_TABLE

enum {
	MOD_NONE  = (0),
	MOD_SHIFT = (1 << 0),
	MOD_ALT   = (1 << 1),
	MOD_CTRL  = (1 << 2),
	MOD_ANY   = (1 << 3),
	NUM_MOD
};

#endif
