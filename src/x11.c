/*------------------------------------------------------------------------------*
 * This file is part of temu
 * Copyright (C) 2021-2022 Benjamin Harkins
 *
 * temu is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 *------------------------------------------------------------------------------*/

#include "utils.h"
#include "window.h"
#include "events.h"
#include "x11.h"

#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <unistd.h>
#include <sys/select.h>

#define DEFAULT_TIMEOUT 100 /* milliseconds */
#define DEFAULT_WIDTH   800
#define DEFAULT_HEIGHT  600
#define DEFAULT_EVENT_MASK ( \
   StructureNotifyMask  | \
   KeyPressMask         | \
   KeyReleaseMask       | \
   PointerMotionMask    | \
   ButtonPressMask      | \
   ButtonReleaseMask    | \
   ExposureMask         | \
   FocusChangeMask      | \
   VisibilityChangeMask | \
   EnterWindowMask      | \
   LeaveWindowMask      | \
   PropertyChangeMask     \
)
typedef struct {
    Win *win;
    WinEventHandler *handler;
    void *arg;
    XEvent *xevent;
} X11EventProcParams;

typedef bool X11EventProc(const X11EventProcParams *params);

static X11EventProc process_clientmessage;
static X11EventProc process_configurenotify;
static X11EventProc process_keypress;
static X11EventProc process_keyrelease;
static X11EventProc process_buttonpress;
static X11EventProc process_buttonrelease;
static X11EventProc process_motionnotify;
static X11EventProc process_focusin;
static X11EventProc process_focusout;
static X11EventProc process_expose;
static X11EventProc process_visibilitynotify;
static X11EventProc process_mapnotify;
static X11EventProc process_unmapnotify;


#define XTABLE_X11_EVENTS \
    X_(KeyPress,         &process_keypress) \
    X_(KeyRelease,       &process_keyrelease) \
    X_(ButtonPress,      &process_buttonpress) \
    X_(ButtonRelease,    &process_buttonrelease) \
    X_(MotionNotify,     &process_motionnotify) \
    X_(EnterNotify,      NULL) \
    X_(LeaveNotify,      NULL) \
    X_(FocusIn,          &process_focusin) \
    X_(FocusOut,         &process_focusout) \
    X_(KeymapNotify,     NULL) \
    X_(Expose,           &process_expose) \
    X_(GraphicsExpose,   NULL) \
    X_(NoExpose,         NULL) \
    X_(VisibilityNotify, &process_visibilitynotify) \
    X_(CreateNotify,     NULL) \
    X_(DestroyNotify,    NULL) \
    X_(UnmapNotify,      &process_unmapnotify) \
    X_(MapNotify,        &process_mapnotify) \
    X_(MapRequest,       NULL) \
    X_(ReparentNotify,   NULL) \
    X_(ConfigureNotify,  &process_configurenotify ) \
    X_(ConfigureRequest, NULL) \
    X_(GravityNotify,    NULL) \
    X_(ResizeRequest,    NULL) \
    X_(CirculateNotify,  NULL) \
    X_(CirculateRequest, NULL) \
    X_(PropertyNotify,   NULL) \
    X_(SelectionClear,   NULL) \
    X_(SelectionRequest, NULL) \
    X_(SelectionNotify,  NULL) \
    X_(ColormapNotify,   NULL) \
    X_(ClientMessage,    &process_clientmessage) \
    X_(MappingNotify,    NULL) \
    X_(GenericEvent,     NULL)

typedef struct {
    const char *name;
    X11EventProc *proc;
} X11EventEntry;

#define X_(name_,proc_) [name_] = { .name = #name_, .proc = proc_ },
static const X11EventEntry xevent_table[LASTEvent] = { XTABLE_X11_EVENTS };
#undef X_
#undef XTABLE_X11_EVENTS

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

#define ATOM(atom_) wm_atoms[(atom_)]
static Atom wm_atoms[NUM_ATOM];

static Server server;

