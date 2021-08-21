#if !defined(_POSIX_C_SOURCE)
  #define _POSIX_C_SOURCE 199309L
#endif

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <limits.h>
#include <locale.h>
#include <unistd.h>
#include <sys/select.h>

#include <X11/keysym.h>
#include <X11/Xatom.h>

#include "utils.h"
#include "x11.h"

#define XW_EVENT_MASK \
  (StructureNotifyMask|KeyPressMask|KeyReleaseMask|     \
   PointerMotionMask|ButtonPressMask|ButtonReleaseMask| \
   ExposureMask|FocusChangeMask|VisibilityChangeMask|   \
   EnterWindowMask|LeaveWindowMask|PropertyChangeMask)
#define WINFLAGS_DEFAULT (WINATTR_RESIZABLE)
#define ATOM(atom_) wm_atoms[(atom_)]

enum {
	WM_PROTOCOLS,
	WM_STATE,
	WM_DELETE_WINDOW,
	_NET_SUPPORTED,
	_NET_SUPPORTING_WM_CHECK,
	_NET_WM_ICON,
	_NET_WM_PING,
	_NET_WM_PID,
	_NET_WM_NAME,
	_NET_WM_STATE,
	_NET_WM_STATE_ABOVE,
	_NET_WM_ICON_NAME,
	_NET_WM_BYPASS_COMPOSITOR,
	_NET_WM_WINDOW_OPACITY,
	_MOTIF_WM_HINTS,
	UTF8_STRING,
	COMPOUND_STRING,
	ATOM_PAIR,

	NUM_ATOM
};

static Atom wm_atoms[NUM_ATOM];
static X11 g_x11;

static X11 *x11_init(void);
static bool x11_test_im_style(XIM);
static bool x11_set_visual_format(X11 *);
static bool x11_wait_sys(Display *, double *);
static int x11_translate_key(uint, uint, int *, int *);
static int win_poll_events(WinData *);
static Win *win_validate_config(Win *);
static size_t win_get_property(Display *, Window, Atom, Atom, uchar **);

X11 *
x11_init(void)
{
	if (g_x11.dpy) return NULL;

	setlocale(LC_CTYPE, "");

	if (!(g_x11.dpy = XOpenDisplay(NULL))) {
		return NULL;
	}
	g_x11.screen = DefaultScreen(g_x11.dpy);
	g_x11.root = RootWindow(g_x11.dpy, g_x11.screen);
	g_x11.maxw = DisplayWidth(g_x11.dpy, g_x11.screen);
	g_x11.maxh = DisplayHeight(g_x11.dpy, g_x11.screen);
	g_x11.dpi = (((double)g_x11.maxh * 25.4) / // 1 inch == 25.4 mm
	              (double)DisplayHeightMM(g_x11.dpy, g_x11.screen));
	g_x11.fd = ConnectionNumber(g_x11.dpy);

	if (XSupportsLocale()) {
		XSetLocaleModifiers("");
		g_x11.im = XOpenIM(g_x11.dpy, 0, NULL, NULL);
		if (g_x11.im && !x11_test_im_style(g_x11.im)) {
			XCloseIM(g_x11.im);
			g_x11.im = NULL;
		}
	}

	uchar *supported;

#define INTERN_ATOM(atom_) ATOM((atom_)) = XInternAtom(g_x11.dpy, #atom_, False)
	INTERN_ATOM(_NET_SUPPORTED);
	{
		win_get_property(g_x11.dpy, g_x11.root,
		    ATOM(_NET_SUPPORTED), XA_ATOM, &supported);
	}
	INTERN_ATOM(WM_PROTOCOLS);
	INTERN_ATOM(WM_STATE);
	INTERN_ATOM(WM_DELETE_WINDOW);
	INTERN_ATOM(_NET_SUPPORTING_WM_CHECK);
	INTERN_ATOM(_NET_WM_ICON);
	INTERN_ATOM(_NET_WM_PING);
	INTERN_ATOM(_NET_WM_PID);
	INTERN_ATOM(_NET_WM_NAME);
	INTERN_ATOM(_NET_WM_STATE);
	INTERN_ATOM(_NET_WM_ICON_NAME);
	INTERN_ATOM(_NET_WM_BYPASS_COMPOSITOR);
	INTERN_ATOM(_NET_WM_WINDOW_OPACITY);
	INTERN_ATOM(_MOTIF_WM_HINTS);
	INTERN_ATOM(UTF8_STRING);
	INTERN_ATOM(COMPOUND_STRING);
	INTERN_ATOM(ATOM_PAIR);
#undef INTERN_ATOM

	if (supported) {
		XFree(supported);
	}

	return &g_x11;
}

