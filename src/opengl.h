#ifndef OPENGL_H__
#define OPENGL_H__

#include "defs.h"

#define GL_GLEXT_PROTOTYPES 1
#include <GLES3/gl32.h>

#ifdef OPENGL_INCLUDE_PLATFORM
#include <EGL/egl.h>

void egl_print_info(EGLDisplay);
#endif

void gl_message_callback(GLenum source,
                         GLenum type,
                         GLuint id,
                         GLenum severity,
                         GLsizei length,
                         const GLchar *message,
                         const void *userParam);
GLuint gl_compile_shader(const char *src, GLenum type);
GLuint gl_link_shaders(GLuint *shaders, uint count);

#if BUILD_DEBUG
#define gl_get_error(...) gl__get_error(__FILE__,__func__,__LINE__)
#else
#define gl_get_error(...)
#endif
void gl__get_error(const char *file, const char *func, int line);

#endif
