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

struct GlyphBitmap {
	GlyphInfo info;
	void *data;
};

struct GlyphCache {
	Display *dpy;
	XRenderPictFormat *pictformat;
	GlyphSet glyphset;
	PixelFormat format;
	int count;
	int ascent, descent;
	struct GlyphBitmap bitmaps[];
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

struct GlyphCache *
glyphcache_create(int count, PixelFormat format, int ascent, int descent)
{
	int format_id;

	switch (format) {
	case PixelFormatMono: format_id = PictStandardA1;     break;
	case PixelFormatRGB:  format_id = PictStandardRGB24;  break;
	case PixelFormatRGBA: format_id = PictStandardARGB32; break;
	default: format_id = PictStandardA8; break;
	}

	size_t size = offsetof(struct GlyphCache, bitmaps) + sizeof(struct GlyphBitmap) * count;

	struct GlyphCache *cache = xcalloc(size, 1);

	cache->dpy        = server_get_display();
	cache->count      = count;
	cache->format     = format;
	cache->ascent     = ascent;
	cache->descent    = descent;
	cache->pictformat = XRenderFindStandardFormat(cache->dpy, format_id);
	cache->glyphset   = XRenderCreateGlyphSet(cache->dpy, cache->pictformat);

	return cache;
}

void
glyphcache_destroy(GlyphCache *cache)
{
	XRenderFreeGlyphSet(cache->dpy, cache->glyphset);
	free(cache);
}

GlyphInfo *
glyphcache_submit_bitmap(GlyphCache *cache, uint32 glyph, const uchar *srcbuf, GlyphInfo srcinfo)
{
	ASSERT(cache && cache->dpy);

	GlyphInfo dstinfo = srcinfo;

	dstinfo.format = cache->format;

	// Compute the aligned pitch
	switch (srcinfo.format) {
	case PixelFormatMono:
		dstinfo.pitch = ALIGN_UP(srcinfo.width, 32) >> 3;
		break;
	case PixelFormatRGBA:
		dstinfo.pitch = srcinfo.width * 4;
		break;
	case PixelFormatAlpha:
	default:
		dstinfo.pitch = ALIGN_UP(srcinfo.width, 4);
		break;
	}

	static uchar local[4096];
	uchar *dstbuf = local;

	if (dstinfo.pitch * dstinfo.height > (int)sizeof(local)) {
		dstbuf = xmalloc(dstinfo.pitch * dstinfo.height, 1);
	}

	const uchar *src = srcbuf;
	uchar *dst = dstbuf;

	switch (srcinfo.format) {
	case PixelFormatMono:
		goto cleanup;
	case PixelFormatAlpha:
		switch (dstinfo.format) {
		// grayscale to grayscale
		case PixelFormatAlpha:
			for (int y = 0; y < dstinfo.height; y++) {
				memcpy(dst, src, dstinfo.width);
				src += srcinfo.pitch;
				dst += dstinfo.pitch;
			}
			break;
		// grayscale to ARGB
		case PixelFormatRGBA:
			for (int y = 0; y < dstinfo.height; y++) {
				for (int x = 0; x < dstinfo.width; x++) {
					((uint32 *)dst)[x] = (
						(src[x] << 24)|
						(src[x] << 16)|
						(src[x] <<  8)|
						(src[x] <<  0)
					);
				}
				src += srcinfo.pitch;
				dst += dstinfo.pitch;
			}
			break;
		default:
			goto cleanup;
		}
		break;
	case PixelFormatRGBA:
		switch (dstinfo.format) {
		// BGRA to grayscale
		case PixelFormatAlpha:
			goto cleanup;
		// BGRA to ARGB
		case PixelFormatRGBA:
			// TODO(ben): Almost certainly wrong. Need to test with color glyphs.
			for (int y = 0; y < dstinfo.height; y++) {
				for (int x = 0; x < dstinfo.width; x += 4) {
					uint32 pixel = (
						(src[x+3] << 24)|
						(src[x+2] << 16)|
						(src[x+1] <<  8)|
						(src[x+0] <<  0)
					);
					*((uint32 *)(dst + x)) = pixel;
				}
				src += srcinfo.pitch * 4;
				dst += dstinfo.pitch;
			}
			break;
		default:
			goto cleanup;
		}
		break;
	default:
		goto cleanup;
	}

	// Add bitmap to glyphset
	XRenderAddGlyphs(
		cache->dpy,
		cache->glyphset,
		(Glyph *)&glyph,
		&(XGlyphInfo){
			.width  = dstinfo.width,
			.height = dstinfo.height,
			.x      = dstinfo.x_bearing,
			.y      = dstinfo.y_bearing,
			.xOff   = dstinfo.x_advance,
			.yOff   = dstinfo.y_advance
		},
		1,
		(char *)dstbuf,
		dstinfo.height * dstinfo.pitch
	);

	cache->bitmaps[glyph].info = dstinfo;

cleanup:
	if (dstbuf != local) {
		free(dstbuf);
	}

	return &cache->bitmaps[glyph].info;
}

void
draw_text_utf8(const WinClient *win, const GlyphRender *cmds, uint max, int x, int y)
{
	ASSERT(win);
	/* WinData *win = (WinData *)pub; */
	WinSurface *surface = window_get_surface(win);

	if (!max || !cmds) return;

	struct {
		uint32 font;
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

	brush.bg = cmds[0].bg;
	brush.fg = cmds[0].fg;
	brush.font = cmds[0].font;

	pos.x1 = pos.x0 = x;
	pos.y1 = pos.y0 = y;
	pos.dx = pos.x1 - pos.x0;
	pos.dy = pos.y1 - pos.y0;

	bool flushed = true;

	for (uint i = 0; i < max; i++) {
		const GlyphCache *cache = font_get_render_data(cmds[i].font);

		int chdx = cache->bitmaps[cmds[i].glyph].info.x_advance;
		int chdy = cache->bitmaps[cmds[i].glyph].info.y_advance;
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
			pos.ky = cache->ascent;
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
		text.buf[text.n++] = cmds[i].glyph;

		flushed = false;
		// accumulate position so we can set the new leading elt after drawing
		pos.x1 += (pos.dx = chdx);
		pos.y1 += (pos.dy = chdy);

		// If none of the local buffers are at capacity, keep going.
		// Otherwise, just draw now and reset everything at the new orgin
		if (i + 1 < max && elts.n < LEN(elts.buf) && text.n < LEN(text.buf)) {
			if (cmds[i+1].fg == brush.fg &&
			    cmds[i+1].bg == brush.bg &&
			    cmds[i+1].font == cmds[i].font)
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
				cache->ascent + cache->descent
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
			brush.font = cmds[i+1].font;
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

