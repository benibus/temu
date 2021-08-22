#ifndef X11_H__
#define X11_H__

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

#include "types.h"
#include "window.h"

typedef struct {
	Display *dpy;
	int screen;
	Window root;
	int fd;
	XIM im;
	XVisualInfo *vis;
	XRenderPictFormat *fmt;
	uint maxw, maxh;
	double dpi;
} X11;

typedef struct {
	Win pub;
	X11 *x11;
	Window xid, parent;
	Colormap colormap;
	Pixmap buf;
	Picture pic;
	XIC ic;
	GC gc;
} WinData;

#endif
