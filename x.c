#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#include "utils.h"

#include "x.h"
#include "term.h"

#define LOGGING(f_) ( log_flags & LOG_##f_ )
#define FILE_IN "tests/readme1.txt"

enum WM_ATOMS { WM_Protocols, WM_Delete, WM_Name, WM_State, NUM_WM };
struct X11 {
	Display *dpy;
	Window root;
	int sid; // screen ID
	Visual *visual;
	Colormap colormap;
	XSetWindowAttributes attr;
	Atom atoms[NUM_WM];
	struct {
		XIM im; // input method
		XIC ic; // input context
		XPoint spot;
		XVaNestedList spotlist;
	} ime;
	int w, h;
	char *fontstr;
	char *wmtitle, *wmname, *wmclass;
};

enum { COLOR_BG, COLOR_FG, NUM_COLOR };
static struct {
	XftFont *font;
	FcPattern *pattern;
	XftColor color[NUM_COLOR];
} rsc = { 0 }; // global resources

struct Win {
	Window wid;
	Drawable map;
	XftDraw *render;
	GC gc;
	int x, y;       // window coordinates
	int w, h;       // width/height (pixels)
	int dw, dh;     // delta of width/height (pixels)
	int cols, rows; // number of columns/rows
	int bpx;        // border width (pixels)
	bool up;        // session status
} Win;

static void draw_clear(uint, uint);
static void draw_cursor(Cell, uint, uint);
static void draw_range(uint, uint, uint, uint);
static void draw(void);
static void x_resize(int, int);
static bool x_init_inputmethod(void);
static bool x_init_resources(void);

static void ximcb_create(Display *, XPointer, XPointer);
static void ximcb_destroy(XIM, XPointer, XPointer);
static void xiccb_destroy(XIM, XPointer, XPointer);

static int x_read_string(const char *, size_t);

static ulong rgb2ul(u8, u8, u8);

static void log__xevent(XEvent *);

static struct X11 x11 = { 0 }; // X11 Defaults
static struct Win win = { 0 }; // Main session
TTY tty = { 0 };
PTY pty = { 0 };

struct XEventType {
	const char *name;
	void (*handler)(XEvent *);
};

static const char ascii__[] =
    " !\"#$%&'()*+,-./0123456789:;<=>?"
    "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
    "`abcdefghijklmnopqrstuvwxyz{|}~";

// Handled XEvent types
#define X_XEVENTS \
  X(KeyPress, keypress)                 \
  X(ButtonPress, buttonpress)           \
  X(ButtonRelease, buttonrelease)       \
  X(MotionNotify, motionnotify)         \
  X(FocusIn, focusin)                   \
  X(FocusOut, focusout)                 \
  X(Expose, expose)                     \
  X(VisibilityNotify, visibilitynotify) \
  X(DestroyNotify, destroynotify)       \
  X(UnmapNotify, unmapnotify)           \
  X(ConfigureNotify, configurenotify)   \
  X(ConfigureRequest, configurerequest) \
  X(PropertyNotify, propertynotify)     \
  X(SelectionRequest, selectionrequest) \
  X(SelectionNotify, selectionnotify)   \
  X(ClientMessage, clientmessage)

// Expands to XEvent handler-function prototypes
#define X(event_,func_) static void xev_##func_(XEvent *);
X_XEVENTS
#undef X
// Expands to XEventType lookup-array
static const struct XEventType xevtypes[LASTEvent] = {
#define X(event_,func_) [event_] = { #event_, xev_##func_ },
	X_XEVENTS
#undef X
};

#define COL2PX(col_,bpx_) ( (2 * (bpx_)) + ((col_) * win.dw) )
#define ROW2PX(row_,bpx_) ( (2 * (bpx_)) + ((row_) * win.dh) )
#define PX2COL(pxw_,bpx_) ( ((pxw_) - (2 * (bpx_))) / win.dw )
#define PX2ROW(pxh_,bpx_) ( ((pxh_) - (2 * (bpx_))) / win.dh )

#define RGB2NUM(r_,g_,b_) ( (b_) + ((g_) << 8) + ((r_) << 16) )

#define TIME_DIFF(t1_,t2_) \
 ( (((t1_).tv_sec  - (t2_).tv_sec)  * 1E3) + \
   (((t1_).tv_nsec - (t2_).tv_nsec) / 1E6) )

ulong
rgb2ul(u8 r, u8 g, u8 b)
{
	return (b + (g << 8) + (r << 16));
}

