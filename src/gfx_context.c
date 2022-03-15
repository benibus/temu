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
#include "opengl.h"
#include "gfx_context.h"
#include "gfx_renderer.h"

typedef struct {
    EGLSurface id;
    EGLNativeWindowType win;
} GfxSurface;

struct Gfx {
    bool online;
    EGLDisplay dpy;
    EGLContext ctx;
    EGLConfig cfg;
    struct {
        EGLint major;
        EGLint minor;
    } ver;
    GfxSurface surface;
};

static struct {
    Gfx gfx;
} globals;

Gfx *
gfx_create_context(EGLNativeDisplayType dpy)
{
    Gfx *const gfx = &globals.gfx;

    if (gfx->ctx) return gfx;

    if (!(gfx->dpy = eglGetDisplay(dpy))) {
        return NULL;
    } else if (!(eglInitialize(gfx->dpy, &gfx->ver.major, &gfx->ver.minor))) {
        gfx->dpy = 0;
        return NULL;
    }

    gfx->surface.id  = EGL_NO_SURFACE;
    gfx->surface.win = 0;

    eglBindAPI(EGL_OPENGL_ES_API);

    gfx->ctx = eglCreateContext(
        gfx->dpy,
        gfx->cfg,
        EGL_NO_CONTEXT,
        (EGLint []){
            EGL_CONTEXT_CLIENT_VERSION, 2,
#if (defined(GL_ES_VERSION_3_2) && BUILD_DEBUG)
            EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE,
#endif
            EGL_NONE
        });

    if (!gfx->ctx) {
        err_printf("eglCreateContext failed\n");
        goto bail;
    }

    EGLint result = 0;
    eglQueryContext(gfx->dpy, gfx->ctx, EGL_CONTEXT_CLIENT_TYPE, &result);
    ASSERT(result == EGL_OPENGL_ES_API);

    if (!eglMakeCurrent(gfx->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, gfx->ctx)) {
        err_printf("eglMakeCurrent failed to bind context\n");
        goto bail;
    }
    if (!gfx_renderer_init()) {
        err_printf("Failed to initialize renderer\n");
        goto bail;
    }

    return gfx;
bail:
    gfx_destroy_context(gfx);
    return NULL;
}

void
gfx_destroy_context(Gfx *gfx)
{
    if (!gfx || !gfx->dpy) {
        return;
    }

    eglMakeCurrent(gfx->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if (gfx->surface.id != EGL_NO_SURFACE) {
        eglDestroySurface(gfx->dpy, gfx->surface.id);
    }

    if (gfx->ctx) {
        gfx_renderer_fini();
        eglDestroyContext(gfx->dpy, gfx->ctx);
    }

    eglTerminate(gfx->dpy);

    memset(gfx, 0, sizeof(*gfx));
}

EGLint
gfx_get_native_visual(Gfx *gfx)
{
    if (!gfx || !gfx->dpy) {
        return 0;
    }

    EGLint visid, count;
    static const EGLint attrs[] = {
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    if (!eglChooseConfig(gfx->dpy, attrs, &gfx->cfg, 1, &count)) {
        return 0;
    }

    ASSERT(gfx->cfg && count > 0);

    if (!eglGetConfigAttrib(gfx->dpy, gfx->cfg, EGL_NATIVE_VISUAL_ID, &visid)) {
        return 0;
    }

    return visid;
}

bool
gfx_bind_surface(Gfx *gfx, EGLNativeWindowType win)
{
    if (!gfx || !gfx->dpy || !gfx->ctx) {
        return false;
    }

    GfxSurface *const surface = &gfx->surface;

    if (win == surface->win) {
        return true;
    }

    if (surface->win) {
        ASSERT(surface->id != EGL_NO_SURFACE);
        eglDestroySurface(gfx->dpy, surface->id);
        surface->id = EGL_NO_SURFACE;
        surface->win = 0;
    }

    if (win) {
        ASSERT(gfx->cfg);
        surface->id = eglCreateWindowSurface(gfx->dpy, gfx->cfg, win, NULL);
        if (surface->id == EGL_NO_SURFACE) {
            err_printf("eglCreateWindowSurface failed\n");
            return false;
        }
        surface->win = win;
    }

    if (!eglMakeCurrent(gfx->dpy, surface->id, surface->id, gfx->ctx)) {
        err_printf("eglMakeCurrent failed\n");
        if (surface->id != EGL_NO_SURFACE) {
            eglDestroySurface(gfx->dpy, surface->id);
        }
        surface->id = EGL_NO_SURFACE;
        surface->win = 0;
        return false;
    }

    return true;
}

bool
gfx_get_size(const Gfx *gfx, int *r_width, int *r_height)
{
    if (!gfx || gfx->surface.id == EGL_NO_SURFACE) {
        return false;
    }

    int width, height;
    if (!eglQuerySurface(gfx->dpy, gfx->surface.id, EGL_WIDTH, &width) ||
        !eglQuerySurface(gfx->dpy, gfx->surface.id, EGL_HEIGHT, &height))
    {
        return false;
    }

    SETPTR(r_width, width);
    SETPTR(r_height, height);

    return true;
}

void
gfx_resize(Gfx *gfx, uint width, uint height)
{
    if (gfx && gfx->ctx) {
        gfx_renderer_resize(width, height);
    }
}

void
gfx_set_vsync(const Gfx *gfx, bool enable)
{
    if (gfx && gfx->dpy) {
        eglSwapInterval(gfx->dpy, (enable) ? 1 : 0);
    }
}

void
gfx_swap_buffers(const Gfx *gfx)
{
    if (gfx && gfx->surface.id) {
        eglSwapBuffers(gfx->dpy, gfx->surface.id);
    }
}

void
gfx_set_debug_object(const void *obj)
{
    gl_set_debug_object(obj);
}

void
gfx_print_info(const Gfx *gfx)
{
    ASSERT(gfx);

    fprintf(stderr,
        "EGL_VERSION     = %s\n"
        "EGL_VENDOR      = %s\n"
        "EGL_CLIENT_APIS = %s\n"
        "GL_VERSION      = %s\n"
        "GL_VENDOR       = %s\n"
        "GL_RENDERER     = %s\n"
        "GL_SHADING_LANGUAGE_VERSION = %s\n"
        "\n",
        eglQueryString(gfx->dpy, EGL_VERSION),
        eglQueryString(gfx->dpy, EGL_VENDOR),
        eglQueryString(gfx->dpy, EGL_CLIENT_APIS),
        glGetString(GL_VERSION),
        glGetString(GL_VENDOR),
        glGetString(GL_RENDERER),
        glGetString(GL_SHADING_LANGUAGE_VERSION)
    );
}

