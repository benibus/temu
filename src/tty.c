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

typedef struct VTE_ VTE;

struct VTE_ {
	Cell templ;
	uchar *buf;
	uchar *args;
	uint32 *opts;
	char tokens[8];
	uint8 depth;
	uint8 state;
};

typedef struct VTFunc_ {
	const char *name;
	void (*handler)(TTY *, VTE *, uint);
} VTFunc;

typedef struct VTRes_ {
	uint8 state;
	uint8 func;
} VTRes;

#define RES(state_,func_) ((VTRes){ .state = (state_), .func = (func_) })

#define VT_FUNC_TABLE \
    X_(NONE)    \
    X_(NEL)     \
    X_(HTS)     \
    X_(RI)      \
    X_(S7CIT)   \
    X_(S8CIT)   \
    X_(ANSI)    \
    X_(DECDHL)  \
    X_(DECSWL)  \
    X_(DECDWL)  \
    X_(DECALN)  \
    X_(ISO)     \
    X_(DECSC)   \
    X_(DECRC)   \
    X_(DECPAM)  \
    X_(DECPNM)  \
    X_(RIS)     \
    X_(LS2)     \
    X_(LS3)     \
    X_(LS3R)    \
    X_(LS2R)    \
    X_(LS1R)    \
    X_(DECUDK)  \
    X_(DECRQSS) \
    X_(DECSCA)  \
    X_(DECSCL)  \
    X_(DECSTBM) \
    X_(SGR)     \
    X_(ICH)     \
    X_(CUU)     \
    X_(CUD)     \
    X_(CUF)     \
    X_(CUB)     \
    X_(CNL)     \
    X_(CPL)     \
    X_(CHA)     \
    X_(CUP)     \
    X_(CHT)     \
    X_(ED)      \
    X_(DECSED)  \
    X_(EL)      \
    X_(DECSEL)  \
    X_(IL)      \
    X_(DL)      \
    X_(DCH)     \
    X_(SU)      \
    X_(SD)      \
    X_(ECH)     \
    X_(CBT)     \
    X_(HPA)     \
    X_(REP)     \
    X_(VPA)     \
    X_(HVP)     \
    X_(TBC)     \
    X_(SM)      \
    X_(AM)      \
    X_(IRM)     \
    X_(SRM)     \
    X_(LNM)     \
    X_(DECSET)  \
    X_(DECRST)  \
    X_(DECCKM)  \
    X_(DECANM)  \
    X_(DECCOLM) \
    X_(DECSCLM) \
    X_(DECSCNM) \
    X_(DECOM)   \
    X_(DECAWM)  \
    X_(DECARM)  \
    X_(DECPFF)  \
    X_(DECPEX)  \
    X_(DECTCEM) \
    X_(DECTEK)  \
    X_(DECNRCM) \
    X_(DECNKM)  \
    X_(DECBKM)  \
    X_(MC)      \
    X_(DECMC)   \
    X_(RM)      \
    X_(DSR)     \
    X_(DECDSR)  \
    X_(DECSTR)  \
    X_(DECCARA) \
    X_(DECSLPP) \
    X_(DECRARA) \
    X_(DECCRA)  \
    X_(DECEFR)  \
    X_(DECREQTPARM) \
    X_(DECSACE) \
    X_(DECFRA)  \
    X_(DECELR)  \
    X_(DECERA)  \
    X_(DECSLE)  \
    X_(DECSERA) \
    X_(DECRQLP) \
    X_(OSC)     \
    X_(OTHER)

enum {
#define X_(id) VT_##id,
	VT_FUNC_TABLE
#undef X_
	VT_COUNT
};

static VTFunc vtfuncs[VT_COUNT] = {
#define X_(id) [VT_##id] = { .name = #id },
	VT_FUNC_TABLE
#undef X_
};

#undef VT_FUNC_TABLE

enum {
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
	STATE_DEFAULT,
	STATE_ESC = (1 << 0),
	STATE_CSI = (1 << 1),
	STATE_DCS = (1 << 2),
	STATE_OSC = (1 << 3),
	STATE_MAX = (1 << 4)
};

static const char *statenames[STATE_MAX] = {
	[STATE_DEFAULT] = "---",
	[STATE_ESC] = "ESC",
	[STATE_CSI] = "CSI",
	[STATE_DCS] = "DCS",
	[STATE_DCS|STATE_ESC] = "DCS|ESC",
	[STATE_OSC] = "OSC",
	[STATE_OSC|STATE_ESC] = "OSC|ESC"
};

