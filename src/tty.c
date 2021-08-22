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

#define VTE_FUNC_TABLE \
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
	#define X_(id) VTE_##id,
	VTE_FUNC_TABLE
	#undef X_
	VTE_COUNT
};

static struct {
	const char *name;
	void (*handler)(TTY *, Seq *, uint);
} vtfuncs[VTE_COUNT] = {
	#define X_(id) [VTE_##id] = { .name = #id },
	VTE_FUNC_TABLE
	#undef X_
};

#undef VTE_FUNC_TABLE

typedef struct SeqRes_ {
	uint8 state;
	uint8 func;
} SeqRes;

#define RES(state_,func_) ((SeqRes){ .state = (state_), .func = (func_) })

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

static bool parse_token(TTY *, Seq *, uint32);
static SeqRes seq_parse_esc(Seq *, uint32, uint32 *);
static SeqRes seq_parse_csi(Seq *, uint32);
static SeqRes seq_parse_osc(Seq *, uint32);
static SeqRes seq_parse_dcs(Seq *, uint32);
static bool seq_dispatch(TTY *, Seq *, uint);
static void seq_reset_buffers(Seq *);
static void seq_reset_template(Seq *);
static bool seq_get_opt(Seq *, uint32);
static void seq_print_func(Seq *, uint);
static void vte_ri(TTY *, Seq *, uint);
static void vte_ich(TTY *, Seq *, uint);
static void vte_cu(TTY *, Seq *, uint);
static void vte_cht(TTY *, Seq *, uint);
static void vte_dch(TTY *, Seq *, uint);
static void vte_ed(TTY *, Seq *, uint);
static void vte_el(TTY *, Seq *, uint);
static void vte_sgr(TTY *, Seq *, uint);
static void vte_decset(TTY *, Seq *, uint);
static void vte_osc(TTY *, Seq *, uint);

static char *input_;

TTY *
tty_create(int cols, int rows, int histsize, int tablen)
{
	TTY *tty = xmalloc(1, sizeof(*tty));

	if (!tty_init(tty, cols, rows, histsize, tablen)) {
		FREE(tty);
	}

	return tty;
}

bool
tty_init(TTY *tty, int cols, int rows, int histsize, int tablen)
{
	if (!tty) return false;

	memclear(tty, 1, sizeof(*tty));

	stream_init(tty, cols, rows, histsize);
	ASSERT(tty->cols == tty->pitch);

	for (int i = 0; i < tty->hist.max; i++) {
		Row *row = ring_data(&tty->hist, i);
		row->index = i;
	}

	ASSERT(tablen > 0);
	tty->tablen = tablen;

	for (int i = 0; ++i < tty->pitch; ) {
		tty->tabstops[i] |= (i % tty->tablen == 0) ? 1 : 0;
	}

	seq_reset_buffers(&tty->seq);
	seq_reset_template(&tty->seq);

#define SET_FUNC(id,fn) vtfuncs[(id)].handler = (fn)
	SET_FUNC(VTE_RI,  vte_ri);
	SET_FUNC(VTE_SGR, vte_sgr);
	SET_FUNC(VTE_ICH, vte_ich);
	SET_FUNC(VTE_CUU, vte_cu);
	SET_FUNC(VTE_CUD, vte_cu);
	SET_FUNC(VTE_CUF, vte_cu);
	SET_FUNC(VTE_CUB, vte_cu);
	SET_FUNC(VTE_CUP, vte_cu);
	SET_FUNC(VTE_ED,  vte_ed);
	SET_FUNC(VTE_EL,  vte_el);
	SET_FUNC(VTE_DCH, vte_dch);
	SET_FUNC(VTE_OSC, vte_osc);
	SET_FUNC(VTE_DECSET, vte_decset);
	SET_FUNC(VTE_DECRST, vte_decset);
#undef SET_FUNC

	return true;
}

