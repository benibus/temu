#ifndef VTE_H__
#define VTE_H__

#define ARG_DEFAULT 0
#define ARG_CHAR    1
#define ARG_STRING  2

typedef struct VTArgDesc_ {
	uint index;
	uint len;
	uint8 type;
} VTArgDesc;

typedef struct VTArgs_ {
	uint32 data[BUFSIZ];
	VTArgDesc buf[128];
	uint count, max_count;
	uint index, max_index;
} VTArgs;

typedef struct VTState_ {
	Cell spec;
	VTArgs args;
	uint32 *chars;
	char tokens[8];
	uint8 depth;
	uint8 flags;
} VTState;

#define VT_FUNC_TABLE \
    X_(NEL,     NULL) \
    X_(HTS,     NULL) \
    X_(RI,      vte_op_ri) \
    X_(S7CIT,   NULL) \
    X_(S8CIT,   NULL) \
    X_(ANSI,    NULL) \
    X_(DECDHL,  NULL) \
    X_(DECSWL,  NULL) \
    X_(DECDWL,  NULL) \
    X_(DECALN,  NULL) \
    X_(ISO,     NULL) \
    X_(DECSC,   NULL) \
    X_(DECRC,   NULL) \
    X_(DECPAM,  NULL) \
    X_(DECPNM,  NULL) \
    X_(RIS,     NULL) \
    X_(LS2,     NULL) \
    X_(LS3,     NULL) \
    X_(LS3R,    NULL) \
    X_(LS2R,    NULL) \
    X_(LS1R,    NULL) \
    X_(DECUDK,  NULL) \
    X_(DECRQSS, NULL) \
    X_(DECSCA,  NULL) \
    X_(DECSCL,  NULL) \
    X_(DECSTBM, NULL) \
    X_(SGR,     vte_op_sgr) \
    X_(ICH,     vte_op_ich) \
    X_(CUU,     vte_op_cu) \
    X_(CUD,     vte_op_cu) \
    X_(CUF,     vte_op_cu) \
    X_(CUB,     vte_op_cu) \
    X_(CNL,     NULL) \
    X_(CPL,     NULL) \
    X_(CHA,     NULL) \
    X_(CUP,     vte_op_cu) \
    X_(CHT,     NULL) \
    X_(ED,      vte_op_ed) \
    X_(DECSED,  NULL) \
    X_(EL,      vte_op_el) \
    X_(DECSEL,  NULL) \
    X_(IL,      NULL) \
    X_(DL,      NULL) \
    X_(DCH,     vte_op_dch) \
    X_(SU,      NULL) \
    X_(SD,      NULL) \
    X_(ECH,     NULL) \
    X_(CBT,     NULL) \
    X_(HPA,     NULL) \
    X_(REP,     NULL) \
    X_(VPA,     NULL) \
    X_(HVP,     NULL) \
    X_(TBC,     NULL) \
    X_(SM,      NULL) \
    X_(AM,      NULL) \
    X_(IRM,     NULL) \
    X_(SRM,     NULL) \
    X_(LNM,     NULL) \
    X_(DECSET,  vte_op_decset) \
    X_(DECRST,  vte_op_decset) \
    X_(DECCKM,  NULL) \
    X_(DECANM,  NULL) \
    X_(DECCOLM, NULL) \
    X_(DECSCLM, NULL) \
    X_(DECSCNM, NULL) \
    X_(DECOM,   NULL) \
    X_(DECAWM,  NULL) \
    X_(DECARM,  NULL) \
    X_(DECPFF,  NULL) \
    X_(DECPEX,  NULL) \
    X_(DECTCEM, NULL) \
    X_(DECTEK,  NULL) \
    X_(DECNRCM, NULL) \
    X_(DECNKM,  NULL) \
    X_(DECBKM,  NULL) \
    X_(MC,      NULL) \
    X_(DECMC,   NULL) \
    X_(RM,      NULL) \
    X_(DSR,     NULL) \
    X_(DECDSR,  NULL) \
    X_(DECSTR,  NULL) \
    X_(DECCARA, NULL) \
    X_(DECSLPP, NULL) \
    X_(DECRARA, NULL) \
    X_(DECCRA,  NULL) \
    X_(DECEFR,  NULL) \
    X_(DECREQTPARM, NULL) \
    X_(DECSACE, NULL) \
    X_(DECFRA,  NULL) \
    X_(DECELR,  NULL) \
    X_(DECERA,  NULL) \
    X_(DECSLE,  NULL) \
    X_(DECSERA, NULL) \
    X_(DECRQLP, NULL) \
    X_(OSC,     NULL) \
    X_(CONTINUE, vte_op_continue) \
    X_(TERMINATE, vte_op_terminate) \
    X_(UNKNOWN, NULL)

typedef struct VTFunc_ {
	uint8 id;
	const char *name;
	void (*handler)(TTY *, VTState *, uint);
} VTFunc;

void vte_op_ri(TTY *, VTState *, uint);
void vte_op_ich(TTY *, VTState *, uint);
void vte_op_cu(TTY *, VTState *, uint);
void vte_op_cht(TTY *, VTState *, uint);
void vte_op_dch(TTY *, VTState *, uint);
void vte_op_ed(TTY *, VTState *, uint);
void vte_op_el(TTY *, VTState *, uint);
void vte_op_sgr(TTY *, VTState *, uint);
void vte_op_decset(TTY *, VTState *, uint);

void vte_op_continue(TTY *, VTState *, uint);
void vte_op_terminate(TTY *, VTState *, uint);

enum vtfunc_id {
	VT_NONE,
#define X_(id, handler) VT_##id,
	VT_FUNC_TABLE
#undef X_
	VT_COUNT
};

static const VTFunc vtfuncs[] = {
#define X_(id, handler) [VT_##id] = { VT_##id, #id, handler },
	VT_FUNC_TABLE
#undef X_
};

#undef VT_FUNC_TABLE

#endif
