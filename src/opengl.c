#include "utils.h"
#include "fonts.h"
#define OPENGL_INCLUDE_PLATFORM 1
#include "opengl.h"

void
egl_print_info(EGLDisplay dpy)
{
	if (!dpy) return;

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
		eglQueryString(dpy, EGL_VERSION),
		eglQueryString(dpy, EGL_VENDOR),
		eglQueryString(dpy, EGL_CLIENT_APIS),
		glGetString(GL_VERSION),
		glGetString(GL_VENDOR),
		glGetString(GL_RENDERER),
		glGetString(GL_SHADING_LANGUAGE_VERSION)
	);
}

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
		dbgprintf("OpenGLError > %.*s\n", len, local);
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
		dbgprintf("OpenGLError > %.*s\n", len, local);
		return 0;
	}

	return program;
}

void
gl_message_callback(GLenum source_,
                    GLenum type_,
                    GLuint id,
                    GLenum severity_,
                    GLsizei length,
                    const GLchar *message,
                    const void *param)
{
	const char *source, *type, *severity;
	(void)param;

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
		abort();
	}
}

void
gl__get_error(const char *file, const char *func, int line)
{
	GLenum error = GL_NO_ERROR;
	while ((error = glGetError()) != GL_NO_ERROR) {
		dbgprintf("GLerror(%s:%d/%s): %d (%#.04x)\n", file, line, func, error, error);
		dbgbreak();
	}
}

