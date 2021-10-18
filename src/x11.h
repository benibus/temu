#ifndef X11_H__
#define X11_H__

#include "defs.h"
#include "window.h"
#include "surface.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

struct WinServer {
	Display *dpy;
	int screen;
	Window root;
	Visual *visual;
	XIM im;
	Colormap colormap;
	int fd;
	int dpy_width;
	int dpy_height;
	float dpi;
	int depth;
};

struct WinClient {
	WinSurface *surface;
	WinServer *server;
	Window xid;
	XIC ic;
	GC gc;
	bool online;
	int pid;
	int xpos;
	int ypos;
	int width;
	int height;
	int border;
	struct {
		void *generic;
		WinCallbackResize   resize;
		WinCallbackKeyPress keypress;
		WinCallbackExpose   expose;
	} callbacks;
};

void x11_query_dimensions(WinClient *win, int *width, int *height, int *border);
void x11_query_coordinates(WinClient *win, int *xpos, int *ypos);

#endif
