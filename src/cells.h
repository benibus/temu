#ifndef CELLS_H__
#define CELLS_H__

#include "defs.h"

typedef enum {
	CellTypeBlank,
	CellTypeNormal,
	CellTypeComplex,
	CellTypeTab,
	CellTypeDummyTab,
	CellTypeDummyWide,
	CellTypeCount
} CellType;

typedef struct {
	uint32 ucs4;
	uint32 bg;
	uint32 fg;
	CellType type:8;
	uint8 width;
	uint16 attrs;
} Cell;

#endif

