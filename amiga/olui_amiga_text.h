/* GFOpenLayout UI's outline text on true-colour screens, inside the library
 * (olui_amiga_text.c). MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef OLUI_AMIGA_TEXT_H
#define OLUI_AMIGA_TEXT_H

#include "gfopenlayout_ui_amiga.h"

/* 1: rp's bitmap is true colour or has 16 to 256 pens, and gfopenfont.library 1.3 is there */
int olui_aa_target(struct RastPort *rp);
/* 1 with its size in whole pixels; 0: family isn't an outline font here */
int olui_aa_measure(const char *family, int px, unsigned flags, const char *utf8, size_t length,
                    olui_amiga_text_size *size);
/* 1: drawn with its baseline at y; 0: not (fewer than 16 pens, no such outline
 * font). pens: the screen's, for a bitmap with a palette (NULL: rp's window's,
 * else the default public screen's) */
struct ColorMap;
int olui_aa_draw(struct RastPort *rp, int x, int y, const char *utf8, size_t length, const char *family, int px,
                 unsigned long argb, unsigned flags, unsigned long shadow, struct ColorMap *pens);
/* the faces, glyphs and libraries let go (the library's expunge) */
void olui_aa_cleanup(void);
/* set by the library: the faces stay when the last window goes */
extern int olui_aa_keep;

#endif
