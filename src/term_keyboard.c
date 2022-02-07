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

#include <ctype.h>

#include "utils.h"
#include "keycodes.h"
#include "term_private.h"

// temporary
#define MODE_APPKEYPAD 0
#define MODE_APPCURSOR 0

#define PARAM_MASK (KEYMOD_SHIFT|KEYMOD_ALT|KEYMOD_CTRL)
#define KEYBUF_MAX 128

static inline uint8
mods_to_param(uint mods)
{
    return (mods &= PARAM_MASK) ? mods + 1 : 0;
}

#if 0
static inline bool
resolve_shift(uint mods)
{
    const bool shift = (mods & KEYMOD_SHIFT);
    const bool numlk = (mods & KEYMOD_NUMLK);

    return (numlk) ? shift : !shift;
}
#endif

static bool
resolve_appkeypad(uint mods, uint flags)
{
    const bool shift = (mods & KEYMOD_SHIFT);
    const bool numlk = (mods & KEYMOD_NUMLK);

    return !numlk && (flags & MODE_APPKEYPAD) ? !shift : shift;
}

static uint
remap_keypad(uint key, bool appkp)
{
    switch (key) {
    case KeyKPUp:     return (appkp) ? KeyKP8 : KeyUp;
    case KeyKPDown:   return (appkp) ? KeyKP2 : KeyDown;
    case KeyKPRight:  return (appkp) ? KeyKP6 : KeyRight;
    case KeyKPLeft:   return (appkp) ? KeyKP4 : KeyLeft;
    case KeyKPBegin:  return (appkp) ? KeyKP5 : KeyBegin;
    case KeyKPEnd:    return (appkp) ? KeyKP1 : KeyEnd;
    case KeyKPHome:   return (appkp) ? KeyKP7 : KeyHome;
    case KeyKPInsert: return (appkp) ? KeyKP0 : KeyInsert;
    case KeyKPDelete: return (appkp) ? KeyKPDecimal : KeyDelete;
    case KeyKPPgUp:   return (appkp) ? KeyKP9 : KeyPgUp;
    case KeyKPPgDown: return (appkp) ? KeyKP3 : KeyPgDown;
    case KeyKPTab:    return (appkp) ? key : KeyTab;
    case KeyKPEnter:  return (appkp) ? key : KeyReturn;
    case KeyKPSpace:  return (appkp) ? key : ' ';
    case KeyKPEqual:  return (appkp) ? key : '=';
    }

    return key;
}

#define ESC "\x1b"
#define CSI ESC"["
#define SS3 ESC"O"

#define PARAM_BYTE '\1'
#define P "\1" /* Expands to parameters in post-processing */

