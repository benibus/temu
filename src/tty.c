#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <unistd.h>

#include "utils.h"
#include "term.h"

#define SET(base_,mask_,opt_) \
( (base_) = ((opt_)) ? ((base_) | (mask_)) : ((base_) & ~(mask_)) )

#if 1
#define PRINTSEQ(s_) do { \
	msg_log("Parser", "%-6s %-15s (%02u)  lc:%-6s #%02u:", \
	    asciistr(g_ucode), (s_), parser.state, asciistr(parser.lastc), parser.narg); \
	for (uint i = 0; i < parser.narg; i++) {               \
		msg_log("\0", " %04zu", parser.args[i]);       \
	}                                                      \
	fprintf(stderr, "\n");                                 \
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

enum stateflags_e_ {
	STATE_DEFAULT,
	STATE_ESC = (1 << 0),
	STATE_CSI = (1 << 1),
	STATE_DEC = (1 << 2),
	STATE_STR = (1 << 3),
	STATE_MAX = (1 << 4)
};

#define ESC_MASK (STATE_MAX-1)

#define MAX_ARGS 256
typedef struct parser_s_ {
	u32 state;
	char buf[BUFSIZ];
	size_t idx;
	size_t args[MAX_ARGS];
	uint narg;
	int lastc;
} Parser;

static int g_ucode = 0; // debug only

static Parser parser = { 0 };
static char mbuf[BUFSIZ];
static size_t msize = 0;

static void state_set(uint, bool, int);
static void state_reset(void);
static void state_set_esc(bool);
static void buf_push(int);
static void buf_reset(void);
static int parse_codepoint(int);

bool
tty_init(int cols, int rows)
{
	memclear(&tty, 1, sizeof(tty));
	tty.maxcols = cols;
	tty.maxrows = rows;
	stream_realloc(bitround(histsize * tty.maxcols, 1) + 1);

	ASSERT(tabstop > 0);
	for (int i = 0; ++i < tty.maxcols; ) {
		tty.tabs[i] |= (i % tabstop == 0) ? 1 : 0;
	}

	return (tty.data && tty.attr);
}

size_t
tty_write(const char *str, size_t len)
{
	size_t i = 0;
#if 1
	{
		for (size_t j = 0; j < len; j++) {
			long tmp;
			if (msize >= LEN(mbuf)-1-1)
				break;
			tmp = snprintf(&mbuf[msize], LEN(mbuf)-msize-1-1, "%s ", asciistr(str[j]));
			/* printf("%zu/%zu\n", msize, LEN(mbuf)); */
			if (tmp <= 0) break;
			msize += tmp;
		}
		msg_log("Input", "%s\n", mbuf);
		msize = 0;
		mbuf[msize] = 0;
	}
#endif
	for (i = 0; str[i] && i < len; i++) {
		if (tty.size + tty.maxcols >= tty.max) {
			stream_realloc(bitround(tty.size * 2, 1) + 1);
		}
		if (!parse_codepoint(str[i])) {
			int u = str[i];
			if (u == '\r' && i + 1 < len) {
				if (str[i+1] == CTRL_LF) {
					u = '\n';
					i++;
				}
			}
			stream_write(u);
		} else {
			dummy__();
		}
	}

	tty.data[tty.size] = 0;

	return i;
}

void
buf_reset(void)
{
	parser.idx = 0;
	parser.buf[parser.idx] = 0;
	if (parser.narg) {
		memclear(parser.args, parser.narg, sizeof(*parser.args));
		parser.narg = 0;
	}
}

void
buf_push(int ucode)
{
	ASSERT(parser.idx < BUFSIZ); // temporary
	parser.buf[parser.idx++] = ucode;
	parser.idx = 0;
}

