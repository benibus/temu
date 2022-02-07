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
#include "vector.h"
#include "gfx_renderer.h"
#include "gfx_draw.h"

#include <math.h>

typedef struct GfxDraw_ GfxDraw; // NOTE(ben): temporary
typedef struct GfxQuad_ GfxQuad;
typedef struct GfxQuadAttr_ GfxQuadAttr;

struct GfxDraw_ {
    int width;
    int height;

    GLuint prog;
    GLuint vao;
    GLuint vbo;
    GfxQuad *quads;

    struct {
        GLuint projection;
    } uniforms;
};

#define GLENUM_F GL_FLOAT
#define GLENUM_I GL_INT
#define GLENUM_U GL_UNSIGNED_INT

#define X_QUAD_ATTRS \
    X_(4, F, dst) /* Screen bbox      */ \
    X_(4, F, src) /* Texture bbox     */ \
    X_(1, I, tex) /* Texture ID       */ \
    X_(4, F, bg)  /* Background color */ \
    X_(4, F, fg)  /* Foreground color */ \

struct GfxQuad_ {
#define X_(n,t,v) Vec##n##t v;
    X_QUAD_ATTRS
#undef X_
};

struct GfxQuadAttr_ {
    GLenum type;
    int count;
    size_t stride;
    uintptr_t offset;
};

static const GfxQuadAttr quad_attrs[] = {
#define X_(n,t,v)                      \
    {                                  \
        .type   = GLENUM_##t,          \
        .count  = (n),                 \
        .stride = sizeof(GfxQuad),     \
        .offset = offsetof(GfxQuad, v) \
    },
    X_QUAD_ATTRS
#undef X_
};
#undef X_QUAD_ATTRS

#define MAX_QUADS (1024)

static struct {
    GfxDraw draw;
} globals;

static const char shader_vert[] =
"#version 300 es\n"
"\n"
"layout (location = 0) in vec4 a_dst;\n"
"layout (location = 1) in vec4 a_src;\n"
"layout (location = 2) in int  a_tex;\n"
"layout (location = 3) in vec4 a_bg;\n"
"layout (location = 4) in vec4 a_fg;\n"
"\n"
"flat out int tex;\n"
"out vec2 pos;\n"
"out vec4 bg;\n"
"out vec4 fg;\n"
"uniform mat4 u_projection;\n"
"\n"
"vec2 get_corner(vec4 rect) {\n"
"    return rect.xy + rect.zw * vec2(gl_VertexID >> 1, gl_VertexID & 1);\n"
"}\n"
"\n"
"void set_position(vec2 point) {\n"
"    gl_Position = u_projection * vec4(point, 0.0, 1.0);\n"
"}\n"
"\n"
"void main() {\n"
"    pos = get_corner(a_src);\n"
"    tex = a_tex;\n"
"    bg  = a_bg;\n"
"    fg  = a_fg;\n"
"    set_position(get_corner(a_dst));\n"
"}\n"
;

static const char shader_frag[] =
"#version 300 es\n"
"\n"
"precision highp float;\n"
"\n"
"flat in int tex;\n"
"in vec2 pos;\n"
"in vec4 bg;\n"
"in vec4 fg;\n"
"\n"
"out vec4 color;\n"
"\n"
"uniform sampler2D samplers[4];\n"
"\n"
"float get_alpha(int idx) {\n"
"    // Hopefully temporary workaround for undefined behavior stemming from indexing into a\n"
"    // uniform sampler array. Actively produces visible artifacts on my AMD card.\n"
"    switch (idx) {\n"
"#define SAMPLE(n) texture(samplers[(n)], pos).r\n"
"    case 1:  return SAMPLE(0);\n"
"    case 2:  return SAMPLE(1);\n"
"    case 3:  return SAMPLE(2);\n"
"    case 4:  return SAMPLE(3);\n"
"    case 5:  return SAMPLE(0);\n"
"    case 6:  return SAMPLE(1);\n"
"    case 7:  return SAMPLE(2);\n"
"    case 8:  return SAMPLE(3);\n"
"    case 9:  return SAMPLE(0);\n"
"    case 10: return SAMPLE(1);\n"
"    case 11: return SAMPLE(2);\n"
"    case 12: return SAMPLE(3);\n"
"    case 13: return SAMPLE(0);\n"
"    case 14: return SAMPLE(1);\n"
"    case 15: return SAMPLE(2);\n"
"    case 16: return SAMPLE(3);\n"
"#undef SAMPLE\n"
"    default: return 0.0;\n"
"    }\n"
"}\n"
"\n"
"void main() {\n"
"    color = mix(bg, fg, get_alpha(tex));\n"
"}\n"
;

static inline GfxDraw *get_draw(void) { return &globals.draw; }

static inline Vec4F
unpack_argb(uint32 argb)
{
    return VEC4F(
        ((argb & 0x00ff0000) >> 16) / 255.f,
        ((argb & 0x0000ff00) >>  8) / 255.f,
        ((argb & 0x000000ff) >>  0) / 255.f,
        1.f
    );
}

void
gfx_clear_rgb3f(float r, float g, float b)
{
    glClearColor(r, g, b, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

}

void
gfx_clear_rgb3u(uint8 r, uint8 g, uint8 b)
{
    gfx_clear_rgb3f(r / 255.f,
                    g / 255.f,
                    b / 255.f);
}

void
gfx_clear_rgb1u(uint32 rgb)
{
    gfx_clear_rgb3u((rgb >> 16) & 0xff,
                    (rgb >>  8) & 0xff,
                    (rgb >>  0) & 0xff);
}

static void
draw_prepare(GLuint prog, GLuint vao, uint32 bg)
{
    gfx_clear_rgb1u(bg);
    glUseProgram(prog);
    glBindVertexArray(vao);
}

static void
draw_quads(const GfxQuad *quads, int count)
{
    if (count > 0) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(*quads), quads);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count);
    }
}

