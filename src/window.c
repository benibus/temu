#include "utils.h"
#include "window.h"
#define OPENGL_INCLUDE_PLATFORM 1
#include "opengl.h"

#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

#define XW_EVENT_MASK \
  (StructureNotifyMask|KeyPressMask|KeyReleaseMask|     \
   PointerMotionMask|ButtonPressMask|ButtonReleaseMask| \
   ExposureMask|FocusChangeMask|VisibilityChangeMask|   \
   EnterWindowMask|LeaveWindowMask|PropertyChangeMask)

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

struct Server {
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
    struct {
        EGLDisplay dpy;
        EGLContext context;
        EGLConfig config;
        struct {
            EGLint major;
            EGLint minor;
        } version;
    } egl;
};

static struct Server server;

struct Win_ {
    void *param;
    struct Server *server;
    Window xid;
    XIC ic;
    GC gc;
    EGLSurface surface;
    bool online;
    int pid;
    int xpos;
    int ypos;
    int width;
    int height;
    int border;
    struct {
        EventFuncResize   resize;
        EventFuncKeyPress keypress;
        EventFuncExpose   expose;
    } callbacks;
};

static Atom wm_atoms[NUM_ATOM];
static Win clients[4];

static void x11_query_dimensions(Win *win, int *width, int *height, int *border);
static void x11_query_coordinates(Win *win, int *xpos, int *ypos);

