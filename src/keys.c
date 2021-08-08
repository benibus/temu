#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "keymap.h"

// temporary
#define MODE_APPKEYPAD 0
#define MODE_APPCURSOR 0
#define MODE_NUMLOCK 0

#define ESC "\033"
#define CSI ESC"["
#define SS3 ESC"O"
#define MDC "\377"

typedef struct KeyString {
	char *prefix[2];
	char *suffix;
} KeyString;

static KeyString keys_normal[NUM_KEY] = {
	[KEY_UP]           = { { CSI,     CSI"1" }, "A" },
	[KEY_DOWN]         = { { CSI,     CSI"1" }, "B" },
	[KEY_RIGHT]        = { { CSI,     CSI"1" }, "C" },
	[KEY_LEFT]         = { { CSI,     CSI"1" }, "D" },
	[KEY_END]          = { { CSI,     CSI"1" }, "F" },
	[KEY_HOME]         = { { CSI,     CSI"1" }, "H" },
	[KEY_INSERT]       = { { CSI"2",  NULL   }, "~" },
	[KEY_DELETE]       = { { CSI"3",  NULL   }, "~" },
	[KEY_PAGE_UP]      = { { CSI"5",  NULL   }, "~" },
	[KEY_PAGE_DOWN]    = { { CSI"6",  NULL   }, "~" },

	[KEY_KP_UP]        = { { CSI,     CSI"1" }, "A" },
	[KEY_KP_DOWN]      = { { CSI,     CSI"1" }, "B" },
	[KEY_KP_RIGHT]     = { { CSI,     CSI"1" }, "C" },
	[KEY_KP_LEFT]      = { { CSI,     CSI"1" }, "D" },
	[KEY_KP_END]       = { { CSI,     CSI"1" }, "F" },
	[KEY_KP_HOME]      = { { CSI,     CSI"1" }, "H" },
	[KEY_KP_INSERT]    = { { CSI"2",  NULL   }, "~" },
	[KEY_KP_DELETE]    = { { CSI"3",  NULL   }, "~" },
	[KEY_KP_PAGE_UP]   = { { CSI"5",  NULL   }, "~" },
	[KEY_KP_PAGE_DOWN] = { { CSI"6",  NULL   }, "~" },

	[KEY_F1]           = { { CSI,     SS3"1" }, "P" },
	[KEY_F2]           = { { CSI,     SS3"1" }, "Q" },
	[KEY_F3]           = { { CSI,     SS3"1" }, "R" },
	[KEY_F4]           = { { CSI,     SS3"1" }, "S" },
	[KEY_F5]           = { { CSI"15", NULL   }, "~" },
	[KEY_F6]           = { { CSI"17", NULL   }, "~" },
	[KEY_F7]           = { { CSI"18", NULL   }, "~" },
	[KEY_F8]           = { { CSI"19", NULL   }, "~" },
	[KEY_F9]           = { { CSI"20", NULL   }, "~" },
	[KEY_F10]          = { { CSI"21", NULL   }, "~" },
	[KEY_F11]          = { { CSI"23", NULL   }, "~" },
	[KEY_F12]          = { { CSI"24", NULL   }, "~" },
	[KEY_F13]          = { { CSI"25", NULL   }, "~" },
	[KEY_F14]          = { { CSI"26", NULL   }, "~" },
	[KEY_F15]          = { { CSI"28", NULL   }, "~" },
	[KEY_F16]          = { { CSI"29", NULL   }, "~" },
	[KEY_F17]          = { { CSI"31", NULL   }, "~" },
	[KEY_F18]          = { { CSI"32", NULL   }, "~" },
	[KEY_F19]          = { { CSI"33", NULL   }, "~" },
	[KEY_F20]          = { { CSI"34", NULL   }, "~" }
};

