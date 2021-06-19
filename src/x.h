#ifndef X_H__
#define X_H__

typedef int ClientID;

struct WinConfig {
	int x, y;
	int cols, rows;
	int borderpx;
	int histsize;
	int tabstop;
	int min_latency, max_latency;
	char *fontstr;
	char *wmtitle, *wmname, *wmclass;
};

enum LOG_Flags {
	LOG_EVENTS   = 1 << 0,
	LOG_KEYPRESS = 1 << 1,
	LOG_DRAWING  = 1 << 2
};
extern u32 log_flags;

void x_configure(struct WinConfig);
bool x_init_session(void);

uint key_get_id(uint);
uint key_get_string(uint, uint, char *, uint);

void run(void);

void dbg__test_draw(void);

#endif
