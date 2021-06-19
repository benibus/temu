#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xutil.h>

#include "utils.h"
#include "x.h"

// temporary
#define MODE_APPKEYPAD 0
#define MODE_APPCURSOR 0
#define MODE_NUMLOCK 0

// expands standard keysyms
#define X_KEY_TABLE                  \
  X(XK_BackSpace,    KeyBackSpace)   \
  X(XK_Tab,          KeyTab)         \
  X(XK_Return,       KeyReturn)      \
  X(XK_Escape,       KeyEscape)      \
  X(XK_Insert,       KeyInsert)      \
  X(XK_Delete,       KeyDelete)      \
  X(XK_Page_Up,      KeyPageUp)      \
  X(XK_Page_Down,    KeyPageDown)    \
  X(XK_F1,           KeyF1)          \
  X(XK_F2,           KeyF2)          \
  X(XK_F3,           KeyF3)          \
  X(XK_F4,           KeyF4)          \
  X(XK_F5,           KeyF5)          \
  X(XK_F6,           KeyF6)          \
  X(XK_F7,           KeyF7)          \
  X(XK_F8,           KeyF8)          \
  X(XK_F9,           KeyF9)          \
  X(XK_F10,          KeyF10)         \
  X(XK_F11,          KeyF11)         \
  X(XK_F12,          KeyF12)         \
  X(XK_F13,          KeyF13)         \
  X(XK_F14,          KeyF14)         \
  X(XK_F15,          KeyF15)         \
  X(XK_F16,          KeyF16)         \
  X(XK_F17,          KeyF17)         \
  X(XK_F18,          KeyF18)         \
  X(XK_F19,          KeyF19)         \
  X(XK_F20,          KeyF20)
// expands keypad keysyms that always map to standard keysyms
#define X_KEYPAD_TABLE            \
  X(XK_KP_Insert,    KeyInsert)   \
  X(XK_KP_Delete,    KeyDelete)   \
  X(XK_KP_Page_Up,   KeyPageUp)   \
  X(XK_KP_Page_Down, KeyPageDown) \
  X(XK_KP_F1,        KeyF1)       \
  X(XK_KP_F2,        KeyF2)       \
  X(XK_KP_F3,        KeyF3)       \
  X(XK_KP_F4,        KeyF4)
// expands keypad keysyms that conditionally map to appkeypad keys
#define X_APPKEYPAD_TABLE                          \
  X(XK_KP_Tab,       KeyTab,      KeyAppTab)       \
  X(XK_KP_Enter,     KeyReturn,   KeyAppEnter)     \
  X(XK_KP_Space,     KeyNone,     KeyAppSpace)     \
  X(XK_KP_Multiply,  KeyNone,     KeyAppMultiply)  \
  X(XK_KP_Add,       KeyNone,     KeyAppAdd)       \
  X(XK_KP_Separator, KeyNone,     KeyAppSeparator) \
  X(XK_KP_Subtract,  KeyNone,     KeyAppSubtract)  \
  X(XK_KP_Decimal,   KeyNone,     KeyAppDecimal)   \
  X(XK_KP_Divide,    KeyNone,     KeyAppDivide)    \
  X(XK_KP_Equal,     KeyNone,     KeyAppEqual)     \
  X(XK_KP_0,         KeyNone,     KeyApp0)         \
  X(XK_KP_1,         KeyNone,     KeyApp1)         \
  X(XK_KP_2,         KeyNone,     KeyApp2)         \
  X(XK_KP_3,         KeyNone,     KeyApp3)         \
  X(XK_KP_4,         KeyNone,     KeyApp4)         \
  X(XK_KP_5,         KeyNone,     KeyApp5)         \
  X(XK_KP_6,         KeyNone,     KeyApp6)         \
  X(XK_KP_7,         KeyNone,     KeyApp7)         \
  X(XK_KP_8,         KeyNone,     KeyApp8)         \
  X(XK_KP_9,         KeyNone,     KeyApp9)
// expands keypad/standard keysyms that conditionally map to appcursor keys
#define X_APPKEYPAD_CURSOR_TABLE                  \
  X(XK_KP_Up,    XK_Up,    KeyUp,    KeyAppUp)    \
  X(XK_KP_Down,  XK_Down,  KeyDown,  KeyAppDown)  \
  X(XK_KP_Right, XK_Right, KeyRight, KeyAppRight) \
  X(XK_KP_Left,  XK_Left,  KeyLeft,  KeyAppLeft)  \
  X(XK_KP_Home,  XK_Home,  KeyHome,  KeyAppHome)  \
  X(XK_KP_End,   XK_End,   KeyEnd,   KeyAppEnd)