void
x_configure(struct WinConfig config)
{
	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");

	win.x       = config.x;
	win.y       = config.y;
	win.cols    = config.cols;
	win.rows    = config.rows;
	win.bpx     = config.borderpx;
	x11.fontstr = config.fontstr;
	x11.wmtitle = config.wmtitle;
	x11.wmname  = config.wmname;
	x11.wmclass = config.wmclass;
}

void
run(void)
{
	struct timespec *tp;
	struct timespec time_base, time_off, time_sel;
	double timeout = -1;
	bool drawing = false;

	fd_set fds_r;
	int maxfd, xfd = ConnectionNumber(x11.dpy);
	int num_evs;

	pty_init("/bin/dash");
	pty_resize(win.w, win.h, win.cols, win.rows);
	maxfd = max(xfd, pty.mfd);

	while (win.up) {
		FD_ZERO(&fds_r);
		FD_SET(xfd, &fds_r);
		FD_SET(pty.mfd, &fds_r);

		timeout = (XPending(x11.dpy)) ? 0 : timeout;
		time_sel.tv_sec = timeout / 1E3;
		time_sel.tv_nsec = 1E6 * (timeout - 1E3 * time_sel.tv_sec);
		tp = (timeout >= 0) ? &time_sel : NULL;

		if (pselect(maxfd + 1, &fds_r, NULL, NULL, tp, NULL) < 0) {
			exit(2);
		}
		clock_gettime(CLOCK_MONOTONIC, &time_base);

		if (FD_ISSET(pty.mfd, &fds_r)) {
#if 0
fputc('\n', stderr); fputn(stderr, 50, '-');
fputc('\n', stderr); PRINTSRC;
fputn(stderr, 50, '-') ;fputc('\n', stderr);
#endif
			pty_read();
		}

		XEvent xev = { 0 };
		num_evs = 0;
		while (XPending(x11.dpy) > 0) {
			XNextEvent(x11.dpy, &xev);
			if (xevtypes[xev.type].handler) {
				(xevtypes[xev.type].handler)(&xev);
			}
			num_evs++;
		}

		if (FD_ISSET(pty.mfd, &fds_r) || num_evs > 0) {
			if (!drawing) {
				time_off = time_base;
				drawing = true;
			}
			timeout = ((max_latency - TIME_DIFF(time_base, time_off)) / max_latency) * min_latency;
			/* printf("==> %lf\n", timeout); */
			if (timeout > 0) continue;
		}
		timeout = -1;

		draw();
		XFlush(x11.dpy);
		drawing = false;
	}
}

void
draw_cursor(Cell glyph, uint y, uint x)
{
	glyph = (isprint(glyph)) ? glyph : ' ';

	assert(x < (uint)win.cols);
	assert(y < (uint)win.rows);

	XftDrawRect(win.render, &rsc.color[COLOR_FG],
	    x * win.dw + win.bpx,
	    y * win.dh + win.bpx,
	    win.dw, win.dh);

	XftDrawString8(win.render, &rsc.color[COLOR_BG], rsc.font,
	    x * win.dw + win.bpx, y * win.dh + win.bpx + rsc.font->ascent,
	    (FcChar8 *)&glyph, 1);
}

void
draw_clear(uint y1, uint y2)
{
	assert(y2 >= y1);

	XftDrawRect(win.render, &rsc.color[COLOR_BG],
	    0, MIN(ROW2PX(y1, 0), (uint)win.h),
	    win.w, MIN(ROW2PX(y2, 0), (uint)win.h));
}

void
draw_range(uint y1, uint x1, uint y2, uint x2)
{
	for (uint i = y1; i <= y2; i++) {
		Row row = ROW(tty.top + i);

		if (!row.len || IS_BLANK(row)) {
			continue;
		}

		uint offset = 0, len = row.len;

		if (i == y2) {
			len = MIN(x2, len);
			offset = (i == y1) ? CLAMP(x1, 0, len) : 0;
		} else if (i == y1) {
			offset = MIN(x1, len);
		}
		len -= offset;

		XftDrawString8(win.render, &rsc.color[COLOR_FG], rsc.font,
		    win.bpx, i * win.dh + win.bpx + rsc.font->ascent,
		    (FcChar8 *)streamptr_s(row.offset + offset), len);
	}
}

