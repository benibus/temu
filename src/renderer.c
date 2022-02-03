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
#include "vector.h"
#include "gfx_private.h"
#include "renderer.h"

#include <math.h>

#define MAX_INSTANCES (1024)

struct GfxInstance_ {
    Vec2U screen_pos;
    uint texid;
    Vec2F tile_pos;
    Vec2F tile_size;
    Vec4F color_bg;
    Vec4F color_fg;
};

static struct {
    GfxImage images[1];
} globals;

static const char shader_vert[] =
"#version " OPENGL_SHADER_HEADER "\n"
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
"flat out int texid;\n"
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

static const char shader_frag[] =
"#version " OPENGL_SHADER_HEADER "\n"
"\n"
"precision highp float;\n"
"\n"
"flat in int texid;\n"
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

static GfxImage *gfx_image_get(void) { return &globals.images[0]; }

GfxImage *
gfx_image_create(void)
{
    return gfx_image_get();
}

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

static void
draw_start(GLuint prog, GLuint vao, GLuint vbo, Vec4F color)
{
    glClearColor(color.r, color.g, color.b, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(prog);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
}

static void
draw_finish(void)
{
    return;
}

static void
draw_flush(const GfxInstance *instances, int count)
{
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(*instances), instances);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count);
}

void
gfx_render_frame(const Frame *frame, FontSet *fontset)
{
    GfxImage *const img = gfx_image_get();

    if (!frame) {
        return;
    }

    GfxInstance *cinst = NULL;
    uint at = 0;
    const Vec4F bg = unpack_argb(frame->default_bg);
    const Vec4F fg = unpack_argb(frame->default_fg);

    draw_start(img->prog, img->vao, img->vbo, bg);

    if (!fontset) {
        goto draw;
    }

    const Cell *cells = frame->cells;
    const int cx = frame->cursor.col;
    const int cy = frame->cursor.row;

    int ix, iy;

    for (iy = 0; iy < frame->rows; iy++) {
        if (at + frame->cols + 1 > MAX_INSTANCES) {
            draw_flush(img->instances, at);
            at = 0;
        }
        for (ix = 0; cells[ix].ucs4 && ix < frame->cols; ix++, at++) {
            const Cell cell = cells[ix];
            const Texture tex = fontset_get_glyph_texture(
                fontset,
                cell.attrs & (ATTR_BOLD|ATTR_ITALIC),
                cell.ucs4
            );
            img->instances[at].screen_pos = VEC2U(ix, iy);
            img->instances[at].texid      = tex.id;
            img->instances[at].tile_pos   = VEC2F(tex.u, tex.v);
            img->instances[at].tile_size  = VEC2F(tex.w, tex.h);
            if (cell.attrs & ATTR_INVERT) {
                img->instances[at].color_bg = unpack_argb(cell.fg);
                img->instances[at].color_fg = unpack_argb(cell.bg);
            } else {
                img->instances[at].color_bg = unpack_argb(cell.bg);
                img->instances[at].color_fg = unpack_argb(cell.fg);
            }
        }
        if (iy == cy && ix > cx) {
            cinst = &img->instances[at - (ix - cx)];
        }
        cells += frame->cols;
    }

    if (frame->cursor.visible) {
        if (!cinst) {
            cinst = &img->instances[at++];
            cinst->screen_pos = VEC2U(cx, cy);
            cinst->texid      = 0;
            cinst->tile_pos   = VEC2F(0, 0);
            cinst->tile_size  = VEC2F(0, 0);
        }
        cinst->color_bg = fg;
        cinst->color_fg = bg;
    }

draw:
    if (at > 0) {
        draw_flush(img->instances, at);
        at = 0;
    }

    draw_finish();
}

bool
gfx_image_init(GfxImage *img)
{
    GLuint shaders[2] = { 0 };

    if (!(shaders[0] = gl_compile_shader(shader_vert, GL_VERTEX_SHADER))) {
        return false;
    }
    if (!(shaders[1] = gl_compile_shader(shader_frag, GL_FRAGMENT_SHADER))) {
        return false;
    }
    if (!(img->prog = gl_link_shaders(shaders, LEN(shaders)))) {
        return false;
    }
    glUseProgram(img->prog);

    glGenVertexArrays(1, &img->vao);
    glGenBuffers(1, &img->vbo);
    glBindVertexArray(img->vao);
    glBindBuffer(GL_ARRAY_BUFFER, img->vbo);

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
        sizeof(GfxInstance),
        (void *)offsetof(GfxInstance, screen_pos)
    );
    glVertexAttribIPointer(
        1,
        1, GL_UNSIGNED_INT,
        sizeof(GfxInstance),
        (void *)offsetof(GfxInstance, texid)
    );
    glVertexAttribPointer(
        2,
        2, GL_FLOAT,
        GL_FALSE,
        sizeof(GfxInstance),
        (void *)offsetof(GfxInstance, tile_pos)
    );
    glVertexAttribPointer(
        3,
        2, GL_FLOAT,
        GL_FALSE,
        sizeof(GfxInstance),
        (void *)offsetof(GfxInstance, tile_size)
    );
    glVertexAttribPointer(
        4,
        4, GL_FLOAT,
        GL_FALSE,
        sizeof(GfxInstance),
        (void *)offsetof(GfxInstance, color_bg)
    );
    glVertexAttribPointer(
        5,
        4, GL_FLOAT,
        GL_FALSE,
        sizeof(GfxInstance),
        (void *)offsetof(GfxInstance, color_fg)
    );

    glUniform1i(glGetUniformLocation(img->prog, "tex[0]"), 0);
    glUniform1i(glGetUniformLocation(img->prog, "tex[1]"), 1);
    glUniform1i(glGetUniformLocation(img->prog, "tex[2]"), 2);
    glUniform1i(glGetUniformLocation(img->prog, "tex[3]"), 3);

    img->uniforms.projection = glGetUniformLocation(img->prog, "u_projection");
    img->uniforms.cellpx     = glGetUniformLocation(img->prog, "u_cellpx");
    img->uniforms.borderpx   = glGetUniformLocation(img->prog, "u_borderpx");
    img->uniforms.screenpx   = glGetUniformLocation(img->prog, "u_screenpx");

    glBufferData(GL_ARRAY_BUFFER,
                 MAX_INSTANCES * sizeof(*img->instances),
                 NULL,
                 GL_DYNAMIC_DRAW);
    ASSERT(!img->instances);
    img->instances = xcalloc(MAX_INSTANCES, sizeof(*img->instances));

    return true;
}

void
gfx_image_destroy(GfxImage *img)
{
    FREE(img->instances);
}

void
gfx_image_set_size(GfxImage *img,
                    int width, int height,
                    int colpx, int rowpx,
                    int borderpx)
{
    const float matrix[4][4] = {
        [0][0] = +2.f / width,
        [1][1] = -2.f / height,
        [2][2] = -1.f,
        [3][0] = -1.f,
        [3][1] = +1.f,
        [3][3] = +1.f
    };

    glUniformMatrix4fv(img->uniforms.projection, 1, GL_FALSE, (const float *)matrix);
    glUniform2ui(img->uniforms.screenpx, width, height);
    if (colpx != img->colpx || rowpx != img->rowpx) {
        glUniform2ui(img->uniforms.cellpx, colpx, rowpx);
    }
    if (borderpx != img->borderpx) {
        glUniform2ui(img->uniforms.borderpx, borderpx, borderpx);
    }

    img->width    = width;
    img->height   = height;
    img->colpx    = colpx;
    img->rowpx    = rowpx;
    img->borderpx = borderpx;

    glViewport(0, 0, width, height);
}