size_t
tty_write(TTY *tty, const char *str, size_t len)
{
	uint i = 0;

	while (str[i] && i < len) {
		uint err = 0;
		uint32 ucs4 = 0;
		uint width;

		width = utf8_decode(str + i, len - i, &ucs4, &err);

		if (!width) break;

		if (!parse_token(tty, &tty->seq, ucs4)) {
			stream_write(tty, &tty->seq.templ);
		} else {
			dummy__(tty);
		}

#if 1
#define DBGOPT_PRINT_INPUT 1
		{
			char tmp[128] = { 0 };
			uint n = ucs4str(ucs4, tmp, LEN(tmp));
			for (uint j = 0; j + !j <= n; j++) {
				arr_push(input_, (n > j) ? tmp[j] : ' ');
			}
		}
#endif
		i += width;
	}

#ifdef DBGOPT_PRINT_INPUT
	if (arr_count(input_)) {
		arr_push(input_, 0);
		msg_log("Input", "%s\n", input_);
		arr_clear(input_);
	}
#endif

	return i;
}

void
tty_scroll(TTY *tty, int dy)
{
	tty->scroll = CLAMP(tty->scroll + dy, -tty->top, 0);
}

void
tty_resize(TTY *tty, uint cols_, uint rows_)
{
	ASSERT(tty->hist.max > 0);
	int cols = CLAMP((int64)cols_, 1, INT_MAX & ~0x0f);
	int rows = CLAMP((int64)rows_, 1, tty->hist.max - 1);

	if (cols != tty->cols || rows != tty->rows) {
		stream_resize(tty, cols, rows);
	}
}

void
seq_reset_buffers(Seq *seq)
{
	ASSERT(seq);

	seq->tokens[0] = 0;
	seq->depth = 0;
	arr_set(seq->buf,  0, 0), arr_clear(seq->buf);
	arr_set(seq->opts, 0, 0), arr_clear(seq->opts);
	arr_set(seq->args, 0, 0), arr_clear(seq->args);
}

void
seq_reset_template(Seq *seq)
{
	ASSERT(seq);

	memclear(&seq->templ, 1, sizeof(seq->templ));
	seq->templ.color.fg = COLOR_FG;
	seq->templ.color.bg = COLOR_BG;
	seq->templ.width = 1;
}

bool
seq_dispatch(TTY *tty, Seq *seq, uint id)
{
	ASSERT(id < VTE_COUNT);
	seq_print_func(seq, id);

	if (vtfuncs[id].handler) {
		vtfuncs[id].handler(tty, seq, id);
		return true;
	}

	return false;
}

bool
parse_token(TTY *tty, Seq *seq, uint32 ucs4)
{
	ASSERT(seq);
	ASSERT(seq->depth + 2u < LEN(seq->tokens));

	SeqRes res = { 0 };
	seq->templ.ucs4  = 0;
	seq->templ.width = 0;
	seq->templ.type  = 0;

	if (ucs4 == CTRL_ESC) {
		seq->state |= STATE_ESC;
		return true;
	}

	if (seq->state & STATE_ESC) {
		uint32 ucs4_ = 0;
		res = seq_parse_esc(seq, ucs4, &ucs4_);
		if (ucs4_) {
			seq->state = res.state;
			ucs4 = ucs4_;
		} else {
			goto dispatch;
		}
	}
	if (seq->state & STATE_OSC) {
		res = seq_parse_osc(seq, ucs4);
	} else if (seq->state & STATE_DCS) {
		res = seq_parse_dcs(seq, ucs4);
	} else if (seq->state & STATE_CSI) {
		res = seq_parse_csi(seq, ucs4);
	}

dispatch:
	arr_push(seq->buf, ucs4);

	if (res.func) {
		seq_dispatch(tty, seq, res.func);
	}
	if (!res.state) {
		seq_reset_buffers(seq);
		seq->state = 0;
		seq->templ.ucs4  = ucs4;
		seq->templ.width = 1;
		seq->templ.type  = CELLTYPE_NORMAL;
	} else if (res.state != seq->state) {
		seq->state = res.state;
		seq->tokens[0] = 0;
		seq->depth = 0;
	}

	return (res.state || res.func);
}