size_t
win_get_property(Display *dpy, Window winid, Atom prop, Atom type, uchar **buf)
{
	Atom ret_type;
	int ret_fmt;
	size_t offset;
	size_t count;

	XGetWindowProperty(dpy, winid,
	                   prop, 0, LONG_MAX, False, type,
	                   &ret_type, &ret_fmt, &count, &offset,
	                   buf);

	return count;
}

bool
x11_test_im_style(XIM im)
{
	XIMStyles *styles = NULL;
	bool found = false;

	if (XGetIMValues(im, XNQueryInputStyle, &styles, NULL)) {
		return false;
	}
	for (size_t i = 0; !found && i < styles->count_styles; i++) {
		found = (styles->supported_styles[i] == (XIMPreeditNothing|XIMStatusNothing));
	}
	XFree(styles);

	return found;
}

bool
x11_set_visual_format(X11 *x11)
{
	if (!x11->dpy) return false;

	XVisualInfo vistmp = { 0 };
	int count;

#if defined(XW_API_X11)
	vistmp.visualid =
	    XVisualIDFromVisual(
	        DefaultVisual(x11->dpy, x11->screen));
#else
	// OpenGL context initialization (eventually)
	return false;
#endif
	if (x11->vis) {
		XFree(x11->vis);
		x11->vis = NULL;
	}
	x11->vis = XGetVisualInfo(x11->dpy, VisualIDMask, &vistmp, &count);
	if (!x11->vis) return false;
	x11->fmt = XRenderFindVisualFormat(x11->dpy, x11->vis->visual);
	ASSERT(x11->fmt);

	return true;
}

Win *
win_create_client(void)
{
	WinData *win = calloc(1, sizeof(*win));
	assert(win);

	if (g_x11.dpy) {
		win->x11 = &g_x11;
	} else {
		if (!(win->x11 = x11_init()))
			return NULL;
		if (!x11_set_visual_format(win->x11))
			return NULL;
		win->colormap = XCreateColormap(win->x11->dpy,
		                                win->x11->root,
		                                win->x11->vis->visual,
		                                AllocNone);
	}

	win->pub.fd = win->x11->fd;

	return &win->pub;
}