static bool server_init(void);
static void server_fini(void);
static void query_dimensions(Win *, int *, int *);
static void query_coordinates(Win *, int *, int *);
static uint convert_keysym(KeySym);
static uint convert_modmask(uint);
static void set_size_hints(Win *, uint, uint, uint, uint);
static void set_class_hints(Win *, char *, char *);
static void set_title(Win *, const char *, size_t);
static void set_icon(Win *, const char *, size_t);
static int pump_events(Win *, WinEventHandler *, void *);
static int queue_length(bool);

bool
server_init(void)
{
    if (server.dpy) {
        return true;
    }

    setlocale(LC_CTYPE, "");

    if (!(server.dpy = XOpenDisplay(NULL))) {
        return false;
    }
    if (!(server.gfx = gfx_create_context(server.dpy))) {
        return false;
    }

    gfx_print_info(server.gfx);

    server.screen = DefaultScreen(server.dpy);
    server.dpy_width = DisplayWidth(server.dpy, server.screen);
    server.dpy_height = DisplayHeight(server.dpy, server.screen);
    server.root = RootWindow(server.dpy, server.screen);
    server.dpi = ((server.dpy_height * 25.4f) / // 1 inch == 25.4 mm
                    (float)DisplayHeightMM(server.dpy, server.screen));

    if (XSupportsLocale()) {
        XSetLocaleModifiers("");
        server.im = XOpenIM(server.dpy, 0, NULL, NULL);
        if (server.im) {
            XIMStyles *styles = NULL;
            bool found = false;
            if (!XGetIMValues(server.im, XNQueryInputStyle, &styles, NULL)) {
                for (size_t i = 0; !found && i < styles->count_styles; i++) {
                    if (styles->supported_styles[i] == (XIMPreeditNothing|XIMStatusNothing)) {
                        found = true;
                    }
                }
                XFree(styles);
            }
            if (!found) {
                XCloseIM(server.im);
                server.im = NULL;
            }
        }
    }

#define INTERN_ATOM(atom_) ATOM((atom_)) = XInternAtom(server.dpy, #atom_, False)
    {
        INTERN_ATOM(_NET_SUPPORTED);

        uchar *supported;

        {
            Atom type_;
            int format_;
            size_t offset_;
            size_t count_;

            XGetWindowProperty(server.dpy,
                               server.root,
                               ATOM(_NET_SUPPORTED),
                               0,
                               LONG_MAX,
                               False,
                               XA_ATOM,
                               &type_,
                               &format_,
                               &count_,
                               &offset_,
                               &supported);
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

        if (supported) {
            XFree(supported);
        }
    }
#undef INTERN_ATOM

    {
        XVisualInfo visreq = { 0 };
        XVisualInfo *visinfo;
        int count_;

        visreq.visualid = gfx_get_native_visual(server.gfx);

        if (!visreq.visualid) {
            return false;
        }

        visinfo = XGetVisualInfo(server.dpy, VisualIDMask, &visreq, &count_);
        if (!visinfo) {
            return false;
        }

        server.visual   = visinfo->visual;
        server.depth    = visinfo->depth;
        server.colormap = XCreateColormap(server.dpy, server.root, server.visual, AllocNone);

        XFree(visinfo);
    }

    server.fd = ConnectionNumber(server.dpy);

    if (!server.fd) {
        return false;
    }

    return true;
}

void
server_fini(void)
{
    gfx_destroy_context(server.gfx);
    XCloseDisplay(server.dpy);
}

float
window_get_dpi(const Win *win)
{
    return server.dpi;
}

int
window_get_fileno(const Win *win)
{
    return server.fd;
}

int
window_events_pending(const Win *win)
{
    return queue_length(true);
}

bool
window_query_color(const Win *win, const char *name, uint32 *color)
{
    XColor xcolor = { 0 };

    if (!XParseColor(server.dpy, server.colormap, name, &xcolor)) {
        return false;
    }

    if (color) {
        *color = PACK_4x8(0x00,
                          xcolor.red   >> 8,
                          xcolor.green >> 8,
                          xcolor.blue  >> 8);
    }

    return true;
}

void
query_dimensions(Win *win, int *width, int *height)
{
    ASSERT(win);

    XWindowAttributes attr;
    XGetWindowAttributes(server.dpy, win->xid, &attr);

    SETPTR(width,  attr.width);
    SETPTR(height, attr.height);
}

void
query_coordinates(Win *win, int *r_x, int *r_y)
{
    ASSERT(win);

    int x, y;
    XTranslateCoordinates(server.dpy,
                          win->xid,
                          server.root,
                          0, 0,
                          &x, &y,
                          (Window [1]){0});
    SETPTR(r_x, x);
    SETPTR(r_y, y);
}

Win *
window_create(void)
{
    if (!server.dpy) {
        if (!server_init()) {
            return NULL;
        }
    }

    Win *win = &server.clients[0];

    if (!win->srv) {
        win->srv = &server;
    }

    return win;
}

static void
validate_config(WinConfig *cfg)
{
    cfg->wm_name    = DEFAULT(cfg->wm_name, "wm_name");
    cfg->wm_class   = DEFAULT(cfg->wm_class, "wm_class");
    cfg->wm_title   = DEFAULT(cfg->wm_title, "wm_title");
    cfg->inc_width  = DEFAULT(cfg->inc_width, 1);
    cfg->inc_height = DEFAULT(cfg->inc_height, 1);
    cfg->min_width  = DEFAULT(cfg->min_width, cfg->inc_width);
    cfg->min_height = DEFAULT(cfg->min_height, cfg->inc_height);
    cfg->width      = MAX(DEFAULT(cfg->width, DEFAULT_WIDTH), cfg->min_width);
    cfg->height     = MAX(DEFAULT(cfg->height, DEFAULT_HEIGHT), cfg->min_height);
    cfg->width      = MIN(cfg->width, (uint)server.dpy_width);
    cfg->height     = MIN(cfg->height, (uint)server.dpy_height);
}

bool
window_configure(Win *win, WinConfig cfg)
{
    ASSERT(win);

    validate_config(&cfg);

    win->xid = XCreateWindow(
        server.dpy,
        server.root,
        0, 0,
        cfg.width, cfg.height,
        0,
        server.depth,
        InputOutput,
        server.visual,
        CWBackPixel|CWColormap|CWBitGravity|CWEventMask,
        &(XSetWindowAttributes){
#if BUILD_DEBUG
            .background_pixel = 0xff00ff,
#else
            .background_pixel = 0,
#endif
            .colormap    = server.colormap,
            .event_mask  = DEFAULT_EVENT_MASK,
            .bit_gravity = NorthWestGravity
        }
    );

    if (!win->xid) return NULL;

    // Set WM protocols
    {
        Atom supported[] = {
            ATOM(WM_DELETE_WINDOW),
            ATOM(_NET_WM_PID),
            ATOM(_NET_WM_NAME),
            ATOM(_NET_WM_STATE)
        };
        XSetWMProtocols(server.dpy, win->xid, supported, LEN(supported));

        // link window to pid
        win->pid = getpid();
        XChangeProperty(server.dpy,
                        win->xid,
                        ATOM(_NET_WM_PID),
                        XA_CARDINAL,
                        32,
                        PropModeReplace,
                        (uchar *)&win->pid,
                        1);
    }

    // Set WM hints
    {
        XWMHints *hints = XAllocWMHints();
        if (!hints) {
            err_printf("XAllocWMHints failure\n");
            abort();
        }
        hints->flags         = (StateHint|InputHint);
        hints->initial_state = NormalState;
        hints->input         = 1;

        XSetWMHints(server.dpy, win->xid, hints);
        XFree(hints);
    }

    set_class_hints(win, cfg.wm_name, cfg.wm_class);
    set_size_hints(win, cfg.inc_width, cfg.inc_height, cfg.min_width, cfg.min_height);
    set_title(win, cfg.wm_title, strlen(cfg.wm_title));

    XFlush(server.dpy);

    if (server.im) {
        win->ic = XCreateIC(server.im,
                            XNInputStyle,
                            XIMPreeditNothing|XIMStatusNothing,
                            XNClientWindow,
                            win->xid,
                            XNFocusWindow,
                            win->xid,
                            NULL);
        if (win->ic) {
            uint64 filter = 0;
            if (!XGetICValues(win->ic, XNFilterEvents, &filter, NULL)) {
                XSelectInput(server.dpy, win->xid, DEFAULT_EVENT_MASK|filter);
            }
        }
    }

    {
        XGCValues gcvals = { 0 };
        gcvals.graphics_exposures = False;
        win->gc = XCreateGC(server.dpy, server.root, GCGraphicsExposures, &gcvals);
    }

    if (!gfx_bind_surface(server.gfx, win->xid)) {
        err_printf("Failed to bind window surface\n");
        return false;
    }

    {
        int w1, h1, w2, h2;
        query_dimensions(win, &w1, &h1);
        gfx_get_size(server.gfx, &w2, &h2);
        if (w1 != w2 || h1 != h2) {
            err_printf("Mismatched window/viewport size\n");
            return false;
        }
    }

    gfx_set_debug_object(win);

    // Broadcast $WINDOWID (inherited by child process)
    char idstr[32] = { 0 };
    snprintf(idstr, sizeof(idstr) - 1, "%ld", win->xid);
    setenv("WINDOWID", idstr, 1);

    return win;
}

void
window_destroy(Win *win)
{
    ASSERT(server.dpy);

    if (!win) return;

    gfx_bind_surface(server.gfx, 0);

    if (win->xid) {
        XDestroyWindow(server.dpy, win->xid);
    }

    if (win->srv) {
        memset(win, 0, sizeof(*win));
        server_fini();
    }
}

static bool
server_wait(uint32 timeout, int *r_error)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(server.fd, &fds);

    struct timespec ts;
    ts.tv_sec  = timeout / 1e3;
    ts.tv_nsec = (timeout - ts.tv_sec * 1e3) * 1e6;

    const int res = pselect(server.fd + 1, NULL, &fds, NULL, &ts, NULL);
    if (res < 0) {
        SETPTR(r_error, errno);
        return false;
    } else {
        SETPTR(r_error, 0);
        return (res > 0);
    }
}