void
draw(void)
{
#if 1
	draw_clear(0, win.rows);
	draw_range(0, 0, tty.bot - tty.top, tty.max_cols);
#else
	draw_clear(tty.dirty, win.rows);
	draw_range(tty.dirty, 0, tty.bot - tty.top, tty.max_cols);
#endif
	draw_cursor(tty.data[tty.c.i], tty.c.y, tty.c.x);

	XCopyArea(x11.dpy, win.map, win.wid, win.gc,
	  0, 0, win.w, win.h, 0, 0);

	screen_set_clean();
}

int
x_read_string(const char *str, size_t len)
{
	/* int ret = tty_write(str, len); */
	int ret = pty_write(str, len);
	/* if (ret > 0) { */
	/* 	draw(); */
	/* } */

	return ret;
}

void
x_resize(int w, int h)
{
	int cols = PX2COL(w, win.bpx);
	int rows = PX2ROW(h, win.bpx);

	win.w = w;
	win.h = h;

	if (cols != win.cols || rows != win.rows) {
		win.cols = cols;
		win.rows = rows;
		/* tty_resize(cols, rows); */

		XFreePixmap(x11.dpy, win.map);
		win.map = XCreatePixmap(x11.dpy, win.wid,
		  win.w, win.h, DefaultDepth(x11.dpy, x11.sid));

		draw();
	}
}

void
log__xevent(XEvent *xevp)
{
	if (LOGGING(EVENTS)) {
		fprintf(stderr, " * XEvent: %s\n", xevtypes[xevp->type].name);
	}
}

bool
x_init_session(void)
{
	/*
	 * Get base X11 handles/info
	 */
	x11.dpy = XOpenDisplay(NULL);
	if (!x11.dpy) return false;
	x11.sid = XDefaultScreen(x11.dpy);

	x11.w = DisplayWidth(x11.dpy, x11.sid);
	x11.h = DisplayHeight(x11.dpy, x11.sid);

	x11.root = XRootWindow(x11.dpy, x11.sid);
	x11.visual = XDefaultVisual(x11.dpy, x11.sid);
	x11.colormap = XDefaultColormap(x11.dpy, x11.sid);

	/*
	 * Create input context
	 */
	if (!x_init_inputmethod()) return false;

	/*
	 * Configure X properties
	 */
	x11.attr.colormap = XDefaultColormap(x11.dpy, x11.sid);
	x11.attr.background_pixel = RGB2NUM(255, 0, 255);
	x11.attr.bit_gravity = NorthWestGravity;
	x11.attr.event_mask = (
	  FocusChangeMask|KeyPressMask|KeyReleaseMask|ExposureMask|VisibilityChangeMask|
	  StructureNotifyMask|ButtonMotionMask|ButtonPressMask|ButtonReleaseMask);

	x11.atoms[WM_Name]   = XInternAtom(x11.dpy, "_NET_WM_NAME", False);
	x11.atoms[WM_Delete] = XInternAtom(x11.dpy, "WM_DELETE_WINDOW", False);
	x11.atoms[WM_State]  = XInternAtom(x11.dpy, "_NET_WM_STATE", False);

	/*
	 * Load fonts and colors
	 */
	if (!x_init_resources()) return false;

	/*
	 * Determine window size
	 */
	XGlyphInfo extents;
	XftTextExtents8(x11.dpy, rsc.font, (FcChar8 *)ascii__, sizeof(ascii__), &extents);

	win.dh = rsc.font->ascent + rsc.font->descent;
	win.dw = (extents.xOff + (sizeof(ascii__) - 1)) / sizeof(ascii__);
	win.w = COL2PX(win.cols, win.bpx);
	win.h = ROW2PX(win.rows, win.bpx);

	/*
	 * Initialize window, set size hints
	 */
	win.wid = XCreateWindow(x11.dpy, x11.root,
	    win.x, win.y, win.w, win.h, win.bpx,
	    XDefaultDepth(x11.dpy, x11.sid),
	    InputOutput,
	    x11.visual,
	    CWBackPixel|CWBorderPixel|CWBitGravity|CWEventMask|CWColormap,
	    &x11.attr);

	if (!win.wid) return false;

	if (!XSetWMProtocols(x11.dpy, win.wid, &x11.atoms[WM_Delete], 1))
		return false;

	XClassHint classhint = { x11.wmname, x11.wmclass };
	XWMHints wmhint = { .flags = InputHint, .input = 1 };
	XSizeHints *szhint = XAllocSizeHints();

	szhint->flags       = PSize|PResizeInc|PBaseSize|PMinSize;
	szhint->width       = win.w;
	szhint->height      = win.h;
	szhint->width_inc   = win.dw;
	szhint->height_inc  = win.dh;
	szhint->base_width  = 2 * win.bpx;
	szhint->base_height = 2 * win.bpx;
	szhint->min_width   = win.dw + 2 * win.bpx;
	szhint->min_height  = win.dh + 2 * win.bpx;

	XStoreName(x11.dpy, win.wid, x11.wmtitle);
	XSetWMProperties(x11.dpy, win.wid, NULL, NULL, NULL, 0, szhint, &wmhint, &classhint);
	XFree(szhint);

	/*
	 * Setup graphics context and pixel buffers
	 */
	win.gc = DefaultGC(x11.dpy, x11.sid);

	win.map = XCreatePixmap(x11.dpy, win.wid,
	    win.w, win.h, XDefaultDepth(x11.dpy, x11.sid));

	XSetForeground(x11.dpy, win.gc, rsc.color[COLOR_BG].pixel);
	XFillRectangle(x11.dpy, win.map, win.gc,
	    0, 0, win.w, win.h);
	XSetForeground(x11.dpy, win.gc, rsc.color[COLOR_FG].pixel);

	win.render = XftDrawCreate(x11.dpy, win.map, x11.visual, x11.colormap);

	/*
	 * Initialize terminal backend
	 */
	if (!tty_init(win.cols, win.rows))
		return false;

	XMapWindow(x11.dpy, win.wid);
	XSync(x11.dpy, False);
	win.up = true;

	return win.up;
}

