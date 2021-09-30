#include <limits.h>

#include "utils.h"
#include "term.h"
#include "parser.h"
#include "fsm.h"

static void parser_dispatch(Term *, StateCode, ActionCode, uchar);

// C0 control functions
static void emu_c0_ctrl(Term *, char);

// C1 control functions
static void emu_c1_ri(Term *);

// CSI functions
static void emu_csi_ich(Term *, const int *, int);
static void emu_csi_cuu(Term *, const int *, int);
static void emu_csi_cud(Term *, const int *, int);
static void emu_csi_cuf(Term *, const int *, int);
static void emu_csi_cub(Term *, const int *, int);
static void emu_csi_cup(Term *, const int *, int);
static void emu_csi_cht(Term *, const int *, int);
static void emu_csi_dch(Term *, const int *, int);
static void emu_csi_ed(Term *, const int *, int);
static void emu_csi_el(Term *, const int *, int);
static void emu_csi_sgr(Term *, const int *, int);
static void emu_csi_decset(Term *, const int *, int);
static void emu_csi_decrst(Term *, const int *, int);
static void emu_csi_decscusr(Term *, const int *, int);

// OSC/DCS functions
static void emu_osc(Term *, const char *, const int *, int);

// Debug only
#define FUNC_ENTRIES \
  X_(PRINT,    "*"               ) \
  X_(RI,       "ESC"          "M") \
  X_(OSC,      "ESC]" "\3"       ) \
  X_(ICH,      "ESC[" "\1"    "@") \
  X_(DCH,      "ESC[" "\1"    "P") \
  X_(CUU,      "ESC[" "\1"    "A") \
  X_(CUD,      "ESC[" "\1"    "B") \
  X_(CUF,      "ESC[" "\1"    "C") \
  X_(CUB,      "ESC[" "\1"    "D") \
  X_(CUP,      "ESC[" "\1;\1" "H") \
  X_(CHT,      "ESC[" "\1"    "I") \
  X_(ED,       "ESC[" "\1"    "J") \
  X_(EL,       "ESC[" "\1"    "K") \
  X_(SGR,      "ESC[" "\2"    "m") \
  X_(DECSCUSR, "ESC[" "\1"   " q") \
  X_(DECSET,   "ESC[" "\1"   "?h") \
  X_(DECRST,   "ESC[" "\1"   "?l")

typedef enum {
#define X_(sym,...) Func##sym,
	FUNC_ENTRIES
#undef X_
	FuncCount
} FuncID;

static const struct FuncEntry {
	const char *symbol;
	const char *codestring;
} func_entries[FuncCount] = {
#define X_(sym,...) [Func##sym] = { #sym, __VA_ARGS__ },
	FUNC_ENTRIES
#undef X_
};
#undef FUNC_ENTRIES

#if 1
#define FUNC_DEBUG(sym) parser_print_debug(term, Func##sym)
#else
#define FUNC_DEBUG(...)
#endif

static void parser_print_debug(const Term *, FuncID);

void
parse_byte(Term *term, unsigned char c)
{
	Parser *parser = &term->parser;

	StateTrans result = fsm_next_state(parser->state, c);

	for (uint n = 0; n < LEN(result.actions); n++) {
		parser_dispatch(term, result.state, result.actions[n], c);
	}

	parser->state = result.state;
}

void
parser_print_debug(const Term *term, FuncID id)
{
	struct FuncEntry ent = func_entries[id];
	char buf[64] = { 0 };
	const char *str = ent.codestring;

	const int *argv = term->parser.argv;
	const int argc = term->parser.argi + 1;
	const char *post = "";
	int ai = 0;

	for (uint i = 0, j = 0; j + 1 < LEN(buf) && str[i]; i++) {
		if (str[i] > ' ') {
			buf[j++] = str[i];
		} else {
			int lim = ai;
			if (str[i] == 1) {
				lim++;
			} else if (str[i] == 2) {
				lim = argc;
			} else {
				post = (const char *)term->parser.data;
			}
			while (ai < lim) {
				j += snprintf(buf + j, LEN(buf)-1-j, "%02d", argv[ai]);
				if (++ai < lim && j + 2 < LEN(buf)) {
					buf[j++] = ';';
				} else {
					break;
				}
			}
		}
	}

	dbgprintf("[[%s]] %s%s\n", ent.symbol, buf, post);
}