enum KEY_IDS {
	KeyNone,
#define X(k,id) id,
	X_KEY_TABLE
#undef X
#define X(k,id_off,id_on) id_on,
	X_APPKEYPAD_TABLE
#undef X
#define X(k1,k2,id_off,id_on) id_off, id_on,
	X_APPKEYPAD_CURSOR_TABLE
#undef X
	NUM_KEY
};

#define ESC "\033"
#define CSI ESC"["
#define SS3 ESC"O"
#define MDC "\377"

enum {
	KeyModNone  = (0),      // 0000
	KeyModShift = (1 << 0), // 0001
	KeyModAlt   = (1 << 1), // 0010
	KeyModCtrl  = (1 << 2), // 0100
	KeyModAny   = (1 << 3), // 1000
	KeyModMax
};

struct KeyString {
	char *prefix[KeyModMax];
	char *suffix;
};
/*
 * modifed keysyms use the corresponding mod prefix but default to prefix[0] if there isn't one
 * entries with a suffix get concatenated with the appropriate prefix and arguments
 * entries without a suffix don't take modifier arguments and just return the appropriate prefix string
 */
static struct KeyString keystr[NUM_KEY] = {
	[KeyReturn] = {
		{
			[0] = "\r",
			[KeyModAlt] = ESC"\r",
			[KeyModCtrl] = CSI"27;5;13~"
		},
		NULL
	},
	[KeyTab] = {
		{
			[0] = "\t",
			[KeyModAlt] = ESC"\t",
			[KeyModCtrl] = CSI"27;5;13~"
		},
		NULL
	},
	[KeyBackSpace] = {
		{
			[0] = "\177",
			[KeyModAlt] = ESC"\177"
		},
		NULL
	},
	// 0 = no mod, 1 = any mod
	[KeyUp]           = { { [0] = CSI, [1] = CSI"1" }, "A" },
	[KeyDown]         = { { [0] = CSI, [1] = CSI"1" }, "B" },
	[KeyRight]        = { { [0] = CSI, [1] = CSI"1" }, "C" },
	[KeyLeft]         = { { [0] = CSI, [1] = CSI"1" }, "D" },
	[KeyEnd]          = { { [0] = CSI, [1] = CSI"1" }, "F" },
	[KeyHome]         = { { [0] = CSI, [1] = CSI"1" }, "H" },
	[KeyInsert]       = { { [0] = CSI"2" },  "~" },
	[KeyDelete]       = { { [0] = CSI"3" },  "~" },
	[KeyPageUp]       = { { [0] = CSI"5" },  "~" },
	[KeyPageDown]     = { { [0] = CSI"6" },  "~" },
	[KeyF1]           = { { [0] = CSI, [1] = SS3"1" }, "P" },
	[KeyF2]           = { { [0] = CSI, [1] = SS3"1" }, "Q" },
	[KeyF3]           = { { [0] = CSI, [1] = SS3"1" }, "R" },
	[KeyF4]           = { { [0] = CSI, [1] = SS3"1" }, "S" },
	[KeyF5]           = { { [0] = CSI"15" }, "~" },
	[KeyF6]           = { { [0] = CSI"17" }, "~" },
	[KeyF7]           = { { [0] = CSI"18" }, "~" },
	[KeyF8]           = { { [0] = CSI"19" }, "~" },
	[KeyF9]           = { { [0] = CSI"20" }, "~" },
	[KeyF10]          = { { [0] = CSI"21" }, "~" },
	[KeyF11]          = { { [0] = CSI"23" }, "~" },
	[KeyF12]          = { { [0] = CSI"24" }, "~" },
	[KeyF13]          = { { [0] = CSI"25" }, "~" },
	[KeyF14]          = { { [0] = CSI"26" }, "~" },
	[KeyF15]          = { { [0] = CSI"28" }, "~" },
	[KeyF16]          = { { [0] = CSI"29" }, "~" },
	[KeyF17]          = { { [0] = CSI"31" }, "~" },
	[KeyF18]          = { { [0] = CSI"32" }, "~" },
	[KeyF19]          = { { [0] = CSI"33" }, "~" },
	[KeyF20]          = { { [0] = CSI"34" }, "~" },
	[KeyAppTab]       = { { [0] = SS3 },     "I" },
	[KeyAppEnter]     = { { [0] = SS3 },     "M" },
	[KeyAppSpace]     = { { [0] = SS3 },     " " },
	[KeyAppMultiply]  = { { [0] = SS3 },     "j" },
	[KeyAppAdd]       = { { [0] = SS3 },     "k" },
	[KeyAppSeparator] = { { [0] = SS3 },     "l" },
	[KeyAppSubtract]  = { { [0] = SS3 },     "m" },
	[KeyAppDecimal]   = { { [0] = SS3 },     "n" },
	[KeyAppDivide]    = { { [0] = SS3 },     "o" },
	[KeyAppEqual]     = { { [0] = SS3 },     "X" },
	[KeyApp0]         = { { [0] = SS3 },     "p" },
	[KeyApp1]         = { { [0] = SS3 },     "q" },
	[KeyApp2]         = { { [0] = SS3 },     "r" },
	[KeyApp3]         = { { [0] = SS3 },     "s" },
	[KeyApp4]         = { { [0] = SS3 },     "t" },
	[KeyApp5]         = { { [0] = SS3 },     "u" },
	[KeyApp6]         = { { [0] = SS3 },     "v" },
	[KeyApp7]         = { { [0] = SS3 },     "w" },
	[KeyApp8]         = { { [0] = SS3 },     "x" },
	[KeyApp9]         = { { [0] = SS3 },     "y" },
	[KeyAppUp]        = { { [0] = SS3"A" },  NULL },
	[KeyAppDown]      = { { [0] = SS3"B" },  NULL },
	[KeyAppRight]     = { { [0] = SS3"C" },  NULL },
	[KeyAppLeft]      = { { [0] = SS3"D" },  NULL },
	[KeyAppHome]      = { { [0] = SS3"H" },  NULL },
	[KeyAppEnd]       = { { [0] = SS3"F" },  NULL }
};

