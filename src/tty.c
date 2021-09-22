#include <limits.h>

#include "utils.h"
#include "term.h"
#include "pty.h"
#include "fsm.h"

static void vte_action_dispatch(TTY *, StateCode, ActionCode, uchar);

// Standard operations
static void vte_write(TTY *, uint32);
// C1 control functions
static void vte_c1_ri(TTY *);
// CSI functions
static void vte_csi_ich(TTY *, const int *, int);
static void vte_csi_cuu(TTY *, const int *, int);
static void vte_csi_cud(TTY *, const int *, int);
static void vte_csi_cuf(TTY *, const int *, int);
static void vte_csi_cub(TTY *, const int *, int);
static void vte_csi_cup(TTY *, const int *, int);
static void vte_csi_cht(TTY *, const int *, int);
static void vte_csi_dch(TTY *, const int *, int);
static void vte_csi_ed(TTY *, const int *, int);
static void vte_csi_el(TTY *, const int *, int);
static void vte_csi_sgr(TTY *, const int *, int);
static void vte_csi_decset(TTY *, const int *, int);
static void vte_csi_decrst(TTY *, const int *, int);
static void vte_csi_decscusr(TTY *, const int *, int);
// OSC/DCS functions
static void vte_osc(TTY *, const char *, const int *, int);

static const Cell g_celltempl = {
	.ucs4  = ' ',
	.type  = CellTypeNormal,
	.width = 1,
	.attr  = 0,
	.color.bg = {
		.tag = ColorTagNone,
		.index = TCOLOR_BG
	},
	.color.fg = {
		.tag = ColorTagNone,
		.index = TCOLOR_FG
	}
};

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

static void vte_print_debug(const TTY *, FuncID);

#if 1
#define FUNC_DEBUG(sym) vte_print_debug(tty, Func##sym)
#else
#define FUNC_DEBUG(...)
#endif

size_t
tty_write_raw(TTY *tty, const uchar *str, size_t len, uint8 type)
{
	Parser *parser = &tty->parser;
	uint i = 0;
	static uchar *input_; // Debug


	for (; str[i] && i < len; i++) {
		StateTrans result = fsm_next_state(parser->state, str[i]);

		for (uint n = 0; n < LEN(result.actions); n++) {
			vte_action_dispatch(tty, result.state, result.actions[n], str[i]);
		}
		parser->state = result.state;

#if 1
#define DBGOPT_PRINT_INPUT 1
		{
			const char *tmp = charstring(str[i]);
			int len = strlen(tmp);
			for (int j = 0; j + !j <= len; j++) {
				arr_push(input_, (j <= len) ? tmp[j] : ' ');
			}
		}
#endif
	}

#ifdef DBGOPT_PRINT_INPUT
	if (arr_count(input_)) {
		arr_push(input_, 0);
		msg_log("Input", "[%u] %s\n", type, input_);
		arr_clear(input_);
	}
#endif
	cmd_update_cursor(tty);

	return i;
}

