#include <stdlib.h>
#include <stdio.h>

#include "../uxn.h"
#include "screen.h"

/*
Copyright (c) 2021-2023 Devine Lu Linvega, Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

/* c = !ch ? (color % 5 ? color >> 2 : 0) : color % 4 + ch == 1 ? 0 : (ch - 2 + (color & 3)) % 3 + 1; */

static Uint8 blending[4][16] = {
	{0, 0, 0, 0, 1, 0, 1, 1, 2, 2, 0, 2, 3, 3, 3, 0},
	{0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3},
	{1, 2, 3, 1, 1, 2, 3, 1, 1, 2, 3, 1, 1, 2, 3, 1},
	{2, 3, 1, 2, 2, 3, 1, 2, 2, 3, 1, 2, 2, 3, 1, 2}};

void
screen_change(Screen *scr, Uint16 x1, Uint16 y1, Uint16 x2, Uint16 y2)
{
	if(x1 > scr->w && x2 > x1) return;
	if(y1 > scr->h && y2 > y1) return;
	if(x1 > x2) x1 = 0;
	if(y1 > y2) y1 = 0;
	if(x1 < scr->x1) scr->x1 = x1;
	if(y1 < scr->y1) scr->y1 = y1;
	if(x2 > scr->x2) scr->x2 = x2;
	if(y2 > scr->y2) scr->y2 = y2;
}

void
screen_wipe(Screen *scr)
{
	int i, length = scr->w * scr->h;
	Uint8 *fg = scr->fg, *bg = scr->bg;
	for(i = 0; i < length; i++)
		fg[i] = 0, bg[i] = 0;
}

void
screen_fill(Screen *scr, Uint8 *layer, int color)
{
	int i, length = scr->w * scr->h;
	for(i = 0; i < length; i++)
		layer[i] = color;
}

void
screen_rect(Screen *scr, Uint8 *layer, Uint16 x1, Uint16 y1, Uint16 x2, Uint16 y2, int color)
{
	int row, x, y, w = scr->w, h = scr->h;
	for(y = y1; y < y2 && y < h; y++)
		for(x = x1, row = y * w; x < x2 && x < w; x++)
			layer[x + row] = color;
}

static void
screen_2bpp(Screen *scr, Uint8 *layer, Uint8 *addr, Uint16 x1, Uint16 y1, Uint16 color, int fx, int fy)
{
	int row, w = scr->w, h = scr->h, opaque = (color % 5);
	Uint16 y, ymod = (fy < 0 ? 7 : 0), ymax = y1 + ymod + fy * 8;
	Uint16 x, xmod = (fx > 0 ? 7 : 0), xmax = x1 + xmod - fx * 8;
	for(y = y1 + ymod; y != ymax; y += fy) {
		int c = *addr++ | (*(addr + 7) << 8);
		if(y < h)
			for(x = x1 + xmod, row = y * w; x != xmax; x -= fx, c >>= 1) {
				Uint8 ch = (c & 1) | ((c >> 7) & 2);
				if((opaque || ch) && x < w)
					layer[x + row] = blending[ch][color];
			}
	}
}

static void
screen_1bpp(Screen *scr, Uint8 *layer, Uint8 *addr, Uint16 x1, Uint16 y1, Uint16 color, int fx, int fy)
{
	int row, w = scr->w, h = scr->h, opaque = (color % 5);
	Uint16 y, ymod = (fy < 0 ? 7 : 0), ymax = y1 + ymod + fy * 8;
	Uint16 x, xmod = (fx > 0 ? 7 : 0), xmax = x1 + xmod - fx * 8;
	for(y = y1 + ymod; y != ymax; y += fy) {
		int c = *addr++;
		if(y < h)
			for(x = x1 + xmod, row = y * w; x != xmax; x -= fx, c >>= 1) {
				Uint8 ch = c & 1;
				if((opaque || ch) && x < w)
					layer[x + row] = blending[ch][color];
			}
	}
}

void
screen_resize(Screen *scr, Uint16 width, Uint16 height)
{
	Uint8 *bg, *fg;
	Uint32 *pixels = NULL;
	if(scr->w == width && scr->h == height)
		return;
	bg = malloc(width * height), fg = malloc(width * height);
	if(bg && fg)
		pixels = realloc(scr->pixels, width * height * sizeof(Uint32));
	if(!bg || !fg || !pixels) {
		free(bg), free(fg);
		return;
	}
	free(scr->bg), free(scr->fg);
	scr->bg = bg, scr->fg = fg;
	scr->pixels = pixels;
	scr->w = width, scr->h = height;
	screen_wipe(scr);
	screen_change(scr, 0, 0, width, height);
}

