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
  #include <GLES3/gl32.h>
#elif (GLES_VERSION >= 310)
  #include <GLES3/gl31.h>
#elif (GLES_VERSION >= 300)
  #include <GLES3/gl3.h>
#elif (GLES_VERSION >= 200)
  #include <GLES2/gl2.h>
#endif

GLuint gl_compile_shader(const char *src, GLenum type);
GLuint gl_link_shaders(GLuint *shaders, uint count);
void gl_define_attr(GLuint idx, GLint count, GLenum type, GLsizei stride, uintptr_t offset);
const char *gl_type_string(GLenum type);

#if (BUILD_DEBUG)
  #define gl_get_error(...) gl__get_error(__FILE__,__func__,__LINE__)
  #ifdef GL_ES_VERSION_3_2
    void gl__set_debug_object(const void *obj);
    #define gl_set_debug_object(...) gl__set_debug_object(__VA_ARGS__)
  #else
    #define gl_set_debug_object(...)
  #endif
#else
  #define gl_get_error(...)
  #define gl_set_debug_object(...)
#endif

void gl__get_error(const char *file, const char *func, int line);

#endif