/* TODO(ben):
 * This function is just a hamfisted way of drawing the screen in the absence of a
 * proper terminal -> renderer interface. Ideally, the terminal would have more control
 * and the renderer wouldn't require knowledge of the terminal's cell format.
 * Glyph texture querying is also a mess - but that's part of a more complicated
 * architectural problem
 */
void
gfx_draw_frame(const Frame *frame, FontSet *fontset)
{
    GfxDraw *const draw = get_draw();

    if (!frame) {
        return;
    }

    uint idx = 0;

    draw_prepare(draw->prog, draw->vao, frame->default_bg);

    if (!fontset) {
        goto finish;
    }

    // Cursor coordinates
    const int ccol = frame->cursor.col;
    const int crow = frame->cursor.row;
    // Pixel border offset
    const int bx = MAX(0, draw->width - frame->width);
    const int by = MAX(0, draw->height - frame->height);
    // Pixel advance per cell
    const int dx = frame->width / frame->cols;
    const int dy = frame->height / frame->rows;

    GfxQuad *const quads = draw->quads;

    for (int row = 0; row < frame->rows; row++) {
        if (idx + frame->cols > MAX_QUADS) {
            draw_quads(quads, idx);
            idx = 0;
        }

        const Cell *const cells = &frame->cells[row*frame->cols];
        int col;

        for (col = 0; col < frame->cols && cells[col].ucs4; col++, idx++) {
            const Cell cell = cells[col];
            const Texture tex = fontset_get_glyph_texture(
                fontset,
                cell.attrs & (ATTR_BOLD|ATTR_ITALIC),
                cell.ucs4
            );

            /* FIXME(ben):
             * Screen coords arent't normalized but texture coords are. Pretty weird
             */
            quads[idx].dst = VEC4F(bx + col * dx, by + row * dy, dx, dy);
            quads[idx].src = VEC4F(tex.u, tex.v, tex.w, tex.h);
            quads[idx].tex = tex.id;

            if (cell.attrs & ATTR_INVERT) {
                quads[idx].bg = unpack_argb(cell.fg);
                quads[idx].fg = unpack_argb(cell.bg);
            } else {
                quads[idx].bg = unpack_argb(cell.bg);
                quads[idx].fg = unpack_argb(cell.fg);
            }
        }

        if (row == crow && frame->cursor.visible && ccol < frame->cols) {
            // Write the cursor cell in this row
            GfxQuad *quad;

            if (col > ccol) {
                // Cursor cell was already seen
                quad = &quads[idx-(col-ccol)];
            } else {
                // Cursor cell is beyond the last glyph, gets appended
                quad = &quads[idx++];
                quad->dst = VEC4F(bx + ccol * dx, by + crow * dy, dx, dy);
                quad->src = VEC4F(0, 0, 0, 0);
                quad->tex = 0;
            }

            // Always the same colors
            quad->bg = unpack_argb(frame->default_fg);
            quad->fg = unpack_argb(frame->default_bg);
        }
    }

finish:
    draw_quads(quads, idx);
}

bool
gfx_renderer_init(void)
{
    GfxDraw *const draw = get_draw();

    GLuint shaders[2] = { 0 };

    if (!(shaders[0] = gl_compile_shader(shader_vert, GL_VERTEX_SHADER))) {
        return false;
    }
    if (!(shaders[1] = gl_compile_shader(shader_frag, GL_FRAGMENT_SHADER))) {
        return false;
    }
    if (!(draw->prog = gl_link_shaders(shaders, LEN(shaders)))) {
        return false;
    }

    glUseProgram(draw->prog);

    glGenVertexArrays(1, &draw->vao);
    glGenBuffers(1, &draw->vbo);
    glBindVertexArray(draw->vao);
    glBindBuffer(GL_ARRAY_BUFFER, draw->vbo);

    for (uint i = 0; i < LEN(quad_attrs); i++) {
        const GfxQuadAttr *qa = &quad_attrs[i];
        gl_define_attr(i, qa->count, qa->type, qa->stride, qa->offset);
    }

    glUniform1i(glGetUniformLocation(draw->prog, "samplers[0]"), 0);
    glUniform1i(glGetUniformLocation(draw->prog, "samplers[1]"), 1);
    glUniform1i(glGetUniformLocation(draw->prog, "samplers[2]"), 2);
    glUniform1i(glGetUniformLocation(draw->prog, "samplers[3]"), 3);

    draw->uniforms.projection = glGetUniformLocation(draw->prog, "u_projection");

    glBufferData(GL_ARRAY_BUFFER,
                 MAX_QUADS * sizeof(*draw->quads),
                 NULL,
                 GL_DYNAMIC_DRAW);
    ASSERT(!draw->quads);
    draw->quads = xcalloc(MAX_QUADS, sizeof(*draw->quads));

    return true;
}

void
gfx_renderer_fini(void)
{
    GfxDraw *const draw = get_draw();

    FREE(draw->quads);
}

void
gfx_renderer_resize(int width, int height)
{
    GfxDraw *const draw = get_draw();

    // Typical translation matrix, except it flips the y-axis (0,0 maps to upper-left)
    const float projection[4][4] = {
        [0][0] = +2.f / width,
        [1][1] = -2.f / height,
        [2][2] = -1.f,
        [3][0] = -1.f,
        [3][1] = +1.f,
        [3][3] = +1.f
    };

    glUniformMatrix4fv(draw->uniforms.projection, 1, GL_FALSE, *projection);

    draw->width  = width;
    draw->height = height;

    glViewport(0, 0, width, height);
}