static inline uint
key__get_mod(uint mask)
{
	uint mod = KeyModNone;

	mod |= (mask & ShiftMask)   ? KeyModShift : 0;
	mod |= (mask & Mod1Mask)    ? KeyModAlt   : 0;
	mod |= (mask & ControlMask) ? KeyModCtrl  : 0;

	return mod;
}

uint
key_get_id(uint sym)
{
	/*
	 * we translate the X11 keysym to our own internal ID
	 */
	switch (sym) {
	// normal (non-keypad) keysyms
#define X(k,id) case (k): return (id);
	X_KEY_TABLE
	X_KEYPAD_TABLE
#undef X
	// kp keysyms that conditionally translate to APPKEYPAD sequences
#define X(k,id_off,id_on) case (k): return (!MODE_APPKEYPAD) ? (id_off) : (id_on);
	X_APPKEYPAD_TABLE
#undef X
	// normal/kp keysym pairs that conditionally translate to the same APPCURSOR sequences
#define X(k1,k2,id_off,id_on) case (k1): case (k2): return (!MODE_APPCURSOR) ? (id_off) : (id_on);
	X_APPKEYPAD_CURSOR_TABLE
#undef X
	// extra cases
	case XK_ISO_Left_Tab: return KeyTab;
	}

	return KeyNone;
}

static inline uint
key__build_string(uint idx, uint mod, char *buf, uint size)
{
	struct KeyString ks = keystr[idx];
	uint arg = (mod) ? mod + 1 : 0;
	uint len = 0;

	if (ks.prefix[0]) {
		if (!ks.suffix) {
			len = snprintf(buf, size, "%s",
			    FALLBACK(ks.prefix[mod], ks.prefix[0]));
		} else if (!mod) {
			len = snprintf(buf, size, "%s%s", ks.prefix[0], ks.suffix);
		} else {
			len = snprintf(buf, size, "%s%c%u%s",
			    FALLBACK(ks.prefix[1], ks.prefix[0]), ';', arg, ks.suffix);
		}
	}

	assert(!len || len < size);

	return len;
}

#undef FALLBACK
#undef MODARG

uint
key_get_string(uint sym, uint mask, char *buf, uint size)
{
	uint mod = key__get_mod(mask);
	uint idx = key_get_id(sym);

	return (idx) ? key__build_string(idx, mod, buf, size) : 0;
}

