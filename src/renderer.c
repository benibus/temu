#include "utils.h"
#include "opengl.h"
#include "renderer.h"

#include <math.h>

typedef struct Instance_ Instance;

struct RenderContext {
	int width;
	int height;
	int ncols;
	int nrows;
	int colpx;
	int rowpx;
	int borderpx;

	GLuint program;
	GLuint vao;
	GLuint vbo;
	Instance *instances;

	struct {
		GLuint projection;
		GLuint cellpx;
		GLuint borderpx;
		GLuint screenpx;
	} uniforms;
};

static struct RenderContext rc;

struct Instance_ {
	Vec2U screen_pos;
	uint texid;
	Vec2F tile_pos;
	Vec2F tile_size;
	Vec4F color_bg;
	Vec4F color_fg;
};

static const char *shader_vert =
"#version 320 es\n"
"\n"
"#define EXTENTS (gl_VertexID >> 1), (gl_VertexID & 1)\n"
"\n"
"layout (location = 0) in uvec2 a_screen_pos;\n"
"layout (location = 1) in int   a_texid;\n"
"layout (location = 2) in vec2  a_tile_pos;\n"
"layout (location = 3) in vec2  a_tile_size;\n"
"layout (location = 4) in vec4  a_color_bg;\n"
"layout (location = 5) in vec4  a_color_fg;\n"
"\n"
"out flat int texid;\n"
"out vec2 coords;\n"
"out vec4 color_bg;\n"
"out vec4 color_fg;\n"
"\n"
"uniform mat4 u_projection;\n"
"uniform uvec2 u_cellpx;\n"
"uniform uvec2 u_borderpx;\n"
"uniform uvec2 u_screenpx;\n"
"\n"
"void main() {\n"
"    uvec2 pos = u_borderpx + (a_screen_pos * u_cellpx) + (u_cellpx * uvec2(EXTENTS));\n"
"    texid = a_texid;\n"
"    coords = a_tile_pos + a_tile_size * vec2(EXTENTS);\n"
"    color_bg = a_color_bg;\n"
"    color_fg = a_color_fg;\n"
"    gl_Position = u_projection * vec4(pos, 0.0, 1.0);\n"
"}\n"
;

static const char *shader_frag =
"#version 320 es\n"
"\n"
"precision highp float;\n"
"\n"
"in flat int texid;\n"
"in vec2 coords;\n"
"in vec4 color_bg;\n"
"in vec4 color_fg;\n"
"\n"
"out vec4 color;\n"
"\n"
"uniform sampler2D tex[4];\n"
"\n"
"float get_alpha(int idx) {\n"
"    // Hopefully temporary workaround for undefined behavior stemming from indexing into a\n"
"    // uniform sampler array. Actively produces visible artifacts on my AMD card.\n"
"    switch (idx) {\n"
"    case 1:  return texture(tex[0], coords).r;\n"
"    case 2:  return texture(tex[1], coords).r;\n"
"    case 3:  return texture(tex[2], coords).r;\n"
"    case 4:  return texture(tex[3], coords).r;\n"
"    case 5:  return texture(tex[0], coords).r;\n"
"    case 6:  return texture(tex[1], coords).r;\n"
"    case 7:  return texture(tex[2], coords).r;\n"
"    case 8:  return texture(tex[3], coords).r;\n"
"    case 9:  return texture(tex[0], coords).r;\n"
"    case 10: return texture(tex[1], coords).r;\n"
"    case 11: return texture(tex[2], coords).r;\n"
"    case 12: return texture(tex[3], coords).r;\n"
"    case 13: return texture(tex[0], coords).r;\n"
"    case 14: return texture(tex[1], coords).r;\n"
"    case 15: return texture(tex[2], coords).r;\n"
"    case 16: return texture(tex[3], coords).r;\n"
"    default: return 0.0;\n"
"    }\n"
"}\n"
"\n"
"void main() {\n"
"    color = mix(color_bg, color_fg, get_alpha(texid));\n"
"}\n"
;

static inline Vec4F
unpack_argb(uint32 argb)
{
	return (Vec4F)vec4(
		((argb & 0x00ff0000) >> 16) / 255.f,
		((argb & 0x0000ff00) >>  8) / 255.f,
		((argb & 0x000000ff) >>  0) / 255.f,
		1.f
	);
}

void
renderer_draw_frame(const Frame *frame)
{
	if (!frame) {
		return;
	}

	Instance *cinst = NULL;
	uint at = 0;

	const Vec4F bg = unpack_argb(frame->default_bg);
	const Vec4F fg = unpack_argb(frame->default_fg);

	const Cell *cells = frame->cells;
	const int cx = frame->cursor.col;
	const int cy = frame->cursor.row;

	int ix, iy;

	for (iy = 0; iy < frame->rows; iy++) {
		for (ix = 0; cells[ix].ucs4 && ix < frame->cols; ix++, at++) {
			const Cell cell = cells[ix];
			const Texture tex = fontset_get_glyph_texture(
				fontset,
				cell.attrs & (ATTR_BOLD|ATTR_ITALIC),
				cell.ucs4
			);
			/* ASSERT(g); */
			rc.instances[at].screen_pos = (Vec2U)vec2(ix, iy);
			rc.instances[at].texid      = tex.id;
			rc.instances[at].tile_pos   = (Vec2F)vec2(tex.u, tex.v);
			rc.instances[at].tile_size  = (Vec2F)vec2(tex.w, tex.h);
			if (cell.attrs & ATTR_INVERT) {
				rc.instances[at].color_bg = unpack_argb(cell.fg);
				rc.instances[at].color_fg = unpack_argb(cell.bg);
			} else {
				rc.instances[at].color_bg = unpack_argb(cell.bg);
				rc.instances[at].color_fg = unpack_argb(cell.fg);
			}
		}
		if (iy == cy && ix > cx) {
			cinst = &rc.instances[at - (ix - cx)];
		}
		cells += frame->cols;
	}

	if (frame->cursor.visible) {
		if (!cinst) {
			cinst = &rc.instances[at++];
			cinst->screen_pos = (Vec2U)vec2(cx, cy);
			cinst->texid      = 0;
			cinst->tile_pos   = (Vec2F)vecx(0);
			cinst->tile_size  = (Vec2F)vecx(0);
		}
		cinst->color_bg = fg;
		cinst->color_fg = bg;
	}

	glClearColor(bg.r, bg.g, bg.b, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(rc.program);
	glBindVertexArray(rc.vao);
	glBindBuffer(GL_ARRAY_BUFFER, rc.vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, at * sizeof(*rc.instances), rc.instances);
	glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, at);
}