void
screen_redraw(Screen *scr)
{
	int i, j, o, y;
	Uint8 *fg = scr->fg, *bg = scr->bg;
	Uint16 w = scr->w, h = scr->h;
	Uint16 x1 = scr->x1, y1 = scr->y1;
	Uint16 x2 = scr->x2 > w ? w : scr->x2, y2 = scr->y2 > h ? h : scr->y2;
	Uint32 palette[16], *pixels = scr->pixels;
	scr->x1 = scr->y1 = 0xffff;
	scr->x2 = scr->y2 = 0;
	for(i = 0; i < 16; i++)
		palette[i] = scr->palette[(i >> 2) ? (i >> 2) : (i & 3)];
	for(y = y1; y < y2; y++)
		for(o = y * w, i = x1 + o, j = x2 + o; i < j; i++)
			pixels[i] = palette[fg[i] << 2 | bg[i]];
}

void
screen_palette(Uint32 *palette, Uint8 *addr)
{
	int i, shift;
	for(i = 0, shift = 4; i < 4; ++i, shift ^= 4) {
		Uint8
			r = (addr[0 + i / 2] >> shift) & 0xf,
			g = (addr[2 + i / 2] >> shift) & 0xf,
			b = (addr[4 + i / 2] >> shift) & 0xf;
		palette[i] = 0x0f000000 | r << 16 | g << 8 | b;
		palette[i] |= palette[i] << 4;
	}
}

Uint8
screen_dei(Varvara *prg, Uxn *u, Uint8 addr)
{
	Screen *scr = &prg->screen;
	switch(addr) {
	case 0x22: return scr->w >> 8;
	case 0x23: return scr->w;
	case 0x24: return scr->h >> 8;
	case 0x25: return scr->h;
	default: return u->dev[addr];
	}
}

void
screen_deo(Varvara *prg, Uint8 *ram, Uint8 *d, Uint8 port)
{
	Uint8 *port_x, *port_y, *port_addr;
	Uint16 x, y, dx, dy, dxy, dyx, addr, addr_incr;
	switch(port) {
	case 0x3: {
		Uint8 *port_width = d + 0x2;
		screen_resize(&prg->screen, PEEK2(port_width), prg->screen.h);
	} break;
	case 0x5: {
		Uint8 *port_height = d + 0x4;
		screen_resize(&prg->screen, prg->screen.w, PEEK2(port_height));
	} break;
	case 0xe: {
		Screen *scr = &prg->screen;
		Uint8 ctrl = d[0xe];
		Uint8 color = ctrl & 0x3;
		Uint8 *layer = (ctrl & 0x40) ? scr->fg : scr->bg;
		port_x = d + 0x8, port_y = d + 0xa;
		x = PEEK2(port_x);
		y = PEEK2(port_y);
		/* fill mode */
		if(ctrl & 0x80) {
			Uint16 x2 = scr->w, y2 = scr->h;
			if(ctrl & 0x10) x2 = x, x = 0;
			if(ctrl & 0x20) y2 = y, y = 0;
			if(!x && !y && x2 == scr->w && y2 == scr->h)
				screen_fill(scr, layer, color);
			else
				screen_rect(scr, layer, x, y, x2, y2, color);
			screen_change(scr, x, y, x2, y2);
		}
		/* pixel mode */
		else {
			Uint16 w = scr->w, h = scr->h;
			if(x < w && y < h)
				layer[x + y * w] = color;
			screen_change(scr, x, y, x + 1, y + 1);
			if(d[0x6] & 0x1) POKE2(port_x, x + 1);
			if(d[0x6] & 0x2) POKE2(port_y, y + 1);
		}
		break;
	}
	case 0xf: {
		Screen *scr = &prg->screen;
		Uint8 i;
		Uint8 ctrl = d[0xf];
		Uint8 move = d[0x6];
		Uint8 length = move >> 4;
		Uint8 twobpp = !!(ctrl & 0x80);
		Uint8 *layer = (ctrl & 0x40) ? scr->fg : scr->bg;
		Uint8 color = ctrl & 0xf;
		int flipx = (ctrl & 0x10), fx = flipx ? -1 : 1;
		int flipy = (ctrl & 0x20), fy = flipy ? -1 : 1;
		port_x = d + 0x8, port_y = d + 0xa;
		port_addr = d + 0xc;
		x = PEEK2(port_x), dx = (move & 0x1) << 3, dxy = dx * fy;
		y = PEEK2(port_y), dy = (move & 0x2) << 2, dyx = dy * fx;
		addr = PEEK2(port_addr), addr_incr = (move & 0x4) << (1 + twobpp);
		if(twobpp) {
			for(i = 0; i <= length; i++) {
				screen_2bpp(scr, layer, &ram[addr], x + dyx * i, y + dxy * i, color, fx, fy);
				addr += addr_incr;
			}
		} else {
			for(i = 0; i <= length; i++) {
				screen_1bpp(scr, layer, &ram[addr], x + dyx * i, y + dxy * i, color, fx, fy);
				addr += addr_incr;
			}
		}
		screen_change(scr, x, y, x + dyx * length + 8, y + dxy * length + 8);
		if(move & 0x1) {
			x = x + dx * fx;
			POKE2(port_x, x);
		}
		if(move & 0x2) {
			y = y + dy * fy;
			POKE2(port_y, y);
		}
		if(move & 0x4) POKE2(port_addr, addr); /* auto addr+length */
		break;
	}
	}
}
