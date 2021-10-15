#ifndef PTY_H__
#define PTY_H__

#include "defs.h"
#include "terminal.h"

int pty_init(const char *, int *, int *);
size_t pty_read(int, uchar *, size_t);
size_t pty_write(int, const uchar *, size_t);
void pty_resize(int, int, int, int, int);

#endif

