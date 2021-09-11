#ifndef UTF8_H__
#define UTF8_H__

#include "defs.h"

#define UCS4_INVALID 0xfffd
#define UCS4_MAX     0x10ffff

uint8 utf8_decode(const void *, size_t, uint32 *, uint *);
uint ucs4str(uint32, char *, size_t);

#endif
