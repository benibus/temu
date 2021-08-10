#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <unistd.h>

#include "utils.h"
#include "term.h"
#include "utf8.h"

#define SET(base_,mask_,opt_) \
( (base_) = ((opt_)) ? ((base_) | (mask_)) : ((base_) & ~(mask_)) )

#if 0
#define PRINTSEQ(s_) do { \
	msg_log("Parser", "%-6s %-15s (%#.4x|%#.4x)  bg:%03u fg:%03u  lc:%-6s #%02u:",             \
	    asciistr(g_ucode), (s_), parser.state, parser.flags, parser.color.bg, parser.color.fg, \
	    asciistr(parser.lastc), parser.args.count);                                            \
	for (uint i = 0; i < parser.args.count; i++) {       \
		msg_log("\0", " %04zu", parser.args.buf[i]); \
	}                                                    \
	fprintf(stderr, "\n");                               \
} while (0)
#else
#define PRINTSEQ(s_)
#endif

enum ctrlcodes_e_ {
	CTRL_NUL,
	CTRL_BEL = '\a',
	CTRL_BS  = '\b',
	CTRL_HT  = '\t',
	CTRL_LF  = '\n',
	CTRL_VT  = '\v',
	CTRL_FF  = '\f',
	CTRL_CR  = '\r',
	CTRL_SO  = 0x0e,
	CTRL_SI  = 0x0f,
	CTRL_CAN = 0x18,
	CTRL_SUB = 0x1a,
	CTRL_ESC = 0x1b,
	CTRL_NEL = 0x85,
	CTRL_HTS = 0x88,
	CTRL_DCS = 0x90,
	CTRL_SOS = 0x98,
	CTRL_ST  = 0x9c,
	CTRL_OSC = 0x9d,
	CTRL_PM  = 0x9e,
	CTRL_APC = 0x9f
};

enum {
	// Active escape modes
	STATE_DEFAULT,
	STATE_ESC = (1 << 0),
	STATE_CSI = (1 << 1),
	STATE_DEC = (1 << 2),
	STATE_STR = (1 << 3),
	STATE_MAX = (1 << 4)
};

#define STATE_MASK  (STATE_MAX - 1)

#define MAX_ARGS 256
typedef struct parser_s_ {
	Cell spec;
	uint8 state;
	uint16 flags;
	uint32 lastc;
	ColorSet color;
	struct {
		size_t buf[MAX_ARGS];
		uint count;
	} args;
	struct {
		uint32 buf[BUFSIZ];
		uint count;
	} chars;
} Parser;

static int g_ucode = 0; // debug only

static Parser parser = { 0 };
static char mbuf[BUFSIZ];
static size_t msize = 0;

static void state_set(uint, bool, uint32);
static void state_reset(void);
static void state_set_esc(bool);
static void buf_push(uint32);
static void buf_reset(void);
static int parse_codepoint(uint32);

bool
tty_init(int cols, int rows)
{
	memclear(&tty, 1, sizeof(tty));
	stream_init(&tty, cols, rows, histsize);

	for (int i = 0; i < tty.hist.max; i++) {
		Row *row = ring_data(&tty.hist, i);
		row->offset = i * tty.cols;
	}

	ASSERT(tabstop > 0);
	for (int i = 0; ++i < tty.cols; ) {
		tty.tabs[i] |= (i % tabstop == 0) ? 1 : 0;
	}

	parser.flags = ATTR_NONE;
	parser.state = STATE_DEFAULT;
	parser.color.bg = COLOR_BG;
	parser.color.fg = COLOR_FG;

	return !!tty.cells;
}

size_t
tty_write(const char *str, size_t len)
{
#if 1
	{
		for (size_t j = 0; j < len; j++) {
			long tmp;
			if (msize >= LEN(mbuf)-1-1)
				break;
			tmp = snprintf(&mbuf[msize], LEN(mbuf)-msize-1-1, "%s ", asciistr(str[j]));
			if (tmp <= 0) break;
			msize += tmp;
		}
		msg_log("Input", "%s\n", mbuf);
		msize = 0;
		mbuf[msize] = 0;
	}
#endif
	uint i = 0;

	while (str[i] && i < len) {
		uint width = 1;
		uint err = 0;
		uint32 ucs4 = 0;

		width = utf8_decode(str + i, &ucs4, &err);
		ASSERT(!err);

		if (!parse_codepoint(ucs4)) {
			stream_write(ucs4, parser.color, parser.flags);
		} else {
			dummy__();
		}

		i += width;
	}

	tty.cells[tty.size].ucs4 = 0;

	return i;
}

void
tty_resize(uint cols, uint rows)
{
	tty.cols = cols;
	tty.rows = rows;
}

