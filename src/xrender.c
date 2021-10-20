#include "utils.h"
#include "render.h"
#include "fonts.h"
#include "x11.h"

#include <X11/extensions/Xrender.h>

#define XRENDER_ARGB(argb) ( \
  (XRenderColor){                       \
    .alpha = (argb & 0xff000000) >> 16, \
    .red   = (argb & 0x00ff0000) >>  8, \
    .green = (argb & 0x0000ff00) >>  0, \
    .blue  = (argb & 0x000000ff) <<  8  \
  }                                     \
)

#define XRENDER_COLOR(color) XRENDER_ARGB(color)

struct WinSurface {
	WinClient *win;
	Pixmap dstbuf;
	Picture framebuf;
	XRenderPictFormat *pictformat;
	struct FillColor {
		Picture xid;
		uint32 color;
	} solids[16];
};

static struct {
	struct WinSurface surface;
} globals;

struct GlyphCache {
	Display *dpy;
	XRenderPictFormat *pictformat;
	GlyphSet glyphset;
};

static Picture get_color_handle(WinSurface *, uint32);

WinSurface *
surface_create(WinClient *win)
{
	ASSERT(win);

	struct WinSurface *self = &globals.surface;

	self->pictformat = XRenderFindVisualFormat(win->server->dpy, win->server->visual);
	self->dstbuf = XCreatePixmap(
		win->server->dpy,
		win->xid,
		win->width,
		win->height,
		win->server->depth
	);
	self->framebuf = XRenderCreatePicture(
		win->server->dpy,
		self->dstbuf,
		self->pictformat,
		0,
		&(XRenderPictureAttributes){ 0 }
	);
	self->win = win;

	return self;
}

void
surface_resize(WinSurface *surface, int width, int height)
{
	WinServer *server = surface->win->server;

	XRenderFreePicture(server->dpy, surface->framebuf);
	XFreePixmap(server->dpy, surface->dstbuf);

	surface->dstbuf = XCreatePixmap(
		server->dpy,
		surface->win->xid,
		width, height,
		server->depth
	);
	surface->framebuf = XRenderCreatePicture(
		server->dpy,
		surface->dstbuf,
		surface->pictformat,
		0,
		&(XRenderPictureAttributes){ 0 }
	);
}

void *
glyphcache_create(int depth)
{
	int format_id;

	switch (depth) {
	case 1:  format_id = PictStandardA1;     break;
	case 8:  format_id = PictStandardA8;     break;
	case 24: format_id = PictStandardRGB24;  break;
	case 32: format_id = PictStandardARGB32; break;
	}

	GlyphCache *cache = xcalloc(1, sizeof(*cache));
	cache->dpy        = server_get_display();
	cache->pictformat = XRenderFindStandardFormat(cache->dpy, format_id);
	cache->glyphset   = XRenderCreateGlyphSet(cache->dpy, cache->pictformat);

	return cache;
}

void
glyphcache_destroy(void *generic)
{
	GlyphCache *cache = generic;
	XRenderFreeGlyphSet(cache->dpy, cache->glyphset);
	free(cache);
}

void
glyphcache_add_glyph(void *generic, uint32 glyph, const Bitmap *bitmap)
{
	GlyphCache *cache = generic;

	XRenderAddGlyphs(
		cache->dpy,
		cache->glyphset,
		(Glyph *)&glyph,
		&(XGlyphInfo){
			.width  = bitmap->width,
			.height = bitmap->height,
			.x      = bitmap->x_bearing,
			.y      = bitmap->y_bearing,
			.xOff   = bitmap->x_advance,
			.yOff   = bitmap->y_advance
		},
		1,
		(char *)bitmap->data,
		bitmap->height * bitmap->pitch
	);
}