static const char *
query_substitute(uint key, uint mods, uint flags)
{
    const bool appkp = resolve_appkeypad(mods, flags);
    key = remap_keypad(key, appkp);

    if (appkp) {
        switch (key) {
        case KeyKPSpace:     return SS3 " ";
        case KeyKPTab:       return SS3 "I";
        case KeyKPEnter:     return SS3 "M";
        case KeyKPMultiply:  return SS3 "j";
        case KeyKPAdd:       return SS3 "k";
        case KeyKPSeparator: return SS3 "l";
        case KeyKPSubtract:  return SS3 "m";
        case KeyKPDecimal:   return SS3 "n";
        case KeyKPDivide:    return SS3 "o";
        case KeyKP0:         return SS3 "p";
        case KeyKP1:         return SS3 "q";
        case KeyKP2:         return SS3 "r";
        case KeyKP3:         return SS3 "s";
        case KeyKP4:         return SS3 "t";
        case KeyKP5:         return SS3 "u";
        case KeyKP6:         return SS3 "v";
        case KeyKP7:         return SS3 "w";
        case KeyKP8:         return SS3 "x";
        case KeyKP9:         return SS3 "y";
        case KeyKPEqual:     return SS3 "X";
        }
    }

    const bool appcrs = (flags & MODE_APPCURSOR);

    switch (key) {
    case KeyUp:    return (appcrs) ? SS3 "A" : CSI P "A";
    case KeyDown:  return (appcrs) ? SS3 "B" : CSI P "B";
    case KeyRight: return (appcrs) ? SS3 "C" : CSI P "C";
    case KeyLeft:  return (appcrs) ? SS3 "D" : CSI P "D";
    case KeyBegin: return (appcrs) ? SS3 "E" : CSI P "E";
    case KeyEnd:   return (appcrs) ? SS3 "F" : CSI P "F";
    case KeyHome:  return (appcrs) ? SS3 "H" : CSI P "H";

    case KeyInsert: return CSI "2" P "~";
    case KeyDelete: return CSI "3" P "~";
    case KeyPgUp:   return CSI "5" P "~";
    case KeyPgDown: return CSI "6" P "~";

    case KeyF1:  return SS3 P "P";
    case KeyF2:  return SS3 P "Q";
    case KeyF3:  return SS3 P "R";
    case KeyF4:  return SS3 P "S";
    case KeyF5:  return CSI "15" P "~";
    case KeyF6:  return CSI "17" P "~";
    case KeyF7:  return CSI "18" P "~";
    case KeyF8:  return CSI "19" P "~";
    case KeyF9:  return CSI "20" P "~";
    case KeyF10: return CSI "21" P "~";
    case KeyF11: return CSI "23" P "~";
    case KeyF12: return CSI "24" P "~";
    case KeyF13: return CSI "25" P "~";
    case KeyF14: return CSI "26" P "~";
    case KeyF15: return CSI "28" P "~";
    case KeyF16: return CSI "29" P "~";
    case KeyF17: return CSI "31" P "~";
    case KeyF18: return CSI "32" P "~";
    case KeyF19: return CSI "33" P "~";
    case KeyF20: return CSI "34" P "~";
    }

    if (mods && !(mods & KEYMOD_ALT) && (key == KeyReturn || key == KeyTab)) {
        return CSI "27" P ";13~";
    } else if (!(mods & KEYMOD_CTRL) && key == KeyBackspace) {
        return "\x7f"; // Delete
    }

    return NULL;
}

#undef ESC
#undef CSI
#undef SS3
#undef P

static size_t
parse_sequence(const char *restrict str, uint mods, uchar *restrict buf, size_t max)
{
    if (!str || !buf) {
        return 0;
    }

    const uint8 param = mods_to_param(mods);
    size_t len = 0;

    // Copy while expanding modifier parameters
#define PUSH(c) ((++len <= max) ? (buf[len-1] = (c)) : 0)
    for (size_t i = 0; str[i]; i++) {
        if (str[i] != PARAM_BYTE) {
            PUSH(str[i]);
        } else if (param) {
            ASSERT(i > 0);
            if (!isdigit(str[i-1])) {
                PUSH('1');
            }
            PUSH(';');
            PUSH('0' + param);
        }
    }
#undef PUSH

    // Special handling of ALT for single-byte sequences
    if (len == 1 && (mods & KEYMOD_ALT) && ++len <= max) {
        buf[1] = buf[0];
        buf[0] = '\x1b';
    }

    return len;
}

size_t
term_push_input(Term *term, uint key, uint mods, const uchar *text, size_t len)
{
    static_assert(PARAM_MASK <= 8, "Key modifier masks cannot exceed 8");
    ASSERT(key < KeyCount);
    ASSERT(!(mods & ~KEYMOD_MASK));

    // Check for a pre-defined function key sequence before anything else
    const char *subst = query_substitute(key, mods, 0);

    // Handle pre-defined sequence
    if (subst) {
        uchar buf[KEYBUF_MAX] = { 0 };
        const size_t n = parse_sequence(subst, mods, buf, sizeof(buf));
        if (n && n <= sizeof(buf)) {
            return term_push(term, buf, n);
        }
    // Fall back to the raw input (either standard text or an unknown function key)
    } else if (len == 1 && (mods & KEYMOD_ALT)) {
        return term_push(term, (uchar [2]){ '\x1b', text[0] }, 2);
    } else if (len) {
        return term_push(term, text, len);
    }

    return 0;
}

#undef PARAM_BYTE

