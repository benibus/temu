#ifndef RENDERER_H__
#define RENDERER_H__

#include "defs.h"
#include "cells.h"
#include "fonts.h"

void renderer_set_dimensions(int, int, int, int, int, int, int);
bool renderer_init(void);
void renderer_draw_frame(const Frame *);

extern FontSet *fontset;

#endif