bool
win_init_client(Win *pub)
{
	WinData *win = (WinData *)pub;
	ASSERT(win);

	X11 *x11 = win->x11;
	if (!x11 || !x11->dpy) {
		return false;
	}
	win_validate_config(&win->pub);

	if (!x11->vis) {
		if (!x11_set_visual_format(x11)) {
			return false;
		}
	}

	XSetWindowAttributes wa = {
		.background_pixel = 0,
		.colormap    = win->colormap,
		.event_mask  = XW_EVENT_MASK,
		.bit_gravity = NorthWestGravity,
	};

	win->parent = x11->root;
	win->xid = XCreateWindow(x11->dpy, x11->root,
	    0, 0, win->pub.w, win->pub.h, win->pub.bw,
	    x11->vis->depth, InputOutput, x11->vis->visual,
	    (CWBackPixel|CWBorderPixel|CWColormap|CWBitGravity|CWEventMask),
	    &wa);

	if (!win->xid) return false;

	// Set WM protocols
	{
		Atom supported[] = {
			ATOM(WM_DELETE_WINDOW),
			ATOM(_NET_WM_PID),
			ATOM(_NET_WM_NAME),
			ATOM(_NET_WM_STATE)
		};

		XSetWMProtocols(x11->dpy, win->xid, supported, LEN(supported));

		// link window to pid
		pid_t pid = getpid();
		XChangeProperty(x11->dpy, win->xid,
		    ATOM(_NET_WM_PID), XA_CARDINAL,
		    32, PropModeReplace, (u8 *)&pid, 1);
	}

	// Set WM hints
	{
		XWMHints *hintp = XAllocWMHints();
		if (!hintp) {
			return false;
		}
		hintp->flags         = (StateHint|InputHint);
		hintp->initial_state = NormalState;
		hintp->input         = 1;

		XSetWMHints(x11->dpy, win->xid, hintp);
		XFree(hintp);
	}

	// Set WM_CLASS hints
	{
		XClassHint *hintp = XAllocClassHint();
		hintp->res_name  = win->pub.instance;
		hintp->res_class = win->pub.class;

		XSetClassHint(x11->dpy, win->xid, hintp);
		XFree(hintp);
	}

	// Set WM_SIZE hints
	{
		XSizeHints *hintp = XAllocSizeHints();

		// we broadcast the starting size in all cases, for simplicity
		hintp->width  = win->pub.w;
		hintp->height = win->pub.h;

		if (win->pub.bw) {
			hintp->flags |= (PBaseSize);
			hintp->base_width  = 2 * win->pub.bw;
			hintp->base_height = 2 * win->pub.bw;
		}

		if (win->pub.flags & WINATTR_RESIZABLE) {
			hintp->flags |= (USSize);
			if (win->pub.iw && win->pub.ih) {
				hintp->flags |= PResizeInc;
				hintp->width_inc  = win->pub.iw;
				hintp->height_inc = win->pub.ih;
			}
			if (win->pub.minw && win->pub.minh) {
				hintp->flags |= (PMinSize);
				hintp->min_width  = win->pub.minw;
				hintp->min_height = win->pub.minh;
			}
			if (win->pub.maxw && win->pub.maxh) {
				hintp->flags |= (PMaxSize);
				hintp->max_width  = win->pub.maxw;
				hintp->max_height = win->pub.maxh;
			}
		} else {
			hintp->flags |= (PSize|PMinSize|PMaxSize);
			hintp->max_width = hintp->min_width = win->pub.w;
			hintp->max_height = hintp->min_height = win->pub.h;
		}

		XSetNormalHints(x11->dpy, win->xid, hintp);

		// set title property
		XSetStandardProperties(x11->dpy, win->xid,
		    win->pub.title, NULL, None, NULL, 0, hintp);

		XFree(hintp);
	}

	XFlush(x11->dpy);

	if (x11->im) {
		win->ic = XCreateIC(x11->im,
		    XNInputStyle, (XIMPreeditNothing|XIMStatusNothing),
		    XNClientWindow, win->xid, XNFocusWindow, win->xid,
		    NULL);
		if (win->ic) {
			ulong filter = 0;
			if (!XGetICValues(win->ic, XNFilterEvents, &filter, NULL)) {
				XSelectInput(x11->dpy, win->xid,
				    (XW_EVENT_MASK|filter));
			}
		}
	}

	win_get_size(&win->pub, &win->pub.w, &win->pub.h);
	{
		XGCValues gcvals;

		memset(&gcvals, 0, sizeof(gcvals));
		// magic boolean that stops the program from redrawing 1000+ times per second
		gcvals.graphics_exposures = False;

		win->gc = XCreateGC(x11->dpy, x11->root,
		    GCGraphicsExposures, &gcvals);
	}

	win->buf = XCreatePixmap(x11->dpy,
	    win->xid, win->pub.w, win->pub.h, x11->vis->depth);

	XRenderPictureAttributes picattr = { 0 };
	win->pic = XRenderCreatePicture(x11->dpy, win->buf, x11->fmt, 0, &picattr);

	return true;
}

void
win_resize_client(Win *pub, uint w, uint h)
{
	WinData *win = (WinData *)pub;

	if (win && (win->pub.w != w || win->pub.h != h)) {
		XRenderFreePicture(win->x11->dpy, win->pic);
		XFreePixmap(win->x11->dpy, win->buf);
		win->buf = XCreatePixmap(win->x11->dpy,
		                         win->xid, w, h,
		                         win->x11->vis->depth);
		ASSERT(win->buf);
		XRenderPictureAttributes attr = { 0 };
		win->pic = XRenderCreatePicture(win->x11->dpy,
		                                win->buf,
		                                win->x11->fmt,
		                                0, &attr);
		ASSERT(win->pic);
		win_get_size(&win->pub, &win->pub.w, &win->pub.h);
	}
}