void
vte_action_dispatch(TTY *tty, StateCode state, ActionCode action, uchar c)
{
	if (!action) return;

	Parser *parser = &tty->parser;
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
		vte_write(tty, parser->ucs4 | (c & 0x7f));
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
		vte_write(tty, c);
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
		memset(parser->osc_offsets, 0, sizeof(parser->osc_offsets));
		parser->osc_index = 0;
		arr_clear(parser->data);
		break;
	case ActionOscPut:
		if (c == ';') {
			// NOTE(ben): I'm assuming OSC takes default parameters here...
			if (!arr_count(parser->data)) {
				arr_push(parser->data, '0');
			}
			// Semicolons could be part of an arg string so we always collect them, but
			// we don't record their offsets past the parameter limit.
			// The OSC parser handles excess parameters itself based on the leading opt.
			arr_push(parser->data, ';');
			if (parser->osc_index + 1 == LEN(parser->osc_offsets)) {
				dbgprint("warning: ignoring excess potential OSC parameter");
			} else {
				parser->osc_index++;
				parser->osc_offsets[parser->osc_index] = arr_count(parser->data);
			}
		} else {
			arr_push(parser->data, c);
		}
		break;
	case ActionOscEnd:
#define osc_params__ tty, (const char *)parser->data, parser->osc_offsets, parser->osc_index+1
		arr_push(parser->data, 0);
		vte_osc(osc_params__);
		break;
	case ActionCollect:
		if (parser->stash_index == LEN(parser->stash)) {
			dbgprint("warning: ignoring excess intermediate");
		} else {
			parser->stash[parser->stash_index++] = c;
		}
		break;
	case ActionParam:
		if (c == ';') {
			if (parser->csi_index + 1 == LEN(parser->csi_params)) {
				dbgprint("warning: ignoring excess parameter");
			} else {
				parser->csi_params[++parser->csi_index] = 0;
			}
		} else {
			ASSERT(INT_MAX / 10 > parser->csi_params[parser->csi_index]);
			parser->csi_params[parser->csi_index] *= 10;
			parser->csi_params[parser->csi_index] += (c - '0');
		}
		break;
	case ActionClear:
		memset(parser->stash, 0, sizeof(parser->stash));
		memset(parser->csi_params, 0, sizeof(parser->csi_params));
		parser->stash_index = 0;
		parser->csi_index = 0;
		arr_clear(parser->data);
		break;
	case ActionEscDispatch:
#define c1_params__ tty
		switch (c) {
		case 'E': break; // NEL
		case 'H': break; // HTS
		case 'M': vte_c1_ri(c1_params__); break;
		case '\\': break; // ST
		case '6': break; // DECBI
		case '7': break; // DECSC
		case '8': break; // DECRC
		case '9': break; // DECFI
		}
		break;
	case ActionCsiDispatch: {
#define csi_params__ tty, parser->csi_params, parser->csi_index + 1
		ASSERT(parser->stash_index == 1 || (!parser->stash_index && !parser->stash[0]));
		switch (parser->stash[0]) {
		case 0:
			switch (c) {
			case '@': vte_csi_ich(csi_params__); break;
			case 'A': vte_csi_cuu(csi_params__); break;
			case 'B': vte_csi_cud(csi_params__); break;
			case 'C': vte_csi_cuf(csi_params__); break;
			case 'D': vte_csi_cub(csi_params__); break;
			case 'E': break; // CNL
			case 'F': break; // CPL
			case 'G': break; // CHA
			case 'H': vte_csi_cup(csi_params__); break;
			case 'I': vte_csi_cht(csi_params__); break;
			case 'J': vte_csi_ed(csi_params__); break;
			case 'K': vte_csi_el(csi_params__); break;
			case 'L': break; // IL
			case 'M': break; // DL
			case 'P': vte_csi_dch(csi_params__); break;
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
			case 'm': vte_csi_sgr(csi_params__); break;
			case 'r': break; // DECSTBM
			case 'c': break; // DA
			case 's': break; // SCOSC
			case 't': break; // XTERM_WM
			}
			break;
		case ' ':
			if (c == 'q') { vte_csi_decscusr(csi_params__); }
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
			case 'h': vte_csi_decset(csi_params__); break; // DECSET/DECTCEM
			case 'i': break; // DECMC
			case 'l': vte_csi_decrst(csi_params__); break; // DECRST/DECTCEM
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
vte_write(TTY *tty, uint32 ucs4)
{
	tty->parser.cell.ucs4 = ucs4;
	tty->parser.cell.width = 1;

	stream_write(tty, &tty->parser.cell);
}

void
vte_c1_ri(TTY *tty)
{
	FUNC_DEBUG(RI);
	cmd_move_cursor_y(tty, -1);
}

void
vte_csi_ich(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(ICH);
	cmd_insert_cells(tty, &g_celltempl, DEFAULT(argv[0], 1));
}

void
vte_csi_cuu(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(CUU);
	cmd_move_cursor_y(tty, -DEFAULT(argv[0], 1));
}

void
vte_csi_cud(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(CUD);
	cmd_move_cursor_y(tty, +DEFAULT(argv[0], 1));
}

void
vte_csi_cuf(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(CUF);
	cmd_move_cursor_x(tty, +DEFAULT(argv[0], 1));
}

void
vte_csi_cub(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(CUB);
	cmd_move_cursor_x(tty, -DEFAULT(argv[0], 1));
}

void
vte_csi_cup(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(CUP);
	cmd_set_cursor_x(tty, argv[1]);
	cmd_set_cursor_y(tty, argv[0]);
}

void
vte_csi_cht(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(CHT);

	Cell cell = tty->parser.cell;
	cell.ucs4 = '\t';
	cell.width = 1;

	for (int n = DEFAULT(argv[0], 1); n > 0; n--) {
		if (!stream_write(tty, &cell)) {
			break;
		}
	}
}

void
vte_csi_dch(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(DCH);
	int delta = DEFAULT(argv[0], 1);
	cmd_shift_cells(tty, tty->pos.x + delta, tty->pos.y, -delta);
}

void
vte_csi_ed(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(ED);

	switch (argv[0]) {
	case 0:
		cmd_clear_rows(tty, tty->pos.y + 1, tty->rows);
		cmd_set_cells(tty, &(Cell){ 0 }, tty->pos.x, tty->pos.y, tty->cols);
		break;
	case 1:
		cmd_clear_rows(tty, tty->top, tty->pos.y - tty->top);
		cmd_set_cells(tty, &g_celltempl, 0, tty->pos.y, tty->pos.x);
		break;
	case 2:
		cmd_clear_rows(tty, tty->top, tty->rows);
		cmd_set_cursor_y(tty, 0);
		break;
	}
}

void
vte_csi_el(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(EL);

	switch (argv[0]) {
	case 0:
		cmd_set_cells(tty, &(Cell){ 0 }, tty->pos.x, tty->pos.y, tty->cols - tty->pos.x);
		break;
	case 1:
		cmd_set_cells(tty, &g_celltempl, 0, tty->pos.y, tty->pos.x);
		break;
	case 2:
		cmd_set_cells(tty, &(Cell){ 0 }, 0, tty->pos.y, tty->cols);
		cmd_set_cursor_x(tty, 0);
		break;
	}
}

void
vte_csi_sgr(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(SGR);

	Cell *cell = &tty->parser.cell;
	int i = 0;

	do {
		const int start = i;

		switch (argv[i]) {
		case 0:
			cell->attr &= ~ATTR_MASK;
			cell->color.bg = termcolor(ColorTagNone, 0);
			cell->color.fg = termcolor(ColorTagNone, 1);
			break;

		case 1:  cell->attr |=  ATTR_BOLD;      break;
		case 4:  cell->attr |=  ATTR_UNDERLINE; break;
		case 5:  cell->attr |=  ATTR_BLINK;     break;
		case 7:  cell->attr |=  ATTR_INVERT;    break;
		case 8:  cell->attr |=  ATTR_INVISIBLE; break;
		case 22: cell->attr &= ~ATTR_BOLD;      break;
		case 24: cell->attr &= ~ATTR_UNDERLINE; break;
		case 25: cell->attr &= ~ATTR_BLINK;     break;
		case 27: cell->attr &= ~ATTR_INVERT;    break;
		case 28: cell->attr &= ~ATTR_INVISIBLE; break;

		case 30: case 31:
		case 32: case 33:
		case 34: case 35:
		case 36: case 37:
			cell->color.fg = termcolor(ColorTag256, argv[i] - 30, 0);
			break;
		case 39: cell->color.fg = termcolor(ColorTagNone, 1); break;

		case 40: case 41:
		case 42: case 43:
		case 44: case 45:
		case 46: case 47:
			cell->color.bg = termcolor(ColorTag256, argv[i] - 40, 0);
			break;
		case 49: cell->color.fg = termcolor(ColorTagNone, 0); break;

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
					cell->color.bg = termcolor(ColorTag256, argv[i] & 0xff);
				} else if (argv[start] == 38) {
					cell->color.fg = termcolor(ColorTag256, argv[i] & 0xff);
				}
			} else if (i - start == 4) {
				if (argv[start] == 48) {
					cell->color.bg = termcolor(
						ColorTagRGB,
						argv[i-2] & 0xff,
						argv[i-1] & 0xff,
						argv[i-0] & 0xff
					);
				} else if (argv[start] == 38) {
					cell->color.fg = termcolor(
						ColorTagRGB,
						argv[i-2] & 0xff,
						argv[i-1] & 0xff,
						argv[i-0] & 0xff
					);
				}
			} else {
				// TODO(ben): confirm whether errors reset the defaults
				dbgprint("skiping invalid CSI:SGR sequence");
				cell->attr &= ~ATTR_MASK;
				cell->color.bg = termcolor(ColorTagNone, 0);
				cell->color.fg = termcolor(ColorTagNone, 1);
				return;
			}
			break;

		case 90: case 91:
		case 92: case 93:
		case 94: case 95:
		case 96: case 97:
			cell->color.fg = termcolor(ColorTag256, argv[i] - 90 + 8);
			break;

		case 100: case 101:
		case 102: case 103:
		case 104: case 105:
		case 106: case 107:
			cell->color.bg = termcolor(ColorTag256, argv[i] - 100 + 8);
			break;
		}
	} while (++i < argc);
}

static void
vte__csi_decprv(TTY *tty, int mode, bool enable)
{
	switch (mode) {
	case 25: tty->cursor.hide = !enable; break; // DECTCEM
	}
}

// NOTE(ben): Wrapper
void
vte_csi_decset(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(DECSET);
	vte__csi_decprv(tty, argv[0], true);
}

// NOTE(ben): Wrapper
void
vte_csi_decrst(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(DECRST);
	vte__csi_decprv(tty, argv[0], false);
}

void
vte_csi_decscusr(TTY *tty, const int *argv, int argc)
{
	FUNC_DEBUG(DECSCUSR);
	if (argv[0] <= 7) tty->cursor.style = argv[0];
}

void
vte_osc(TTY *tty, const char *mem, const int *argv, int argc)
{
	FUNC_DEBUG(OSC);

	if (argc < 2 || !*mem) return;

	const char *str = NULL;

	int64 opcode = strtol(mem + argv[0], (char **)&str, 10);

	if (!str || *str != ';' || !*(++str)) {
		return;
	}

	// TODO(ben): Implement the actual OSC handlers
#define dbgmsg__(msg_) dbgprintf("OSC(%s): \"%s\"\n", msg_, str)
	switch (opcode) {
	case 0:  dbgmsg__("Icon/Title"); break;
	case 1:  dbgmsg__("Icon");       break;
	case 2:  dbgmsg__("Title");      break;
	case 3:  dbgmsg__("XProp");      break;
	case 4:  dbgmsg__("Color");      break;
	default: dbgmsg__("???");        break;
	}
#undef dbgmsg__
}

TTY *
tty_create(struct TTYConfig config)
{
	TTY *tty = xmalloc(1, sizeof(*tty));

	if (!tty_init(tty, config)) {
		FREE(tty);
	}

	return tty;
}

void
vte_print_debug(const TTY *tty, FuncID id)
{
	struct FuncEntry ent = func_entries[id];
	char buf[64] = { 0 };
	const char *str = ent.codestring;

	const int *argv = tty->parser.csi_params;
	const int argc = tty->parser.csi_index + 1;
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
				post = (const char *)tty->parser.data;
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

bool
tty_init(TTY *tty, struct TTYConfig config)
{
	if (!tty) return false;

	memclear(tty, 1, sizeof(*tty));

	stream_init(tty, config.cols, config.rows, config.histsize);

	tty->tablen = config.tablen;

	for (int i = 0; ++i < tty->cols; ) {
		tty->tabstops[i] |= (i % tty->tablen == 0) ? 1 : 0;
	}

	arr_reserve(tty->parser.data, 4);
	/* tty->parser.cell = CELLDFL(' '); */
	tty->parser.cell = (Cell){
		.ucs4 = ' ',
		.type = CellTypeNormal,
		.width = 1,
		.color = {
			.bg = termcolor(ColorTagNone, 0),
			.fg = termcolor(ColorTagNone, 1)
		}
	};

	tty->ref = config.ref;
	tty->colpx = config.colpx;
	tty->rowpx = config.rowpx;

	return true;
}

int
tty_exec(TTY *tty, const char *shell)
{
	if (!tty->pty.mfd && pty_init(tty, shell) > 0) {
		pty_resize(tty, tty->cols, tty->rows);
	}

	return tty->pty.mfd;
}

size_t
tty_read(TTY *tty)
{
	return pty_read(tty, 0);
}

size_t
tty_write(TTY *tty, const char *str, size_t len, uint type)
{
	ASSERT(type == INPUT_CHAR || type == INPUT_KEY);
	return pty_write(tty, str, len, type);
}

void
tty_scroll(TTY *tty, int dy)
{
	tty->scroll = CLAMP(tty->scroll + dy, -tty->top, 0);
}

void
tty_resize(TTY *tty, int cols, int rows)
{
	cols = MAX(cols, 1);
	rows = MAX(rows, 1);

	if (cols != tty->cols || rows != tty->rows) {
		pty_resize(tty, cols, rows);
		stream_resize(tty, cols, rows);
	}
}