bool
x_init_inputmethod(void)
{
	XIMCallback im_destroy = { .client_data = NULL, .callback = ximcb_destroy };
	XIMCallback ic_destroy = { .client_data = NULL, .callback = xiccb_destroy };

	x11.ime.im = XOpenIM(x11.dpy, NULL, NULL, NULL);
	if (!x11.ime.im) return false;
	if (XSetIMValues(x11.ime.im, XNDestroyCallback, &im_destroy, NULL)) {
		fprintf(stderr, " ! ERROR(XSetIMValues): failed to set destroy callback\n");
	}
	x11.ime.spotlist = XVaCreateNestedList(0, XNSpotLocation, &x11.ime.spot, NULL);

	if (!x11.ime.ic) {
		x11.ime.ic = XCreateIC(x11.ime.im,
		    XNInputStyle,
		    XIMPreeditNothing|XIMStatusNothing,
		    XNClientWindow,
		    x11.root,
		    XNDestroyCallback,
		    &ic_destroy,
		    NULL);
	}
	if (!x11.ime.ic) {
		fprintf(stderr, " ! ERROR(XCreateIC): failed to create input context\n");
	}

	return true;
}

bool
x_init_resources(void)
{
#if 0
	XRenderColor color_fg = { .red = 0xff, .green = 0xff, .blue = 0xff, .alpha = 0xffff };
	XRenderColor color_bg = { .red = 0x00, .green = 0x00, .blue = 0x00, .alpha = 0xffff };

	XftColorAllocValue(x11.dpy, x11.visual, x11.colormap, &color_fg, &rsc.color[COLOR_FG]);
	XftColorAllocValue(x11.dpy, x11.visual, x11.colormap, &color_bg, &rsc.color[COLOR_BG]);
#else
	if (!XftColorAllocName(x11.dpy, x11.visual, x11.colormap, "white", &rsc.color[COLOR_FG]))
		exit(3);
	if (!XftColorAllocName(x11.dpy, x11.visual, x11.colormap, "black", &rsc.color[COLOR_BG]))
		exit(4);
#endif
	if (!(rsc.pattern = FcNameParse((FcChar8 *)x11.fontstr))) {
		exit(2);
	}
	rsc.font = XftFontOpenName(x11.dpy, x11.sid, x11.fontstr);
	if (!rsc.font) return false;

	return true;
}

