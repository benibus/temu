#include "utils.h"
#include "ring.h"

#define DATAOFF offsetof(struct Ring_, data)

struct Ring_ *
ring_create(uint count, uint pitch)
{
	uint limit = count + 1;
	uint size  = pitch * limit;

	struct Ring_ *ring = xcalloc(DATAOFF + size, 1);
	ring->limit = limit;
	ring->pitch = pitch;
	ring->count = 0;
	ring->read  = 1;
	ring->write = 1;

	return ring;
}

void
ring_destroy(struct Ring_ *ring)
{
	memset(ring, 0, DATAOFF + ring->pitch * ring->limit);
	free(ring);
}

int
ring_advance(struct Ring_ *ring)
{
	int net = 0;

	if (!RING_FULL(ring)) {
		ring->count++;
		net++;
	} else {
		ring->read = (ring->read + 1) % ring->limit;
		net--;
	}
	ring->write = (ring->write + 1) % ring->limit;

	return net;
}

