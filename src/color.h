#ifndef COLOR_H__
#define COLOR_H__

#define COLOR_BLACK   0x0
#define COLOR_RED     0x1
#define COLOR_GREEN   0x2
#define COLOR_YELLOW  0x3
#define COLOR_BLUE    0x4
#define COLOR_MAGENTA 0x5
#define COLOR_CYAN    0x6
#define COLOR_WHITE   0x7

#define COLOR(sym,isbright) (((isbright) ? 0x8 : 0x0)|COLOR_##sym)

#define COLOR_DEFAULT_BG COLOR(BLACK, 0)
#define COLOR_DEFAULT_FG COLOR(WHITE, 0)

typedef struct {
	enum ColorTag {
		ColorTagDefault,
		ColorTag256,
		ColorTagRGB
	} tag:8;

	union {
		struct {
			int index;
			bool isbright;
		};
		struct {
			uint8 r;
			uint8 g;
			uint8 b;
		};
	};
} ColorSpec;

#endif