bool
window_open(Win *win, int *r_width, int *r_height)
{
    ASSERT(win);

    if (window_online(win)) goto success;

    ASSERT(!win->mapped);
    XMapWindow(server.dpy, win->xid);

    for (uint32 t0 = timer_msec(NULL);;) {
        pump_events(win, NULL, NULL);

        if (window_online(win)) break;

        int error;
        if ((!server_wait(1000, &error) && error != EINTR) ||
            (timer_msec(NULL) - t0 >= 1000))
        {
            return false;
        }
    }

success:
    ASSERT(win->mapped);
    SETPTR(r_width, win->width);
    SETPTR(r_height, win->height);

    return true;
}

void
set_size_hints(Win *win,
               uint inc_width,
               uint inc_height,
               uint min_width,
               uint min_height)
{
    ASSERT(win);

    XSizeHints *hints = XAllocSizeHints();
    if (!hints) {
        err_printf("XAllocSizeHints failure\n");
        abort();
    }

    hints->flags = PMinSize|PMaxSize|PResizeInc;

    // Assume pre-validated input
    hints->width_inc  = inc_width;
    hints->height_inc = inc_height;
    hints->min_width  = min_width;
    hints->min_height = min_height;
    hints->max_width  = server.dpy_width;
    hints->max_height = server.dpy_height;

    XSetWMNormalHints(server.dpy, win->xid, hints);
    XFree(hints);
}

