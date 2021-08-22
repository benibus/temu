#ifndef PTY_H__
#define PTY_H__

#include "term.h"

int pty_init(TTY *, const char *);
size_t pty_read(TTY *, uint8);
size_t pty_write(TTY *, const char *, size_t, uint8);
void pty_resize(const TTY *, int, int);

#endif