void
draw_text_utf8(const WinClient *win,
               FontSet *fontset,
               const GlyphRender *cmds, uint max,
               int x, int y)
{
	ASSERT(win);
	WinSurface *surface = window_get_surface(win);

	if (!max || !cmds) return;

	struct {
		uint32 style;
		uint32 bg;
		uint32 fg;
	} brush = { 0 };
	struct {
		int x0, y0, x1, y1; // current region (in pixels)
		int dx, dy;         // advance of previous glyph
		int kx, ky;         // constant offset from start of region
	} pos = { 0 };
	struct { uint32 buf[2048]; uint n; } text = { 0 };
	struct { XGlyphElt32 buf[32]; uint n; } elts = { 0 };

	int width, ascent, descent;
	if (!fontset_get_metrics(fontset, &width, NULL, &ascent, &descent)) {
		return;
	}

	brush.style = cmds[0].style;
	brush.bg = cmds[0].bg;
	brush.fg = cmds[0].fg;

	pos.x1 = pos.x0 = x;
	pos.y1 = pos.y0 = y;
	pos.dx = pos.x1 - pos.x0;
	pos.dy = pos.y1 - pos.y0;

	bool flushed = true;

	for (uint i = 0; i < max; i++) {
		FontGlyph result = fontset_get_codepoint(fontset, brush.style, cmds[i].ucs4);
		const GlyphCache *cache = font_get_generic(result.font);

		int chdx = width;
		int chdy = 0;
		int adjx = 0;
		int adjy = 0;

		// We keep accumulating elts until the brush changes, then "flush".
		// Glyphs with different metrics can be drawn in bulk, but
		// using a different font/color requires a separate draw call
		if (flushed || pos.dx != chdx || pos.dy != chdy) {
			// get next elt if new metrics
			elts.n += !flushed;
			elts.buf[elts.n].chars = &text.buf[text.n];
			pos.x0 = pos.x1;
			pos.y0 = pos.y1;
			pos.kx = 0;
			pos.ky = ascent;
		}
		// Elt positions must be specified relative to the previous elt in the array.
		// This is why only the first elt (per draw call) receives the offset from
		// the drawing orgin
		if (pos.x1 != pos.x0 || pos.y1 != pos.y0) {
			adjx = chdx - pos.dx;
			adjy = chdy - pos.dy;
		}

		elts.buf[elts.n].xOff = pos.x0 + pos.kx + adjx;
		elts.buf[elts.n].yOff = pos.y0 + pos.ky + adjy;
		elts.buf[elts.n].glyphset = cache->glyphset;
		elts.buf[elts.n].nchars++;
		text.buf[text.n++] = result.glyph;

		flushed = false;
		// accumulate position so we can set the new leading elt after drawing
		pos.x1 += (pos.dx = chdx);
		pos.y1 += (pos.dy = chdy);

		// If none of the local buffers are at capacity, keep going.
		// Otherwise, just draw now and reset everything at the new orgin
		if (i + 1 < max && elts.n < LEN(elts.buf) && text.n < LEN(text.buf)) {
			if (cmds[i+1].fg == brush.fg &&
			    cmds[i+1].bg == brush.bg &&
			    cmds[i+1].style == cmds[i].style)
			{
				continue;
			}
		}

		if (brush.bg >> 24) {
			draw_rect(
				win,
				brush.bg,
				pos.x0,
				pos.y0,
				pos.x1 - pos.x0,
				ascent + descent
			);
		}

		// render elt buffer
		XRenderCompositeText32(
			cache->dpy,
			PictOpOver,
			get_color_handle(surface, brush.fg),
			surface->framebuf,
			cache->pictformat,
			0,
			0,
			elts.buf[0].xOff,
			elts.buf[0].yOff,
			elts.buf,
			elts.n + 1
		);

		// finalize draw, reset buffers
		if (i + 1 < max) {
			memset(elts.buf, 0, sizeof(*elts.buf) * (elts.n + 1));
			text.n = elts.n = 0, flushed = true;
			brush.fg = cmds[i+1].fg;
			brush.bg = cmds[i+1].bg;
			brush.style = cmds[i+1].style;
		}
	}
}

void
draw_rect(const WinClient *win, uint32 color, int x, int y, int w, int h)
{
	const WinSurface *surface = window_get_surface(win);

	x = CLAMP(x, 0, win->width);
	y = CLAMP(y, 0, win->height);
	w = CLAMP(x + w, x, (int)win->width) - x;
	h = CLAMP(y + h, y, (int)win->height) - y;

	XRenderFillRectangle(
		win->server->dpy,
		PictOpOver,
		surface->framebuf,
		&XRENDER_COLOR(color),
		x, y, w, h
	);
}

void
window_render(WinClient *win)
{
	ASSERT(win);

	XCopyArea(
		win->server->dpy,
		win->surface->dstbuf,
		win->xid,
		win->gc,
		0, 0,
		win->width,
		win->height,
		0, 0
	);
}

Picture
get_color_handle(WinSurface *surface, uint32 color)
{
	ASSERT(surface);

	if (!color) return 0;

	WinClient *win = surface->win;

	for (uint i = 0; i < LEN(surface->solids); i++) {
		if (surface->solids[i].color == color) {
			return surface->solids[i].xid;
		}
	}

	struct FillColor *slot = surface->solids + rand() % LEN(surface->solids);

	if (slot->xid) {
		XRenderFreePicture(win->server->dpy, slot->xid);
	}
	slot->xid = XRenderCreateSolidFill(win->server->dpy, &XRENDER_COLOR(color));
	slot->color = color;

	return slot->xid;
}