void
set_class_hints(Win *win, char *wm_name, char *wm_class)
{
    ASSERT(win);

    XClassHint *hints = XAllocClassHint();
    if (!hints) {
        err_printf("XAllocClassHint failure\n");
        abort();
    }

    // Assume pre-validated input
    hints->res_name  = wm_name;
    hints->res_class = wm_class;

    XSetClassHint(server.dpy, win->xid, hints);
    XFree(hints);
}

bool
window_online(const Win *win)
{
    return (win) ? win->online : false;
}

void
window_refresh(const Win *win)
{
    if (window_online(win)) {
        gfx_swap_buffers(server.gfx);
    }
}

bool
window_make_current(const Win *win)
{
    return (win && server.gfx);
}

int
window_width(const Win *win)
{
    return (win) ? win->width : 0;
}

int
window_height(const Win *win)
{
    return (win) ? win->height : 0;
}

static void
set_utf8_property(Win *win,
                  const char *str, size_t len,
                  int atom_id,
                  void (*func)(Display *, Window, XTextProperty *))
{
    if (!win) return;

    ASSERT(str && *str && len);

    char *name;
    errno = 0;
    if (!(name = strndup(str, len))) {
        err_printf("strndup(%s, %zu): %s\n", str, len, strerror(errno));
        abort();
    }

    XTextProperty txtprop;
    Xutf8TextListToTextProperty(server.dpy, &name, 1, XUTF8StringStyle, &txtprop);
    XSetTextProperty(server.dpy, win->xid, &txtprop, ATOM(atom_id));
    if (func) {
        func(server.dpy, win->xid, &txtprop);
    }

    XFree(txtprop.value);
    free(name);
}

