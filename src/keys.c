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
#include "terminal.h"
#include "keymap.h"

// temporary
#define MODE_APPKEYPAD 0
#define MODE_APPCURSOR 0
#define MODE_NUMLOCK 0

#define ESC "\033"
#define CSI ESC"["
#define SS3 ESC"O"

typedef struct KeyString {
    char *prefix[2];
    char *suffix;
} KeyString;

static KeyString keys_normal[KeyCount] = {
    [KeyUp]          = { { CSI,     CSI"1" }, "A" },
    [KeyDown]        = { { CSI,     CSI"1" }, "B" },
    [KeyRight]       = { { CSI,     CSI"1" }, "C" },
    [KeyLeft]        = { { CSI,     CSI"1" }, "D" },
    [KeyEnd]         = { { CSI,     CSI"1" }, "F" },
    [KeyHome]        = { { CSI,     CSI"1" }, "H" },
    [KeyInsert]      = { { CSI"2",  NULL   }, "~" },
    [KeyDelete]      = { { CSI"3",  NULL   }, "~" },
    [KeyPageUp]      = { { CSI"5",  NULL   }, "~" },
    [KeyPageDown]    = { { CSI"6",  NULL   }, "~" },

    [KeyKPUp]        = { { CSI,     CSI"1" }, "A" },
    [KeyKPDown]      = { { CSI,     CSI"1" }, "B" },
    [KeyKPRight]     = { { CSI,     CSI"1" }, "C" },
    [KeyKPLeft]      = { { CSI,     CSI"1" }, "D" },
    [KeyKPEnd]       = { { CSI,     CSI"1" }, "F" },
    [KeyKPHome]      = { { CSI,     CSI"1" }, "H" },
    [KeyKPInsert]    = { { CSI"2",  NULL   }, "~" },
    [KeyKPDelete]    = { { CSI"3",  NULL   }, "~" },
    [KeyKPPageUp]    = { { CSI"5",  NULL   }, "~" },
    [KeyKPPageDown]  = { { CSI"6",  NULL   }, "~" },

    [KeyF1]          = { { CSI,     SS3"1" }, "P" },
    [KeyF2]          = { { CSI,     SS3"1" }, "Q" },
    [KeyF3]          = { { CSI,     SS3"1" }, "R" },
    [KeyF4]          = { { CSI,     SS3"1" }, "S" },
    [KeyF5]          = { { CSI"15", NULL   }, "~" },
    [KeyF6]          = { { CSI"17", NULL   }, "~" },
    [KeyF7]          = { { CSI"18", NULL   }, "~" },
    [KeyF8]          = { { CSI"19", NULL   }, "~" },
    [KeyF9]          = { { CSI"20", NULL   }, "~" },
    [KeyF10]         = { { CSI"21", NULL   }, "~" },
    [KeyF11]         = { { CSI"23", NULL   }, "~" },
    [KeyF12]         = { { CSI"24", NULL   }, "~" },
    [KeyF13]         = { { CSI"25", NULL   }, "~" },
    [KeyF14]         = { { CSI"26", NULL   }, "~" },
    [KeyF15]         = { { CSI"28", NULL   }, "~" },
    [KeyF16]         = { { CSI"29", NULL   }, "~" },
    [KeyF17]         = { { CSI"31", NULL   }, "~" },
    [KeyF18]         = { { CSI"32", NULL   }, "~" },
    [KeyF19]         = { { CSI"33", NULL   }, "~" },
    [KeyF20]         = { { CSI"34", NULL   }, "~" }
};

static KeyString keys_appkeypad[KeyCount] = {
    [KeyKPTab]       = { { SS3, NULL }, "I" },
    [KeyKPEnter]     = { { SS3, NULL }, "M" },
    [KeyKPSpace]     = { { SS3, NULL }, " " },
    [KeyKPMultiply]  = { { SS3, NULL }, "j" },
    [KeyKPAdd]       = { { SS3, NULL }, "k" },
    [KeyKPSeparator] = { { SS3, NULL }, "l" },
    [KeyKPSubtract]  = { { SS3, NULL }, "m" },
    [KeyKPDecimal]   = { { SS3, NULL }, "n" },
    [KeyKPDivide]    = { { SS3, NULL }, "o" },
    [KeyKPEqual]     = { { SS3, NULL }, "X" },

    [KeyKP0]         = { { SS3, NULL }, "p" },
    [KeyKP1]         = { { SS3, NULL }, "q" },
    [KeyKP2]         = { { SS3, NULL }, "r" },
    [KeyKP3]         = { { SS3, NULL }, "s" },
    [KeyKP4]         = { { SS3, NULL }, "t" },
    [KeyKP5]         = { { SS3, NULL }, "u" },
    [KeyKP6]         = { { SS3, NULL }, "v" },
    [KeyKP7]         = { { SS3, NULL }, "w" },
    [KeyKP8]         = { { SS3, NULL }, "x" },
    [KeyKP9]         = { { SS3, NULL }, "y" }
};

static KeyString keys_appcursor[KeyCount] = {
    [KeyUp]          = { { SS3"A", NULL }, NULL },
    [KeyDown]        = { { SS3"B", NULL }, NULL },
    [KeyRight]       = { { SS3"C", NULL }, NULL },
    [KeyLeft]        = { { SS3"D", NULL }, NULL },
    [KeyHome]        = { { SS3"H", NULL }, NULL },
    [KeyEnd]         = { { SS3"F", NULL }, NULL },

    [KeyKPUp]        = { { SS3"A", NULL }, NULL },
    [KeyKPDown]      = { { SS3"B", NULL }, NULL },
    [KeyKPRight]     = { { SS3"C", NULL }, NULL },
    [KeyKPLeft]      = { { SS3"D", NULL }, NULL },
    [KeyKPHome]      = { { SS3"H", NULL }, NULL },
    [KeyKPEnd]       = { { SS3"F", NULL }, NULL }
};

static inline KeyString *
key_lookup(int key, KeyString *table)
{
    KeyString *item = &table[key];

    if (!item->prefix[0]) return NULL;

    return item;
}

size_t
term_make_key_string(const Term *term, uint key, uint mod, char *buf, size_t size)
{
    UNUSED(term);

    static_assert(ModCount <= 9, "Key modifier masks cannot exceed 8");

    if (key >= KeyCount || mod >= ModCount) {
        return 0;
    }

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
        case KeyReturn:
        case KeyKPEnter:
            switch (mod) {
            case ModAlt:
                str = ESC"\r";
                break;
            case ModCtrl:
                str = CSI"27;5;13~";
                break;
            default:
                str = "\r";
                break;
            }
            break;
        case KeyTab:
        case KeyKPTab:
            switch (mod) {
            case ModAlt:
                str = ESC"\t";
                break;
            case ModCtrl:
                str = CSI"27;5;13~";
                break;
            default:
                str = "\t";
                break;
            }
            break;
        case KeyBackspace:
            switch (mod) {
            case ModAlt:
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
        char *prefix = item->prefix[(mod) ? 1 : 0];
        char *suffix = item->suffix;

        len = snprintf(buf, size - 1, "%s%s%s",
            (prefix) ? prefix : item->prefix[0],
            (mod && suffix) ? (char [3]){ ';', '0' + mod + 1 } : "",
            (suffix) ? suffix : ""
        );
    }

    ASSERT(!len || len < size);

    return len;
}