Win *
win_validate_config(Win *pub)
{
	if (pub) {
		pub->w    = DEFAULT(pub->w, 800);
		pub->h    = DEFAULT(pub->h, 600);
		pub->iw   = DEFAULT(pub->iw, 1);
		pub->ih   = DEFAULT(pub->ih, 1);
		pub->bw   = DEFAULT(pub->bw, 0);
		pub->minw = DEFAULT(pub->minw, pub->iw + 2 * pub->bw);
		pub->minh = DEFAULT(pub->minh, pub->ih + 2 * pub->bw);
		pub->maxw = DEFAULT(pub->maxw, 0);
		pub->maxh = DEFAULT(pub->maxh, 0);

		if (pub->flags == WINATTR_DEFAULT) {
			pub->flags = WINFLAGS_DEFAULT;
		}
	}

	return pub;
}

void
win_get_size(Win *pub, uint *w, uint *h)
{
	assert(pub);
	WinData *win = (WinData *)pub;

	XWindowAttributes attr;
	XGetWindowAttributes(win->x11->dpy, win->xid, &attr);

	if (w) *w = attr.width;
	if (h) *h = attr.height;
}

void
win_get_coords(Win *pub, int *x, int *y)
{
	assert(pub);
	WinData *win = (WinData *)pub;

	Window dummy;
	int x_, y_;

	XTranslateCoordinates(win->x11->dpy,
	                      win->xid,
	                      win->x11->root,
	                      0, 0, &x_, &y_,
	                      &dummy);

	if (x) *x = x_;
	if (y) *y = y_;
}

void
win_show_client(Win *pub)
{
	if (!pub) return;

	WinData *win = (WinData *)pub;
	X11 x11 = *win->x11;

	XMapWindow(x11.dpy, win->xid);
	XSync(x11.dpy, False);
	pub->state = true;

	XEvent dummy;
	double timeout = 0.1;

	while (!XCheckTypedWindowEvent(x11.dpy, win->xid, VisibilityNotify, &dummy)) {
		if (!x11_wait_sys(x11.dpy, &timeout)) {
			break;
		}
	}

	win_get_coords(pub, &pub->x, &pub->y);
}

u64
timer_current_ns(void)
{
	struct timespec ts;
	assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);

	return (ts.tv_sec * (u64)1E9) + (u64)ts.tv_nsec;
}

double
timer_elapsed_s(u64 base_ns, u64 *ret_ns)
{
	u64 curr_ns = timer_current_ns();
	if (ret_ns) *ret_ns = curr_ns;

	return (curr_ns - base_ns) / (double)1E9;
}

bool
x11_wait_sys(Display *dpy, double *timeout)
{
	fd_set fdsr;
	const int xwfd = ConnectionNumber(dpy);
	int fd = xwfd + 1;
	int count = 0;

	for (;;) {
		FD_ZERO(&fdsr);
		FD_SET(xwfd, &fdsr);

		if (timeout) {
			const long sec = (long)*timeout;
			const long usec = (long)((*timeout - sec) * 1E6);
			struct timeval tv = { sec, usec };
			const u64 base = timer_current_ns();

			const int res = select(fd, &fdsr, NULL, NULL, &tv);
			*timeout -= timer_elapsed_s(base, NULL);

			if (res > 0) {
				return true;
			}
			if (res == -1 || *timeout <= 0) {
				return false;
			}
		} else if (select(fd, &fdsr, NULL, NULL, NULL) != -1) {
			return true;
		}
	}
}