void
set_title(Win *win, const char *str, size_t len)
{
    set_utf8_property(win, str, len, _NET_WM_NAME, XSetWMName);
}

void
set_icon(Win *win, const char *str, size_t len)
{
    set_utf8_property(win, str, len, _NET_WM_ICON_NAME, XSetWMIconName);
}

void
window_set_title(Win *win, const char *str, size_t len)
{
    if (window_online(win)) {
        set_title(win, str, len);
    }
}

void
window_set_icon(Win *win, const char *str, size_t len)
{
    if (window_online(win)) {
        set_icon(win, str, len);
    }
}

int
queue_length(bool flush)
{
    return XEventsQueued(server.dpy, (flush) ? QueuedAfterFlush : QueuedAlready);
}

int
pump_events(Win *win, WinEventHandler *handler, void *arg)
{
    int count = 0;

    XEvent xevent;
    X11EventProcParams params = {
        .win = win,
        .arg = DEFAULT(arg, win),
        .handler = handler,
        .xevent = &xevent,
    };

    for (bool flush = true; queue_length(flush); flush = false) {
        XNextEvent(server.dpy, &xevent);
        if (!XFilterEvent(&xevent, None) && xevent.xany.window == win->xid) {
            const X11EventEntry entry = xevent_table[xevent.type];
            if (entry.proc) {
                count += !!entry.proc(&params);
            }
        }
    }

    return count;
}

int
window_pump_events(Win *win, WinEventHandler *handler, void *arg)
{
    if (window_online(win)) {
        return pump_events(win, handler, arg);
    }

    return 0;
}

static inline bool
is_literal_ascii(KeySym xkey)
{
    return ((xkey >= 0x20 && xkey <= 0x7e) || (xkey >= 0xa0 && xkey <= 0xff));
}

