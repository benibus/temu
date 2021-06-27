#define _POSIX_C_SOURCE 199309L

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
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#include "utils.h"
#include "window.h"

#define XW_EVENT_MASK \
  (StructureNotifyMask|KeyPressMask|KeyReleaseMask|     \
   PointerMotionMask|ButtonPressMask|ButtonReleaseMask| \
   ExposureMask|FocusChangeMask|VisibilityChangeMask|   \
   EnterWindowMask|LeaveWindowMask|PropertyChangeMask)
#define WINFLAGS_DEFAULT (WINATTR_RESIZABLE)
#define WINHEADER(wp) ((WinHdr *)((uchar *)wp - offsetof(WinHdr, win)))
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

typedef struct {
	Display *dpy;
	int screen;
	Window root;
	XIM im;
	XVisualInfo *vis;
	uint maxw, maxh;
} WinEnv;

typedef struct {
	WinEnv *env;
	Window self, parent;
	Colormap colormap;
	Drawable buf;
	XIC ic;
	GC gc;
	Win win;
} WinHdr;

typedef XftColor Color;
typedef XftDraw *Surface;
#define Font Font_

typedef struct {
	XftFont *self;
	FcFontSet *set;
	FcPattern *pattern;
	int height, width;
	int ascent, descent;
} FontFace;

typedef struct {
	FontFace face[NUM_FACE];
	char *name;
} Font;

struct RC_ {
	WinHdr *hdr;
	Surface srf;
	int fontid;
	int faceid;
	struct {
		int bg;
		int fg;
	} colorid;
};

static Atom wm_atoms[NUM_ATOM];
static WinEnv g_env;
static RC g_rc;
static struct {
	struct { Font data[MAX_FONTS]; int count; } fonts;
	struct { Color data[MAX_COLORS]; int count; } colors;
} pool;
static const char ascii_[] =
    " !\"#$%&'()*+,-./0123456789:;<=>?"
    "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
    "`abcdefghijklmnopqrstuvwxyz{|}~";

static WinEnv *ws_init_session(void);
static bool ws_test_im_style(XIM);
static bool ws_set_default_visual(WinEnv *);
static bool ws_wait_sys(Display *, double *);
static void ws_poll_events(Win *);
static Win *ws_validate(Win *win);
static size_t ws_get_property(Display *, Window, Atom, Atom, uchar **);
static int ws_translate_key(uint, uint, int *, int *);

static void wsr_draw_rect(RC *, Color *, uint, uint, uint, uint, uint, uint, uint, uint);

static inline Font *
getfont(int fontid)
{
	return pool.fonts.data + fontid;
}

static inline FontFace *
getface(int fontid, int faceid)
{
	return getfont(fontid)->face + faceid;
}

static inline Color *
getcolor(int colorid)
{
	return pool.colors.data + colorid;
}

