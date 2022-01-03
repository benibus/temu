#ifndef PLATFORM_H__
#define PLATFORM_H__

#include "common.h"
#include "keymap.h"

typedef struct Win_ Win;

typedef enum {
    EventTypeNone,
    EventTypeDestroy,
    EventTypeResize,
    EventTypeTextInput,
    EventTypeKeyPress,
    EventTypeKeyRelease,
    EventTypeButtonPress,
    EventTypeButtonRelease,
    EventTypePointerMove,
    EventTypeExpose,
    EventTypeCount
} EventType;

typedef void (*EventFuncResize)(void *, int, int);
typedef void (*EventFuncKeyPress)(void *, int, int, char *, int);
typedef void (*EventFuncExpose)(void *);

struct WinConfig {
    void *param;
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
        EventFuncResize resize;
        EventFuncKeyPress keypress;
        EventFuncExpose expose;
    } callbacks;
};

bool platform_setup(void);
void platform_shutdown(void);
Win *platform_create_window(struct WinConfig);
float platform_get_dpi(void);
int platform_get_fileno(void);
bool platform_parse_color_string(const char *, uint32 *);
int platform_events_pending(void);

bool window_online(const Win *);
void window_destroy(Win *);
void window_get_dimensions(const Win *, int *, int *, int *);
void window_set_title(Win *win, const char *name, size_t len);
void window_set_icon(Win *win, const char *name, size_t len);
int window_poll_events(Win *);
bool window_show(Win *);
void window_update(const Win *);
bool window_make_current(const Win *);

#endif