void
xev_keypress(XEvent *xevp)
{
	log__xevent(xevp);
	XKeyEvent *event = (XKeyEvent *)xevp;
	char keystr[16];
	char buf[64];
	uint len, keylen;
	KeySym keysym;
	Status status;

	len = (x11.ime.ic)
	    ? XmbLookupString(x11.ime.ic, event, buf, sizeof(buf), &keysym, &status)
	    : XLookupString(event, buf, sizeof(buf), &keysym, NULL);
#if 1
	keylen = key_get_string(keysym, event->state, keystr, 16);
	if (keylen) {
#if 1
		fprintf(stderr, "==> ");
		for (uint i = 0; i < keylen; ++i) {
			fprintf(stderr, "%03u,", keystr[i]);
		}
		fprintf(stderr, "END\n");
#else
		fprintf(stderr, "==> %s\n", keystr);
#endif
		x_read_string(keystr, keylen);
		return;
	}

	if (len == 1) {
		x_read_string(buf, len);
	}
#else
	if (len == 0) {
		// temporary
		switch (keysym) {
		case XK_Left:  cursor_move_x(-1); break;
		case XK_Right: cursor_move_x(+1); break;
		case XK_Up:    cursor_move_y(-1); break;
		case XK_Down:  cursor_move_y(+1); break;

		case XK_F10:
			DBG_PRINT(history, 1);
			break;
		case XK_F11:
			DBG_PRINT(cursor, 1);
			break;
		case XK_F12:
			DBG_PRINT(state, 1);
			break;

		default: return;
		}

		/* draw(); */
	} else {
		switch (keysym) {
		case XK_BackSpace:
			{
			char *seq = "\177";
			x_read_string(seq, strlen(seq));
			}
			break;
		default:
			x_read_string(buf, len);
			break;
		}
		/* int c = buf[0]; */
		/* fprintf(stderr, " * XKeyChar: (%02u) ASCII[%d] = \"%s\"\n", len, c, asciistr(c)); */
	}
#endif
}

void
xev_buttonpress(XEvent *xevp)
{
	log__xevent(xevp);
}

void
xev_buttonrelease(XEvent *xevp)
{
	log__xevent(xevp);
}

void
xev_motionnotify(XEvent *xevp)
{
	log__xevent(xevp);
}

void
xev_focusin(XEvent *xevp)
{
	log__xevent(xevp);
}

void
xev_focusout(XEvent *xevp)
{
	log__xevent(xevp);
}

void
xev_expose(XEvent *xevp)
{
	log__xevent(xevp);
	draw();
}

void
xev_visibilitynotify(XEvent *xevp)
{
	log__xevent(xevp);
}

void
xev_destroynotify(XEvent *xevp)
{
	log__xevent(xevp);
	XDestroyWindowEvent *event = (XDestroyWindowEvent *)xevp;
	if (event->window == win.wid)
		win.up = false;
}

void
xev_unmapnotify(XEvent *xevp)
{
	log__xevent(xevp);
}

void
xev_configurenotify(XEvent *xevp)
{
	log__xevent(xevp);
	XConfigureEvent *event = (XConfigureEvent *)xevp;
	/* x_resize(win.sel, event->width, event->height); */
}

void
xev_configurerequest(XEvent *xevp)
{
	log__xevent(xevp);
	/* XConfigureRequestEvent *event = (XConfigureEvent *)&event; */
}

void
xev_propertynotify(XEvent *xevp)
{
	log__xevent(xevp);
}

void
xev_selectionrequest(XEvent *xevp)
{
	log__xevent(xevp);
}

void
xev_selectionnotify(XEvent *xevp)
{
	log__xevent(xevp);
}

void
xev_clientmessage(XEvent *xevp)
{
	log__xevent(xevp);
	XClientMessageEvent *event = (XClientMessageEvent *)xevp;
	if ((Atom)event->data.l[0] == x11.atoms[WM_Delete]) {
		XDestroyWindow(x11.dpy, win.wid);
		win.up = false;
	}
}

void
ximcb_create(Display *dpy, XPointer client, XPointer call)
{
(void)dpy;
(void)client;
(void)call;
	if (x_init_inputmethod()) {
		XUnregisterIMInstantiateCallback(x11.dpy,
		  NULL, NULL, NULL, ximcb_create, NULL);
	}
}

void
ximcb_destroy(XIM xim, XPointer client, XPointer call)
{
(void)xim;
(void)client;
(void)call;
	x11.ime.im = NULL;
	XRegisterIMInstantiateCallback(x11.dpy,
	  NULL, NULL, NULL, ximcb_create, NULL);
	XFree(x11.ime.spotlist);
}

void
xiccb_destroy(XIM xic, XPointer client, XPointer call)
{
(void)xic;
(void)client;
(void)call;
	x11.ime.ic = NULL;
}