WinEnv *
ws_init_session(void)
{
	if (g_env.dpy) return NULL;

	setlocale(LC_CTYPE, "");

	if (!(g_env.dpy = XOpenDisplay(NULL))) {
		return NULL;
	}
	g_env.screen = DefaultScreen(g_env.dpy);
	g_env.root   = RootWindow(g_env.dpy, g_env.screen);
	g_env.maxw   = DisplayWidth(g_env.dpy, g_env.screen);
	g_env.maxh   = DisplayHeight(g_env.dpy, g_env.screen);

	if (XSupportsLocale()) {
		XSetLocaleModifiers("");
		g_env.im = XOpenIM(g_env.dpy, 0, NULL, NULL);
		if (g_env.im && !ws_test_im_style(g_env.im)) {
			XCloseIM(g_env.im);
			g_env.im = NULL;
		}
	}

	uchar *supported;

#define INTERN_ATOM(atom_) ATOM((atom_)) = XInternAtom(g_env.dpy, #atom_, False)
	INTERN_ATOM(_NET_SUPPORTED);
	{
		ws_get_property(g_env.dpy, g_env.root,
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

	return &g_env;
}

size_t
ws_get_property(Display *dpy, Window winid, Atom prop, Atom type, uchar **buf)
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
ws_test_im_style(XIM im)
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
ws_set_default_visual(WinEnv *env)
{
	if (!env->dpy) return false;

	XVisualInfo vistmp = { 0 };
	int count;

#if defined(XW_API_X11)
	vistmp.visualid =
	    XVisualIDFromVisual(
	        DefaultVisual(env->dpy, env->screen));
#else
	// OpenGL context initialization (eventually)
	return false;
#endif
	if (env->vis) {
		XFree(env->vis);
		env->vis = NULL;
	}
	env->vis = XGetVisualInfo(env->dpy, VisualIDMask, &vistmp, &count);
	if (!env->vis) {
		return false;
	}

	return true;
}

Win *
ws_init_window(void)
{
	WinHdr *hdr = calloc(1, sizeof(*hdr));
	assert(hdr);

	if (g_env.dpy) {
		hdr->env = &g_env;
	} else {
		if (!(hdr->env = ws_init_session()))
			return NULL;
		if (!ws_set_default_visual(hdr->env))
			return NULL;
	}

	return &hdr->win;
}

bool
ws_create_window(Win *win)
{
	if (!win) return false;

	WinHdr *hdr = WINHEADER(win);
	assert(&hdr->win == win);
	if (!hdr || !hdr->env || !hdr->env->dpy) {
		return false;
	}
	ws_validate(&hdr->win);

	WinEnv *env = hdr->env;

	if (!env->vis) {
		if (!ws_set_default_visual(env)) {
			return false;
		}
	}
	hdr->colormap =
	    XCreateColormap(env->dpy, env->root, env->vis->visual, AllocNone);

	XSetWindowAttributes wa = {
		.background_pixel = 0,
		.colormap    = hdr->colormap,
		.event_mask  = XW_EVENT_MASK,
		.bit_gravity = NorthWestGravity,
	};

	hdr->parent = env->root;
	hdr->self = XCreateWindow(env->dpy, env->root,
	    0, 0, win->w, win->h, win->bw,
	    env->vis->depth, InputOutput, env->vis->visual,
	    (CWBackPixel|CWBorderPixel|CWColormap|CWBitGravity|CWEventMask),
	    &wa);

	if (!hdr->self) return false;

	// Set WM protocols
	{
		Atom supported[] = {
			ATOM(WM_DELETE_WINDOW),
			ATOM(_NET_WM_PID),
			ATOM(_NET_WM_NAME),
			ATOM(_NET_WM_STATE)
		};

		XSetWMProtocols(env->dpy, hdr->self, supported, LEN(supported));

		// link window to pid
		pid_t pid = getpid();
		XChangeProperty(env->dpy, hdr->self,
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

		XSetWMHints(env->dpy, hdr->self, hintp);
		XFree(hintp);
	}

	// Set WM_CLASS hints
	{
		XClassHint *hintp = XAllocClassHint();
		hintp->res_name  = win->instance;
		hintp->res_class = win->class;

		XSetClassHint(env->dpy, hdr->self, hintp);
		XFree(hintp);
	}

	// Set WM_SIZE hints
	{
		XSizeHints *hintp = XAllocSizeHints();

		// we broadcast the starting size in all cases, for simplicity
		hintp->width  = win->w;
		hintp->height = win->h;

		if (win->bw) {
			hintp->flags |= (PBaseSize);
			hintp->base_width  = 2 * win->bw;
			hintp->base_height = 2 * win->bw;
		}

		if (win->flags & WINATTR_RESIZABLE) {
			hintp->flags |= (USSize);
			if (win->iw && win->ih) {
				hintp->flags |= PResizeInc;
				hintp->width_inc  = win->iw;
				hintp->height_inc = win->ih;
			}
			if (win->minw && win->minh) {
				hintp->flags |= (PMinSize);
				hintp->min_width  = win->minw;
				hintp->min_height = win->minh;
			}
			if (win->maxw && win->maxh) {
				hintp->flags |= (PMaxSize);
				hintp->max_width  = win->maxw;
				hintp->max_height = win->maxh;
			}
		} else {
			hintp->flags |= (PSize|PMinSize|PMaxSize);
			hintp->max_width = hintp->min_width = win->w;
			hintp->max_height = hintp->min_height = win->h;
		}

		XSetNormalHints(env->dpy, hdr->self, hintp);

		// set title property
		XSetStandardProperties(env->dpy, hdr->self,
		    win->title, NULL, None, NULL, 0, hintp);

		XFree(hintp);
	}

	XFlush(env->dpy);

	if (env->im) {
		hdr->ic = XCreateIC(env->im,
		    XNInputStyle, (XIMPreeditNothing|XIMStatusNothing),
		    XNClientWindow, hdr->self, XNFocusWindow, hdr->self,
		    NULL);
		if (hdr->ic) {
			ulong filter = 0;
			if (!XGetICValues(hdr->ic, XNFilterEvents, &filter, NULL)) {
				XSelectInput(env->dpy, hdr->self,
				    (XW_EVENT_MASK|filter));
			}
		}
	}

	ws_get_win_size(win, &win->w, &win->h);
	{
		XGCValues gcvals;

		memset(&gcvals, 0, sizeof(gcvals));
		// magic boolean that stops the program from redrawing 1000+ times per second
		gcvals.graphics_exposures = False;

		hdr->gc = XCreateGC(env->dpy, env->root,
		    GCGraphicsExposures, &gcvals);
	}

	hdr->buf = XCreatePixmap(env->dpy,
	    hdr->self, win->w, win->h, env->vis->depth);

	return true;
}

Win *
ws_validate(Win *win)
{
	win->w    = DEFAULT(win->w, 800);
	win->h    = DEFAULT(win->h, 600);
	win->iw   = DEFAULT(win->iw, 1);
	win->ih   = DEFAULT(win->ih, 1);
	win->bw   = DEFAULT(win->bw, 0);
	win->minw = DEFAULT(win->minw, win->iw + 2 * win->bw);
	win->minh = DEFAULT(win->minh, win->ih + 2 * win->bw);
	win->maxw = DEFAULT(win->maxw, 0);
	win->maxh = DEFAULT(win->maxh, 0);

	if (win->flags == WINATTR_DEFAULT) {
		win->flags = WINFLAGS_DEFAULT;
	}

	return win;
}

void
ws_get_win_size(Win *win, uint *w, uint *h)
{
	assert(win);
	WinHdr *hdr = WINHEADER(win);

	XWindowAttributes attr;
	XGetWindowAttributes(hdr->env->dpy, hdr->self, &attr);

	if (w) *w = attr.width;
	if (h) *h = attr.height;
}

void
ws_get_win_pos(Win *win, int *x, int *y)
{
	assert(win);
	WinHdr *hdr = WINHEADER(win);
	WinEnv env = *hdr->env;

	Window dummy;
	int x_, y_;

	XTranslateCoordinates(env.dpy, hdr->self, env.root,
	    0, 0, &x_, &y_, &dummy);

	if (x) *x = x_;
	if (y) *y = y_;
}

void
ws_show_window(Win *win)
{
	if (!win) return;

	WinHdr *hdr = WINHEADER(win);
	WinEnv env = *hdr->env;

	XMapWindow(env.dpy, hdr->self);
	XSync(env.dpy, False);
	win->state = true;

	XEvent dummy;
	double timeout = 0.1;

	while (!XCheckTypedWindowEvent(env.dpy, hdr->self, VisibilityNotify, &dummy)) {
		if (!ws_wait_sys(env.dpy, &timeout)) {
			break;
		}
	}

	ws_get_win_pos(win, &win->x, &win->y);
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
ws_wait_sys(Display *dpy, double *timeout)
{
	fd_set fdsr;
	const int xwfd = ConnectionNumber(dpy);
	int fd = xwfd + 1;
	int count = 0;

	while (true) {
		FD_ZERO(&fdsr);
		FD_SET(xwfd, &fdsr);

		if (timeout != NULL) {
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
ws_translate_key(uint ksym, uint mask, int *id_, int *mod_)
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

void
ws_poll_events(Win *win)
{
	assert(win);
	WinHdr *hdr = WINHEADER(win);
	WinEnv env = *hdr->env;

	XPending(env.dpy);

	while (XQLength(env.dpy)) {
		XEvent event = { 0 };
		XNextEvent(env.dpy, &event);
		if (XFilterEvent(&event, None)) {
			continue;
		}

		switch (event.type) {
		case ConfigureNotify:
			{
				XConfigureEvent *e = (void *)&event;
				if (win->events.resize) {
					win->events.resize(e->width, e->height);
				}
				win->w = e->width;
				win->h = e->height;
			}
			break;
		case KeyPress:
			{
				XKeyEvent *e = (void *)&event;
				KeySym ksym;
				struct {
					int id;
					int mod;
					char buf[128];
					int len;
				} k;
				char *buf = k.buf;

				if (!hdr->ic) {
					k.len = XLookupString(
					    e, k.buf, LEN(k.buf) - 1, &ksym, NULL);
				} else {
					Status status;
					for (int n = 0;; n++) {
						k.len = XmbLookupString(
						    hdr->ic, e, k.buf, LEN(k.buf) - 1,
						    &ksym, &status);
						if (status != XBufferOverflow) {
							break;
						}
						assert(!n);
						buf = calloc(k.len + 1, 1);
					}
				}

				buf[k.len] = 0;
				ws_translate_key(ksym, e->state, &k.id, &k.mod);

				if (win->events.key_press && k.id != KEY_NONE) {
					win->events.key_press(k.id, k.mod, buf, k.len);
				}

				if (buf != k.buf) free(buf);
			}
			break;
		case ClientMessage:
			{
				XClientMessageEvent *e = (void *)&event;
				if ((Atom)e->data.l[0] == ATOM(WM_DELETE_WINDOW)) {
					XDestroyWindow(env.dpy, hdr->self);
					win->state = false;
				}
			}
			break;
		}
	}

	XFlush(env.dpy);
}

double
ws_process_events(Win *win, double timeout)
{
	assert(win);
	WinHdr *hdr = WINHEADER(win);
	WinEnv env = *hdr->env;

	while (!XPending(env.dpy)) {
		if (!ws_wait_sys(env.dpy, &timeout)) {
			goto done;
		}
	}

	ws_poll_events(win);
done:
	return timeout;
}

RC *
wsr_init_context(Win *win)
{
	if (!win) return NULL;

	WinHdr *hdr = WINHEADER(win);

	if (!hdr->env || !hdr->env->dpy || !hdr->env->vis || !hdr->colormap) {
		return NULL;
	}

	g_rc.srf = XftDrawCreate(hdr->env->dpy, hdr->buf, hdr->env->vis->visual, hdr->colormap);
	if (!g_rc.srf) return NULL;

	g_rc.hdr = hdr;

	return &g_rc;
}

bool
wsr_set_font(RC *rc, int fontid, int faceid)
{
	if (!rc) return false;

	rc->fontid = fontid;
	rc->faceid = faceid;

	return true;
}

int
wsr_load_font(Win *win, const char *name)
{
	WinHdr *hdr = WINHEADER(win);
	WinEnv *env = hdr->env;

	if (!env || !env->dpy || !name) {
		return ID_NULL;
	}
	assert(!g_rc.hdr); // temporary

	if (pool.fonts.count == MAX_FONTS) {
		return ID_NULL;
	}

	FontFace *face = getface(pool.fonts.count, FACE_REGULAR);

	if ((face->pattern = FcNameParse((FcChar8 *)name))) {
		if (!(face->self = XftFontOpenName(env->dpy, env->screen, name))) {
		    return ID_NULL;
		}

		XGlyphInfo extents;
		XftTextExtents8(env->dpy, face->self,
		    (FcChar8 *)ascii_, LEN(ascii_), &extents);

		face->ascent  = face->self->ascent;
		face->descent = face->self->descent;
		face->width   = (extents.xOff + (LEN(ascii_) - 1)) / LEN(ascii_);
		face->height  = face->ascent + face->descent;
	} else {
		return ID_NULL;
	}

	return pool.fonts.count++;
}

bool
wsr_get_avg_font_size(int fontid, int faceid, int *w_, int *h_)
{
	FontFace *face = getface(fontid, faceid);

	if (face->self) {
		if (w_) *w_ = face->width;
		if (h_) *h_ = face->height;

		return true;
	}

	return false;
}

bool
wsr_set_colors(RC *rc, int colorbg, int colorfg)
{
	if (!rc) return false;

	if (colorbg != ID_NULL) rc->colorid.bg = colorbg;
	if (colorfg != ID_NULL) rc->colorid.fg = colorfg;

	return true;
}

int
wsr_load_color_name(RC *rc, const char *name)
{
	if (!rc || !name) {
		return ID_NULL;
	}

	WinHdr *hdr = rc->hdr;

	if (pool.colors.count == MAX_COLORS) {
		return ID_NULL;
	}

	Color *color = getcolor(pool.colors.count);

	if (!XftColorAllocName(hdr->env->dpy, hdr->env->vis->visual, hdr->colormap, name, color)) {
		return ID_NULL;
	}

	return pool.colors.count++;
}

void
wsr_draw_rect(RC *rc, Color *color,
             uint x1, uint y1,
             uint x2, uint y2,
             uint dx, uint dy,
             uint ox, uint oy)
{
	if (!rc) return;

	uint x = ox + (x1 * dx);
	uint y = oy + (y1 * dy);
	uint w = ox + (x2 * dx) - x;
	uint h = oy + (y2 * dy) - y;

	XftDrawRect(rc->srf, color, x, y, w, h);
}

void
wsr_fill_region(RC *rc, uint x1, uint y1, uint x2, uint y2, uint dx, uint dy)
{
	wsr_draw_rect(rc, getcolor(rc->colorid.fg),
	    x1, y1, x2, y2, dx, dy, rc->hdr->win.bw, rc->hdr->win.bw);
}

void
wsr_clear_region(RC *rc, uint x1, uint y1, uint x2, uint y2, uint dx, uint dy)
{
	wsr_draw_rect(rc, getcolor(rc->colorid.bg),
	    x1, y1, x2, y2, dx, dy, rc->hdr->win.bw, rc->hdr->win.bw);
}

void
wsr_clear_screen(RC *rc)
{
	wsr_clear_region(rc, 0, 0, rc->hdr->win.w, rc->hdr->win.h, 1, 1);
}

void
wsr_draw_string(RC *rc, const char *str, uint len, uint col, uint row, bool invert)
{
	if (!rc || !len) return;

	FontFace *face = getface(rc->fontid, rc->faceid);
	Color *color;

	if (!invert) {
		color = getcolor(rc->colorid.fg);
	} else {
		wsr_fill_region(rc,
		    col, row, col + len - 1, row + 1, face->width, face->height);
		color = getcolor(rc->colorid.bg);
	}

	XftDrawString8(rc->srf, color, face->self,
	    rc->hdr->win.bw + (col * face->width),
	    rc->hdr->win.bw + (row * face->height) + face->ascent,
	    (FcChar8 *)str, len);
}

void
ws_swap_buffers(Win *win)
{
	if (!win) return;
	WinHdr hdr = *WINHEADER(win);

	XCopyArea(hdr.env->dpy, hdr.buf, hdr.self, hdr.gc, 0, 0, hdr.win.w, hdr.win.h, 0, 0);
}

