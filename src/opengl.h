#ifndef OPENGL_H__
#define OPENGL_H__

#include "common.h"

#define GLES_MIN_VERSION 300
#ifndef GLES_VERSION
  #define GLES_VERSION 300
#endif
#if (GLES_VERSION < GLES_MIN_VERSION)
  #error "Unsupported or invalid GLES_VERSION"
#endif

#define GL_GLEXT_PROTOTYPES 1
#if (GLES_VERSION >= 320)
  #define OPENGL_SHADER_HEADER "300 es"
  #include <GLES3/gl32.h>
#elif (GLES_VERSION >= 310)
  #define OPENGL_SHADER_HEADER "300 es"
  #include <GLES3/gl31.h>
#elif (GLES_VERSION >= 300)
  #define OPENGL_SHADER_HEADER "300 es"
  #include <GLES3/gl3.h>
#elif (GLES_VERSION >= 200)
  #define OPENGL_SHADER_HEADER "200 es"
  #include <GLES2/gl2.h>
#endif

#ifdef OPENGL_INCLUDE_PLATFORM
#include <EGL/egl.h>

void egl_print_info(EGLDisplay);
#endif

GLuint gl_compile_shader(const char *src, GLenum type);
GLuint gl_link_shaders(GLuint *shaders, uint count);

#if (BUILD_DEBUG)
  #define gl_get_error(...) gl__get_error(__FILE__,__func__,__LINE__)
  #ifdef GL_ES_VERSION_3_2
    void gl__set_debug_object(const void *obj);
    #define gl_set_debug_object(...) gl__set_debug_object(__VA_ARGS__)
  #else
    #define gl_set_debug_object(...)
  #endif
  #define OPENGL_FAIL dbgbreak
#else
  #define gl_get_error(...)
  #define gl_set_debug_object(...)
  #define OPENGL_FAIL abort
#endif

void gl__get_error(const char *file, const char *func, int line);

#endif
