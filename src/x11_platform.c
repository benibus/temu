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
#include "x11_platform.h"

#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <unistd.h>

#define X11_EVENT_MASK \
  (StructureNotifyMask|KeyPressMask|KeyReleaseMask|     \
   PointerMotionMask|ButtonPressMask|ButtonReleaseMask| \
   ExposureMask|FocusChangeMask|VisibilityChangeMask|   \
   EnterWindowMask|LeaveWindowMask|PropertyChangeMask)

#define X11_EVENT_TABLE \
    X_(KeyPress,         xhandler_key_press) \
    X_(KeyRelease,       NULL) \
    X_(ButtonPress,      NULL) \
    X_(ButtonRelease,    NULL) \
    X_(MotionNotify,     NULL) \
    X_(EnterNotify,      NULL) \
    X_(LeaveNotify,      NULL) \
    X_(FocusIn,          NULL) \
    X_(FocusOut,         NULL) \
    X_(KeymapNotify,     NULL) \
    X_(Expose,           NULL) \
    X_(GraphicsExpose,   NULL) \
    X_(NoExpose,         NULL) \
    X_(VisibilityNotify, NULL) \
    X_(CreateNotify,     NULL) \
    X_(DestroyNotify,    NULL) \
    X_(UnmapNotify,      NULL) \
    X_(MapNotify,        NULL) \
    X_(MapRequest,       NULL) \
    X_(ReparentNotify,   NULL) \
    X_(ConfigureNotify,  xhandler_configure_notify ) \
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
    X_(ClientMessage,    xhandler_client_message) \
    X_(MappingNotify,    NULL) \
    X_(GenericEvent,     NULL)

static void xhandler_client_message(XEvent *, Win *, uint32);
static void xhandler_configure_notify(XEvent *, Win *, uint32);
static void xhandler_key_press(XEvent *, Win *, uint32);

struct X11Event {
    const char *name;
    void (*handler)(XEvent *, Win *, uint32);
};

static const struct X11Event xevent_table[] = {
#define X_(symbol_,handler_) [symbol_] = { .name = #symbol_, .handler = handler_ },
    X11_EVENT_TABLE
#undef X_
    { 0 }
};
#undef X11_EVENT_TABLE

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

static void query_dimensions(Win *, int *, int *, int *);
static void query_coordinates(Win *, int *, int *);
static int translate_key(uint, uint, int *, int *);

bool
platform_setup(void)
{
    static_assert(LEN(xevent_table) >= LASTEvent, "Insufficient table size");

    if (server.dpy) {
        return true;
    }

    setlocale(LC_CTYPE, "");

    if (!(server.dpy = XOpenDisplay(NULL))) {
        return false;
    }
    if (!(server.egl.dpy = eglGetDisplay((EGLNativeDisplayType)server.dpy))) {
        return false;
    }
    if (!(eglInitialize(server.egl.dpy, &server.egl.version.major, &server.egl.version.minor))) {
        return false;
    }

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

        byte *supported;
        {
            Atom type_;
            int format_;
            size_t offset_;
            size_t count_;

            XGetWindowProperty(
                server.dpy,
                server.root,
                ATOM(_NET_SUPPORTED),
                0, LONG_MAX, False,
                XA_ATOM,
                &type_, &format_, &count_, &offset_,
                &supported
            );
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
        XVisualInfo template = { 0 };
        XVisualInfo *visinfo;
        int count_;
        static const EGLint eglattrs[] = {
            EGL_RED_SIZE,        8,
            EGL_GREEN_SIZE,      8,
            EGL_BLUE_SIZE,       8,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };
        EGLint visid, nconfig;

        if (!eglChooseConfig(server.egl.dpy, eglattrs, &server.egl.config, 1, &nconfig)) {
            return false;
        }
        ASSERT(server.egl.config && nconfig > 0);
        if (!eglGetConfigAttrib(server.egl.dpy, server.egl.config, EGL_NATIVE_VISUAL_ID, &visid)) {
            return false;
        }
        template.visualid = visid;

        visinfo = XGetVisualInfo(server.dpy, VisualIDMask, &template, &count_);
        if (!visinfo) {
            return false;
        }

        server.visual   = visinfo->visual;
        server.depth    = visinfo->depth;
        server.colormap = XCreateColormap(server.dpy, server.root, server.visual, AllocNone);

        XFree(visinfo);

        eglBindAPI(EGL_OPENGL_ES_API);

        // TODO(ben): Global or window-specific context?
        server.egl.context = eglCreateContext(
            server.egl.dpy,
            server.egl.config,
            EGL_NO_CONTEXT,
            (EGLint []){
                EGL_CONTEXT_CLIENT_VERSION, 2,
#if (defined(GL_ES_VERSION_3_2) && BUILD_DEBUG)
                EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE,
#endif
                EGL_NONE
            }
        );

        if (!server.egl.context) {
            dbgprint("Failed to open EGL context");
            return false;
        }

        EGLint result = 0;
        eglQueryContext(server.egl.dpy, server.egl.context, EGL_CONTEXT_CLIENT_TYPE, &result);
        ASSERT(result == EGL_OPENGL_ES_API);
    }

    server.fd = ConnectionNumber(server.dpy);

    if (!server.fd) {
        return false;
    }

    return true;
}