static bool parse_token(VTE *, uint32);
static VTRes vte_parse_esc(VTE *, uint32, uint32 *);
static VTRes vte_parse_csi(VTE *, uint32);
static VTRes vte_parse_osc(VTE *, uint32);
static VTRes vte_parse_dcs(VTE *, uint32);
static bool vte_dispatch(TTY *, VTE *, uint);
static void vte_print_func(VTE *, uint);
static void vte_reset_buffers(VTE *);
static void vte_reset_template(VTE *);
static bool vte_get_opt(VTE *, uint32);
static void func_ri(TTY *, VTE *, uint);
static void func_ich(TTY *, VTE *, uint);
static void func_cu(TTY *, VTE *, uint);
static void func_cht(TTY *, VTE *, uint);
static void func_dch(TTY *, VTE *, uint);
static void func_ed(TTY *, VTE *, uint);
static void func_el(TTY *, VTE *, uint);
static void func_sgr(TTY *, VTE *, uint);
static void func_decset(TTY *, VTE *, uint);
static void func_osc(TTY *, VTE *, uint);

static char mbuf[BUFSIZ];
static size_t msize = 0;
static VTE vte_ = { 0 };

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

	vte_.buf  = NULL;
	vte_.opts = NULL;
	vte_.args = NULL;
	vte_.state = 0;
	vte_reset_buffers(&vte_);
	vte_reset_template(&vte_);

#define SET_FUNC(id,fn) vtfuncs[(id)].handler = (fn)
	SET_FUNC(VT_RI,  func_ri);
	SET_FUNC(VT_SGR, func_sgr);
	SET_FUNC(VT_ICH, func_ich);
	SET_FUNC(VT_CUU, func_cu);
	SET_FUNC(VT_CUD, func_cu);
	SET_FUNC(VT_CUF, func_cu);
	SET_FUNC(VT_CUB, func_cu);
	SET_FUNC(VT_CUP, func_cu);
	SET_FUNC(VT_ED,  func_ed);
	SET_FUNC(VT_EL,  func_el);
	SET_FUNC(VT_DCH, func_dch);
	SET_FUNC(VT_OSC, func_osc);
	SET_FUNC(VT_DECSET, func_decset);
	SET_FUNC(VT_DECRST, func_decset);
