#ifndef PLATFORM_X11_H__
#define PLATFORM_X11_H__

#include "common.h"
#define OPENGL_INCLUDE_PLATFORM 1
#include "opengl.h"
#include "platform.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

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

#endif

