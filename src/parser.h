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

int parse_codepoint(int);

#endif
