#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "utils.h"
#include "ring.h"

#define ring__offset(r,i) ((i) * (r)->stride)

Ring *
ring_init(Ring *ring_, uint max, uint stride)
{
	Ring *ring = ring_;

	if (!ring) {
		ring = xmalloc(1, sizeof(*ring));
	}
	memclear(ring, 1, sizeof(*ring));

	size_t tmp = bitround(max, +1) + 1;
	ASSERT(tmp && tmp < INT32_MAX);
	ASSERT(stride < INT32_MAX);

	ring->max = tmp;
	ring->stride = DEFAULT(stride, 1);
	ring->data = xcalloc(ring->max, ring->stride);
	ring->write = ring->read = 1;
	ring->count = 0;

	return ring;
}

void
ring_free(Ring *ring)
{
	ASSERT(ring);
	ASSERT(ring->data && ring->max);

	FREE(ring->data);
}

void *
ring_push(Ring *ring, void *value)
{
	void *data = ring->data + ring__offset(ring, ring->write);

	if (value) {
		memcpy(data, value, ring->stride);
	}
	if (ring_count(ring) + 1 != ring->max) {
		ring->count++;
	} else {
		ring->read = (ring->read + 1) % ring->max;
	}
	ring->write = (ring->write + 1) % ring->max;

	return data;
}

void *
ring_data(Ring *ring, int index)
{
	return ring->data + ring__offset(ring, ring_index(ring, index));
}

int
ring_index(Ring *ring, int index)
{
	ASSERT(index >= 0 && index < ring->max);

	return (ring->read + index) % ring->max;
}

int
ring_count(Ring *ring)
{
	int count = ring->write - ring->read;

	return (count < 0) ? ring->max - 1 : count;
}

