#ifndef SURFACE_H__
#define SURFACE_H__

#include "defs.h"
#include "window.h"

typedef struct WinSurface WinSurface;

WinSurface *surface_create(WinClient *);
void surface_resize(WinSurface *, int, int);
void surface_render(WinSurface *);

#endif

