#ifndef PARSER_H__
#define PARSER_H__

enum ctrlcodes_e_ {
	CtrlNUL,
	CtrlBEL = '\a',
	CtrlBS  = '\b',
	CtrlHT  = '\t',
	CtrlLF  = '\n',
	CtrlVT  = '\v',
	CtrlFF  = '\f',
	CtrlCR  = '\r',
	CtrlSO  = 0x0e,
	CtrlSI  = 0x0f,
	CtrlCAN = 0x18,
	CtrlSUB = 0x1a,
	CtrlESC = 0x1b,
	CtrlNEL = 0x85,
	CtrlHTS = 0x88,
	CtrlDCS = 0x90,
	CtrlSOS = 0x98,
	CtrlST  = 0x9c,
	CtrlOSC = 0x9d,
	CtrlPM  = 0x9e,
	CtrlAPC = 0x9f
};

enum stateflags_e_ {
	StateDefault,
	StateESC = (1 << 0),
	StateCSI = (1 << 1),
	StateDEC = (1 << 2),
	StateSTR = (1 << 3),
	StateMax = (1 << 4)
};

#define STATE_MASK (StateMax-1)
#define ESC_MASK   (STATE_MASK)

#define MAX_ARGS 256

typedef struct parser_s_ {
	u32 state;
	char buf[BUFSIZ];
	size_t idx;
	size_t args[MAX_ARGS];
	uint narg;
	int lastc;
} Parser;

int parse_codepoint(int);

#endif