bool
renderer_init(void)
{
	GLuint shaders[2] = { 0 };

	if (!(shaders[0] = gl_compile_shader(shader_vert, GL_VERTEX_SHADER))) {
		return false;
	}
	if (!(shaders[1] = gl_compile_shader(shader_frag, GL_FRAGMENT_SHADER))) {
		return false;
	}
	if (!(rc.program = gl_link_shaders(shaders, LEN(shaders)))) {
		return false;
	}
	glUseProgram(rc.program);

	glGenVertexArrays(1, &rc.vao);
	glGenBuffers(1, &rc.vbo);
	glBindVertexArray(rc.vao);
	glBindBuffer(GL_ARRAY_BUFFER, rc.vbo);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glEnableVertexAttribArray(3);
	glEnableVertexAttribArray(4);
	glEnableVertexAttribArray(5);

	glVertexAttribDivisor(0, 1);
	glVertexAttribDivisor(1, 1);
	glVertexAttribDivisor(2, 1);
	glVertexAttribDivisor(3, 1);
	glVertexAttribDivisor(4, 1);
	glVertexAttribDivisor(5, 1);

	glVertexAttribIPointer(
		0,
		2, GL_UNSIGNED_INT,
		sizeof(Instance),
		(void *)offsetof(Instance, screen_pos)
	);
	glVertexAttribIPointer(
		1,
		1, GL_UNSIGNED_INT,
		sizeof(Instance),
		(void *)offsetof(Instance, texid)
	);
	glVertexAttribPointer(
		2,
		2, GL_FLOAT,
		GL_FALSE,
		sizeof(Instance),
		(void *)offsetof(Instance, tile_pos)
	);
	glVertexAttribPointer(
		3,
		2, GL_FLOAT,
		GL_FALSE,
		sizeof(Instance),
		(void *)offsetof(Instance, tile_size)
	);
	glVertexAttribPointer(
		4,
		4, GL_FLOAT,
		GL_FALSE,
		sizeof(Instance),
		(void *)offsetof(Instance, color_bg)
	);
	glVertexAttribPointer(
		5,
		4, GL_FLOAT,
		GL_FALSE,
		sizeof(Instance),
		(void *)offsetof(Instance, color_fg)
	);

	glUniform1i(glGetUniformLocation(rc.program, "tex[0]"), 0);
	glUniform1i(glGetUniformLocation(rc.program, "tex[1]"), 1);
	glUniform1i(glGetUniformLocation(rc.program, "tex[2]"), 2);
	glUniform1i(glGetUniformLocation(rc.program, "tex[3]"), 3);

	rc.uniforms.projection = glGetUniformLocation(rc.program, "u_projection");
	rc.uniforms.cellpx     = glGetUniformLocation(rc.program, "u_cellpx");
	rc.uniforms.borderpx   = glGetUniformLocation(rc.program, "u_borderpx");
	rc.uniforms.screenpx   = glGetUniformLocation(rc.program, "u_screenpx");

	return true;
}

void
renderer_set_dimensions(int width, int height,
                        int ncols, int nrows,
                        int colpx, int rowpx,
                        int borderpx)
{
	static int max_inst = 0;

	ASSERT(ncols * colpx + 2 * borderpx <= width);
	ASSERT(nrows * rowpx + 2 * borderpx <= height);

	const float matrix[4][4] = {
		[0][0] = +2.f / width,
		[1][1] = -2.f / height,
		[2][2] = -1.f,
		[3][0] = -1.f,
		[3][1] = +1.f,
		[3][3] = +1.f
	};

	glUniformMatrix4fv(rc.uniforms.projection, 1, GL_FALSE, (const float *)matrix);
	glUniform2ui(rc.uniforms.screenpx, width, height);
	if (colpx != rc.colpx || rowpx != rc.rowpx) {
		glUniform2ui(rc.uniforms.cellpx, colpx, rowpx);
	}
	if (borderpx != rc.borderpx) {
		glUniform2ui(rc.uniforms.borderpx, borderpx, borderpx);
	}

	if (ncols * nrows > max_inst) {
		max_inst = ncols * nrows;
		glBindBuffer(GL_ARRAY_BUFFER, rc.vbo);
		glBufferData(GL_ARRAY_BUFFER, max_inst * sizeof(*rc.instances), NULL, GL_DYNAMIC_DRAW);
		rc.instances = xrealloc(rc.instances, max_inst, sizeof(*rc.instances));
	}

	rc.width    = width;
	rc.height   = height;
	rc.ncols    = ncols;
	rc.nrows    = nrows;
	rc.colpx    = colpx;
	rc.rowpx    = rowpx;
	rc.borderpx = borderpx;

	glViewport(0, 0, width, height);
}

