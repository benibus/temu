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

#include <unistd.h>

#include "utils.h"
#include "init.h"

static char *
get_str(char *str)
{
    return (!strempty(str)) ? str : NULL;
}

// String to unsigned int, return 0 if error or result exceeds "max"
static uint
get_uint(char *str, uint max)
{
    char *err;
    ulong res = strtoul(str, &err, 10);
    return (!*err && res <= DEFAULT(max, UINT_MAX)) ? res : 0;
}

int
main(int argc, char **argv)
{
    Options opts = { 0 };

    // TODO(ben): Long options
    for (int opt; (opt = getopt(argc, argv, "T:N:C:S:F:f:b:l:c:r:s:")) != -1; ) {
        switch (opt) {
        case 'T': opts.wm_title  = get_str(optarg); break;
        case 'N': opts.wm_name   = get_str(optarg); break;
        case 'C': opts.wm_class  = get_str(optarg); break;
        case 'S': opts.shell     = get_str(optarg); break;
        case 'f': opts.font      = get_str(optarg); break;
        case 'F': opts.fontpath  = get_str(optarg); break;
        case 'b': opts.border    = get_uint(optarg, INT16_MAX); break;
        case 'l': opts.histlines = get_uint(optarg, INT16_MAX); break;
        case 'c': opts.cols      = get_uint(optarg, INT16_MAX); break;
        case 'r': opts.rows      = get_uint(optarg, INT16_MAX); break;
        case '?':
        case ':':
            goto error_invalid;
            break;
        }
        continue;
error_invalid:
        exit(1);
    }

    return app_main(&opts);
}

