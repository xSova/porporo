/*
Copyright (c) 2021 Devine Lu Linvega, Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

void screen_wipe(Screen *scr);
void screen_fill(Screen *scr, Uint8 *layer, int color);
void screen_rect(Screen *scr, Uint8 *layer, Uint16 x1, Uint16 y1, Uint16 x2, Uint16 y2, int color);
void screen_resize(Screen *scr, Uint16 width, Uint16 height);
void screen_change(Screen *scr, Uint16 x1, Uint16 y1, Uint16 x2, Uint16 y2);
void screen_redraw(Screen *scr);

void screen_palette(Uint32 *palette, Uint8 *addr);

Uint8 screen_dei(Varvara *prg, Uxn *u, Uint8 addr);
void screen_deo(Varvara *prg, Uint8 *ram, Uint8 *d, Uint8 port);