int
x11_translate_key(uint ksym, uint mask, int *id_, int *mod_)
{
	int id = KEY_NONE;
	int mod = MOD_NONE;

	// X11's printable Latin range (keysymdef.h)
	if ((ksym > 0x01f && ksym < 0x07f) ||
	    (ksym > 0x09f && ksym < 0x100)) {
		id = ksym;
	} else if (ksym & 0x100) {
#define SETKEY(k1,k2) case (k1): { id = (k2); }; break
		switch (ksym) {
		SETKEY(XK_Escape,       KEY_ESCAPE);
		SETKEY(XK_Return,       KEY_RETURN);
		SETKEY(XK_Tab,          KEY_TAB);
		SETKEY(XK_BackSpace,    KEY_BACKSPACE);
		SETKEY(XK_Insert,       KEY_INSERT);
		SETKEY(XK_Delete,       KEY_DELETE);
		SETKEY(XK_Right,        KEY_RIGHT);
		SETKEY(XK_Left,         KEY_LEFT);
		SETKEY(XK_Down,         KEY_DOWN);
		SETKEY(XK_Up,           KEY_UP);
		SETKEY(XK_Page_Up,      KEY_PAGE_UP);
		SETKEY(XK_Page_Down,    KEY_PAGE_DOWN);
		SETKEY(XK_Home,         KEY_HOME);
		SETKEY(XK_End,          KEY_END);

		SETKEY(XK_F1,           KEY_F1);
		SETKEY(XK_F2,           KEY_F2);
		SETKEY(XK_F3,           KEY_F3);
		SETKEY(XK_F4,           KEY_F4);
		SETKEY(XK_F5,           KEY_F5);
		SETKEY(XK_F6,           KEY_F6);
		SETKEY(XK_F7,           KEY_F7);
		SETKEY(XK_F8,           KEY_F8);
		SETKEY(XK_F9,           KEY_F9);
		SETKEY(XK_F10,          KEY_F10);
		SETKEY(XK_F11,          KEY_F11);
		SETKEY(XK_F12,          KEY_F12);
		SETKEY(XK_F13,          KEY_F13);
		SETKEY(XK_F14,          KEY_F14);
		SETKEY(XK_F15,          KEY_F15);
		SETKEY(XK_F16,          KEY_F16);
		SETKEY(XK_F17,          KEY_F17);
		SETKEY(XK_F18,          KEY_F18);
		SETKEY(XK_F19,          KEY_F19);
		SETKEY(XK_F20,          KEY_F20);
		SETKEY(XK_F21,          KEY_F21);
		SETKEY(XK_F22,          KEY_F22);
		SETKEY(XK_F23,          KEY_F23);
		SETKEY(XK_F24,          KEY_F24);
		SETKEY(XK_F25,          KEY_F25);

		SETKEY(XK_KP_0,         KEY_KP_0);
		SETKEY(XK_KP_1,         KEY_KP_1);
		SETKEY(XK_KP_2,         KEY_KP_2);
		SETKEY(XK_KP_3,         KEY_KP_3);
		SETKEY(XK_KP_4,         KEY_KP_4);
		SETKEY(XK_KP_5,         KEY_KP_5);
		SETKEY(XK_KP_6,         KEY_KP_6);
		SETKEY(XK_KP_7,         KEY_KP_7);
		SETKEY(XK_KP_8,         KEY_KP_8);
		SETKEY(XK_KP_9,         KEY_KP_9);

		SETKEY(XK_KP_Decimal,   KEY_KP_DECIMAL);
		SETKEY(XK_KP_Divide,    KEY_KP_DIVIDE);
		SETKEY(XK_KP_Multiply,  KEY_KP_MULTIPLY);
		SETKEY(XK_KP_Subtract,  KEY_KP_SUBTRACT);
		SETKEY(XK_KP_Add,       KEY_KP_ADD);
		SETKEY(XK_KP_Enter,     KEY_KP_ENTER);
		SETKEY(XK_KP_Equal,     KEY_KP_EQUAL);
		SETKEY(XK_KP_Tab,       KEY_KP_TAB);
		SETKEY(XK_KP_Space,     KEY_KP_SPACE);
		SETKEY(XK_KP_Insert,    KEY_KP_INSERT);
		SETKEY(XK_KP_Delete,    KEY_KP_DELETE);
		SETKEY(XK_KP_Right,     KEY_KP_RIGHT);
		SETKEY(XK_KP_Left,      KEY_KP_LEFT);
		SETKEY(XK_KP_Down,      KEY_KP_DOWN);
		SETKEY(XK_KP_Up,        KEY_KP_UP);
		SETKEY(XK_KP_Page_Up,   KEY_KP_PAGE_UP);
		SETKEY(XK_KP_Page_Down, KEY_KP_PAGE_DOWN);
		SETKEY(XK_KP_Home,      KEY_KP_HOME);
		SETKEY(XK_KP_End,       KEY_KP_END);

		default: goto done;
		}
#undef  SETKEY

		mod |= (mask & ShiftMask) ? MOD_SHIFT : 0;
	}

done:
	if (id != ID_NULL) {
		mod |= (mask & Mod1Mask)    ? MOD_ALT  : 0;
		mod |= (mask & ControlMask) ? MOD_CTRL : 0;
	}

	*id_  = id;
	*mod_ = mod;

	return id;
}

