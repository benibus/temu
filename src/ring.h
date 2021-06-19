#ifndef RING_H__
#define RING_H__

typedef void (*hist_set_fn_)(int, int, void *, int);

void *hist_init(size_t);
void  hist_reset(void);
void  hist_append(int);
void  hist_decrement(void);
void  hist_delete_entry(int, int);
void  hist_iterate(hist_set_fn_, int);
void  hist_iterate_rev(hist_set_fn_, int);
void  hist_shift_from_row(int, int);
int   hist_get_size(void);
int   hist_get_value(int);
int   hist_get_first(void);
int   hist_get_last(void);
bool  hist_is_full(void);
bool  hist_is_empty(void);

#endif
