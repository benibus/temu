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

	// A more workable representation of the hardware's 16-bit RGB color format
	struct RGBMask {
		uint16 mask; // right-aligned mask
		uint8 off;   // offset from right to start of mask
		uint8 len;   // length of mask from start of mask
	} red, green, blue;
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

#define XCOLOR(color) (       \
  (XRenderColor){             \
    .red   = (color)->r << 8, \
    .green = (color)->g << 8, \
    .blue  = (color)->b << 8, \
    .alpha = (color)->a << 8  \
  }                           \
)

#endif