void
parser_dispatch(Term *term, StateCode state, ActionCode action, uchar c)
{
	if (!action) return;

	struct Parser *parser = &term->parser;
#if 0
	dbgprintf("FSM(%s|%#.02x): State%s -> State%s ... %s()\n",
	  charstring(c), c,
	  fsm_get_state_string(parser->state),
	  fsm_get_state_string(state),
	  fsm_get_action_string(action));
#endif

	// TODO(ben): DCS functions
	switch (action) {
	case ActionIgnore:
		break;
	case ActionPrint:
		write_codepoint(term, parser->ucs4|(c & 0x7f), CellTypeNormal);
		parser->ucs4 = 0;
		break;
	case ActionUtf8Start:
		switch (state) {
		case StateUtf8B3: parser->ucs4 |= (c & 0x07) << 18; break;
		case StateUtf8B2: parser->ucs4 |= (c & 0x0f) << 12; break;
		case StateUtf8B1: parser->ucs4 |= (c & 0x1f) <<  6; break;
		default: break;
		}
		break;
	case ActionUtf8Cont:
		switch (state) {
		case StateUtf8B2: parser->ucs4 |= (c & 0x3f) << 12; break;
		case StateUtf8B1: parser->ucs4 |= (c & 0x3f) <<  6; break;
		default: break;
		}
		break;
	case ActionUtf8Fail:
		dbgprint("discarding malformed UTF-8 sequence");
		parser->ucs4 = 0;
		break;
	case ActionExec:
		emu_c0_ctrl(term, c);
		break;
	case ActionHook:
		// sets handler based on DCS parameters, intermediates, and new char
		break;
	case ActionUnhook:
		break;
	case ActionPut:
		arr_push(parser->data, c);
		break;
	case ActionOscStart:
		memset(parser->argv, 0, sizeof(parser->argv));
		parser->argi = 0;
		arr_clear(parser->data);
		break;
	case ActionOscPut:
		// All OSC sequences take a leading numeric parameter, which we consume in the arg buffer.
		// Semicolon-separated string parameters are handled by the OSC parser itself
		if (!parser->argi) {
			if (c >= '0' && c <= '9') {
				parser->argv[0] *= 10;
				parser->argv[0] += c - '0';
			} else {
				// NOTE(ben): Assuming OSC sequences take a default '0' parameter
				if (c != ';') {
					parser->argv[0] = 0;
				}
				parser->argv[++parser->argi] = 0;
			}
		} else {
			arr_push(parser->data, c);
		}
		break;
	case ActionOscEnd:
#define osc_params__ term, (const char *)parser->data, parser->argv, parser->argi+1
		arr_push(parser->data, 0);
		emu_osc(osc_params__);
		break;
	case ActionCollect:
		if (parser->depth == LEN(parser->tokens)) {
			dbgprint("warning: ignoring excess intermediate");
		} else {
			parser->tokens[parser->depth++] = c;
		}
		break;
	case ActionParam:
		if (c == ';') {
			if (parser->argi + 1 == LEN(parser->argv)) {
				dbgprint("warning: ignoring excess parameter");
			} else {
				parser->argv[++parser->argi] = 0;
			}
		} else {
			ASSERT(INT_MAX / 10 > parser->argv[parser->argi]);
			parser->argv[parser->argi] *= 10;
			parser->argv[parser->argi] += (c - '0');
		}
		break;
	case ActionClear:
		memset(parser->tokens, 0, sizeof(parser->tokens));
		memset(parser->argv, 0, sizeof(parser->argv));
		parser->depth = 0;
		parser->argi = 0;
		arr_clear(parser->data);
		break;
	case ActionEscDispatch:
#define c1_params__ term
		switch (c) {
		case 'E': break; // NEL
		case 'H': break; // HTS
		case 'M': emu_c1_ri(c1_params__); break;
		case '\\': break; // ST
		case '6': break; // DECBI
		case '7': break; // DECSC
		case '8': break; // DECRC
		case '9': break; // DECFI
		}
		break;
	case ActionCsiDispatch: {
#define csi_params__ term, parser->argv, parser->argi + 1
		ASSERT(parser->depth == 1 || (!parser->depth && !parser->tokens[0]));
		switch (parser->tokens[0]) {
		case 0:
			switch (c) {
			case '@': emu_csi_ich(csi_params__); break;
			case 'A': emu_csi_cuu(csi_params__); break;
			case 'B': emu_csi_cud(csi_params__); break;
			case 'C': emu_csi_cuf(csi_params__); break;
			case 'D': emu_csi_cub(csi_params__); break;
			case 'E': break; // CNL
			case 'F': break; // CPL
			case 'G': break; // CHA
			case 'H': emu_csi_cup(csi_params__); break;
			case 'I': emu_csi_cht(csi_params__); break;
			case 'J': emu_csi_ed(csi_params__); break;
			case 'K': emu_csi_el(csi_params__); break;
			case 'L': break; // IL
			case 'M': break; // DL
			case 'P': emu_csi_dch(csi_params__); break;
			case 'S': break; // SU
			case 'T': break; // SD
			case 'X': break; // ECH
			case 'Z': break; // CBT
			case '`': break; // HPA
			case 'a': break; // HPR
			case 'b': break; // REP
			case 'd': break; // VPA
			case 'f': break; // HVP
			case 'g': break; // TBC
			case 'h': break; // SM
			case 'i': break; // MC
			case 'l': break; // RM
			case 'm': emu_csi_sgr(csi_params__); break;
			case 'r': break; // DECSTBM
			case 'c': break; // DA
			case 's': break; // SCOSC
			case 't': break; // XTERM_WM
			}
			break;
		case ' ':
			if (c == 'q') { emu_csi_decscusr(csi_params__); }
			break;
		case '!':
			// 'p' == DECSTR
			break;
		case '"':
			// 'p' == DECSCL
			break;
		case '$':
			switch (c) {
			case 't': break; // DECCARA
			case 'v': break; // DECCRA
			case 'x': break; // DECFRA
			case 'z': break; // DECERA
			}
			break;
		case '\'':
			// '}' == DECIC
			// '~' == DECDC
			break;
		case '>':
			switch (c) {
			case 'w': break; // DECEFR
			case 'z': break; // DECELR
			case '{': break; // DECSLE
			case '|': break; // DECRQLP
			}
			break;
		case '?':
			switch (c) {
			case 'J': break; // DECSED
			case 'K': break; // DECSEL
			case 'h': emu_csi_decset(csi_params__); break; // DECSET/DECTCEM
			case 'i': break; // DECMC
			case 'l': emu_csi_decrst(csi_params__); break; // DECRST/DECTCEM
			case 'n': break; // DECDSR
			}
			break;
		case '}':
			break;
		case '~':
			break;
		default:
			break;
		}
		break;
	}
	default:
		break;
	}
#undef c1_params__
#undef csi_params__
#undef osc_params__
}