void
platform_shutdown()
{
    window_make_current(NULL);
    eglDestroyContext(server.egl.dpy, server.egl.context);
    eglTerminate(server.egl.dpy);
    XCloseDisplay(server.dpy);
}

float
platform_get_dpi(void)
{
    return server.dpi;
}

int
platform_events_pending(void)
{
    return XPending(server.dpy);
}

int
platform_get_fileno(void)
{
    return server.fd;
}

bool
platform_parse_color_string(const char *name, uint32 *color)
{
    XColor xcolor = { 0 };

    if (!XParseColor(server.dpy, server.colormap, name, &xcolor)) {
        return false;
    }

    if (color) {
        *color = pack_argb(
            xcolor.red   >> 8,
            xcolor.green >> 8,
            xcolor.blue  >> 8,
            0x00
        );
    }

    return true;
}

void
query_dimensions(Win *win, int *width, int *height, int *border)
{
    ASSERT(win);

    XWindowAttributes attr;
    XGetWindowAttributes(server.dpy, win->xid, &attr);

    if (width)  *width  = attr.width;
    if (height) *height = attr.height;
    if (border) *border = attr.border_width;
}

void
query_coordinates(Win *win, int *xpos, int *ypos)
{
    ASSERT(win);

    Window dummy_;
    int xpos_, ypos_;

    XTranslateCoordinates(
        server.dpy,
        win->xid,
        server.root,
        0, 0,
        &xpos_, &ypos_,
        &dummy_
    );

    if (xpos) *xpos = xpos_;
    if (ypos) *ypos = ypos_;
}

