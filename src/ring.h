#ifndef RING_H__
#define RING_H__

#include "defs.h"

struct Ring_ {
	int read, write;
	int count, limit;
	int pitch;
	uchar data[];
};

#define RING_FULL(r)  ((r)->count + 1 >= (r)->limit)
#define RING_EMPTY(r) ((r)->count == 0)

#define RING_INDEX(r,n)  (((r)->read + (n)) % (r)->limit)
#define RING_OFFSET(r,n) (RING_INDEX(r,n) * (r)->pitch)

struct Ring_ *ring_create(uint, uint);
void ring_destroy(struct Ring_ *);
int ring_advance(struct Ring_ *);

static inline uint
ringindex(const struct Ring_ *ring, int n)
{
	return (ring->read + n) % ring->limit;
}

static inline uint
ringoffset(const struct Ring_ *ring, int n)
{
	return ringindex(ring, n) * ring->pitch;
}

#endif
