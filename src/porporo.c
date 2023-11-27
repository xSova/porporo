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
static Varvara varvaras[0x10], *menu, *focused;
static Uint8 *ram, plen;

static SDL_DisplayMode DM;
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
drawconnection(Uint32 *dst, Varvara *a, int color)
{
	int i;
	for(i = 0; i < a->clen; i++) {
		Varvara *b = a->routes[i];
		int x1 = a->x + 1 + camerax + a->screen.w, y1 = a->y - 2 + cameray;
		int x2 = b->x - 2 + camerax, y2 = b->y - 2 + cameray;
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
drawpixels(Uint32 *dst, Varvara *p)
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
	for(i = 1; i < plen; i++)
		drawpixels(dst, &varvaras[i]);
	drawpixels(dst, menu);
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
	action = NORMAL;
}

static void
focusvv(Varvara *a)
{
	if(focused == a)
		return;
	if(focused) {
		mouse_move(&focused->u, &focused->u.dev[0x90], 0x8000, 0x8000);
		reqdraw = 1;
	}
	focused = a;
}

static void
endvv(Varvara *p)
{
	p->done = 1;
	focusvv(0);
	clear(pixels);
}

static Varvara *
addvv(int x, int y, char *rom, int eval)
{
	Varvara *p;
	if(plen >= RAM_PAGES)
		return 0;
	p = &varvaras[plen++];
	p->x = x, p->y = y, p->rom = rom;
	p->u.ram = ram + (plen - 1) * 0x10000;
	p->u.id = plen - 1;
	screen_resize(&p->screen, 128, 128);
	system_init(&p->u, p->u.ram, rom);
	if(eval)
		uxn_eval(&p->u, 0x100);
	return p;
}

static int
withinvv(Varvara *p, int x, int y)
{
	return !p->done && x > p->x && x < p->x + p->screen.w && y > p->y && y < p->y + p->screen.h;
}

static Varvara *
pickvv(int x, int y)
{
	int i;
	for(i = plen; i > -1; --i) {
		Varvara *p = &varvaras[i];
		if(withinvv(p, x, y))
			return p;
	}
	return 0;
}

static void
connect(Varvara *a, Varvara *b)
{
	int i;
	clear(pixels);
	for(i = 0; i < a->clen; i++) {
		if(b == a->routes[i]) {
			a->clen = 0;
			return;
		}
	}
	a->routes[a->clen++] = b;
}

/* = COMMAND ===================================== */

int cmdlen;
char cmd[0x40];

void
sendcmd(char c)
{
	if(c < 0x20) {
		clear(pixels);
		menu->done = 1;
		addvv(menu->x, menu->y, cmd, 0);
		cmdlen = 0;
		return;
	}
	if(cmdlen >= 0x3f) {
		cmdlen = 0;
		return;
	}
	cmd[cmdlen++] = c, cmd[cmdlen] = 0;
}

/* = MOUSE ======================================= */

static void
pickfocus(int x, int y)
{
	int i;
	if(withinvv(menu, x, y)) {
		focusvv(menu);
		return;
	}
	for(i = plen; i > -1; --i) {
		Varvara *p = &varvaras[i];
		if(withinvv(p, x, y)) {
			focusvv(p);
			return;
		}
	}
	focusvv(0);
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
		Varvara *a = pickvv(dragx - camerax, dragy - cameray);
		Varvara *b = pickvv(x - camerax, y - cameray);
		if(a && b && a != b)
			connect(a, b);
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
	return 0;
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
	return 0;
}

static int
on_porporo_key(char c, int sym)
{
	switch(c) {
	case 0x1b:
		action = NORMAL;
		reqdraw = 1;
		break;
	case 'd':
		action = action == DRAW ? NORMAL : DRAW;
		reqdraw = 1;
		break;
	case 'm':
		action = action == MOVE ? NORMAL : MOVE;
		reqdraw = 1;
		break;
	}
	switch(sym) {
	case SDLK_F1:
		printf("!!!\n");
		break;
	}
	return 1;
}

static int
on_controller_input(char c)
{
	Uxn *u;
	if(!focused || action)
		return on_porporo_key(c, 0);
	u = &focused->u;
	controller_key(u, &u->dev[0x80], c);
	return 1;
}

static int
on_controller_down(Uint8 key, Uint8 button, int sym)
{
	Uxn *u;
	if(sym)
		return on_porporo_key(key, sym);
	else if(!focused || action)
		return on_porporo_key(key, sym);
	u = &focused->u;
	if(key)
		controller_key(u, &u->dev[0x80], key);
	else
		controller_down(u, &u->dev[0x80], button);
	return (reqdraw = 1);
}

static int
on_controller_up(Uint8 button)
{
	Uxn *u;
	if(!focused) return 1;
	u = &focused->u;
	controller_up(u, &u->dev[0x80], button);
	return (reqdraw = 1);
}

/* =============================================== */

static int
init(void)
{
	if(SDL_Init(SDL_INIT_VIDEO) < 0)
		return error("Init", SDL_GetError());
	SDL_GetCurrentDisplayMode(0, &DM);
	WIDTH = DM.w - 0x20;
	HEIGHT = DM.h - 0x20;
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
	Uint8 *address = &p->u.dev[0x20];
	Uint16 vector = PEEK2(address);
	uxn_eval(&p->u, vector);
	reqdraw = 1;
}

void
console_deo(Varvara *a, Uint8 addr, Uint8 value)
{
	int i;
	(void)addr;
	if(a == menu && !menu->clen) {
		sendcmd(value);
		return;
	}
	for(i = 0; i < a->clen; i++) {
		Varvara *b = a->routes[i];
		if(b) {
			Uint8 *address = &b->u.dev[0x10];
			Uint16 vector = PEEK2(address);
			b->u.dev[0x12] = value;
			if(vector)
				uxn_eval(&b->u, vector);
		}
	}
}

Uint8
emu_dei(Uxn *u, Uint8 addr)
{
	switch(addr & 0xf0) {
	case 0xc0: return datetime_dei(u, addr); break;
	}
	return u->dev[addr];
}

void
emu_deo(Uxn *u, Uint8 addr, Uint8 value)
{
	Uint8 p = addr & 0x0f, d = addr & 0xf0;
	Varvara *prg = &varvaras[u->id];
	u->dev[addr] = value;
	switch(d) {
	case 0x00:
		if(p > 0x7 && p < 0xe) screen_palette(&prg->screen, &u->dev[0x8]);
		if(p == 0xf)
			endvv(prg);
		break;
	case 0x10: console_deo(prg, addr, value); break;
	case 0x20: screen_deo(prg, u->ram, &u->dev[d], p); break;
	case 0xa0: file_deo(prg, 0, u->ram, &u->dev[d], p); break;
	case 0xb0: file_deo(prg, 1, u->ram, &u->dev[d], p); break;
	}
}

int
main(int argc, char **argv)
{
	int i, anchor = 0x20;
	Uint32 begintime = 0, endtime = 0, delta = 0;

	ram = (Uint8 *)calloc(0x10000 * RAM_PAGES, sizeof(Uint8));

	if(!init())
		return error("Init", "Failure");

	menu = addvv(200, 150, "bin/menu.rom", 0);
	menu->done = 1;

	for(i = 1; i < argc; i++) {
		Varvara *a = addvv(anchor, 0x20 * i, argv[i], 1);
		anchor += a->screen.w + 0x20;
	}

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
			case SDL_KEYDOWN: on_controller_down(get_key(&event), get_button(&event), event.key.keysym.sym); break;
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