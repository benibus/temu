#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "utf8.h"

uint8
utf8_decode(const void *data, uint32 *res, uint *err)
{
	static const uint8 lengths[1<<5] = {
		1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1,
		0, 0, 0, 0, 0, 0, 0, 0,
		2, 2, 2, 2, 3, 3, 4, 0
	};

	const uchar *s = data;
	uint8 bytes = lengths[s[0]>>3];
	uint8 n = 0, mask = 0x00;
	uint32 c = '\0';

	switch (bytes) {
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
	*err |= (bytes - n) << 0; // chars remaining after leading byte
	*err |= (!bytes) << 2; // leading byte invalid
	*err |= (c > 0x10ffff) << 3; // out of range
	*err |= ((c >> 11) == 0x1b) << 4; // utf-16 surrogate half

	*res = c;
	return n + !n;
}