uint
convert_keysym(KeySym xkey)
{
    if (is_literal_ascii(xkey)) {
        return xkey;
    } else if ((xkey >> 8) == 0xff) {
        // Recognized function keys
        switch (xkey) {
        case XK_Escape:       return KeyEscape;
        case XK_Return:       return KeyReturn;
        case XK_Tab:          return KeyTab;
        case XK_ISO_Left_Tab: return KeyTab;
        case XK_BackSpace:    return KeyBackspace;
        case XK_Insert:       return KeyInsert;
        case XK_Delete:       return KeyDelete;
        case XK_Right:        return KeyRight;
        case XK_Left:         return KeyLeft;
        case XK_Down:         return KeyDown;
        case XK_Up:           return KeyUp;
        case XK_Page_Up:      return KeyPgUp;
        case XK_Page_Down:    return KeyPgDown;
        case XK_Home:         return KeyHome;
        case XK_Begin:        return KeyBegin;
        case XK_End:          return KeyEnd;
        case XK_F1:           return KeyF1;
        case XK_F2:           return KeyF2;
        case XK_F3:           return KeyF3;
        case XK_F4:           return KeyF4;
        case XK_F5:           return KeyF5;
        case XK_F6:           return KeyF6;
        case XK_F7:           return KeyF7;
        case XK_F8:           return KeyF8;
        case XK_F9:           return KeyF9;
        case XK_F10:          return KeyF10;
        case XK_F11:          return KeyF11;
        case XK_F12:          return KeyF12;
        case XK_F13:          return KeyF13;
        case XK_F14:          return KeyF14;
        case XK_F15:          return KeyF15;
        case XK_F16:          return KeyF16;
        case XK_F17:          return KeyF17;
        case XK_F18:          return KeyF18;
        case XK_F19:          return KeyF19;
        case XK_F20:          return KeyF20;
        case XK_F21:          return KeyF21;
        case XK_F22:          return KeyF22;
        case XK_F23:          return KeyF23;
        case XK_F24:          return KeyF24;
        case XK_F25:          return KeyF25;
        case XK_KP_0:         return KeyKP0;
        case XK_KP_1:         return KeyKP1;
        case XK_KP_2:         return KeyKP2;
        case XK_KP_3:         return KeyKP3;
        case XK_KP_4:         return KeyKP4;
        case XK_KP_5:         return KeyKP5;
        case XK_KP_6:         return KeyKP6;
        case XK_KP_7:         return KeyKP7;
        case XK_KP_8:         return KeyKP8;
        case XK_KP_9:         return KeyKP9;
        case XK_KP_Decimal:   return KeyKPDecimal;
        case XK_KP_Divide:    return KeyKPDivide;
        case XK_KP_Multiply:  return KeyKPMultiply;
        case XK_KP_Subtract:  return KeyKPSubtract;
        case XK_KP_Add:       return KeyKPAdd;
        case XK_KP_Enter:     return KeyKPEnter;
        case XK_KP_Equal:     return KeyKPEqual;
        case XK_KP_Tab:       return KeyKPTab;
        case XK_KP_Space:     return KeyKPSpace;
        case XK_KP_Insert:    return KeyKPInsert;
        case XK_KP_Delete:    return KeyKPDelete;
        case XK_KP_Right:     return KeyKPRight;
        case XK_KP_Left:      return KeyKPLeft;
        case XK_KP_Down:      return KeyKPDown;
        case XK_KP_Up:        return KeyKPUp;
        case XK_KP_Page_Up:   return KeyKPPgUp;
        case XK_KP_Page_Down: return KeyKPPgDown;
        case XK_KP_Home:      return KeyKPHome;
        case XK_KP_Begin:     return KeyKPBegin;
        case XK_KP_End:       return KeyKPEnd;
        }
    } else if ((xkey >> 16) == 0x1008) {
        // Commonly confused multimedia keys
        switch (xkey) {
        case XF86XK_Back:    return KeyPgUp;
        case XF86XK_Forward: return KeyPgDown;
        }
    }

    return 0;
}

uint
convert_modmask(uint xmods)
{
    uint mods = 0;

    mods |= (xmods & ShiftMask)   ? KEYMOD_SHIFT : 0;
    mods |= (xmods & Mod1Mask)    ? KEYMOD_ALT   : 0;
    mods |= (xmods & ControlMask) ? KEYMOD_CTRL  : 0;
    mods |= (xmods & Mod2Mask)    ? KEYMOD_NUMLK : 0;

    return mods;
}

#define DEFAULT_HANDLER(suffix,tag) \
inline bool                                        \
process_##suffix(const X11EventProcParams *params) \
{                                                  \
    WinEvent event;                                \
    event_init(&event, (tag), 0);                  \
    if (params->handler) {                         \
        params->handler(params->arg, &event);      \
    }                                              \
    return true;                                   \
}

