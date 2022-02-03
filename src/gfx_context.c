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
#include "gfx_context.h"
#include "gfx_private.h"

struct GfxTarget_ {
    Gfx *gfx;
    GfxImage *img;
    EGLNativeWindowType win;
    EGLSurface surf;
};

struct Gfx_ {
    bool online;
    EGLDisplay dpy;
    EGLContext ctx;
    EGLConfig cfg;
    struct {
        EGLint major;
        EGLint minor;
    } ver;
    GfxTarget *target;
};

static struct {
    Gfx gfx;
    GfxTarget targets[1];
} globals;

Gfx *
gfx_context_create(EGLNativeDisplayType dpy)
{
    Gfx *const gfx = &globals.gfx;

    if (!gfx->online) {
        if (!(gfx->dpy = eglGetDisplay(dpy))) {
            return NULL;
        } else if (!(eglInitialize(gfx->dpy, &gfx->ver.major, &gfx->ver.minor))) {
            gfx->dpy = 0;
            return NULL;
        }

        gfx->online = true;
    }

    return gfx;
}

void
gfx_context_destroy(Gfx *gfx)
{
    ASSERT(gfx);

    eglDestroyContext(gfx->dpy, gfx->ctx);
    eglTerminate(gfx->dpy);
    memset(gfx, 0, sizeof(*gfx));
}

bool
gfx_context_init(Gfx *gfx)
{
    ASSERT(gfx);

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
        dbgprint("Failed to open EGL context");
        return false;
    }

    EGLint result = 0;
    eglQueryContext(gfx->dpy, gfx->ctx, EGL_CONTEXT_CLIENT_TYPE, &result);
    ASSERT(result == EGL_OPENGL_ES_API);

    return true;
}

EGLint
gfx_get_visual_id(Gfx *gfx)
{
    ASSERT(gfx);

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

GfxTarget *
gfx_target_create(Gfx *gfx, EGLNativeWindowType win)
{
    ASSERT(gfx);

    GfxTarget *target = &globals.targets[0];
    ASSERT(!target->gfx);

    target->surf = eglCreateWindowSurface(gfx->dpy, gfx->cfg, win, NULL);

    if (!target->surf) {
        return NULL;
    }

    target->gfx = gfx;
    target->win = win;

    if (!gfx_target_query_size(target, NULL, NULL)) {
        eglDestroySurface(gfx->dpy, target->surf);
        memset(target, 0, sizeof(*target));
        return NULL;
    }

    return target;
}

bool
gfx_target_init(GfxTarget *target)
{
    ASSERT(target);
    ASSERT(target->gfx);

    target->img = gfx_image_create();

    if (!target->img || !gfx_image_init(target->img)) {
        return false;
    }

    return true;
}

bool
gfx_target_destroy(GfxTarget *target)
{
    ASSERT(target);
    ASSERT(target->gfx);

    Gfx *const gfx = target->gfx;

    eglDestroySurface(gfx->dpy, target->surf);

    if (!gfx_set_target(gfx, NULL)){
        return false;
    }
    if (gfx->target == target) {
        gfx->target = NULL;
    }

    gfx_image_destroy(target->img);

    memset(target, 0, sizeof(*target));

    return true;
}

bool
gfx_target_query_size(const GfxTarget *target, int *r_width, int *r_height)
{
    ASSERT(target);
    ASSERT(target->gfx);

    const Gfx *const gfx = target->gfx;

    int width, height;
    if (!eglQuerySurface(gfx->dpy, target->surf, EGL_WIDTH, &width) ||
        !eglQuerySurface(gfx->dpy, target->surf, EGL_HEIGHT, &height))
    {
        return false;
    }

    SETPTR(r_width, width);
    SETPTR(r_height, height);

    return true;
}

void
gfx_target_set_size(GfxTarget *target, uint width, uint height,
                    // NOTE(ben): Temporary scaffolding
                    uint inc_width,
                    uint inc_height,
                    uint border)
{
    ASSERT(target);
    ASSERT(target->gfx);

    gfx_image_set_size(target->img, width, height, inc_width, inc_height, border);
}

GfxTarget *
gfx_get_target(const Gfx *gfx)
{
    ASSERT(gfx);

    return gfx->target;
}

bool
gfx_set_target(Gfx *gfx, GfxTarget *target)
{
    ASSERT(gfx);

    if (!eglMakeCurrent(gfx->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
        return false;
    }
    if (target && target->surf != EGL_NO_SURFACE &&
        !eglMakeCurrent(gfx->dpy, target->surf, target->surf, gfx->ctx))
    {
        return false;
    }

    gfx->target = target;

    return true;
}

void
gfx_set_vsync(const Gfx *gfx, bool enable)
{
    ASSERT(gfx);

    eglSwapInterval(gfx->dpy, (enable) ? 1 : 0);
}

void
gfx_target_post(const GfxTarget *target)
{
    ASSERT(target);
    ASSERT(target->gfx);

    const Gfx *const gfx = target->gfx;

    eglSwapBuffers(gfx->dpy, target->surf);
}

void
gfx_print_info(const Gfx *gfx)
{
    ASSERT(gfx);

    fprintf(stderr,
        "\n"
        "EGL_VERSION     = %s\n"
        "EGL_VENDOR      = %s\n"
        "EGL_CLIENT_APIS = %s\n"
        "\n"
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

void
gfx_set_debug_object(const void *obj)
{
    gl_set_debug_object(obj);
}

