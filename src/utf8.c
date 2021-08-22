#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "utf8.h"
#include "ascii.h"

static const uint8 sizetable[1<<5] = {
	1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 0, 0,
	2, 2, 2, 2, 3, 3, 4, 0
};

uint8
utf8_decode(const void *data, size_t max, uint32 *res, uint *err)
{
	const uchar *s = data;
	uint8 size = sizetable[s[0]>>3];
	uint8 n = 0, mask = 0x00;
	uint32 c = '\0';

	if (size > max) return 0;

	switch (size) {
		case 4: {
			mask |= 0x07;
			c |= (s[n++] & mask) << 18;
			mask |= 0x3f;
			if ((s[n] & 0xc0) ^ 0x80) break;
		}
		// fallthrough
		case 3: {
			mask |= 0x0f;
			c |= (s[n++] & mask) << 12;
			mask |= 0x3f;
			if ((s[n] & 0xc0) ^ 0x80) break;
		}
		// fallthrough
		case 2: {
			mask |= 0x1f;
			c |= (s[n++] & mask) << 6;
			mask |= 0x3f;
			if ((s[n] & 0xc0) ^ 0x80) break;
			c |= (s[n++] & mask) << 0;
			break;
		}
		case 1: {
			mask |= 0x7f;
			c |= (s[n++] & mask) << 0;
			break;
		}
		default: {
			break;
		}
	}

	// errors (0 is good)
	*err = 0;
	*err |= (size - n) << 0; // chars remaining after leading byte
	*err |= (!size) << 2; // leading byte invalid
	*err |= (c > UCS4_MAX) << 3; // out of range
	*err |= ((c >> 11) == 0x1b) << 4; // utf-16 surrogate half

	*res = (!*err) ? c : UCS4_INVALID;

	return n + !n;
}

uint
ucs4str(uint32 ucs4, char *buf, size_t max)
{
	if (max < 2) return 0;

	char *str = (ucs4 <= 128) ? ascii_db[ucs4].str : NULL;
	uint len = 0;

	if (str) {
		len = snprintf(buf, max - 1, "%s", str);
	} else {
		len = snprintf(buf, max - 1, "%#.2x", ucs4);
	}

	return len;
}