DEFAULT_HANDLER(buttonpress, EVENT_BUTTONPRESS)
DEFAULT_HANDLER(buttonrelease, EVENT_BUTTONRELEASE)
DEFAULT_HANDLER(focusin, EVENT_FOCUS)
DEFAULT_HANDLER(focusout, EVENT_UNFOCUS)
DEFAULT_HANDLER(expose, EVENT_EXPOSE)
DEFAULT_HANDLER(keyrelease, EVENT_KEYRELEASE)
#undef DEFAULT_HANDLER

inline bool
process_motionnotify(const X11EventProcParams *params)
{
    // XMotionEvent *xevent = &params->xevent->xmotion;
    UNUSED(params);

    return true;
}

inline bool
process_visibilitynotify(const X11EventProcParams *params)
{
    XVisibilityEvent *xevent = &params->xevent->xvisibility;

    ASSERT(params->win->mapped);

    // The first VisibilityNotify event is our indication that the window is stable.
    // Until then, we can't make assumptions about the geometry - even if it's mapped.
    if (!window_online(params->win)) {
        WinEvent event;
        event_init(&event, EVENT_OPEN, 0);

        if (params->handler) {
            params->handler(params->arg, &event);
        }

        params->win->online = true;
    }

    params->win->visible = (xevent->state != VisibilityFullyObscured);

    return true;
}

inline bool
process_mapnotify(const X11EventProcParams *params)
{
    // XMapEvent *xevent = &params->xevent->xmap;

    params->win->mapped = true;

    return true;
}

inline bool
process_unmapnotify(const X11EventProcParams *params)
{
    // XUnmapEvent *xevent = &params->xevent->xunmap;

    params->win->mapped = false;

    return true;
}

inline bool
process_configurenotify(const X11EventProcParams *params)
{
    XConfigureEvent *xevent = &params->xevent->xconfigure;
    WinGeomEvent event;

    event_init((WinEvent *)&event, EVENT_RESIZE, 0);

    // The coords in the XConfigureEvent aren't representative of the actual viewport.
    // We need to do the translation ourselves.
    query_coordinates(params->win, &event.x, &event.y);
    event.width  = xevent->width;
    event.height = xevent->height;

    if (params->handler) {
        params->handler(params->arg, (const WinEvent *)&event);
    }

    if (event.width != params->win->width ||
        event.height != params->win->height)
    {
        gfx_resize(server.gfx, event.width, event.height);
        params->win->width  = event.width;
        params->win->height = event.height;
    }

    params->win->x = event.x;
    params->win->y = event.y;

    return true;
}

inline bool
process_keypress(const X11EventProcParams *params)
{
    XKeyEvent *xevent = &params->xevent->xkey;
    WinKeyEvent event;

    char buf[sizeof(event.data)] = { 0 };
    int len;
    KeySym xkey;

    event_init((WinEvent *)&event, EVENT_KEYPRESS, 0);

    if (!params->win->ic) {
        len = XLookupString(xevent, buf, sizeof(buf), &xkey, NULL);
    } else {
        Status status;
        len = XmbLookupString(params->win->ic, xevent, buf, sizeof(buf), &xkey, &status);
        if (status == XBufferOverflow) {
            event.info.error = EMSGSIZE;
            len = 0;
        }
    }

    event.key  = convert_keysym(xkey);
    event.mods = convert_modmask(xevent->state);
    event.len  = len;

    if (!event.len  &&
        !event.key  &&
        !event.mods &&
        !event.info.error)
    {
        return false;
    }

    if (params->handler) {
        memcpy(event.data, buf, len);
        params->handler(params->arg, (WinEvent *)&event);
    }

    return true;
}

inline bool
process_clientmessage(const X11EventProcParams *params)
{
    XClientMessageEvent *xevent = &params->xevent->xclient;
    WinEvent event;
    bool send = false;

    if ((Atom)xevent->data.l[0] == ATOM(WM_DELETE_WINDOW)) {
        event_init(&event, EVENT_CLOSE, 0);
        params->win->online = false;
        send = true;
    }

    if (send) {
        if (params->handler) {
            params->handler(params->arg, &event);
        }
    }

    return send;
}

