#ifndef WINDOW_H__
#define WINDOW_H__

#include "defs.h"

#include "keymap.h"

typedef enum {
	PixelFormatNone,
	PixelFormatMono,
	PixelFormatAlpha,
	PixelFormatRGB,
	PixelFormatRGBA,
	PixelFormatLCDH,
	PixelFormatLCDV,
	PixelFormatUnknown,
	PixelFormatCount
} PixelFormat;

typedef struct WinServer WinServer;
typedef struct WinClient WinClient;

typedef enum {
	WinEventTypeNone,
	WinEventTypeDestroy,
	WinEventTypeResize,
	WinEventTypeTextInput,
	WinEventTypeKeyPress,
	WinEventTypeKeyRelease,
	WinEventTypeButtonPress,
	WinEventTypeButtonRelease,
	WinEventTypePointerMove,
	WinEventTypeExpose,
	WinEventTypeCount
} WinEventType;

typedef void (*WinCallbackResize)(void *, int, int);
typedef void (*WinCallbackKeyPress)(void *, int, int, char *, int);
typedef void (*WinCallbackExpose)(void *);

struct WinConfig {
	char *wm_title;
	char *wm_instance;
	char *wm_class;
	uint16 cols;
	uint16 rows;
	uint16 colpx;
	uint16 rowpx;
	uint16 border;
	bool smooth_resize;
	struct {
		void *generic;
		WinCallbackResize resize;
		WinCallbackKeyPress keypress;
		WinCallbackExpose expose;
	} callbacks;
};

bool server_setup(void);
WinClient *server_create_window(struct WinConfig);
float server_get_dpi(void);
void *server_get_display(void);
PixelFormat server_get_pixel_format(void);
int server_get_fileno(void);
bool server_parse_color_string(const char *, uint32 *);
int server_events_pending(void);
int server_get_fileno(void);

bool window_online(const WinClient *);
void window_get_dimensions(const WinClient *, int *, int *, int *);
int window_process_events(WinClient *, double);
int window_poll_events(WinClient *);
bool window_show(WinClient *);
void *window_get_surface(const WinClient *);
void window_render(WinClient *win);

#endif