void
buf_reset(void)
{
	memclear(&parser.spec, 1, sizeof(parser.spec));
	parser.chars.count = 0;
	parser.chars.buf[parser.chars.count] = 0;
	if (parser.args.count) {
		memclear(parser.args.buf, parser.args.count, sizeof(*parser.args.buf));
		parser.args.count = 0;
	}
}

void
buf_push(uint32 ucs4)
{
	ASSERT(parser.chars.count < BUFSIZ); // temporary
	parser.chars.buf[parser.chars.count++] = ucs4;
	parser.chars.count = 0;
}

static bool
get_arg_csi(uint32 ucs4)
{
	size_t *arg;

	switch (ucs4) {
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		parser.args.count += !parser.args.count;
		arg = &parser.args.buf[parser.args.count-1];
		*arg *= 10;
		*arg += ucs4 - '0';
		break;
	case ';':
		ASSERT(parser.args.count > 0);
		parser.args.count++;
		break;
	default:
		return false;
	}

	return true;
}

#if 1
  #define SEQBEG(s1,s2) do { PRINTSEQ(#s1":"#s2); } while (0)
#else
  #define SEQBEG(s1,s2)
#endif
#define SEQEND(ret)   do { state_reset(); } while (0); return (ret)

int
parse_codepoint(uint32 ucs4)
{
	g_ucode = ucs4;

	switch (ucs4) {
	case CTRL_ESC:
		PRINTSEQ("BEG>ESC");
		state_set_esc(true);
		return 1;
	}

	uint32 state = (parser.state & STATE_MASK);

	if (state & STATE_STR) {
		if (state & STATE_ESC) {
			switch (ucs4) {
			case '\\':
				PRINTSEQ("END");
				state_set(STATE_STR, false, ucs4);
				break;
			default:
				buf_push(CTRL_ESC);
				buf_push(ucs4);
				break;
			}
			state_reset();
		} else {
			switch (ucs4) {
			case CTRL_ST:
			case CTRL_BEL:
				PRINTSEQ("END");
				state_reset();
				break;
			default:
				buf_push(ucs4);
				break;
			}
		}
		return 2;
	}

	if (state & STATE_ESC) {
		switch (ucs4) {
		case CTRL_CAN:
		case CTRL_SUB:
			PRINTSEQ("ESC:CAN/SUB>END");
			state_reset();
			break;
		case '[':
			PRINTSEQ("ESC>CSI");
			state_set_esc(false);
			state_set(STATE_CSI, true, ucs4);
			break;
		case ']':
			PRINTSEQ("ESC>OSC");
			state_set_esc(false);
			state_set(STATE_STR, true, ucs4);
			break;
		case 'E':
			PRINTSEQ("ESC:NEL");
			state_reset();
			break;
		case 'H':
			PRINTSEQ("ESC:HTS");
			state_reset();
			break;
		case 'M':
			SEQBEG(ESC, RI);
			cmd_move_cursor_y(&tty, -1);
			SEQEND(1);
			break;
		default:
			goto quit;
		}
		return 3;
	} else if (state & STATE_CSI) {
		if (get_arg_csi(ucs4)) {
			PRINTSEQ("CSI#ARG");
			return 5;
		}
#define ESC_CSI(str_) do { PRINTSEQ("CSI:"str_); state_reset(); } while(0); return 4
		if (parser.lastc == '[') {
			// we check for an alt char as the first non-arg only
			switch (ucs4) {
			case '?':
			case '!':
			case '"':
			case '\'':
			case '$':
			case '>':
				PRINTSEQ("CSI>DEC");
				state_set(STATE_DEC, true, ucs4);
				return 5;
			// CSI terminators with no alt variations (VT100)
			case '@':
				SEQBEG(CSI, ICH);
				parser.spec.ucs4 = ' ';
				parser.spec.width = 1;
				cmd_insert_cells(&tty, &parser.spec,
				                 DEFAULT(parser.args.buf[0], 1));
				SEQEND(1);
			case 'A':
				SEQBEG(CSI, CUU);
				cmd_move_cursor_y(&tty, -DEFAULT(parser.args.buf[0], 1));
				SEQEND(1);
			case 'B':
				SEQBEG(CSI, CUD);
				cmd_move_cursor_y(&tty, +DEFAULT(parser.args.buf[0], 1));
				SEQEND(1);
			case 'C':
				SEQBEG(CSI, CUF);
				cmd_move_cursor_x(&tty, +DEFAULT(parser.args.buf[0], 1));
				SEQEND(1);
			case 'D':
				SEQBEG(CSI, CUB);
				cmd_move_cursor_x(&tty, -DEFAULT(parser.args.buf[0], 1));
				SEQEND(1);
			case 'E': ESC_CSI("CNL");
			case 'F': ESC_CSI("CPL");
			case 'G': ESC_CSI("CHA");
			case 'H':
				SEQBEG(CSI, CUP);
				cmd_set_cursor_x(&tty, parser.args.buf[1]);
				cmd_set_cursor_y(&tty, parser.args.buf[0]);
				SEQEND(1);
			case 'I':
				SEQBEG(CSI, CHT);
				for (size_t i = 0; i < DEFAULT(parser.args.buf[0], 1); i++) {
					if (!stream_write('\t', parser.color, parser.flags))
					{
						break;
					}
				}
				SEQEND(1);
			case 'L': ESC_CSI("IL");
			case 'M': ESC_CSI("DL");
			case 'P': {
				SEQBEG(CSI, DCH);
				int dx = DEFAULT(parser.args.buf[0], 1);
				ASSERT(dx >= 0);
				cmd_shift_cells(&tty, tty.cursor.x + dx, tty.cursor.y, -dx);
				SEQEND(1);
			}
			case 'S': ESC_CSI("SU");
			case 'T': ESC_CSI("SD");
			case 'X': ESC_CSI("ECH");
			case 'Z': ESC_CSI("CBT");
			case '`': ESC_CSI("HPA");
			case 'b': ESC_CSI("REP");
			case 'd': ESC_CSI("VPA");
			case 'f': ESC_CSI("HVP");
			case 'g': ESC_CSI("TBC");
			case 'm':
				SEQBEG(CSI, SGR);
				if (!parser.args.count) {
					parser.args.buf[parser.args.count++] = 0;
				}
				for (size_t i = 0; i < parser.args.count; i++) {
					switch (parser.args.buf[i]) {
					case 0:
						parser.flags &= ~ATTR_MASK;
						parser.color.bg = 0;
						parser.color.fg = 7;
						break;
					case 1:  parser.flags |= ATTR_BOLD;       break;
					case 4:  parser.flags |= ATTR_UNDERLINE;  break;
					case 5:  parser.flags |= ATTR_BLINK;      break;
					case 7:  parser.flags |= ATTR_INVERT;     break;
					case 8:  parser.flags |= ATTR_INVISIBLE;  break;
					case 22: parser.flags &= ~ATTR_BOLD;      break;
					case 24: parser.flags &= ~ATTR_UNDERLINE; break;
					case 25: parser.flags &= ~ATTR_BLINK;     break;
					case 27: parser.flags &= ~ATTR_INVERT;    break;
					case 28: parser.flags &= ~ATTR_INVISIBLE; break;

					case 30: case 40:
					case 31: case 41:
					case 32: case 42:
					case 33: case 43:
					case 34: case 44:
					case 35: case 45:
					case 36: case 46:
					case 37: case 47:
						if (parser.args.buf[i] < 40) {
							parser.color.fg = parser.args.buf[i] - 30;
						} else {
							parser.color.bg = parser.args.buf[i] - 40;
						}
						break;
					case 38:
					case 48:
						if (i + 2 < parser.args.count &&
						    parser.args.buf[i+1] == 5)
						{
							if (parser.args.buf[i] < 40) {
								parser.color.fg = parser.args.buf[i+2];
							} else {
								parser.color.bg = parser.args.buf[i+2];
							}
							i += 2;
						}
						break;
					case 39:
						parser.color.fg = 7;
						break;
					case 49:
						parser.color.fg = 0;
						break;
					case 90: case 100:
					case 91: case 101:
					case 92: case 102:
					case 93: case 103:
					case 94: case 104:
					case 95: case 105:
					case 96: case 106:
					case 97: case 107:
						if (parser.args.buf[i] < 100) {
							parser.color.fg = parser.args.buf[i] + 8 - 90;
						} else {
							parser.color.bg = parser.args.buf[i] + 8 - 100;
						}
						break;
					}
				}
				SEQEND(1);
			case 'u': ESC_CSI("[u");
			}
		}
		switch (ucs4) {
		case 'J':
			switch (parser.lastc) {
			case '[':
				SEQBEG(CSI, ED);
				switch (parser.args.buf[0]) {
				case 0:
					cmd_clear_rows(&tty, tty.cursor.y + 1, tty.rows);
					cmd_set_cells(&tty, &parser.spec,
					              tty.cursor.x, tty.cursor.y,
					              tty.cols);
					break;
				case 1:
					parser.spec.ucs4  = ' ';
					parser.spec.width = 1;
					cmd_clear_rows(&tty, tty.top, tty.cursor.y - tty.top);
					cmd_set_cells(&tty, &parser.spec,
					              0, tty.cursor.y,
					              tty.cursor.x);
					break;
				case 2:
					cmd_clear_rows(&tty, tty.top, tty.rows);
					cmd_set_cursor_y(&tty, 0);
					break;
				}
				SEQEND(1);
			case '?': ESC_CSI("DECSED");
			} break;
		case 'K':
			switch (parser.lastc) {
			case '[':
				SEQBEG(CSI, EL);
				switch (parser.args.buf[0]) {
				case 0:
					cmd_set_cells(&tty, &parser.spec,
					              tty.cursor.x, tty.cursor.y,
					              tty.cols - tty.cursor.x);
					break;
				case 1:
					parser.spec.ucs4 = ' ';
					parser.spec.width = 1;
					cmd_set_cells(&tty, &parser.spec,
					              0, tty.cursor.y,
					              tty.cursor.x);
					break;
				case 2:
					cmd_set_cells(&tty, &parser.spec,
					              0, tty.cursor.y,
					              tty.cols);
					cmd_set_cursor_x(&tty, 0);
					break;
				}
				SEQEND(1);
			case '?': ESC_CSI("DECSEL");
			} break;
		case 'c':
			switch (parser.lastc) {
			case '[': ESC_CSI("DA");
			case '>': ESC_CSI("DA2"); // made-up name
			} break;
		case 'h':
			switch (parser.lastc) {
			case '[': ESC_CSI("SM");
			case '?':
				  SEQBEG(CSI, DECSET);
				  switch (parser.args.buf[0]) {
				  	  case 25: {
				  	  	  tty.cursor.hide = false;
				  	  	  break;
				  	  }
				  }
				  SEQEND(1);
			} break;
		case 'i':
			switch (parser.lastc) {
			case '[': ESC_CSI("MC");
			case '?': ESC_CSI("DECMC");
			} break;
		case 'l':
			switch (parser.lastc) {
			case '[': ESC_CSI("RM");
			case '?':
				  SEQBEG(CSI, DECRST);
				  switch (parser.args.buf[0]) {
				  	  case 25: {
				  	  	  tty.cursor.hide = true;
				  	  	  break;
				  	  }
				  }
				  SEQEND(1);
			} break;
		case 'n':
			switch (parser.lastc) {
			case '?': ESC_CSI("DECDSR");
			} break;
		case 'p':
			switch (parser.lastc) {
			case '!': ESC_CSI("DECSTR");
			case '"': ESC_CSI("DECSCL");
			} break;
		case 'q':
			switch (parser.lastc) {
			case '?': ESC_CSI("?q");
			case '"': ESC_CSI("DECSCA"); // made-up name
			} break;
		case 'r':
			switch (parser.lastc) {
			case '[': ESC_CSI("DECSTBM");
			case '?': ESC_CSI("DECRSTR"); // made-up name
			case '$': ESC_CSI("DECCARA");
			} break;
		case 's':
			switch (parser.lastc) {
			case '[': ESC_CSI("[s");
			case '?': ESC_CSI("DECSAV"); // made-up name
			} break;
		case 't':
			switch (parser.lastc) {
			case '[': ESC_CSI("[t");
			case '$': ESC_CSI("DECRARA");
			} break;
		case 'v':
			switch (parser.lastc) {
			case '$': ESC_CSI("DECRCRA");
			} break;
		case 'w':
			switch (parser.lastc) {
			case '\'': ESC_CSI("DECEFR");
			} break;
		case 'x':
			switch (parser.lastc) {
			case '[': ESC_CSI("[x");
			case '$': ESC_CSI("DECFRA");
			} break;
		case 'z':
			switch (parser.lastc) {
			case '$':  ESC_CSI("DECERA");
			case '\'': ESC_CSI("DECELR");
			} break;
		case '{':
			switch (parser.lastc) {
			case '\'': ESC_CSI("DECSLE");
			} break;
		case '|':
			switch (parser.lastc) {
			case '\'': ESC_CSI("DECRQLP");
			} break;
		}
	} else {
		return 0;
	}
#undef ESC_CSI

quit:
	PRINTSEQ("QUIT");
	state_reset();
	return 0;
}

void
state_set(uint mask, bool opt, uint32 ucs4)
{
	SET(parser.state, mask, opt);
	parser.lastc = ucs4; // cache the transition character
}

void
state_reset(void)
{
	parser.state &= ~(STATE_MASK);
	parser.lastc = 0;
	buf_reset();
}

void
state_set_esc(bool opt)
{
	SET(parser.state, STATE_ESC, opt);
	SET(parser.state, STATE_CSI|STATE_DEC, false);
	if (!(parser.state & STATE_STR)) {
		buf_reset();
	}
	parser.lastc = 0;
}

#undef SET
