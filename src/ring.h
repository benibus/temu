#ifndef RING_H__
#define RING_H__

#include "types.h"

typedef struct Ring_ Ring;
typedef struct Ring_ RingBuf;

struct Ring_ {
	int read, write;
	int count, max;
	int stride;
	uchar *data;
};

Ring *ring_init(Ring *, uint, uint);
void ring_free(Ring *);
void *ring_push(Ring *, void *);
int ring_count(Ring *);
int ring_index(Ring *, int);
void *ring_data(Ring *, int);

#define RING_FULL(r)  ((r)->count + 1 >= (r)->max)
#define RING_EMPTY(r) ((r)->count == 0)

#endif
