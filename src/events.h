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

#ifndef EVENTS_H__
#define EVENTS_H__

#include "common.h"
#include "platform.h"

enum {
    EVENT_NONE,
    EVENT_OPEN,
    EVENT_CLOSE,
    EVENT_KEYPRESS,
    EVENT_KEYRELEASE,
    EVENT_BUTTONPRESS,
    EVENT_BUTTONRELEASE,
    EVENT_POINTER,
    EVENT_MOVE,
    EVENT_RESIZE,
    EVENT_FOCUS,
    EVENT_UNFOCUS,
    EVENT_SELECT,
    EVENT_CLEARSELECT,
    EVENT_EXPOSE,
    NUM_EVENTS
};

typedef struct {
    uint32 tag;
    uint32 time;
    int32 error;
} WinEventInfo;

#define EVENTSIZE (56)
#define EVENTDEFN(...) union { struct { __VA_ARGS__ }; uchar pad__[EVENTSIZE]; }

typedef EVENTDEFN(
    WinEventInfo info;
    int32 width;
    int32 height;
    int32 x;
    int32 y;
) WinGeomEvent;

typedef EVENTDEFN(
    WinEventInfo info;
    uint32 key;
    uint32 mods;
    uint32 len;
    uchar data[32];
) WinKeyEvent;

typedef EVENTDEFN(
    WinEventInfo info;
    uint32 button;
) WinButtonEvent;

typedef union {
    uint32 tag;
    WinEventInfo   info;
    WinGeomEvent   as_geom;
    WinKeyEvent    as_key;
    WinButtonEvent as_button;
} WinEvent;

static_assert(sizeof(WinEvent) == EVENTSIZE, "Unexpected excess padding");
static_assert(sizeof(WinEvent) == sizeof(WinGeomEvent), "Mismatched padding");
static_assert(sizeof(WinEvent) == sizeof(WinKeyEvent), "Mismatched padding");
static_assert(sizeof(WinEvent) == sizeof(WinButtonEvent), "Mismatched padding");

#undef EVENTSIZE
#undef EVENTDEFN

typedef void WinEventHandler(void *arg, const WinEvent *event);

int window_pump_events(Win *win, WinEventHandler *handler, void *arg);
bool window_get_event(Win *win, bool flush, WinEvent *event);

static inline void
event_init(WinEvent *event, uint8 tag, uint32 time)
{
    ASSERT(tag < NUM_EVENTS);
    memset(event, 0, sizeof(*event));
    event->info.tag = tag;
    event->info.time = DEFAULT(time, timer_msec(NULL));
}

static inline const char *
event_to_string(uint8 tag)
{
#define XTABLE_EVENTS \
    X_(NONE)          \
    X_(OPEN)          \
    X_(CLOSE)         \
    X_(KEYPRESS)      \
    X_(KEYRELEASE)    \
    X_(BUTTONPRESS)   \
    X_(BUTTONRELEASE) \
    X_(POINTER)       \
    X_(MOVE)          \
    X_(RESIZE)        \
    X_(FOCUS)         \
    X_(UNFOCUS)       \
    X_(SELECT)        \
    X_(CLEARSELECT)   \
    X_(EXPOSE)

#define X_(x) [EVENT_##x] = #x,
    static const char *const strings[NUM_EVENTS] = { XTABLE_EVENTS };
#undef X_
    ASSERT(tag < LEN(strings));
    return strings[tag];
#undef XTABLE_EVENTS
}

#endif