#undef SET_FUNC

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

		if (!parse_token(&vte_, ucs4)) {
			stream_write(ucs4, vte_.templ.color, vte_.templ.attr);
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
vte_reset_buffers(VTE *vte)
{
	ASSERT(vte);

	vte->tokens[0] = 0;
	vte->depth = 0;
	arr_set(vte->buf,  0, 0), arr_clear(vte->buf);
	arr_set(vte->opts, 0, 0), arr_clear(vte->opts);
	arr_set(vte->args, 0, 0), arr_clear(vte->args);
}

void
vte_reset_template(VTE *vte)
{
	ASSERT(vte);

	memclear(&vte->templ, 1, sizeof(vte->templ));
	vte->templ.color.fg = COLOR_FG;
	vte->templ.color.bg = COLOR_BG;
	vte->templ.width = 1;
}

bool
vte_dispatch(TTY *this, VTE *vte, uint id)
{
	ASSERT(id < VT_COUNT);
	vte_print_func(vte, id);

	if (vtfuncs[id].handler) {
		vtfuncs[id].handler(this, vte, id);
		return true;
	}

	return false;
}

bool
parse_token(VTE *vte, uint32 ucs4)
{
	ASSERT(vte);
	ASSERT(vte->depth + 2u < LEN(vte->tokens));

	VTRes res = { 0 };
	vte->templ.ucs4  = 0;
	vte->templ.width = 0;

	if (ucs4 == CTRL_ESC) {
		vte->state |= STATE_ESC;
		return true;
	}

	if (vte->state & STATE_ESC) {
		uint32 ucs4_ = 0;
		res = vte_parse_esc(vte, ucs4, &ucs4_);
		if (ucs4_) {
			vte->state = res.state;
			ucs4 = ucs4_;
		} else {
			goto dispatch;
		}
	}
	if (vte->state & STATE_OSC) {
		res = vte_parse_osc(vte, ucs4);
	} else if (vte->state & STATE_DCS) {
		res = vte_parse_dcs(vte, ucs4);
	} else if (vte->state & STATE_CSI) {
		res = vte_parse_csi(vte, ucs4);
	}

dispatch:
	arr_push(vte->buf, ucs4);

	if (res.func) {
		vte_dispatch(&tty, vte, res.func);
	}
	if (!res.state) {
		vte_reset_buffers(vte);
		vte->state = 0;
		vte->templ.ucs4  = ucs4;
		vte->templ.width = 1;
	} else if (res.state != vte->state) {
		vte->state = res.state;
		vte->tokens[0] = 0;
		vte->depth = 0;
	}

	return (res.state || res.func);
}

bool
vte_get_opt(VTE *vte, uint32 ucs4)
{
	ASSERT(vte);

	uint32 *opt = NULL;

	switch (ucs4) {
		case ';': {
			if (!arr_count(vte->opts)) {
				arr_push(vte->opts, 0);
			}
			arr_push(vte->opts, 0);
			break;
		}
		case '0': case '1':
		case '2': case '3':
		case '4': case '5':
		case '6': case '7':
		case '8': case '9': {
			if (!arr_count(vte->opts)) {
				arr_push(vte->opts, 0);
			}
			opt = arr_tail(vte->opts) - 1;

			size_t tmp = *opt;
			ASSERT(tmp + 9 < UINT32_MAX / 10);
			tmp = (tmp * 10) + (ucs4 - '0');
			*opt = tmp;
			break;
		}
		default: {
			return false;
		}
	}

	return true;
}

VTRes
vte_parse_dcs(VTE *vte, uint32 ucs4)
{
	char last = (vte->depth) ? vte->tokens[vte->depth-1] : 0;

	if (!last && arr_count(vte->opts) < 3) {
		if (vte_get_opt(vte, ucs4)) {
			return RES(vte->state, 0);
		}
	}

	switch (last) {
	case 0:
		switch (ucs4) {
		case '|':
		case '$':
		case '+':
			break;
		default:
			return RES(0, 0);
		}
		break;
	case '$':
	case '+':
		if (ucs4 != 'q') {
			return RES(0, 0);
		}
		break;
	}

	if (ucs4 == CTRL_ST || ucs4 == CTRL_BEL) {
		switch (vte->tokens[0]) {
		case '|': return RES(0, VT_DECUDK);
		case '$': return RES(0, VT_DECRQSS);
		case '+': return RES(0, VT_OTHER);
		}
		return RES(0, 0);
	}

	if (last != '|' && last != 'q') {
		vte->tokens[vte->depth++] = ucs4;
		vte->tokens[vte->depth] = 0;
	}
	arr_push(vte->args, ucs4);

	return RES(vte->state, 0);
}

VTRes
vte_parse_osc(VTE *vte, uint32 ucs4)
{
	if (arr_count(vte->opts) < 2) {
		if (vte_get_opt(vte, ucs4)) {
			return RES(vte->state, 0);
		}
		if (!arr_count(vte->opts)) {
			return RES(0, 0);
		}
	}

	if (ucs4 == CTRL_ST || ucs4 == CTRL_BEL) {
		return RES(0, VT_OSC);
	}
	arr_push(vte->args, ucs4);

	return RES(vte->state, 0);
}

VTRes
vte_parse_esc(VTE *vte, uint32 ucs4, uint32 *ret)
{
	uint32 conv = 0;

	switch (ucs4) {
		case CTRL_CAN:
		case CTRL_SUB:
			break;
		case '[': return RES(STATE_CSI, 0);
		case ']': return RES(STATE_OSC, 0);
		case 'E': return RES(0, VT_NEL);
		case 'H': return RES(0, VT_HTS);
		case 'M': return RES(0, VT_RI);
		case 'P': return RES(STATE_DCS, 0);

		case 'X':  conv = CTRL_SOS; break;
		case '\\': conv = CTRL_ST;  break;
	}

	if (conv) {
		if (ret) *ret = conv;
		return RES(vte->state & ~STATE_ESC, 0);
	}
	return RES(0, 0);
}

VTRes
vte_parse_csi(VTE *vte, uint32 ucs4)
{
	if (vte_get_opt(vte, ucs4)) {
		return RES(vte->state, 0);
	}

	char last = (vte->depth) ? vte->tokens[vte->depth-1] : 0;
	vte->tokens[vte->depth++] = ucs4;
	vte->tokens[vte->depth] = 0;

	switch (last) {
	case 0:
		switch (ucs4) {
		case '?':
		case '!':
		case '"':
		case '\'':
		case '$':
		case '>': return RES(vte->state, 0);

		case '@': return RES(0, VT_ICH);
		case 'A': return RES(0, VT_CUU);
		case 'B': return RES(0, VT_CUD);
		case 'C': return RES(0, VT_CUF);
		case 'D': return RES(0, VT_CUB);
		case 'E': return RES(0, VT_CNL);
		case 'F': return RES(0, VT_CPL);
		case 'G': return RES(0, VT_CHA);
		case 'H': return RES(0, VT_CUP);
		case 'I': return RES(0, VT_CHT);
		case 'J': return RES(0, VT_ED);
		case 'K': return RES(0, VT_EL);
		case 'L': return RES(0, VT_IL);
		case 'M': return RES(0, VT_DL);
		case 'P': return RES(0, VT_DCH);
		case 'S': return RES(0, VT_SU);
		case 'T': return RES(0, VT_SD);
		case 'X': return RES(0, VT_ECH);
		case 'Z': return RES(0, VT_CBT);
		case '`': return RES(0, VT_HPA);
		case 'b': return RES(0, VT_REP);
		case 'd': return RES(0, VT_VPA);
		case 'f': return RES(0, VT_HVP);
		case 'g': return RES(0, VT_TBC);
		case 'h': return RES(0, VT_SM);
		case 'i': return RES(0, VT_MC);
		case 'l': return RES(0, VT_RM);
		case 'm': return RES(0, VT_SGR);
		case 'r': return RES(0, VT_DECSTBM);
		case 'c':
		case 's':
		case 't':
		case 'x': return RES(0, VT_OTHER);
		}
		break;
	case '?':
		switch (ucs4) {
		case 'J': return RES(0, VT_DECSED);
		case 'K': return RES(0, VT_DECSEL);
		case 'h': return RES(0, VT_DECSET);
		case 'i': return RES(0, VT_DECMC);
		case 'l': return RES(0, VT_DECRST);
		case 'n': return RES(0, VT_DECDSR);
		case 'q':
		case 'r':
		case 's': return RES(0, VT_OTHER);
		}
		break;
	case '!':
		switch (ucs4) {
		case 'p': return RES(0, VT_DECSTR);
		}
		break;
	case '"':
		switch (ucs4) {
		case 'p': return RES(0, VT_DECSCL);
		case 'q': return RES(0, VT_OTHER);
		}
		break;
	case '\'':
		switch (ucs4) {
		case 'w': return RES(0, VT_DECEFR);
		case 'z': return RES(0, VT_DECELR);
		case '{': return RES(0, VT_DECSLE);
		case '|': return RES(0, VT_DECRQLP);
		}
		break;
	case '$':
		switch (ucs4) {
		case 't': return RES(0, VT_DECCARA);
		case 'v': return RES(0, VT_DECCRA);
		case 'x': return RES(0, VT_DECFRA);
		case 'z': return RES(0, VT_DECERA);
		}
		break;
	case '>':
		switch (ucs4) {
		case 'c': return RES(0, VT_OTHER);
		}
		break;
	}

	return RES(0, 0);
}

#define VTOPT(s,n) (((n) < arr_count((s)->opts)) ? (s)->opts[(n)] : 0)

void
func_osc(TTY *this, VTE *vte, uint id)
{
	uint32 o = VTOPT(vte, 0);
	char *msg = NULL;

	switch (o) {
		case 0:  { msg = "icon name and window title"; break; }
		case 1:  { msg = "icon name";    break; }
		case 2:  { msg = "window title"; break; }
		case 3:  { msg = "X11 property"; break; }
		case 4:  { msg = "color value";  break; }
		default: { msg = "..."; break; }
	}

	fprintf(stderr, "\tOSC >>> Changing %s to: \"%.*s\"\n",
	                msg, (int)arr_count(vte->args), vte->args);
}

void
func_decset(TTY *this, VTE *vte, uint id)
{
	uint32 o = VTOPT(vte, 0);

	switch (o) {
		case 25: {
			this->cursor.hide = (id == VT_DECRST);
			break;
		}
	}
}

void
func_el(TTY *this, VTE *vte, uint id)
{
	uint32 o = VTOPT(vte, 0);
	ASSERT(!vte->templ.ucs4 && !vte->templ.width);

	switch (o) {
		case 0: {
			cmd_set_cells(this, &vte->templ,
			              this->cursor.x, this->cursor.y,
			              this->cols - this->cursor.x);
			break;
		}
		case 1: {
			vte->templ.ucs4 = ' ';
			vte->templ.width = 1;

			cmd_set_cells(&tty, &vte->templ,
			              0, this->cursor.y, this->cursor.x);
			break;
		}
		case 2: {
			cmd_set_cells(this, &vte->templ,
				      0, this->cursor.y, this->cols);
			cmd_set_cursor_x(this, 0);
			break;
		}
	}
}

void
func_ed(TTY *this, VTE *vte, uint id)
{
	uint32 o = VTOPT(vte, 0);
	ASSERT(!vte->templ.ucs4 && !vte->templ.width);

	switch (o) {
		case 0: {
			cmd_clear_rows(this, this->cursor.y + 1, this->rows);
			cmd_set_cells(this, &vte->templ,
				      this->cursor.x, this->cursor.y,
				      this->cols);
			break;
		}
		case 1: {
			vte->templ.ucs4  = ' ';
			vte->templ.width = 1;

			cmd_clear_rows(this, this->top, this->cursor.y - this->top);
			cmd_set_cells(this, &vte->templ,
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
func_sgr(TTY *this, VTE *vte, uint id)
{
	Cell *cell = &vte->templ;
	uint i = 0;
	uint count = arr_count(vte->opts);
	uint32 o[3] = { 0 };

	do {
		o[0] = VTOPT(vte, i);

		switch (o[0]) {
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
				if (o[0] < 40) {
					cell->color.fg = o[0] - 30;
				} else {
					cell->color.bg = o[0] - 40;
				}
				break;
			}

			case 38:
			case 48: {
				o[1] = VTOPT(vte, i + 1);
				if (o[1] == 5) {
					if (!(o[2] = VTOPT(vte, i + 2))) {
						break;
					} else if (o[0] < 40) {
						cell->color.fg = o[2];
					} else {
						cell->color.bg = o[2];
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
				if (o[0] < 100) {
					cell->color.fg = o[0] + 8 - 90;
				} else {
					cell->color.bg = o[0] + 8 - 100;
				}
				break;
			}
		}
	} while (++i < count);
}

void
func_dch(TTY *this, VTE *vte, uint id)
{
	uint32 o = VTOPT(vte, 0);
	int dx = DEFAULT(o, 1);
	ASSERT(dx >= 0);

	cmd_shift_cells(this, this->cursor.x + dx, this->cursor.y, -dx);
}

void
func_cht(TTY *this, VTE *vte, uint id)
{
	uint32 o = VTOPT(vte, 0);

	for (uint i = 0; i < DEFAULT(o, 1); i++) {
		if (!stream_write('\t', vte->templ.color, vte->templ.attr)) {
			break;
		}
	}
}

void
func_cu(TTY *this, VTE *vte, uint id)
{
	uint32 o[2] = { VTOPT(vte, 0), VTOPT(vte, 1) };

	switch (id) {
	case VT_CUU:
		cmd_move_cursor_y(this, -DEFAULT(o[0], 1));
		break;
	case VT_CUD:
		cmd_move_cursor_y(this, +DEFAULT(o[0], 1));
		break;
	case VT_CUF:
		cmd_move_cursor_x(this, +DEFAULT(o[0], 1));
		break;
	case VT_CUB:
		cmd_move_cursor_x(this, -DEFAULT(o[0], 1));
		break;
	case VT_CUP:
		cmd_set_cursor_x(this, o[1]);
		cmd_set_cursor_y(this, o[0]);
		break;
	}
}

void
func_ich(TTY *this, VTE *vte, uint id)
{
	uint32 o = VTOPT(vte, 0);

	vte->templ.ucs4 = ' ';
	vte->templ.width = 1;

	cmd_insert_cells(this, &vte->templ, DEFAULT(o, 1));
}

void
func_ri(TTY *this, VTE *vte, uint id)
{
	cmd_move_cursor_y(this, -1);
}

void
vte_print_func(VTE *vte, uint id)
{
	fprintf(stderr, "\033[01;33m"
	                "VtCode("
	                "\033[0m%02u"
	                "\033[01;33m)\033[0m "
	                "%-8s %-8s ",
	                id, statenames[vte->state], vtfuncs[id].name);
	fprintf(stderr, "%#2.2x (%02lu) [%02u/%02lu] ",
	                vte->state,
	                arr_count(vte->opts),
	                vte->depth,
	                arr_count(vte->buf));

	for (uint i = 0; i < arr_count(vte->buf); i++) {
		fprintf(stderr, "%s%s", asciistr(vte->buf[i]), " ");
	}
	fputc('\n', stderr);
}