bool
server_setup(void)
{
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
                    if (styles->supported_styles[i] ==
                        (XIMPreeditNothing|XIMStatusNothing))
                    {
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

Win *
server_create_window(struct WinConfig config)
{
    if (!server.dpy) {
        if (!server_setup()) {
            return false;
        }
    }

    config.wm_title    = DEFAULT(config.wm_title,    "WindowTitle");
    config.wm_instance = DEFAULT(config.wm_instance, "WindowInstance");
    config.wm_class    = DEFAULT(config.wm_class,    "WindowClass");
    config.cols        = DEFAULT(config.cols,  1);
    config.rows        = DEFAULT(config.rows,  1);
    config.colpx       = DEFAULT(config.colpx, 1);
    config.rowpx       = DEFAULT(config.rowpx, 1);

    const uint16 border     = config.border;
    const uint16 inc_width  = config.colpx;
    const uint16 inc_height = config.rowpx;
    const uint16 width      = 2 * config.border + config.cols * config.colpx;
    const uint16 height     = 2 * config.border + config.rows * config.rowpx;

    Win *win = NULL;
    for (uint i = 0; i < LEN(clients); i++) {
        if (!clients[i].server) {
            win = &clients[i];
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
            .event_mask  = XW_EVENT_MASK,
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
            (uchar *)&win->pid, 1
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
    {
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
        XSetStandardProperties(server.dpy, win->xid, config.wm_title, NULL, None, NULL, 0, hintp);
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
                XSelectInput(server.dpy, win->xid, XW_EVENT_MASK|filter);
            }
        }
    }

    {
        XGCValues gcvals = { 0 };
        gcvals.graphics_exposures = False;
        win->gc = XCreateGC(server.dpy, server.root, GCGraphicsExposures, &gcvals);
    }

    x11_query_dimensions(win, &win->width, &win->height, &win->border);
    x11_query_coordinates(win, &win->xpos, &win->ypos);

    win->param = config.param;
    win->callbacks.resize   = config.callbacks.resize;
    win->callbacks.keypress = config.callbacks.keypress;
    win->callbacks.expose   = config.callbacks.expose;

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

void
server_shutdown()
{
    window_make_current(NULL);
    eglDestroyContext(server.egl.dpy, server.egl.context);
    eglTerminate(server.egl.dpy);
    XCloseDisplay(server.dpy);
}

float
server_get_dpi(void)
{
    return server.dpi;
}

int
server_events_pending(void)
{
    return XPending(server.dpy);
}

int
server_get_fileno(void)
{
    return server.fd;
}

bool
server_parse_color_string(const char *name, uint32 *result)
{
    XColor xcolor = { 0 };

    if (!XParseColor(server.dpy, server.colormap, name, &xcolor)) {
        return false;
    }

    if (result) {
        *result = pack_argb(
            xcolor.red   >> 8,
            xcolor.green >> 8,
            xcolor.blue  >> 8,
            0x00
        );
    }

    return true;
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

    x11_query_coordinates(win, &win->xpos, &win->ypos);
    win->online = true;
done:
    return win->online;
}

int
x11_translate_key(uint ksym, uint mask, int *id_, int *mod_)
{
    int id = KeyNone;
    int mod = ModNone;

    // X11's printable Latin range (keysymdef.h)
    if ((ksym > 0x01f && ksym < 0x07f) ||
        (ksym > 0x09f && ksym < 0x100)) {
        id = ksym;
    } else if (ksym & 0x100) {
#define SETKEY(k1,k2) case (k1): { id = (k2); }; break
        switch (ksym) {
        SETKEY(XK_Escape,       KeyEscape);
        SETKEY(XK_Return,       KeyReturn);
        SETKEY(XK_Tab,          KeyTab);
        SETKEY(XK_BackSpace,    KeyBackspace);
        SETKEY(XK_Insert,       KeyInsert);
        SETKEY(XK_Delete,       KeyDelete);
        SETKEY(XK_Right,        KeyRight);
        SETKEY(XK_Left,         KeyLeft);
        SETKEY(XK_Down,         KeyDown);
        SETKEY(XK_Up,           KeyUp);
        SETKEY(XK_Page_Up,      KeyPageUp);
        SETKEY(XK_Page_Down,    KeyPageDown);
        SETKEY(XK_Home,         KeyHome);
        SETKEY(XK_End,          KeyEnd);

        SETKEY(XK_F1,           KeyF1);
        SETKEY(XK_F2,           KeyF2);
        SETKEY(XK_F3,           KeyF3);
        SETKEY(XK_F4,           KeyF4);
        SETKEY(XK_F5,           KeyF5);
        SETKEY(XK_F6,           KeyF6);
        SETKEY(XK_F7,           KeyF7);
        SETKEY(XK_F8,           KeyF8);
        SETKEY(XK_F9,           KeyF9);
        SETKEY(XK_F10,          KeyF10);
        SETKEY(XK_F11,          KeyF11);
        SETKEY(XK_F12,          KeyF12);
        SETKEY(XK_F13,          KeyF13);
        SETKEY(XK_F14,          KeyF14);
        SETKEY(XK_F15,          KeyF15);
        SETKEY(XK_F16,          KeyF16);
        SETKEY(XK_F17,          KeyF17);
        SETKEY(XK_F18,          KeyF18);
        SETKEY(XK_F19,          KeyF19);
        SETKEY(XK_F20,          KeyF20);
        SETKEY(XK_F21,          KeyF21);
        SETKEY(XK_F22,          KeyF22);
        SETKEY(XK_F23,          KeyF23);
        SETKEY(XK_F24,          KeyF24);
        SETKEY(XK_F25,          KeyF25);

        SETKEY(XK_KP_0,         KeyKP0);
        SETKEY(XK_KP_1,         KeyKP1);
        SETKEY(XK_KP_2,         KeyKP2);
        SETKEY(XK_KP_3,         KeyKP3);
        SETKEY(XK_KP_4,         KeyKP4);
        SETKEY(XK_KP_5,         KeyKP5);
        SETKEY(XK_KP_6,         KeyKP6);
        SETKEY(XK_KP_7,         KeyKP7);
        SETKEY(XK_KP_8,         KeyKP8);
        SETKEY(XK_KP_9,         KeyKP9);

        SETKEY(XK_KP_Decimal,   KeyKPDecimal);
        SETKEY(XK_KP_Divide,    KeyKPDivide);
        SETKEY(XK_KP_Multiply,  KeyKPMultiply);
        SETKEY(XK_KP_Subtract,  KeyKPSubtract);
        SETKEY(XK_KP_Add,       KeyKPAdd);
        SETKEY(XK_KP_Enter,     KeyKPEnter);
        SETKEY(XK_KP_Equal,     KeyKPEqual);
        SETKEY(XK_KP_Tab,       KeyKPTab);
        SETKEY(XK_KP_Space,     KeyKPSpace);
        SETKEY(XK_KP_Insert,    KeyKPInsert);
        SETKEY(XK_KP_Delete,    KeyKPDelete);
        SETKEY(XK_KP_Right,     KeyKPRight);
        SETKEY(XK_KP_Left,      KeyKPLeft);
        SETKEY(XK_KP_Down,      KeyKPDown);
        SETKEY(XK_KP_Up,        KeyKPUp);
        SETKEY(XK_KP_Page_Up,   KeyKPPageUp);
        SETKEY(XK_KP_Page_Down, KeyKPPageDown);
        SETKEY(XK_KP_Home,      KeyKPHome);
        SETKEY(XK_KP_End,       KeyKPEnd);

        default: goto done;
        }
#undef  SETKEY

        mod |= (mask & ShiftMask) ? ModShift : 0;
    }

done:
    if (id >= 0) {
        mod |= (mask & Mod1Mask)    ? ModAlt  : 0;
        mod |= (mask & ControlMask) ? ModCtrl : 0;
    }

    *id_  = id;
    *mod_ = mod;

    return id;
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
        if (XFilterEvent(&event, None)) {
            continue;
        }

        switch (event.type) {
        case ConfigureNotify: {
            XConfigureEvent *e = (void *)&event;
            if (win->callbacks.resize) {
                win->callbacks.resize(win->param, e->width, e->height);
            }
            win->width  = e->width;
            win->height = e->height;
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
                k.len = XLookupString(e, k.buf, LEN(k.buf) - 1, &ksym, NULL);
            } else {
                Status status;
                for (int n = 0;; n++) {
                    k.len = XmbLookupString(win->ic, e, k.buf, LEN(k.buf) - 1, &ksym, &status);
                    if (status != XBufferOverflow) {
                        break;
                    }
                    assert(!n);
                    buf = xcalloc(k.len + 1, 1);
                }
            }

            buf[k.len] = 0;
            x11_translate_key(ksym, e->state, &k.id, &k.mod);

            if (win->callbacks.keypress && k.id != KeyNone) {
                win->callbacks.keypress(win->param, k.id, k.mod, buf, k.len);
            }

            if (buf != k.buf) free(buf);
            break;
        }
        case ClientMessage: {
            XClientMessageEvent *e = (void *)&event;
            if ((Atom)e->data.l[0] == ATOM(WM_DELETE_WINDOW)) {
                /* server_destroy_window(win); */
                win->online = false;
            }
            break;
        }
        default:
            break;
        }
    }

    XFlush(server.dpy);

    return count;
}

bool
window_online(const Win *win)
{
    return win->online;
}

void
x11_query_dimensions(Win *win, int *width, int *height, int *border)
{
    ASSERT(win);

    XWindowAttributes attr;
    XGetWindowAttributes(server.dpy, win->xid, &attr);

    if (width)  *width  = attr.width;
    if (height) *height = attr.height;
    if (border) *border = attr.border_width;
}

void
x11_query_coordinates(Win *win, int *xpos, int *ypos)
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

