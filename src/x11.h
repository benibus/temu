#ifndef X11_H__
#define X11_H__

#include "defs.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

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
	struct XRFillColor {
		int64 xid;
		uint32 color;
	} fillcache[16];
} WinData;

#define XRENDER_ARGB(argb) ( \
  (XRenderColor){                       \
    .alpha = (argb & 0xff000000) >> 16, \
    .red   = (argb & 0x00ff0000) >>  8, \
    .green = (argb & 0x0000ff00) >>  0, \
    .blue  = (argb & 0x000000ff) <<  8  \
  }                                     \
)

#define XRENDER_COLOR(color) XRENDER_ARGB(color)

#endif