Win *
window_create(WinConfig config)
{
    if (!server.dpy) {
        if (!platform_setup()) {
            return false;
        }
    }

    config.cols  = DEFAULT(config.cols,  1);
    config.rows  = DEFAULT(config.rows,  1);
    config.colpx = DEFAULT(config.colpx, 1);
    config.rowpx = DEFAULT(config.rowpx, 1);

    const uint16 border     = config.border;
    const uint16 inc_width  = config.colpx;
    const uint16 inc_height = config.rowpx;
    const uint16 width      = 2 * config.border + config.cols * config.colpx;
    const uint16 height     = 2 * config.border + config.rows * config.rowpx;

    Win *win = NULL;
    for (uint i = 0; i < LEN(server.clients); i++) {
        if (!server.clients[i].server) {
            win = &server.clients[i];
            memset(win, 0, sizeof(*win));
            win->server = &server;
            break;
        }
    }

    ASSERT(win);

    win->xid = XCreateWindow(
        server.dpy,
        server.root,
        0, 0,
        width, height,
        border,
        server.depth,
        InputOutput,
        server.visual,
        CWBackPixel|CWColormap|CWBitGravity|CWEventMask,
        &(XSetWindowAttributes){
#if BUILD_DEBUG
            .background_pixel = 0,
#else
            .background_pixel = 0xff00ff,
#endif
            .colormap    = server.colormap,
            .event_mask  = X11_EVENT_MASK,
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
        XChangeProperty(
            server.dpy,
            win->xid,
            ATOM(_NET_WM_PID),
            XA_CARDINAL,
            32,
            PropModeReplace,
            (byte *)&win->pid, 1
        );
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

        XSetWMHints(server.dpy, win->xid, hintp);
        XFree(hintp);
    }

    // Set WM_CLASS hints
    if (config.wm_instance && config.wm_class) {
        XClassHint *hintp = XAllocClassHint();
        hintp->res_name  = config.wm_instance;
        hintp->res_class = config.wm_class;

        XSetClassHint(server.dpy, win->xid, hintp);
        XFree(hintp);
    }

    // Set WM_SIZE hints
    {
        XSizeHints *hintp = XAllocSizeHints();

        hintp->flags = PBaseSize|PMinSize|PResizeInc;

        hintp->base_width  = width;
        hintp->base_height = height;
        hintp->min_width   = 2 * border + inc_width;
        hintp->min_height  = 2 * border + inc_height;

        if (config.smooth_resize) {
            hintp->width_inc  = 1;
            hintp->height_inc = 1;
        } else {
            hintp->width_inc  = inc_width;
            hintp->height_inc = inc_height;
        }

        XSetNormalHints(server.dpy, win->xid, hintp);
        if (config.wm_title) {
            XSetStandardProperties(server.dpy, win->xid, config.wm_title, NULL, None, NULL, 0, hintp);
        }
        XFree(hintp);
    }

    XFlush(server.dpy);

    if (server.im) {
        win->ic = XCreateIC(
            server.im,
            XNInputStyle,
            XIMPreeditNothing|XIMStatusNothing,
            XNClientWindow,
            win->xid,
            XNFocusWindow,
            win->xid,
            NULL
        );
        if (win->ic) {
            uint64 filter = 0;
            if (!XGetICValues(win->ic, XNFilterEvents, &filter, NULL)) {
                XSelectInput(server.dpy, win->xid, X11_EVENT_MASK|filter);
            }
        }
    }

    {
        XGCValues gcvals = { 0 };
        gcvals.graphics_exposures = False;
        win->gc = XCreateGC(server.dpy, server.root, GCGraphicsExposures, &gcvals);
    }

    query_dimensions(win, &win->width, &win->height, &win->border);
    query_coordinates(win, &win->xpos, &win->ypos);

    win->param = config.param;
    win->callbacks = config.callbacks;

    // OpenGL surface initialization
    win->surface = eglCreateWindowSurface(
        server.egl.dpy,
        server.egl.config,
        win->xid,
        NULL
    );
    if (!win->surface) {
        dbgprint("Failed to create EGL surface");
        return NULL;
    }

    {
        EGLint result;

        eglQuerySurface(server.egl.dpy, win->surface, EGL_WIDTH, &result);
        ASSERT(result == width);
        eglQuerySurface(server.egl.dpy, win->surface, EGL_HEIGHT, &result);
        ASSERT(result == height);
        eglGetConfigAttrib(server.egl.dpy, server.egl.config, EGL_SURFACE_TYPE, &result);
        ASSERT(result & EGL_WINDOW_BIT);
    }

    if (window_make_current(win)) {
        gl_set_debug_object(win);
        egl_print_info(server.egl.dpy);
    } else {
        dbgprint("Failed to set current window");
        return NULL;
    }

    eglSwapInterval(server.egl.dpy, 0);

    return win;
}

void
window_destroy(Win *win)
{
    ASSERT(win && win->xid);
    if (win->surface) {
        eglDestroySurface(server.egl.dpy, win->surface);
        window_make_current(NULL);
    }
    XDestroyWindow(server.dpy, win->xid);
    win->online = false;
}

bool
window_show(Win *win)
{
    if (win->online) {
        return true;
    }

    XMapWindow(server.dpy, win->xid);
    XSync(server.dpy, False);

    struct pollfd pollset = { server.fd, POLLIN, 0 };
    const int timeout = 100;

    XEvent dummy_;
    while (!XCheckTypedWindowEvent(server.dpy, win->xid, VisibilityNotify, &dummy_)) {
        int result = 0;
        if ((result = poll(&pollset, 1, timeout)) <= 0) {
            if (result && errno) {
                perror("poll()");
            }
            goto done;
        } else if (pollset.revents & POLLHUP) {
            goto done;
        }
    }

    query_coordinates(win, &win->xpos, &win->ypos);
    win->online = true;
done:
    return win->online;
}

bool
window_online(const Win *win)
{
    return win->online;
}

void
window_update(const Win *win)
{
    eglSwapBuffers(server.egl.dpy, win->surface);
}

bool
window_make_current(const Win *win)
{
    if (!eglMakeCurrent(server.egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
        return false;
    }
    if (win && !eglMakeCurrent(server.egl.dpy, win->surface, win->surface, server.egl.context)) {
        return false;
    }

    return true;
}

void
window_get_dimensions(const Win *win, int *width, int *height, int *border)
{
    ASSERT(win);

    if (width)  *width  = win->width;
    if (height) *height = win->height;
    if (border) *border = win->border;
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
    if (!(name = strndup(str, len))) {
        printerr("ERROR strndup: %s", strerror(errno));
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
window_set_title(Win *win, const char *str, size_t len)
{
    set_utf8_property(win, str, len, _NET_WM_NAME, XSetWMName);
}

void
window_set_icon(Win *win, const char *str, size_t len)
{
    set_utf8_property(win, str, len, _NET_WM_ICON_NAME, XSetWMIconName);
}

static uint convert_mod(uint);
static uint convert_key(uint);

uint
convert_mod(uint xmod)
{
    uint mod = 0;
    mod |= (xmod & ShiftMask)   ? MOD_SHIFT : 0;
    mod |= (xmod & Mod1Mask)    ? MOD_ALT   : 0;
    mod |= (xmod & ControlMask) ? MOD_CTRL  : 0;

    return mod;
}

uint
convert_key(uint xkey)
{
    if ((xkey & 0xff00) && !(xkey & ~0xffff)) {
        switch (xkey) {
        case XK_Escape:       return KeyEscape;
        case XK_Return:       return KeyReturn;
        case XK_Tab:          return KeyTab;
        case XK_BackSpace:    return KeyBackspace;
        case XK_Insert:       return KeyInsert;
        case XK_Delete:       return KeyDelete;
        case XK_Right:        return KeyRight;
        case XK_Left:         return KeyLeft;
        case XK_Down:         return KeyDown;
        case XK_Up:           return KeyUp;
        case XK_Page_Up:      return KeyPageUp;
        case XK_Page_Down:    return KeyPageDown;
        case XK_Home:         return KeyHome;
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
        case XK_KP_Page_Up:   return KeyKPPageUp;
        case XK_KP_Page_Down: return KeyKPPageDown;
        case XK_KP_Home:      return KeyKPHome;
        case XK_KP_End:       return KeyKPEnd;
        }
    }

    return 0;
}

int
window_poll_events(Win *win)
{
    ASSERT(win);

    if (!win->online) {
        return 0;
    }

    int count = 0;

    for (; XPending(server.dpy); count++) {
        XEvent event = { 0 };
        XNextEvent(server.dpy, &event);

        if (!XFilterEvent(&event, None) && event.xany.window == win->xid) {
            const struct X11Event entry = xevent_table[event.type];
            if (entry.handler) {
                entry.handler(&event, win, 0);
            }
        }
    }

    XFlush(server.dpy);

    return count;
}

inline void
xhandler_configure_notify(XEvent *event_, Win *win, uint32 time)
{
    UNUSED(time);
    XConfigureEvent *event = &event_->xconfigure;

    if (win->callbacks.resize) {
        win->callbacks.resize(win->param, event->width, event->height);
    }

    win->width  = event->width;
    win->height = event->height;
}

inline void
xhandler_key_press(XEvent *event_, Win *win, uint32 time)
{
    UNUSED(time);
    XKeyEvent *event = &event_->xkey;

    byte local[128] = { 0 };
    byte *data = local;
    int max = LEN(local);
    int len = 0;
    KeySym xkey;

    if (!win->ic) {
        len = XLookupString(event, (char *)data, max - 1, &xkey, NULL);
    } else {
        Status status;
        for (;;) {
            len = XmbLookupString(win->ic, event, (char *)data, max - 1, &xkey, &status);
            if (status != XBufferOverflow) {
                break;
            } else if (data != local) { // paranoia
                return;
            } else {
                max = len + 1;
                data = xcalloc(max, 1);
            }
        }
    }

    const uint key = convert_key(xkey);
    const uint mod = convert_mod(event->state);

    if (key || mod || len) {
        if (win->callbacks.key_press) {
            win->callbacks.key_press(win->param, key, mod, data, len);
        }
    }

    if (data != local) {
        free(data);
    }
}

inline void
xhandler_client_message(XEvent *event_, Win *win, uint32 time)
{
    UNUSED(time);
    XClientMessageEvent *event = &event_->xclient;

    if ((Atom)event->data.l[0] == ATOM(WM_DELETE_WINDOW)) {
        win->online = false;
    }
}

