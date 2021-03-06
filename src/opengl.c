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
#include "fonts.h"
#define OPENGL_INCLUDE_PLATFORM 1
#include "opengl.h"

GLuint
gl_compile_shader(const char *src, GLenum type)
{
    GLuint shader = glCreateShader(type);
    GLint status;

    glShaderSource(shader, 1, (const char **)&src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

    if (!status) {
        char local[BUFSIZ];
        GLsizei len = 0;
        glGetShaderInfoLog(shader, sizeof(local), &len, local);
        err_printf("OpenGL: %.*s\n", len, local);
        return 0;
    }

    return shader;
}

GLuint
gl_link_shaders(GLuint *shaders, uint count)
{
    GLuint program = glCreateProgram();
    GLint status;

    for (uint i = 0; i < count; i++) {
        glAttachShader(program, shaders[i]);
    }
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);

    if (!status) {
        char local[BUFSIZ];
        GLsizei len = 0;
        glGetProgramInfoLog(program, sizeof(local), &len, local);
        err_printf("OpenGL: %.*s\n", len, local);
        return 0;
    }

    return program;
}

void
gl_define_attr(GLuint idx, GLint count, GLenum type, GLsizei stride, uintptr_t offset)
{
    bool is_float = false;

    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
    case GL_INT:
    case GL_UNSIGNED_INT:
        break;
    case GL_FLOAT:
        is_float = true;
        break;
    default:
        dbg_printf("Unhandled GLenum: 0x%x\n", type);
        return;
    }

    glEnableVertexAttribArray(idx);
    glVertexAttribDivisor(idx, 1);

    if (is_float) {
        glVertexAttribPointer(idx, count, type, GL_FALSE, stride, (void *)offset);
    } else {
        glVertexAttribIPointer(idx, count, type, stride, (void *)offset);
    }
}

const char *
gl_type_string(GLenum type)
{
#define X_ENUMS           \
    X_(GL_ZERO)           \
    X_(GL_BYTE)           \
    X_(GL_UNSIGNED_BYTE)  \
    X_(GL_SHORT)          \
    X_(GL_UNSIGNED_SHORT) \
    X_(GL_INT)            \
    X_(GL_UNSIGNED_INT)   \
    X_(GL_FLOAT)

    switch (type) {
#define X_(x) case (x): return #x;
    X_ENUMS
#undef X_
    default: return "";
    }
#undef X_ENUMS
}

void
gl__get_error(const char *file, const char *func, int line)
{
    GLenum error = GL_NO_ERROR;
    int count = 0;

    while ((error = glGetError()) != GL_NO_ERROR) {
        fprintf(stderr, "glGetError(%s:%d/%s): %d (%#.04x)", file, line, func, error, error);
        count++;
    }

    if (count) {
        exit(1);
    }
}

#ifdef GL_ES_VERSION_3_2
static void
gl__message_callback(GLenum source_,
                     GLenum type_,
                     GLuint id,
                     GLenum severity_,
                     GLsizei length,
                     const GLchar *message,
                     const void *param)
{
    const char *source, *type, *severity;
    UNUSED(param);

    switch (source_) {
    case GL_DEBUG_SOURCE_API:             source = "API";            break;
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   source = "WindowSystem";   break;
    case GL_DEBUG_SOURCE_SHADER_COMPILER: source = "ShaderCompiler"; break;
    case GL_DEBUG_SOURCE_THIRD_PARTY:     source = "ThirdParty";     break;
    case GL_DEBUG_SOURCE_APPLICATION:     source = "Application";    break;
    case GL_DEBUG_SOURCE_OTHER:           source = "Unknown";        break;
    default:                              source = "Unknown";        break;
    }

    switch (type_) {
    case GL_DEBUG_TYPE_ERROR:               type = "Error";              break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: type = "DeprecatedBehavior"; break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  type = "UndefinedBehavior";  break;
    case GL_DEBUG_TYPE_PORTABILITY:         type = "Portability";        break;
    case GL_DEBUG_TYPE_PERFORMANCE:         type = "Performance";        break;
    case GL_DEBUG_TYPE_OTHER:               type = "Other";              break;
    case GL_DEBUG_TYPE_MARKER:              type = "Marker";             break;
    default:                                type = "Unknown";            break;
    }

    switch (severity_) {
    case GL_DEBUG_SEVERITY_HIGH:         severity = "High";    break;
    case GL_DEBUG_SEVERITY_MEDIUM:       severity = "Medium";  break;
    case GL_DEBUG_SEVERITY_LOW:          severity = "Low";     break;
    case GL_DEBUG_SEVERITY_NOTIFICATION: severity = "Notify";  break;
    default:                             severity = "Unknown"; break;
    }

#ifndef OPENGL_VERBOSE
    if (severity_ != GL_DEBUG_SEVERITY_NOTIFICATION)
#endif
    {
        fprintf(
            stderr,
            "glDebugMessage > "
            "SOURCE(%s) TYPE(%s) ID(%d) SEVERITY(%s) %.*s\n",
            source,
            type,
            id,
            severity,
            length,
            message
        );
    }

    if (type_ == GL_DEBUG_TYPE_ERROR) {
        exit(1);
    }
}

void
gl__set_debug_object(const void *obj)
{
    glDebugMessageCallback(gl__message_callback, obj);
}
#endif

