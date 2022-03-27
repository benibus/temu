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

#ifndef APP_H__
#define APP_H__

#include "common.h"

typedef struct App App;

enum {
    APPPROP_NONE  = (0),
    APPPROP_TITLE = (1 << 0),
    APPPROP_ICON  = (1 << 1),
};

void app_set_properties(App *app, uint8 props, const char *str, size_t len);

static inline void
app_set_icon(App *app, const char *str, size_t len)
{
    app_set_properties(app, APPPROP_ICON, str, len);
}

static inline void
app_set_title(App *app, const char *str, size_t len)
{
    app_set_properties(app, APPPROP_TITLE, str, len);
}

#endif

