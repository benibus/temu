#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <unistd.h>

#include "utils.h"
#include "term.h"
#include "parser.h"

#define SET(base_,mask_,opt_) \
( (base_) = ((opt_)) ? ((base_) | (mask_)) : ((base_) & ~(mask_)) )

#define PRINTSEQ(s_) do { \
	msg_log("Parser", "%-6s %-15s (%02u)  lc:%-6s #%02u:", \
	    asciistr(g_ucode), (s_), parser.state, asciistr(parser.lastc), parser.narg); \
	for (uint i = 0; i < parser.narg; i++) {               \
		msg_log("\0", " %04zu", parser.args[i]);       \
	}                                                      \
	fprintf(stderr, "\n");                                 \
} while (0)

static Parser parser = { 0 };
static int g_ucode = 0; // debug only

static void state_set(uint, bool, int);
static void state_reset(void);
static void state_set_esc(bool);
static void buf_push(int);
static void buf_reset(void);

void
buf_reset(void)
{
	parser.idx = 0;
	parser.buf[parser.idx] = 0;
	if (parser.narg) {
		memset(parser.args, 0, sizeof(*parser.args) * parser.narg);
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

int
parse_codepoint(int ucode)
{
	g_ucode = ucode;

	switch (ucode) {
	case CtrlESC:
		PRINTSEQ("BEG>ESC");
		state_set_esc(true);
		return 1;
	}

	u32 state = (parser.state & ESC_MASK);

	if (state & StateSTR) {
		if (state & StateESC) {
			switch (ucode) {
			case '\\':
				PRINTSEQ("END");
				state_set(StateSTR, false, ucode);
				break;
			default:
				buf_push(CtrlESC);
				buf_push(ucode);
				break;
			}
			state_reset();
		} else {
			buf_push(ucode);
		}
		return 2;
	}

	if (state & StateESC) {
		switch (ucode) {
		case CtrlCAN:
		case CtrlSUB:
			state_reset();
			PRINTSEQ("ESC:CAN/SUB>END");
			break;
		case '[':
			PRINTSEQ("ESC>CSI");
			state_set_esc(false);
			state_set(StateCSI, true, ucode);
			break;
		default:
			goto quit;
		}
		return 3;
	} else if (state & StateCSI) {
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
				state_set(StateDEC, true, ucode);
				return 5;
			// CSI terminators with no alt variations (VT100)
			case '@': ESC_CSI("ICH");
			case 'A': ESC_CSI("CUU");
			case 'B': ESC_CSI("CUD");
			case 'C': ESC_CSI("CUF");
			case 'D': ESC_CSI("CUB");
			case 'E': ESC_CSI("CNL");
			case 'F': ESC_CSI("CPL");
			case 'G': ESC_CSI("CHA");
			case 'L': ESC_CSI("IL");
			case 'M': ESC_CSI("DL");
			case 'P': ESC_CSI("DCH");
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
			case '[': ESC_CSI("ED");
			case '?': ESC_CSI("DECSED");
			} break;
		case 'K':
			switch (parser.lastc) {
			case '[': ESC_CSI("EL");
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
	SET(parser.state, StateESC, opt);
	SET(parser.state, StateCSI|StateDEC, false);
	if (!(parser.state & StateSTR)) {
		buf_reset();
	}
	parser.lastc = 0;
}

#undef SET