void
emu_c0_ctrl(Term *term, char c)
{
	switch (c) {
	case '\n':
	case '\v':
	case '\f':
		write_newline(term);
		break;
	case '\t':
		write_tab(term);
		break;
	case '\r':
		cursor_set_col(term, 0);
		break;
	case '\b':
		cursor_move_cols(term, -1);
		break;
	case '\a':
		break;
	default:
		dbgprintf("unhandled control character: %s\n", charstring(c));
		break;
	}
}

void
emu_c1_ri(Term *term)
{
	FUNC_DEBUG(RI);
	cursor_move_rows(term, -1);
}

void
emu_csi_ich(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(ICH);
	cells_insert(term, term->pos.x, term->pos.y, DEFAULT(argv[0], 1));
}

void
emu_csi_cuu(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(CUU);
	cursor_move_rows(term, -DEFAULT(argv[0], 1));
}

void
emu_csi_cud(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(CUD);
	cursor_move_rows(term, +DEFAULT(argv[0], 1));
}

void
emu_csi_cuf(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(CUF);
	cursor_move_cols(term, +DEFAULT(argv[0], 1));
}

void
emu_csi_cub(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(CUB);
	cursor_move_cols(term, -DEFAULT(argv[0], 1));
}

void
emu_csi_cup(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(CUP);
	cursor_set_col(term, argv[1]);
	cursor_set_row(term, argv[0]);
}

void
emu_csi_cht(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(CHT);

	for (int n = DEFAULT(argv[0], 1); n > 0; n--) {
		write_tab(term);
	}
}

void
emu_csi_dch(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(DCH);
	cells_delete(term, term->pos.x, term->pos.y, argv[0]);
}

void
emu_csi_ed(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(ED);

	switch (argv[0]) {
	case 0:
		cells_clear_lines(term, term->pos.y + 1, term->rows);
		cells_clear(term, term->pos.x, term->pos.y, term->cols);
		break;
	case 1:
		cells_clear_lines(term, 0, term->pos.y);
		cells_init(term, 0, term->pos.y, term->pos.x);
		break;
	case 2:
		cells_clear_lines(term, 0, term->rows);
		cursor_set_row(term, 0);
		break;
	}
}

void
emu_csi_el(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(EL);

	switch (argv[0]) {
	case 0:
		cells_clear(term, term->pos.x, term->pos.y, term->cols);
		break;
	case 1:
		cells_init(term, 0, term->pos.y, term->pos.x);
		break;
	case 2:
		cells_clear(term, 0, term->pos.y, term->cols);
		cursor_set_col(term, 0);
		break;
	}
}

