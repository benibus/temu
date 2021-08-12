#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <unistd.h>

#include "utils.h"
#include "term.h"
#include "vte.h"
#include "utf8.h"

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
	STATE_DCS = (1 << 3),
	STATE_OSC = (1 << 4),
	STATE_MAX = (1 << 5)
};

// temporary
#define STATE_STR STATE_DCS

#define STATE_MASK  (STATE_MAX - 1)

static VTState state_ = { 0 };

static char mbuf[BUFSIZ];
static size_t msize = 0;

static void vte_print_func(VTState *, uint);
static void vte_set_state(VTState *, uint);
static void vte_set_state_mask(VTState *, uint, int);
static void vte_reset_buffers(VTState *);
static void vte_reset_template(VTState *);
static uint vte_parse_token(VTState *, uint32);
static uint vte_parse_mode_str(VTState *, uint32);
static uint vte_parse_mode_esc(VTState *, uint32);
static uint vte_parse_mode_csi(VTState *, uint32);
static bool vte_dispatch(TTY *, VTState *, uint);
static VTArgDesc *vte_push_arg_numeric(VTArgs *, uint32);
static bool vte_get_arg_numeric(VTArgs *, uint32);
static void vte_add_char(VTArgs *, uint32);

void
vte_print_func(VTState *state, uint id)
{
	VTFunc fn = vtfuncs[id];
	fprintf(stderr, "\033[01;33m"
	                "VtCode("
	                "\033[0m%02u"
	                "\033[01;33m)\033[0m "
	                "%s: ",
	                fn.id, fn.name);
	fprintf(stderr, "%#.2x (%u|%u) [%u/%lu] ",
	                state->flags,
	                state->args.count,
	                state->args.index,
	                state->depth,
	                arr_count(state->chars));
	for (uint i = 0; i < arr_count(state->chars); i++) {
		fprintf(stderr, "%s%s", asciistr(state->chars[i]), " ");
	}
	fputc('\n', stderr);
}

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

	vte_set_state(&state_, STATE_DEFAULT);
	vte_reset_buffers(&state_);
	vte_reset_template(&state_);
	state_.chars = NULL;

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

		if (!vte_parse_token(&state_, ucs4)) {
			stream_write(ucs4, state_.spec.color, state_.spec.attr);
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
vte_set_state(VTState *state, uint flags)
{
	ASSERT(state);

	state->flags = flags;
}

void
vte_set_state_mask(VTState *state, uint mask, int enable)
{
	ASSERT(state);

	state->flags = (!!enable)
	             ? state->flags | mask
	             : state->flags & ~mask;
}

void
vte_reset_buffers(VTState *state)
{
	ASSERT(state);

	memclear(state->args.buf, 1, sizeof(*state->args.buf));
	state->args.count = 0;
	state->args.index = 0;
	state->args.data[0] = 0;
	state->tokens[0] = 0;
	state->depth = 0;
	arr_clear(state->chars);
}

void
vte_reset_template(VTState *state)
{
	ASSERT(state);

	memclear(&state->spec, 1, sizeof(state->spec));
	state->spec.color.fg = COLOR_FG;
	state->spec.color.bg = COLOR_BG;
	state->spec.width = 1;
}

bool
vte_dispatch(TTY *this, VTState *state, uint id)
{
	ASSERT(id < VT_COUNT);
	bool res = false;

	if (vtfuncs[id].handler) {
		vte_print_func(state, id);
		vtfuncs[id].handler(this, state, id);
		res = true;
	}

	if (id != VT_CONTINUE) {
		vte_set_state(state, STATE_DEFAULT);
		vte_reset_buffers(state);
	}

	return res;
}

VTArgDesc *
vte_push_arg_numeric(VTArgs *args, uint32 ucs4)
{
	VTArgDesc *arg = args->buf + args->count;

	args->data[args->index] = ucs4;
	{
		arg->index = args->index;
		arg->len   = 1;
		arg->type  = ARG_DEFAULT;
	}
	args->count++;
	args->index++;
	memclear(args->buf + args->count, 1, sizeof(*args->buf));

	return arg;
}

bool
vte_get_arg_numeric(VTArgs *args, uint32 ucs4)
{
	ASSERT(args);
	VTArgDesc *arg = NULL;

	switch (ucs4) {
		case ';': {
			if (!args->count) {
				vte_push_arg_numeric(args, 0);
			}
			vte_push_arg_numeric(args, 0);
			break;
		}
		case '0': case '1':
		case '2': case '3':
		case '4': case '5':
		case '6': case '7':
		case '8': case '9': {
			if (!args->count) {
				vte_push_arg_numeric(args, 0);
			}
			ASSERT(args->count);
			arg = args->buf + args->count - 1;

			size_t tmp = args->data[arg->index];
			tmp = (tmp * 10) + (ucs4 - '0');
			ASSERT(tmp < UINT32_MAX);
			args->data[arg->index] = tmp;
			break;
		}
		default: {
			return false;
		}
	}

	return true;
}

void
vte_add_char(VTArgs *args, uint32 ucs4)
{
#if 1
	return;
#else
	ASSERT(args->index < LEN(args->data));
	VTArgDesc *arg = args->buf + MAX(0, (int)args->count - 1);

	ASSERT(arg->type == ARG_STRING);
	arg->len++;
	args->data[args->index] = ucs4;
	args->index++;
#endif
}

uint
vte_parse_mode_str(VTState *state, uint32 ucs4)
{
	// Will get replaced by separate DCS and OSC parsers.
	// This just consumes the strings for the time being;
	if (state->flags & STATE_ESC) {
		if (ucs4 != '\\') {
			vte_add_char(&state->args, CTRL_ESC);
			vte_add_char(&state->args, ucs4);
		}
	} else if (ucs4 != CTRL_ST && ucs4 != CTRL_BEL) {
		vte_add_char(&state->args, ucs4);
		return VT_CONTINUE;
	}

	return VT_TERMINATE;
}

uint
vte_parse_mode_esc(VTState *state, uint32 ucs4)
{
	switch (ucs4) {
		case CTRL_CAN:
		case CTRL_SUB: {
			return VT_TERMINATE;
		}
		case '[': {
			vte_set_state(state, STATE_CSI);
			state->tokens[state->depth++] = ucs4;
			return VT_CONTINUE;
		}
		case ']': {
			vte_set_state(state, STATE_OSC);
			state->tokens[state->depth++] = ucs4;
			return VT_CONTINUE;
		}
		case 'E': { return VT_NEL; }
		case 'H': { return VT_HTS; }
		case 'M': { return VT_RI;  }
	}

	return 0;
}

uint
vte_parse_mode_csi(VTState *state, uint32 ucs4)
{
	if (vte_get_arg_numeric(&state->args, ucs4)) {
		return VT_CONTINUE;
	}

	ASSERT(state->depth);

	switch (state->tokens[state->depth-1]) {
	case '[':
		switch (ucs4) {
		case '@': return VT_ICH;
		case 'A': return VT_CUU;
		case 'B': return VT_CUD;
		case 'C': return VT_CUF;
		case 'D': return VT_CUB;
		case 'E': return VT_CNL;
		case 'F': return VT_CPL;
		case 'G': return VT_CHA;
		case 'H': return VT_CUP;
		case 'I': return VT_CHT;
		case 'J': return VT_ED;
		case 'K': return VT_EL;
		case 'L': return VT_IL;
		case 'M': return VT_DL;
		case 'P': return VT_DCH;
		case 'S': return VT_SU;
		case 'T': return VT_SD;
		case 'X': return VT_ECH;
		case 'Z': return VT_CBT;
		case '`': return VT_HPA;
		case 'b': return VT_REP;
		case 'd': return VT_VPA;
		case 'f': return VT_HVP;
		case 'g': return VT_TBC;
		case 'h': return VT_SM;
		case 'i': return VT_MC;
		case 'l': return VT_RM;
		case 'm': return VT_SGR;
		case 'r': return VT_DECSTBM;
		case 'c':
		case 's':
		case 't':
		case 'x': return VT_UNKNOWN;

		case '?' :
		case '!' :
		case '"' :
		case '\'':
		case '$' :
		case '>' :
			vte_set_state_mask(state, STATE_DEC, 1);
			state->tokens[state->depth++] = ucs4;
			return VT_CONTINUE;
		}
		break;
	case '?':
		switch (ucs4) {
		case 'J': return VT_DECSED;
		case 'K': return VT_DECSEL;
		case 'h': return VT_DECSET;
		case 'i': return VT_DECMC;
		case 'l': return VT_DECRST;
		case 'n': return VT_DECDSR;
		case 'q':
		case 'r':
		case 's': return VT_UNKNOWN;
		}
		break;
	case '!':
		switch (ucs4) {
		case 'p': return VT_DECSTR;
		}
		break;
	case '"':
		switch (ucs4) {
		case 'p': return VT_DECSCL;
		case 'q': return VT_UNKNOWN;
		}
		break;
	case '\'':
		switch (ucs4) {
		case 'w': return VT_DECEFR;
		case 'z': return VT_DECELR;
		case '{': return VT_DECSLE;
		case '|': return VT_DECRQLP;
		}
		break;
	case '$':
		switch (ucs4) {
		case 't': return VT_DECCARA;
		case 'v': return VT_DECCRA;
		case 'x': return VT_DECFRA;
		case 'z': return VT_DECERA;
		}
		break;
	case '>':
		switch (ucs4) {
		case 'c': return VT_UNKNOWN;
		}
		break;
	}

	return 0;
}

uint
vte_parse_token(VTState *state, uint32 ucs4)
{
	ASSERT(state);
	ASSERT(state->depth + 1u < LEN(state->tokens));

	uint depth = state->depth;
	uint res = 0;
	state->spec.ucs4  = 0;
	state->spec.width = 0;
	(void)depth;

	arr_push(state->chars, ucs4);

	if (ucs4 == CTRL_ESC) {
		if (!(state->flags & (STATE_DCS|STATE_OSC))) {
			vte_reset_buffers(state);
			state->tokens[state->depth++] = ucs4;
		}
		vte_set_state_mask(state, ~(STATE_DCS|STATE_OSC), 0);
		vte_set_state_mask(state,  STATE_ESC, 1);
		res = VT_CONTINUE;
	} else if (state->flags & (STATE_DCS|STATE_OSC)) {
		res = vte_parse_mode_str(state, ucs4);
	} else if (state->flags & STATE_ESC) {
		res = vte_parse_mode_esc(state, ucs4);
	} else if (state->flags & STATE_CSI) {
		res = vte_parse_mode_csi(state, ucs4);
	}

	if (res) {
		vte_dispatch(&tty, state, res);
	} else {
		vte_set_state(state, STATE_DEFAULT);
		vte_reset_buffers(state);
		state->spec.ucs4  = ucs4;
		state->spec.width = 1;
	}

	return res;
}

#define VTARG(s,n) (((n) < (s)->args.count) ? (s)->args.data[(s)->args.buf[(n)].index] : 0)

void
vte_op_decset(TTY *this, VTState *state, uint id)
{
	uint32 a = VTARG(state, 0);

	switch (a) {
		case 25: {
			this->cursor.hide = (id == VT_DECRST);
			break;
		}
	}
}

void
vte_op_el(TTY *this, VTState *state, uint id)
{
	uint32 a = VTARG(state, 0);
	ASSERT(!state->spec.ucs4 && !state->spec.width);

	switch (a) {
		case 0: {
			cmd_set_cells(this, &state->spec,
			              this->cursor.x, this->cursor.y,
			              this->cols - this->cursor.x);
			break;
		}
		case 1: {
			state->spec.ucs4 = ' ';
			state->spec.width = 1;

			cmd_set_cells(&tty, &state->spec,
			              0, this->cursor.y, this->cursor.x);
			break;
		}
		case 2: {
			cmd_set_cells(this, &state->spec,
				      0, this->cursor.y, this->cols);
			cmd_set_cursor_x(this, 0);
			break;
		}
	}
}

void
vte_op_ed(TTY *this, VTState *state, uint id)
{
	uint32 a = VTARG(state, 0);
	ASSERT(!state->spec.ucs4 && !state->spec.width);

	switch (a) {
		case 0: {
			cmd_clear_rows(this, this->cursor.y + 1, this->rows);
			cmd_set_cells(this, &state->spec,
				      this->cursor.x, this->cursor.y,
				      this->cols);
			break;
		}
		case 1: {
			state->spec.ucs4  = ' ';
			state->spec.width = 1;

			cmd_clear_rows(this, this->top, this->cursor.y - this->top);
			cmd_set_cells(this, &state->spec,
				      0, this->cursor.y,
				      this->cursor.x);
			break;
		}
		case 2: {
			cmd_clear_rows(this, this->top, this->rows);
			cmd_set_cursor_y(this, 0);
			break;
		}
	}
}

void
vte_op_sgr(TTY *this, VTState *state, uint id)
{
	Cell *cell = &state->spec;
	uint i = 0;

	do {
		uint32 a = VTARG(state, i);

		switch (a) {
			case 0: {
				cell->attr &= ~ATTR_MASK;
				cell->color.bg = 0;
				cell->color.fg = 7;
				break;
			}

			case 1:  cell->attr |= ATTR_BOLD;       break;
			case 4:  cell->attr |= ATTR_UNDERLINE;  break;
			case 5:  cell->attr |= ATTR_BLINK;      break;
			case 7:  cell->attr |= ATTR_INVERT;     break;
			case 8:  cell->attr |= ATTR_INVISIBLE;  break;
			case 22: cell->attr &= ~ATTR_BOLD;      break;
			case 24: cell->attr &= ~ATTR_UNDERLINE; break;
			case 25: cell->attr &= ~ATTR_BLINK;     break;
			case 27: cell->attr &= ~ATTR_INVERT;    break;
			case 28: cell->attr &= ~ATTR_INVISIBLE; break;

			case 30: case 40:
			case 31: case 41:
			case 32: case 42:
			case 33: case 43:
			case 34: case 44:
			case 35: case 45:
			case 36: case 46:
			case 37: case 47: {
				if (a < 40) {
					cell->color.fg = a - 30;
				} else {
					cell->color.bg = a - 40;
				}
				break;
			}

			case 38:
			case 48: {
				if (VTARG(state, i + 1) == 5) {
					uint32 a2;
					if (!(a2 = VTARG(state, i + 2))) {
						break;
					} else if (a < 40) {
						cell->color.fg = a2;
					} else {
						cell->color.bg = a2;
					}
					i += 2;
				}
				break;
			}

			case 39: cell->color.fg = 7; break;
			case 49: cell->color.fg = 0; break;

			case 90: case 100:
			case 91: case 101:
			case 92: case 102:
			case 93: case 103:
			case 94: case 104:
			case 95: case 105:
			case 96: case 106:
			case 97: case 107: {
				if (a < 100) {
					cell->color.fg = a + 8 - 90;
				} else {
					cell->color.bg = a + 8 - 100;
				}
				break;
			}
		}
	} while (++i < state->args.count);
}

void
vte_op_dch(TTY *this, VTState *state, uint id)
{
	uint32 a = VTARG(state, 0);
	int dx = DEFAULT(a, 1);
	ASSERT(dx >= 0);

	cmd_shift_cells(this, this->cursor.x + dx, this->cursor.y, -dx);
}

void
vte_op_cht(TTY *this, VTState *state, uint id)
{
	uint32 a = VTARG(state, 0);

	for (uint i = 0; i < DEFAULT(a, 1); i++) {
		if (!stream_write('\t', state->spec.color, state->spec.attr)) {
			break;
		}
	}
}

void
vte_op_cu(TTY *this, VTState *state, uint id)
{
	uint32 a[2] = { VTARG(state, 0), VTARG(state, 1) };

	switch (id) {
	case VT_CUU:
		cmd_move_cursor_y(this, -DEFAULT(a[0], 1));
		break;
	case VT_CUD:
		cmd_move_cursor_y(this, +DEFAULT(a[0], 1));
		break;
	case VT_CUF:
		cmd_move_cursor_x(this, +DEFAULT(a[0], 1));
		break;
	case VT_CUB:
		cmd_move_cursor_x(this, -DEFAULT(a[0], 1));
		break;
	case VT_CUP:
		cmd_set_cursor_x(this, a[1]);
		cmd_set_cursor_y(this, a[0]);
		break;
	}
}

void
vte_op_ich(TTY *this, VTState *state, uint id)
{
	uint32 a = VTARG(state, 0);

	state->spec.ucs4 = ' ';
	state->spec.width = 1;

	cmd_insert_cells(this, &state->spec, DEFAULT(a, 1));
}

void
vte_op_ri(TTY *this, VTState *state, uint id)
{
	cmd_move_cursor_y(this, -1);
}

void
vte_op_continue(TTY *this, VTState *state, uint id)
{
	return;
}

void
vte_op_terminate(TTY *this, VTState *state, uint id)
{
	return;
}