static bool
get_arg_csi(int ucode)
{
	size_t *arg;

	switch (ucode) {
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		parser.narg += (!parser.narg);
		arg = &parser.args[parser.narg-1];
		*arg *= 10;
		*arg += ucode - '0';
		break;
	case ';':
		ASSERT(parser.narg > 0);
		parser.narg++;
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
parse_codepoint(int ucode)
{
	g_ucode = ucode;

	switch (ucode) {
	case CTRL_ESC:
		PRINTSEQ("BEG>ESC");
		state_set_esc(true);
		return 1;
	}

	u32 state = (parser.state & ESC_MASK);

	if (state & STATE_STR) {
		if (state & STATE_ESC) {
			switch (ucode) {
			case '\\':
				PRINTSEQ("END");
				state_set(STATE_STR, false, ucode);
				break;
			default:
				buf_push(CTRL_ESC);
				buf_push(ucode);
				break;
			}
			state_reset();
		} else {
			switch (ucode) {
			case CTRL_ST:
			case CTRL_BEL:
				PRINTSEQ("END");
				state_reset();
				break;
			default:
				buf_push(ucode);
				break;
			}
		}
		return 2;
	}

	if (state & STATE_ESC) {
		switch (ucode) {
		case CTRL_CAN:
		case CTRL_SUB:
			PRINTSEQ("ESC:CAN/SUB>END");
			state_reset();
			break;
		case '[':
			PRINTSEQ("ESC>CSI");
			state_set_esc(false);
			state_set(STATE_CSI, true, ucode);
			break;
		case ']':
			PRINTSEQ("ESC>OSC");
			state_set_esc(false);
			state_set(STATE_STR, true, ucode);
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
			stream_move_cursor_row(-1);
			SEQEND(1);
			break;
		default:
			goto quit;
		}
		return 3;
	} else if (state & STATE_CSI) {
		if (get_arg_csi(ucode)) {
			PRINTSEQ("CSI#ARG");
			return 5;
		}
#define ESC_CSI(str_) do { PRINTSEQ("CSI:"str_); state_reset(); } while(0); return 4
		if (parser.lastc == '[') {
			// we check for an alt char as the first non-arg only
			switch (ucode) {
			case '?':
			case '!':
			case '"':
			case '\'':
			case '$':
			case '>':
				PRINTSEQ("CSI>DEC");
				state_set(STATE_DEC, true, ucode);
				return 5;
			// CSI terminators with no alt variations (VT100)
			case '@':
#if 0
				ESC_CSI("ICH");
#else
				SEQBEG(CSI, ICH);
				stream_insert_cells(' ', DEFAULT(parser.args[0], 1));
				SEQEND(1);
#endif
			case 'A':
				SEQBEG(CSI, CUU);
				stream_move_cursor_row(-DEFAULT(parser.args[0], 1));
				SEQEND(1);
			case 'B':
				SEQBEG(CSI, CUD);
				stream_move_cursor_row(+DEFAULT(parser.args[0], 1));
				SEQEND(1);
			case 'C':
				SEQBEG(CSI, CUF);
				stream_move_cursor_col(+DEFAULT(parser.args[0], 1));
				SEQEND(1);
			case 'D':
				SEQBEG(CSI, CUB);
				stream_move_cursor_col(-DEFAULT(parser.args[0], 1));
				SEQEND(1);
			case 'E': ESC_CSI("CNL");
			case 'F': ESC_CSI("CPL");
			case 'G': ESC_CSI("CHA");
			case 'H':
				SEQBEG(CSI, CUP);
				stream_set_cursor_pos(
				    parser.args[1],
				    parser.args[0]
				);
				SEQEND(1);
			case 'I':
				SEQBEG(CSI, CHT);
				for (size_t i = 0; i < DEFAULT(parser.args[0], 1); i++) {
					  if (!stream_write('\t')) break;
				}
				SEQEND(1);
			case 'L': ESC_CSI("IL");
			case 'M': ESC_CSI("DL");
			case 'P':
				SEQBEG(CSI, DCH);
				stream_clear_row_cells(tty.c.row, tty.c.col, DEFAULT(parser.args[0], 1), true, false);
				SEQEND(1);
			case 'S': ESC_CSI("SU");
			case 'T': ESC_CSI("SD");
			case 'X': ESC_CSI("ECH");
			case 'Z': ESC_CSI("CBT");
			case '`': ESC_CSI("HPA");
			case 'b': ESC_CSI("REP");
			case 'd': ESC_CSI("VPA");
			case 'f': ESC_CSI("HVP");
			case 'g': ESC_CSI("TBC");
			case 'm': ESC_CSI("SGR");
			case 'u': ESC_CSI("[u");
			}
		}
		switch (ucode) {
		case 'J':
			switch (parser.lastc) {
			case '[':
#if 1
				SEQBEG(CSI, ED);
				switch (parser.args[0]) {
				case 0:
					stream_clear_rows(tty.c.row + 1, tty.maxrows);
					stream_clear_row_cells(tty.c.row, tty.c.col, tty.maxcols, true, false);
					break;
				case 1:
					stream_clear_rows(tty.rows.top, tty.c.row - tty.rows.top);
					stream_clear_row_cells(tty.c.row, 0, tty.c.col, false, false);
					break;
				case 2:
					stream_clear_rows(tty.rows.top, tty.maxrows);
					stream_set_cursor_row(0);
					break;
				}
				SEQEND(1);
#else
				ESC_CSI("ED");
#endif
			case '?': ESC_CSI("DECSED");
			} break;
		case 'K':
			switch (parser.lastc) {
			case '[':
				SEQBEG(CSI, EL);
				switch (parser.args[0]) {
				case 0:
					stream_clear_row_cells(tty.c.row, tty.c.col, tty.maxcols, true, false);
					break;
				case 1:
					stream_clear_row_cells(tty.c.row, 0, tty.c.col, false, false);
					break;
				case 2:
					stream_clear_row_cells(tty.c.row, 0, tty.maxcols, true, false);
					stream_set_cursor_col(0);
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
			case '?': ESC_CSI("DECSET");
			} break;
		case 'i':
			switch (parser.lastc) {
			case '[': ESC_CSI("MC");
			case '?': ESC_CSI("DECMC");
			} break;
		case 'l':
			switch (parser.lastc) {
			case '[': ESC_CSI("RM");
			case '?': ESC_CSI("DECRST");
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
state_set(uint mask, bool opt, int ucode)
{
	SET(parser.state, mask, opt);
	parser.lastc = ucode; // cache the transition character
}

void
state_reset(void)
{
	parser.state &= ~(ESC_MASK);
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