int
win_events_pending(Win *pub)
{
	return XPending(((WinData *)pub)->x11->dpy);
}

int
win_get_session_fd(Win *pub)
{
	return ConnectionNumber(((WinData *)pub)->x11->dpy);
}

int
win_poll_events(WinData *win)
{
	int count = 0;
	assert(win);

#if 0
	for (; XPending(win->x11->dpy); count++) {
#else
	XPending(win->x11->dpy);

	for (; XQLength(win->x11->dpy); count++) {
#endif
		XEvent event = { 0 };
		XNextEvent(win->x11->dpy, &event);
		if (XFilterEvent(&event, None)) {
			continue;
		}

		switch (event.type) {
			case ConfigureNotify: {
				XConfigureEvent *e = (void *)&event;
				if (win->pub.events.resize) {
					win->pub.events.resize(win->pub.ref,
					                       e->width, e->height);
				}
				win->pub.w = e->width;
				win->pub.h = e->height;
				break;
			}
			case KeyPress: {
				XKeyEvent *e = (void *)&event;
				KeySym ksym;
				struct {
					int id;
					int mod;
					char buf[128];
					int len;
				} k;
				char *buf = k.buf;

				if (!win->ic) {
					k.len = XLookupString(
					    e, k.buf, LEN(k.buf) - 1, &ksym, NULL);
				} else {
					Status status;
					for (int n = 0;; n++) {
						k.len = XmbLookupString(
						    win->ic, e, k.buf, LEN(k.buf) - 1,
						    &ksym, &status);
						if (status != XBufferOverflow) {
							break;
						}
						assert(!n);
						buf = calloc(k.len + 1, 1);
					}
				}

				buf[k.len] = 0;
				x11_translate_key(ksym, e->state, &k.id, &k.mod);

				if (win->pub.events.key_press && k.id != KEY_NONE) {
					win->pub.events.key_press(win->pub.ref,
					                          k.id, k.mod, buf, k.len);
				}

				if (buf != k.buf) free(buf);
				break;
			}
			case ClientMessage: {
				XClientMessageEvent *e = (void *)&event;
				if ((Atom)e->data.l[0] == ATOM(WM_DELETE_WINDOW)) {
					XDestroyWindow(win->x11->dpy, win->xid);
					win->pub.state = false;
				}
				break;
			}
		}
	}

	XFlush(win->x11->dpy);
	return count;
}

int
win_process_events(Win *pub, double timeout)
{
	WinData *win = (WinData *)pub;
	int count = 0;
	assert(win);

	while (!XPending(win->x11->dpy)) {
		if (!x11_wait_sys(win->x11->dpy, &timeout)) {
			goto done;
		}
	}

	count = win_poll_events(win);
done:
	return count;
}

bool
win_init_render_context(Win *pub, RC *rc)
{
	WinData *win = (WinData *)pub;
	ASSERT(win);

	if (!win->colormap)
		return false;
	if (!win->x11 || !win->x11->dpy || !win->x11->vis)
		return false;
	memset(rc, 0, sizeof(*rc));
	rc->win = pub;

	return true;
}

void
win_render_frame(Win *pub)
{
	WinData *win = (WinData *)pub;
	if (!win) return;

	XCopyArea(win->x11->dpy,
	          win->buf,
	          win->xid,
	          win->gc,
	          0, 0,
	          win->pub.w, win->pub.h,
	          0, 0);
}

