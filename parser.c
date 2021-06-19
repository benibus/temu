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
	    asciistr(g_ucode), (s_), parser.state, asciistr(parser.lastch), parser.narg); \
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
	memset(parser.args, 0, sizeof(*parser.args) * parser.narg);
	parser.narg = 0;
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
		PRINTSEQ("BEG>>ESC");
		state_set_esc(true);
		return 1;
	}

	if (parser.state & StateSTR) {
		if (parser.state & StateESC) {
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

	/* ASSERT(parser.state & ESC_MASK); */

	switch (parser.state & ESC_MASK) {
	case StateESC:
		switch (ucode) {
		case CtrlCAN:
		case CtrlSUB:
			state_reset();
			PRINTSEQ("ESC:CAN/SUB>END");
			break;
		case '[':
			PRINTSEQ("ESC>CSI");
			state_set_esc(false);
			state_set(StateCSI, true, 0);
			break;
		default:
			goto quit;
		}
		return 3;
	case StateCSI:
	case StateCSI|StateDEC:
		if (get_arg_csi(ucode)) {
			PRINTSEQ((ucode == ';') ? "CSI#ARG:SEP" : "CSI#ARG:NUM");
			return 6;
		}
		if (parser.state & StateDEC) {
			switch (parser.lastch) {
			case '?':
				switch (ucode) {
				case 'h': // DECSET
					PRINTSEQ("DEC:DECSET");
					break;
				case 'l': // DECRST
					PRINTSEQ("DEC:DECRST");
					break;
				case 'n': // DSR
					PRINTSEQ("DEC:DSR");
					break;
				case 'q': // DECSCA
					PRINTSEQ("DEC:DECSCA");
					break;
				case 'r': // DEC Restore Private
					PRINTSEQ("DEC:DECRSO");
					break;
				case 's': // DEC Save Private
					PRINTSEQ("DEC:DECSAV");
					break;
				default:
					goto quit;
				}
				break;
			case '!':
				switch (ucode) {
				case 'p': // DECSTR
					PRINTSEQ("DEC:DECSTR");
					break;
				default:
					goto quit;
				}
				break;
			case '"':
				switch (ucode) {
				case 'p': // DECSCL
					PRINTSEQ("DEC:DECSCL");
					break;
				default:
					goto quit;
				}
				break;
			case '\'':
				switch (ucode) {
				case 'z': // DECSCL
					PRINTSEQ("DEC:DECELR");
					break;
				default:
					goto quit;
				}
				break;
			case '$':
				switch (ucode) {
				case 'x': // DECFRA
					PRINTSEQ("DEC:DECFRA");
					break;
				default:
					goto quit;
				}
				break;
			default:
				goto quit;
			}
			return 5;
		}
		switch (ucode) {
		case '?':
		case '!':
		case '"':
		case '\'':
		case '$':
			PRINTSEQ("CSI>DEC");
			state_set(StateCSI|StateDEC, true, ucode);
			break;
		case 'A': // CUU
			PRINTSEQ("CSI:CUU");
			break;
		case 'B': // CUD
			PRINTSEQ("CSI:CUD");
			break;
		case 'C': // CUF
			PRINTSEQ("CSI:CUF");
			break;
		case 'D': // CUB
			PRINTSEQ("CSI:CUB");
			break;
		case 'm': // SGR
			PRINTSEQ("CSI:SGR");
			break;
		default:
			goto quit;
		}
		return 4;
	default:
		return 0;
	}

quit:
	PRINTSEQ("QUIT");
	state_reset();
	return 0;
}

void
state_set(uint mask, bool opt, int ucode)
{
	SET(parser.state, mask, opt);
	parser.lastch = ucode;
}

void
state_reset(void)
{
	parser.state &= ~(ESC_MASK);
	parser.lastch = 0;
	buf_reset();
}

void
state_set_esc(bool opt)
{
	SET(parser.state, StateESC, opt);
	SET(parser.state, StateCSI, false);
	SET(parser.state, StateDEC, false);
	if (!(parser.state & StateSTR)) {
		buf_reset();
	}
	parser.lastch = 0;
}

#undef SET

