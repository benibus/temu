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

#ifndef KEYS_H__
#define KEYS_H__

#define KEYCODE_TABLE \
    X_(None,        0)   \
    X_(Escape,      256) \
    X_(Return,      257) \
    X_(Tab,         258) \
    X_(Backspace,   259) \
    X_(Up,          260) \
    X_(Down,        261) \
    X_(Right,       262) \
    X_(Left,        263) \
    X_(Begin,       364) \
    X_(End,         265) \
    X_(Home,        266) \
    X_(Insert,      267) \
    X_(Delete,      268) \
    X_(PgUp,        269) \
    X_(PgDown,      270) \
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
    X_(KPSpace,     320) \
    X_(KPTab,       321) \
    X_(KPEnter,     322) \
    X_(KPMultiply,  323) \
    X_(KPAdd,       324) \
    X_(KPSeparator, 325) \
    X_(KPSubtract,  326) \
    X_(KPDecimal,   327) \
    X_(KPDivide,    328) \
    X_(KP0,         329) \
    X_(KP1,         330) \
    X_(KP2,         331) \
    X_(KP3,         332) \
    X_(KP4,         333) \
    X_(KP5,         334) \
    X_(KP6,         335) \
    X_(KP7,         336) \
    X_(KP8,         337) \
    X_(KP9,         338) \
    X_(KPEqual,     339) \
    X_(KPUp,        340) \
    X_(KPDown,      341) \
    X_(KPRight,     342) \
    X_(KPLeft,      343) \
    X_(KPBegin,     344) \
    X_(KPEnd,       345) \
    X_(KPHome,      346) \
    X_(KPInsert,    347) \
    X_(KPDelete,    348) \
    X_(KPPgUp,      349) \
    X_(KPPgDown,    350) \

enum {
#define X_(key,val) Key##key = val,
    KEYCODE_TABLE
#undef  X_
    KeyCount
};

static const char *keycode_names[] = {
#define X_(key,val) [Key##key] = #key,
    KEYCODE_TABLE
#undef  X_
};

#undef KEYCODE_TABLE

#define KEYMOD_NONE  (0)
#define KEYMOD_SHIFT (1 << 0)
#define KEYMOD_ALT   (1 << 1)
#define KEYMOD_CTRL  (1 << 2)
#define KEYMOD_NUMLK (1 << 3)
#define KEYMOD_MASK  (KEYMOD_CTRL|KEYMOD_ALT|KEYMOD_SHIFT|KEYMOD_NUMLK)

#endif