void
emu_csi_sgr(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(SGR);

	int i = 0;

	do {
		const int start = i;

		switch (argv[i]) {
		case 0:
			cells_set_attrs(term, 0);
			cells_reset_bg(term);
			cells_reset_fg(term);
			break;

		case 1:  cells_add_attrs(term, ATTR_BOLD);      break;
		case 4:  cells_add_attrs(term, ATTR_UNDERLINE); break;
		case 5:  cells_add_attrs(term, ATTR_BLINK);     break;
		case 7:  cells_add_attrs(term, ATTR_INVERT);    break;
		case 8:  cells_add_attrs(term, ATTR_INVISIBLE); break;
		case 22: cells_del_attrs(term, ATTR_BOLD);      break;
		case 24: cells_del_attrs(term, ATTR_UNDERLINE); break;
		case 25: cells_del_attrs(term, ATTR_BLINK);     break;
		case 27: cells_del_attrs(term, ATTR_INVERT);    break;
		case 28: cells_del_attrs(term, ATTR_INVISIBLE); break;

		case 30: case 31:
		case 32: case 33:
		case 34: case 35:
		case 36: case 37:
			cells_set_fg(term, cellcolor(ColorTag256, argv[i] - 30, 0));
			break;
		case 39:
			cells_reset_fg(term);
			break;

		case 40: case 41:
		case 42: case 43:
		case 44: case 45:
		case 46: case 47:
			cells_set_bg(term, cellcolor(ColorTag256, argv[i] - 40, 0));
			break;
		case 49:
			cells_reset_bg(term);
			break;

		case 38:
		case 48:
			if (++i + 1 < argc) {
				if (argv[i] == 5) {
					i++;
				} else if (argv[i] == 2 && i + 3 < argc) {
					i += 3;
				}
			}
			if (i - start == 2) {
				if (argv[start] == 48) {
					cells_set_bg(term, cellcolor(ColorTag256, argv[i] & 0xff));
				} else if (argv[start] == 38) {
					cells_set_fg(term, cellcolor(ColorTag256, argv[i] & 0xff));
				}
			} else if (i - start == 4) {
				if (argv[start] == 48) {
					cells_set_bg(term,
						cellcolor(
							ColorTagRGB,
							argv[i-2] & 0xff,
							argv[i-1] & 0xff,
							argv[i-0] & 0xff
						)
					);
				} else if (argv[start] == 38) {
					cells_set_fg(term,
						cellcolor(
							ColorTagRGB,
							argv[i-2] & 0xff,
							argv[i-1] & 0xff,
							argv[i-0] & 0xff
						)
					);
				}
			} else {
				// TODO(ben): confirm whether errors reset the defaults
				dbgprint("skiping invalid CSI:SGR sequence");
				cells_set_attrs(term, 0);
				cells_reset_bg(term);
				cells_reset_fg(term);
				return;
			}
			break;

		case 90: case 91:
		case 92: case 93:
		case 94: case 95:
		case 96: case 97:
			cells_set_fg(term, cellcolor(ColorTag256, argv[i] - 90 + 8));
			break;

		case 100: case 101:
		case 102: case 103:
		case 104: case 105:
		case 106: case 107:
			cells_set_bg(term, cellcolor(ColorTag256, argv[i] - 100 + 8));
			break;
		}
	} while (++i < argc);
}

static void
emu__csi_decprv(Term *term, int mode, bool enable)
{
	switch (mode) {
	case 25: cursor_set_hidden(term, !enable); break; // DECTCEM
	}
}

// NOTE(ben): Wrapper
void
emu_csi_decset(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(DECSET);
	emu__csi_decprv(term, argv[0], true);
}

// NOTE(ben): Wrapper
void
emu_csi_decrst(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(DECRST);
	emu__csi_decprv(term, argv[0], false);
}

void
emu_csi_decscusr(Term *term, const int *argv, int argc)
{
	FUNC_DEBUG(DECSCUSR);
	cursor_set_style(term, argv[0]);
}

void
emu_osc(Term *term, const char *str, const int *argv, int argc)
{
	FUNC_DEBUG(OSC);

	// TODO(ben): Implement the actual OSC handlers
#define dbgmsg__(msg_) dbgprintf("OSC(%s): \"%s\"\n", msg_, str)
	switch (argv[0]) {
	case 0:  dbgmsg__("Icon/Title"); break;
	case 1:  dbgmsg__("Icon");       break;
	case 2:  dbgmsg__("Title");      break;
	case 3:  dbgmsg__("XProp");      break;
	case 4:  dbgmsg__("Color");      break;
	default: dbgmsg__("???");        break;
	}
#undef dbgmsg__
}

