#ifndef PTY_H__
#define PTY_H__

#include "defs.h"
#include "terminal.h"

int pty_init(Term *, const char *);
size_t pty_read(Term *);
size_t pty_write(Term *, const char *, size_t);
void pty_resize(const Term *, int, int);

#endif