bool
seq_get_opt(Seq *seq, uint32 ucs4)
{
	ASSERT(seq);

	uint32 *opt = NULL;

	switch (ucs4) {
		case ';': {
			if (!arr_count(seq->opts)) {
				arr_push(seq->opts, 0);
			}
			arr_push(seq->opts, 0);
			break;
		}
		case '0': case '1':
		case '2': case '3':
		case '4': case '5':
		case '6': case '7':
		case '8': case '9': {
			if (!arr_count(seq->opts)) {
				arr_push(seq->opts, 0);
			}
			opt = arr_tail(seq->opts) - 1;

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

SeqRes
seq_parse_dcs(Seq *seq, uint32 ucs4)
{
	char last = (seq->depth) ? seq->tokens[seq->depth-1] : 0;

	if (!last && arr_count(seq->opts) < 3) {
		if (seq_get_opt(seq, ucs4)) {
			return RES(seq->state, 0);
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
		switch (seq->tokens[0]) {
		case '|': return RES(0, VTE_DECUDK);
		case '$': return RES(0, VTE_DECRQSS);
		case '+': return RES(0, VTE_OTHER);
		}
		return RES(0, 0);
	}

	if (last != '|' && last != 'q') {
		seq->tokens[seq->depth++] = ucs4;
		seq->tokens[seq->depth] = 0;
	}
	arr_push(seq->args, ucs4);

	return RES(seq->state, 0);
}

SeqRes
seq_parse_osc(Seq *seq, uint32 ucs4)
{
	if (arr_count(seq->opts) < 2) {
		if (seq_get_opt(seq, ucs4)) {
			return RES(seq->state, 0);
		}
		if (!arr_count(seq->opts)) {
			return RES(0, 0);
		}
	}

	if (ucs4 == CTRL_ST || ucs4 == CTRL_BEL) {
		return RES(0, VTE_OSC);
	}
	arr_push(seq->args, ucs4);

	return RES(seq->state, 0);
}

SeqRes
seq_parse_esc(Seq *seq, uint32 ucs4, uint32 *ret)
{
	uint32 conv = 0;

	switch (ucs4) {
		case CTRL_CAN:
		case CTRL_SUB:
			break;
		case '[': return RES(STATE_CSI, 0);
		case ']': return RES(STATE_OSC, 0);
		case 'E': return RES(0, VTE_NEL);
		case 'H': return RES(0, VTE_HTS);
		case 'M': return RES(0, VTE_RI);
		case 'P': return RES(STATE_DCS, 0);

		case 'X':  conv = CTRL_SOS; break;
		case '\\': conv = CTRL_ST;  break;
	}

	if (conv) {
		if (ret) *ret = conv;
		return RES(seq->state & ~STATE_ESC, 0);
	}
	return RES(0, 0);
}

SeqRes
seq_parse_csi(Seq *seq, uint32 ucs4)
{
	if (seq_get_opt(seq, ucs4)) {
		return RES(seq->state, 0);
	}

	char last = (seq->depth) ? seq->tokens[seq->depth-1] : 0;
	seq->tokens[seq->depth++] = ucs4;
	seq->tokens[seq->depth] = 0;

	switch (last) {
	case 0:
		switch (ucs4) {
		case '?':
		case '!':
		case '"':
		case '\'':
		case '$':
		case '>': return RES(seq->state, 0);

		case '@': return RES(0, VTE_ICH);
		case 'A': return RES(0, VTE_CUU);
		case 'B': return RES(0, VTE_CUD);
		case 'C': return RES(0, VTE_CUF);
		case 'D': return RES(0, VTE_CUB);
		case 'E': return RES(0, VTE_CNL);
		case 'F': return RES(0, VTE_CPL);
		case 'G': return RES(0, VTE_CHA);
		case 'H': return RES(0, VTE_CUP);
		case 'I': return RES(0, VTE_CHT);
		case 'J': return RES(0, VTE_ED);
		case 'K': return RES(0, VTE_EL);
		case 'L': return RES(0, VTE_IL);
		case 'M': return RES(0, VTE_DL);
		case 'P': return RES(0, VTE_DCH);
		case 'S': return RES(0, VTE_SU);
		case 'T': return RES(0, VTE_SD);
		case 'X': return RES(0, VTE_ECH);
		case 'Z': return RES(0, VTE_CBT);
		case '`': return RES(0, VTE_HPA);
		case 'b': return RES(0, VTE_REP);
		case 'd': return RES(0, VTE_VPA);
		case 'f': return RES(0, VTE_HVP);
		case 'g': return RES(0, VTE_TBC);
		case 'h': return RES(0, VTE_SM);
		case 'i': return RES(0, VTE_MC);
		case 'l': return RES(0, VTE_RM);
		case 'm': return RES(0, VTE_SGR);
		case 'r': return RES(0, VTE_DECSTBM);
		case 'c':
		case 's':
		case 't':
		case 'x': return RES(0, VTE_OTHER);
		}
		break;
	case '?':
		switch (ucs4) {
		case 'J': return RES(0, VTE_DECSED);
		case 'K': return RES(0, VTE_DECSEL);
		case 'h': return RES(0, VTE_DECSET);
		case 'i': return RES(0, VTE_DECMC);
		case 'l': return RES(0, VTE_DECRST);
		case 'n': return RES(0, VTE_DECDSR);
		case 'q':
		case 'r':
		case 's': return RES(0, VTE_OTHER);
		}
		break;
	case '!':
		switch (ucs4) {
		case 'p': return RES(0, VTE_DECSTR);
		}
		break;
	case '"':
		switch (ucs4) {
		case 'p': return RES(0, VTE_DECSCL);
		case 'q': return RES(0, VTE_OTHER);
		}
		break;
	case '\'':
		switch (ucs4) {
		case 'w': return RES(0, VTE_DECEFR);
		case 'z': return RES(0, VTE_DECELR);
		case '{': return RES(0, VTE_DECSLE);
		case '|': return RES(0, VTE_DECRQLP);
		}
		break;
	case '$':
		switch (ucs4) {
		case 't': return RES(0, VTE_DECCARA);
		case 'v': return RES(0, VTE_DECCRA);
		case 'x': return RES(0, VTE_DECFRA);
		case 'z': return RES(0, VTE_DECERA);
		}
		break;
	case '>':
		switch (ucs4) {
		case 'c': return RES(0, VTE_OTHER);
		}
		break;
	}

	return RES(0, 0);
}

#define SEQOPT(s,n) (((n) < arr_count((s)->opts)) ? (s)->opts[(n)] : 0)

void
vte_osc(TTY *tty, Seq *seq, uint id)
{
	uint32 o = SEQOPT(seq, 0);
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
	                msg, (int)arr_count(seq->args), seq->args);
}

void
vte_decset(TTY *tty, Seq *seq, uint id)
{
	uint32 o = SEQOPT(seq, 0);

	switch (o) {
		case 25: {
			tty->cursor.hide = (id == VTE_DECRST);
			break;
		}
	}
}

void
vte_el(TTY *tty, Seq *seq, uint id)
{
	uint32 o = SEQOPT(seq, 0);
	ASSERT(!seq->templ.ucs4 && !seq->templ.width);

	switch (o) {
		case 0: {
			cmd_set_cells(tty, &seq->templ,
			              tty->cursor.x, tty->cursor.y,
			              tty->pitch - tty->cursor.x);
			break;
		}
		case 1: {
			seq->templ.ucs4 = ' ';
			seq->templ.width = 1;

			cmd_set_cells(tty, &seq->templ,
			              0, tty->cursor.y, tty->cursor.x);
			break;
		}
		case 2: {
			cmd_set_cells(tty, &seq->templ,
				      0, tty->cursor.y, tty->pitch);
			cmd_set_cursor_x(tty, 0);
			break;
		}
	}
}

void
vte_ed(TTY *tty, Seq *seq, uint id)
{
	uint32 o = SEQOPT(seq, 0);
	ASSERT(!seq->templ.ucs4 && !seq->templ.width);

	switch (o) {
		case 0: {
			cmd_clear_rows(tty, tty->cursor.y + 1, tty->rows);
			cmd_set_cells(tty, &seq->templ,
				      tty->cursor.x, tty->cursor.y,
				      tty->pitch);
			break;
		}
		case 1: {
			seq->templ.ucs4  = ' ';
			seq->templ.width = 1;

			cmd_clear_rows(tty, tty->top, tty->cursor.y - tty->top);
			cmd_set_cells(tty, &seq->templ,
				      0, tty->cursor.y,
				      tty->cursor.x);
			break;
		}
		case 2: {
			cmd_clear_rows(tty, tty->top, tty->rows);
			cmd_set_cursor_y(tty, 0);
			break;
		}
	}
}

void
vte_sgr(TTY *tty, Seq *seq, uint id)
{
	Cell *cell = &seq->templ;
	uint i = 0;
	uint count = arr_count(seq->opts);
	uint32 o[3] = { 0 };

	do {
		o[0] = SEQOPT(seq, i);

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
				o[1] = SEQOPT(seq, i + 1);
				if (o[1] == 5) {
					if (!(o[2] = SEQOPT(seq, i + 2))) {
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
vte_dch(TTY *tty, Seq *seq, uint id)
{
	uint32 o = SEQOPT(seq, 0);
	int dx = DEFAULT(o, 1);
	ASSERT(dx >= 0);

	cmd_shift_cells(tty, tty->cursor.x + dx, tty->cursor.y, -dx);
}

void
vte_cht(TTY *tty, Seq *seq, uint id)
{
	uint32 o = SEQOPT(seq, 0);
	seq->templ.ucs4  = '\t';
	seq->templ.width = 1;

	for (uint i = 0; i < DEFAULT(o, 1); i++) {
		if (!stream_write(tty, &seq->templ)) {
			break;
		}
	}
}

void
vte_cu(TTY *tty, Seq *seq, uint id)
{
	uint32 o[2] = { SEQOPT(seq, 0), SEQOPT(seq, 1) };

	switch (id) {
	case VTE_CUU:
		cmd_move_cursor_y(tty, -DEFAULT(o[0], 1));
		break;
	case VTE_CUD:
		cmd_move_cursor_y(tty, +DEFAULT(o[0], 1));
		break;
	case VTE_CUF:
		cmd_move_cursor_x(tty, +DEFAULT(o[0], 1));
		break;
	case VTE_CUB:
		cmd_move_cursor_x(tty, -DEFAULT(o[0], 1));
		break;
	case VTE_CUP:
		cmd_set_cursor_x(tty, o[1]);
		cmd_set_cursor_y(tty, o[0]);
		break;
	}
}

void
vte_ich(TTY *tty, Seq *seq, uint id)
{
	uint32 o = SEQOPT(seq, 0);

	seq->templ.ucs4 = ' ';
	seq->templ.width = 1;

	cmd_insert_cells(tty, &seq->templ, DEFAULT(o, 1));
}

void
vte_ri(TTY *tty, Seq *seq, uint id)
{
	cmd_move_cursor_y(tty, -1);
}

void
seq_print_func(Seq *seq, uint id)
{
#if 0
	fprintf(stderr, "\033[01;33m"
	                "VtCode("
	                "\033[0m%02u"
	                "\033[01;33m)\033[0m "
	                "%-8s %-8s ",
	                id, statenames[seq->state], vtfuncs[id].name);
	fprintf(stderr, "%#2.2x (%02lu) [%02u/%02lu] ",
	                seq->state,
	                arr_count(seq->opts),
	                seq->depth,
	                arr_count(seq->buf));

	for (uint i = 0; i < arr_count(seq->buf); i++) {
		fprintf(stderr, "%s%s", asciistr(seq->buf[i]), " ");
	}
	fputc('\n', stderr);
#else
	return;
#endif
}

