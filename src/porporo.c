#include <SDL2/SDL.h>
#include <stdio.h>

#include "uxn.h"
#include "devices/system.h"
#include "devices/screen.h"
#include "devices/controller.h"
#include "devices/mouse.h"
#include "devices/file.h"
#include "devices/datetime.h"

/*
Copyright (c) 2023 Devine Lu Linvega

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

#define HOR 100
#define VER 60

enum Action {
	NORMAL,
	MOVE,
	DRAW
};
enum Action action;

static Uint32 theme[] = {0xffb545, 0x72DEC2, 0x000000, 0xffffff, 0xeeeeee};
static int WIDTH = HOR << 3, HEIGHT = VER << 3;
static int isdrag, reqdraw, dragx, dragy, camerax, cameray;
static Varvara programs[0x10], *porporo, *menu, *focused;
static Uint8 *ram, plen;

static SDL_Window *gWindow = NULL;
static SDL_Renderer *gRenderer = NULL;
static SDL_Texture *gTexture = NULL;
static Uint32 *pixels;

/* = DRAWING ===================================== */

void
putpixel(Uint32 *dst, int x, int y, int color)
{
	if(x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
		dst[y * WIDTH + x] = theme[color];
}

void
drawscreen(Uint32 *dst, Screen *scr, int x1, int y1)
{
	int x, y, w = scr->w, x2 = x1 + scr->w, y2 = y1 + scr->h;
	for(y = y1; y < y2; y++) {
		for(x = x1; x < x2; x++) {
			if(x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
				int index = (x - x1) + (y - y1) * w;
				dst[y * WIDTH + x] = scr->pixels[index];
			}
		}
	}
}

void
line(Uint32 *dst, int ax, int ay, int bx, int by, int color)
{
	int dx = abs(bx - ax), sx = ax < bx ? 1 : -1;
	int dy = -abs(by - ay), sy = ay < by ? 1 : -1;
	int err = dx + dy, e2;
	for(;;) {
		putpixel(dst, ax, ay, color);
		if(ax == bx && ay == by) break;
		e2 = 2 * err;
		if(e2 >= dy) err += dy, ax += sx;
		if(e2 <= dx) err += dx, ay += sy;
	}
}

void
drawicn(Uint32 *dst, int x, int y, Uint8 *sprite, int fg)
{
	int v, h;
	for(v = 0; v < 8; v++)
		for(h = 0; h < 8; h++)
			if((sprite[v] >> (7 - h)) & 0x1) putpixel(dst, x + h, y + v, fg);
}

void
drawconnection(Uint32 *dst, Varvara *p, int color)
{
	int i, x = camerax + p->screen.w;
	for(i = 0; i < p->clen; i++) {
		Connection *c = &p->out[i];
		int x1 = p->x + 1 + x, y1 = p->y - 2 + cameray;
		int x2 = c->b->x - 2 + camerax, y2 = c->b->y - 2 + cameray;
		line(dst, x1, y1, x2, y2, color);
	}
}

static void
linerect(Uint32 *dst, int x1, int y1, int x2, int y2, int color)
{
	int x, y;
	for(y = y1 - 1; y < y2 + 1; y++) {
		putpixel(dst, x1 - 2, y, color), putpixel(dst, x2, y, color);
		putpixel(dst, x1 - 1, y, color), putpixel(dst, x2 + 1, y, color);
	}
	for(x = x1 - 2; x < x2 + 2; x++) {
		putpixel(dst, x, y1 - 2, color), putpixel(dst, x, y2, color);
		putpixel(dst, x, y1 - 1, color), putpixel(dst, x, y2 + 1, color);
	}
}

static void
drawprogram(Uint32 *dst, Varvara *p)
{
	int w = p->screen.w, h = p->screen.h, x = p->x + camerax, y = p->y + cameray;
	if(p->done) return;
	drawconnection(dst, p, 2 - action);
	linerect(dst, x, y, x + w, y + h, 2 - action);
	if(p->screen.x2)
		screen_redraw(&p->screen);
	drawscreen(pixels, &p->screen, x, y);
}

static void
clear(Uint32 *dst)
{
	int i, l = WIDTH * HEIGHT, c = theme[4];
	for(i = 0; i < l; i++)
		dst[i] = c;
	reqdraw = 1;
}

static void
redraw(Uint32 *dst)
{
	int i;
	for(i = 2; i < plen; i++)
		drawprogram(dst, &programs[i]);
	drawprogram(dst, porporo);
	drawprogram(dst, menu);
	SDL_UpdateTexture(gTexture, NULL, dst, WIDTH * sizeof(Uint32));
	SDL_RenderClear(gRenderer);
	SDL_RenderCopy(gRenderer, gTexture, NULL, NULL);
	SDL_RenderPresent(gRenderer);
}

/* = OPTIONS ===================================== */

static int
error(char *msg, const char *err)
{
	printf("Error %s: %s\n", msg, err);
	return 0;
}

static void
quit(void)
{
	free(pixels);
	SDL_DestroyTexture(gTexture), gTexture = NULL;
	SDL_DestroyRenderer(gRenderer), gRenderer = NULL;
	SDL_DestroyWindow(gWindow), gWindow = NULL;
	SDL_Quit();
	exit(0);
}

static void
showmenu(int x, int y)
{
	clear(pixels);
	menu->done = 0;
	menu->u.dev[0x0f] = 0;
	uxn_eval(&menu->u, 0x100);
	menu->x = x, menu->y = y;
	isdrag = 0;
}

static void
endprogram(Varvara *p)
{
	p->done = 1;
	focused = 0;
	clear(pixels);
}

static void
connect(Varvara *a, Varvara *b, Uint8 ap, Uint8 bp)
{
	int i;
	Connection *c;
	clear(pixels);
	for(i = 0; i < a->clen; i++) {
		if(b == a->out[i].b) {
			a->clen = 0;
			return;
		}
	}
	c = &a->out[a->clen++];
	printf("Connected %s[%02x] -> %s[%02x]\n", a->rom, ap, b->rom, bp);
	c->bp = bp;
	c->b = b;
}

static Varvara *
addprogram(int x, int y, char *rom)
{
	Varvara *p = &programs[plen++];
	p->x = x, p->y = y, p->rom = rom;
	p->u.ram = ram + (plen - 1) * 0x10000;
	p->u.id = plen - 1;
	system_init(&p->u, p->u.ram, rom);
	uxn_eval(&p->u, 0x100);
	return p;
}

/* = MOUSE ======================================= */

static int
withinprogram(Varvara *p, int x, int y)
{
	return !p->done && x > p->x && x < p->x + p->screen.w && y > p->y && y < p->y + p->screen.h;
}

static Varvara *
pickprogram(int x, int y)
{
	int i;
	for(i = plen - 1; i > 0; i--) {
		Varvara *p = &programs[i];
		if(withinprogram(p, x, y))
			return p;
	}
	return 0;
}

static void
pickfocus(int x, int y)
{
	int i;
	if(withinprogram(menu, x, y)) {
		focused = menu;
		return;
	}
	for(i = plen - 1; i > 1; i--) {
		Varvara *p = &programs[i];
		if(withinprogram(p, x, y)) {
			focused = p;
			return;
		}
	}
	focused = 0;
}

static void
on_mouse_move(int x, int y)
{
	Uxn *u;
	int relx = x - camerax, rely = y - cameray;
	if(action == DRAW && isdrag) return;
	pickfocus(relx, rely);
	if(!focused) {
		if(isdrag) {
			camerax += x - dragx, cameray += y - dragy;
			dragx = x, dragy = y;
			clear(pixels);
		}
		return;
	}
	if(action) {
		if(isdrag) {
			focused->x += x - dragx, focused->y += y - dragy;
			dragx = x, dragy = y;
			clear(pixels);
		}
		return;
	}
	u = &focused->u;
	mouse_move(u, &u->dev[0x90], relx - focused->x, rely - focused->y);
}

static void
on_mouse_down(int button, int x, int y)
{
	Uxn *u;
	if(!focused && button > 1) {
		showmenu(x - camerax, y - cameray);
		return;
	}
	if(!focused || action) {
		isdrag = 1, dragx = x, dragy = y;
		return;
	}
	u = &focused->u;
	mouse_down(u, &u->dev[0x90], button);
}

static void
on_mouse_up(int button, int x, int y)
{
	Uxn *u;
	if(action == DRAW) {
		Varvara *a = pickprogram(dragx - camerax, dragy - cameray);
		Varvara *b = pickprogram(x - camerax, y - cameray);
		if(a && b && a != b)
			connect(a, b, 0x12, 0x18);
		isdrag = 0;
		return;
	}
	if(!focused || action) {
		isdrag = 0;
		return;
	}
	u = &focused->u;
	mouse_up(u, &u->dev[0x90], button);
}

static void
on_mouse_wheel(int x, int y)
{
	Uxn *u = &focused->u;
	mouse_scroll(u, &u->dev[0x90], x, y);
}

/* =============================================== */

static Uint8
get_button(SDL_Event *event)
{
	switch(event->key.keysym.sym) {
	case SDLK_LCTRL: return 0x01;
	case SDLK_LALT: return 0x02;
	case SDLK_LSHIFT: return 0x04;
	case SDLK_HOME: return 0x08;
	case SDLK_UP: return 0x10;
	case SDLK_DOWN: return 0x20;
	case SDLK_LEFT: return 0x40;
	case SDLK_RIGHT: return 0x80;
	}
	return 0x00;
}

static Uint8
get_key(SDL_Event *event)
{
	int sym = event->key.keysym.sym;
	SDL_Keymod mods = SDL_GetModState();
	if(sym < 0x20 || sym == SDLK_DELETE)
		return sym;
	if(mods & KMOD_CTRL) {
		if(sym < SDLK_a)
			return sym;
		else if(sym <= SDLK_z)
			return sym - (mods & KMOD_SHIFT) * 0x20;
	}
	return 0x00;
}

static void
on_porporo_key(char c)
{
	switch(c) {
	case 'd':
		action = action == DRAW ? NORMAL : DRAW;
		reqdraw = 1;
		break;
	case 'm':
		action = action == MOVE ? NORMAL : MOVE;
		reqdraw = 1;
		break;
	}
}

static void
on_controller_input(char c)
{
	Uxn *u;
	if(!focused || action) {
		on_porporo_key(c);
		return;
	}
	u = &focused->u;
	controller_key(u, &u->dev[0x80], c);
}

static void
on_controller_down(Uint8 key, Uint8 button)
{
	Uxn *u;
	if(!focused) return;
	u = &focused->u;
	if(key)
		controller_key(u, &u->dev[0x80], key);
	else
		controller_down(u, &u->dev[0x80], button);
	reqdraw = 1;
}

static void
on_controller_up(Uint8 button)
{
	Uxn *u;
	if(!focused) return;
	u = &focused->u;
	controller_up(u, &u->dev[0x80], button);
	reqdraw = 1;
}

/* =============================================== */

static int
init(void)
{
	if(SDL_Init(SDL_INIT_VIDEO) < 0)
		return error("Init", SDL_GetError());
	gWindow = SDL_CreateWindow("Porporo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
	if(gWindow == NULL)
		return error("Window", SDL_GetError());
	gRenderer = SDL_CreateRenderer(gWindow, -1, 0);
	if(gRenderer == NULL)
		return error("Renderer", SDL_GetError());
	gTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, WIDTH, HEIGHT);
	if(gTexture == NULL)
		return error("Texture", SDL_GetError());
	pixels = (Uint32 *)malloc(WIDTH * HEIGHT * sizeof(Uint32));
	if(pixels == NULL)
		return error("Pixels", "Failed to allocate memory");
	clear(pixels);
	return 1;
}

void
screenvector(Varvara *p)
{
	Uint16 vector = (p->u.dev[0x20] << 8) | p->u.dev[0x21];
	if(vector)
		uxn_eval(&p->u, vector);
	reqdraw = 1;
}

Uint8
emu_dei(Uxn *u, Uint8 addr)
{
	Varvara *prg = &programs[u->id];
	switch(addr & 0xf0) {
	case 0xc0: return datetime_dei(u, addr); break;
	}
	return u->dev[addr];
}

void
emu_deo(Uxn *u, Uint8 addr, Uint8 value)
{
	Uint8 p = addr & 0x0f, d = addr & 0xf0;
	Varvara *prg = &programs[u->id];
	u->dev[addr] = value;
	switch(d) {
	case 0x00:
		if(p > 0x7 && p < 0xe) screen_palette(&prg->screen, &u->dev[0x8]);
		if(p == 0xf)
			endprogram(prg);
		break;
	case 0x10: {
		int i;
		for(i = 0; i < prg->clen; i++) {
			Varvara *tprg = prg->out[i].b;
			if(tprg) {
				Uint16 vector = (tprg->u.dev[0x10] << 8) | tprg->u.dev[0x11];
				tprg->u.dev[0x12] = value;
				if(vector)
					uxn_eval(&tprg->u, vector);
			}
		}
	} break;
	case 0x20: screen_deo(prg, u->ram, &u->dev[d], p); break;
	case 0xa0: file_deo(prg, 0, u->ram, &u->dev[d], p); break;
	case 0xb0: file_deo(prg, 1, u->ram, &u->dev[d], p); break;
	}
}

int
main(int argc, char **argv)
{
	Uint32 begintime = 0;
	Uint32 endtime = 0;
	Uint32 delta = 0;
	Varvara *prg_left, *prg_log, *prg_log2;
	(void)argc;
	(void)argv;

	ram = (Uint8 *)calloc(0x10000 * RAM_PAGES, sizeof(Uint8));

	if(!init())
		return error("Init", "Failure");

	porporo = addprogram(650, 250, "bin/porporo.rom");

	menu = addprogram(200, 150, "bin/menu.rom");
	menu->done = 1;

	addprogram(20, 30, "bin/screen.pixel.rom");

	prg_log = addprogram(400, 10, "bin/log.rom");
	prg_log2 = addprogram(500, 110, "bin/log.rom");

	/*
	connect(menu, prg_log, 0x12, 0x18);
	connect(menu, prg_log2, 0x12, 0x18);
	connect(menu, porporo, 0x12, 0x18); */

	fflush(stdout);

	while(1) {
		SDL_Event event;
		if(!begintime)
			begintime = SDL_GetTicks();
		else
			delta = endtime - begintime;
		if(delta < 40)
			SDL_Delay(40 - delta);
		if(focused)
			screenvector(focused);
		if(reqdraw) {
			redraw(pixels);
			reqdraw = 0;
		}
		while(SDL_PollEvent(&event) != 0) {
			switch(event.type) {
			case SDL_QUIT: quit(); break;
			case SDL_MOUSEWHEEL: on_mouse_wheel(event.wheel.x, event.wheel.y); break;
			case SDL_MOUSEMOTION: on_mouse_move(event.motion.x, event.motion.y); break;
			case SDL_MOUSEBUTTONDOWN: on_mouse_down(SDL_BUTTON(event.button.button), event.motion.x, event.motion.y); break;
			case SDL_MOUSEBUTTONUP: on_mouse_up(SDL_BUTTON(event.button.button), event.motion.x, event.motion.y); break;
			case SDL_TEXTINPUT: on_controller_input(event.text.text[0]); break;
			case SDL_KEYDOWN: on_controller_down(get_key(&event), get_button(&event)); break;
			case SDL_KEYUP: on_controller_up(get_button(&event)); break;
			case SDL_WINDOWEVENT:
				if(event.window.event == SDL_WINDOWEVENT_EXPOSED) reqdraw = 1;
				break;
			}
		}
		begintime = endtime;
		endtime = SDL_GetTicks();
	}
	quit();
	return 0;
}