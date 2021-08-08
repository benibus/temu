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

#if 0
struct RingHeader_ {
	int count, max;
	int read, write;
	uchar data[];
};

#define ringt__(p) ((struct RingHeader_ *)((uchar *)(p) - offsetof(struct RingHeader_, data)))

#define ringt_init(p,n) ()

void *
ringt_alloc(const void *data, uint max, uint stride)
{
	struct RingHeader_ *hdr;
	size_t count, size;

	count = bitround(max, +1) + 1;
	stride = DEFAULT(stride, 1);
	ASSERT(count && count < INT32_MAX);
	ASSERT(stride < INT32_MAX);

	size = offsetof(struct RingHeader_, data) + count * stride;
}

void
hist_reset(void)
{
	HistBuf *h = &tty.hist;

	memclear(h->buf, h->max, sizeof(*h->buf));
	h->r = 1;
	h->w = 1;
	h->lap = 0;
}

void *
hist_init(size_t max)
{
	tty.hist.max = max;
	tty.hist.buf = MALLOC(tty.hist.buf, tty.hist.max);
	hist_reset();

	return tty.hist.buf;
}

bool
hist_is_full(void)
{
	HistBuf h = tty.hist;

	return (h.w + 1 == h.r || (h.w + 1 == h.max && !h.r));
}

bool
hist_is_empty(void)
{
	HistBuf h = tty.hist;

	return (h.r == h.w);
}

int
hist_get_size(void)
{
	HistBuf h = tty.hist;

	return (hist_is_full()) ? h.max :
	    ((h.w < h.r) ? h.max - h.r + h.w : h.w - h.r);
}

void
hist_append(int val)
{
	HistBuf *h = &tty.hist;

	h->buf[h->w] = val;

	if (hist_is_full()) {
		h->r = wrap(h->r, h->max);
	}
	h->w = wrap(h->w, h->max);

	h->lap += (h->w == 1);
}

void
hist_decrement(void)
{
	if (hist_is_empty()) return;

	HistBuf *h = &tty.hist;

	h->w = WRAP_INV(h->w, h->max);
	h->buf[h->w] = 0;
}

void
hist_delete_entry(int row, int shift)
{
	if (hist_is_empty()) return;

	HistBuf *h = &tty.hist;

	int end = WRAP_INV(h->w, h->max);
	int last = 0;

	assert(BETWEEN(row, h->buf[h->r], h->buf[end]));

	for (int i = end; h->buf[i] >= row; i = WRAP_INV(i, h->max)) {
		int tmp = h->buf[i];
		if (tmp == last) break;

		h->buf[i] = last;
		last = tmp;

		if (tmp == row || i == h->r) break;
	}

	hist_decrement();
}

void
hist_iterate(hist_set_fn_ action, int val)
{
	HistBuf *h = &tty.hist;

	for (int n = 0, i = h->r; i != h->w; n++, i = WRAP(i, h->max)) {
		action(n, i, &h->buf[i], val);
	}
}

void
hist_iterate_rev(hist_set_fn_ action, int val)
{
	HistBuf *h = &tty.hist;

	int n = MAX(0, hist_get_size());
	for (int i = WRAP_INV(h->w, h->max); n-- > 0; i = WRAP_INV(i, h->max)) {
		action(n, i, &h->buf[i], val);

		if (i == h->r) break;
	}
}

void
hist_shift_from_row(int row, int diff)
{
	HistBuf *h = &tty.hist;

	if (hist_is_empty()) return;

	for (int i = WRAP_INV(h->w, h->max); h->buf[i] >= row; i = WRAP_INV(i, h->max)) {
		h->buf[i] += diff;

		if (i == h->r) break;
	}
}

int
hist_get_value(int val)
{
	HistBuf h = tty.hist;
	int index = val;

	if (index < 0 || index >= hist_get_size()) {
		return 0;
	}
	index += (h.r > h.w && index >= h.max - h.r) ? -(h.max - h.r) : h.r;

	return h.buf[index];
}

int
hist_get_first(void)
{
	HistBuf h = tty.hist;

	return h.buf[h.r];
}

int
hist_get_last(void)
{
	HistBuf h = tty.hist;

	return h.buf[WRAP_INV(h.w, h.max)];
}
#endif