static KeyString keys_appkeypad[] = {
	[KEY_KP_TAB]       = { { SS3, NULL }, "I" },
	[KEY_KP_ENTER]     = { { SS3, NULL }, "M" },
	[KEY_KP_SPACE]     = { { SS3, NULL }, " " },
	[KEY_KP_MULTIPLY]  = { { SS3, NULL }, "j" },
	[KEY_KP_ADD]       = { { SS3, NULL }, "k" },
	[KEY_KP_SEPARATOR] = { { SS3, NULL }, "l" },
	[KEY_KP_SUBTRACT]  = { { SS3, NULL }, "m" },
	[KEY_KP_DECIMAL]   = { { SS3, NULL }, "n" },
	[KEY_KP_DIVIDE]    = { { SS3, NULL }, "o" },
	[KEY_KP_EQUAL]     = { { SS3, NULL }, "X" },

	[KEY_KP_0]         = { { SS3, NULL }, "p" },
	[KEY_KP_1]         = { { SS3, NULL }, "q" },
	[KEY_KP_2]         = { { SS3, NULL }, "r" },
	[KEY_KP_3]         = { { SS3, NULL }, "s" },
	[KEY_KP_4]         = { { SS3, NULL }, "t" },
	[KEY_KP_5]         = { { SS3, NULL }, "u" },
	[KEY_KP_6]         = { { SS3, NULL }, "v" },
	[KEY_KP_7]         = { { SS3, NULL }, "w" },
	[KEY_KP_8]         = { { SS3, NULL }, "x" },
	[KEY_KP_9]         = { { SS3, NULL }, "y" }
};

static KeyString keys_appcursor[NUM_KEY] = {
	[KEY_UP]       = { { SS3"A", NULL }, NULL },
	[KEY_DOWN]     = { { SS3"B", NULL }, NULL },
	[KEY_RIGHT]    = { { SS3"C", NULL }, NULL },
	[KEY_LEFT]     = { { SS3"D", NULL }, NULL },
	[KEY_HOME]     = { { SS3"H", NULL }, NULL },
	[KEY_END]      = { { SS3"F", NULL }, NULL },

	[KEY_KP_UP]    = { { SS3"A", NULL }, NULL },
	[KEY_KP_DOWN]  = { { SS3"B", NULL }, NULL },
	[KEY_KP_RIGHT] = { { SS3"C", NULL }, NULL },
	[KEY_KP_LEFT]  = { { SS3"D", NULL }, NULL },
	[KEY_KP_HOME]  = { { SS3"H", NULL }, NULL },
	[KEY_KP_END]   = { { SS3"F", NULL }, NULL }
};

static inline KeyString *
key_lookup(int key, KeyString *table)
{
	KeyString *item = table + key;

	if (!item->prefix[0]) return NULL;

	return item;
}

size_t
key_get_sequence(uint key, uint mod, char *buf, size_t size)
{
	if (key > NUM_KEY - 1 || mod > NUM_MOD - 1)
		return 0;

	KeyString *item = NULL;
	size_t len = 0;

	if (!item && MODE_APPKEYPAD) {
		item = key_lookup(key, keys_appkeypad);
	}
	if (!item && MODE_APPCURSOR) {
		item = key_lookup(key, keys_appcursor);
	}
	if (!item) {
		char *str = NULL;

		switch (key) {
		case KEY_RETURN:
		case KEY_KP_ENTER:
			switch (mod) {
			case MOD_ALT:
				str = ESC"\r";
				break;
			case MOD_CTRL:
				str = CSI"27;5;13~";
				break;
			default:
				str = "\r";
				break;
			}
			break;
		case KEY_TAB:
		case KEY_KP_TAB:
			switch (mod) {
			case MOD_ALT:
				str = ESC"\t";
				break;
			case MOD_CTRL:
				str = CSI"27;5;13~";
				break;
			default:
				str = "\t";
				break;
			}
			break;
		case KEY_BACKSPACE:
			switch (mod) {
			case MOD_ALT:
				str = ESC"\177";
				break;
			default:
				str = "\177";
				break;
			}
			break;
		}
		if (str) {
			len = snprintf(buf, size-1, "%s", str);
		} else {
			item = key_lookup(key, keys_normal);
		}
	}

	if (item) {
		char *prefix = item->prefix[(mod > 0) ? 1 : 0];
		char *suffix = item->suffix;

		len = snprintf(buf, size - 1, "%s%s%s",
		    (prefix) ? prefix : item->prefix[0],
		    (mod && suffix) ? (char []){ ';', '0' + mod + 1 } : "",
		    (suffix) ? suffix : "");
	}

	ASSERT(!len || len < size);

	return len;
}

