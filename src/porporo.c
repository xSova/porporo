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

static Uint8 *ram, olen;
static Uint32 *pixels, theme[] = {0xffb545, 0x72DEC2, 0x000000, 0xffffff, 0xeeeeee};
static int WIDTH, HEIGHT, isdrag, reqdraw, dragx, dragy, camerax, cameray;
static Varvara varvaras[RAM_PAGES], *order[RAM_PAGES], *menu, *focused;

static SDL_DisplayMode DM;
static SDL_Window *gWindow = NULL;
static SDL_Renderer *gRenderer = NULL;
static SDL_Texture *gTexture = NULL;

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
	int x, y, row, relrow, w = scr->w, x2 = x1 + w, y2 = y1 + scr->h;
	for(y = y1; y < y2; y++) {
		row = y * WIDTH, relrow = (y - y1) * w;
		for(x = x1; x < x2; x++)
			if(x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
				dst[row + x] = scr->pixels[relrow + x - x1];
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

static void
drawborders(Uint32 *dst, int x1, int y1, int x2, int y2, int color)
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

void
drawconnections(Uint32 *dst, Varvara *a, int color)
{
	int i, x1, x2, y1, y2;
	for(i = 0; i < a->clen; i++) {
		Varvara *b = a->routes[i];
		if(b && b->live) {
			x1 = a->x + 1 + camerax + a->screen.w, y1 = a->y - 2 + cameray;
			x2 = b->x - 2 + camerax, y2 = b->y - 2 + cameray;
			line(dst, x1, y1, x2, y2, color);
		}
	}
}

static void
clear(Uint32 *dst)
{
	int i, l = WIDTH * HEIGHT, c = theme[4];
	for(i = 0; i < l; i++) dst[i] = c;
}

static void
drawvarvara(Uint32 *dst, Varvara *p)
{
	int w = p->screen.w, h = p->screen.h, x = p->x, y = p->y;
	if(!p->lock) {
		x += camerax, y += cameray;
		drawborders(dst, x, y, x + w, y + h, 2 - action);
		if(p->clen) drawconnections(dst, p, 2 - action);
	}
	drawscreen(pixels, &p->screen, x, y);
}

static void
redraw(Uint32 *dst)
{
	int i;
	for(i = 0; i < olen; i++)
		if(order[i]->lock) drawvarvara(dst, order[i]);
	for(i = 0; i < olen; i++)
		if(!order[i]->lock) drawvarvara(dst, order[i]);
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
setaction(enum Action a)
{
	action = a, reqdraw = 1;
}

static void
focusvv(Varvara *a)
{
	if(focused == a)
		return;
	if(focused)
		mouse_move(&focused->u, &focused->u.dev[0x90], 0x8000, 0x8000);
	focused = a;
}

static void
order_raise(Varvara *v)
{
	int i, j = 0, last = olen - 1;
	Varvara *a, *b;
	for(i = 0; i < olen; i++) {
		if(v == order[i]) {
			j = i;
			break;
		}
	}
	if(j == last)
		return;
	a = order[j], b = order[last];
	order[j] = b, order[last] = a;
}

static Varvara *
order_push(Varvara *p)
{
	if(p) {
		p->live = 1;
		order[olen++] = p;
	}
	return p;
}

static void
remvv(Varvara *p)
{
	if(p) {
		p->clen = 0, p->live = 0;
		reqdraw |= 2;
		order_raise(p);
		olen--;
	}
}

static void
showmenu(int x, int y)
{
	if(menu->live)
		remvv(menu);
	reqdraw |= 2;
	menu->u.dev[0x0f] = 0;
	uxn_eval(&menu->u, 0x100);
	menu->x = x, menu->y = y;
	isdrag = 0;
	setaction(NORMAL);
	order_push(menu);
}

static Varvara *
setvv(int id, int x, int y, char *rom, int eval)
{
	Varvara *p;
	if(id == -1) return 0;
	p = &varvaras[id];
	p->x = x, p->y = y;
	p->u.id = id, p->u.ram = ram + id * 0x10000;
	if(!system_init(p, &p->u, p->u.ram, rom))
		return 0;
	/* write size of porporo */
	/* TODO: write in memory during screen_resize(&p->screen, 640, 320); */
	p->u.dev[0x22] = WIDTH >> 8;
	p->u.dev[0x23] = WIDTH;
	p->u.dev[0x24] = HEIGHT >> 8;
	p->u.dev[0x25] = HEIGHT;
	if(eval)
		uxn_eval(&p->u, 0x100);
	return p;
}

static int
allocvv(void)
{
	int i;
	for(i = 1; i < RAM_PAGES; i++) {
		Varvara *p = &varvaras[i];
		if(!p || !p->live)
			return i;
	}
	return -1;
}

static int
withinvv(Varvara *p, int x, int y)
{
	Screen *s = &p->screen;
	int xx = p->x, yy = p->y;
	return p->live && x > xx && x < xx + s->w && y > yy && y < yy + s->h;
}

static Varvara *
pickvv(int x, int y)
{
	int i;
	for(i = olen - 1; i > -1; --i) {
		Varvara *p = order[i];
		if(!p->lock && withinvv(p, x, y))
			return p;
	}
	return 0;
}

static void
centervv(Varvara *v)
{
	if(v) {
		reqdraw |= 2;
		v->x = -camerax + WIDTH / 2 - v->screen.w / 2;
		v->y = -cameray + HEIGHT / 2 - v->screen.h / 2;
	}
}

static void
lockvv(Varvara *v)
{
	if(v && !v->lock) {
		v->lock = 1, focused = 0;
		v->x += camerax, v->y += cameray;
	} else {
		int i;
		for(i = olen - 1; i > -1; i--) {
			Varvara *a = order[i];
			if(a->lock) {
				a->lock = 0;
				a->x -= camerax, a->y -= cameray;
				break;
			}
		}
	}
	reqdraw |= 2;
}

static void
pickfocus(int x, int y)
{
	int i;
	if(withinvv(menu, x, y)) {
		focusvv(menu);
		return;
	}
	for(i = olen - 1; i > -1; --i) {
		Varvara *p = order[i];
		if(withinvv(p, x, y) && !p->lock) {
			focusvv(p);
			return;
		}
	}
	focusvv(0);
}

static int
connect(Varvara *a, Varvara *b)
{
	int i;
	reqdraw |= 2;
	for(i = 0; i < a->clen; i++)
		if(b == a->routes[i])
			return a->clen = 0;
	a->routes[a->clen++] = b;
	return 1;
}

static void
restartvv(Varvara *v)
{
	if(v) {
		screen_wipe(&v->screen);
		system_boot(&v->u, 1);
		system_load(&v->u, focused->rom);
		uxn_eval(&v->u, PAGE_PROGRAM);
		reqdraw = 1;
	}
}

/* = COMMAND ===================================== */

static int cmdlen;
static char cmd[0x40];

static void
sendcmd(char c)
{
	int i;
	if(c < 0x20) {
		reqdraw |= 2;
		/* TODO: Handle invalid rom */
		focused = order_push(setvv(allocvv(), menu->x, menu->y, cmd, 1));
		for(i = 0; i < menu->clen; i++)
			connect(focused, menu->routes[i]);
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
on_mouse_move(int x, int y)
{
	Uxn *u;
	int relx = x - camerax, rely = y - cameray;
	if(action == DRAW && isdrag) return;
	if(!isdrag)
		pickfocus(relx, rely);
	if(!focused) {
		if(isdrag) {
			camerax += x - dragx, cameray += y - dragy;
			dragx = x, dragy = y;
			reqdraw |= 2;
		}
		return;
	}
	if(action) {
		if(isdrag) {
			focused->x += x - dragx, focused->y += y - dragy;
			dragx = x, dragy = y;
			reqdraw |= 2;
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
	order_raise(focused);
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
	Uxn *u;
	if(!focused) return;
	u = &focused->u;
	mouse_scroll(u, &u->dev[0x90], x, y);
}

/* = CONTROL ===================================== */

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

static void
on_porporo_key(char c)
{
	switch(c) {
	case 0x1b: setaction(NORMAL); return;
	case 'd': setaction(action == DRAW ? NORMAL : DRAW); return;
	case 'm': setaction(action == MOVE ? NORMAL : MOVE); return;
	}
}

static int
on_controller_input(char c)
{
	Uxn *u;
	if(!focused || action) {
		on_porporo_key(c);
		return 1;
	}
	u = &focused->u;
	controller_key(u, &u->dev[0x80], c);
	return 1;
}

static void
on_controller_down(Uint8 key, Uint8 button, int sym)
{
	Uxn *u;
	switch(sym) {
	case SDLK_F1: lockvv(focused); return;
	case SDLK_F2: centervv(focused); return;
	case SDLK_F4: remvv(focused); return;
	case SDLK_F5: restartvv(focused); return;
	}
	if(!focused || action) {
		on_porporo_key(key);
		return;
	}
	u = &focused->u;
	if(key)
		controller_key(u, &u->dev[0x80], key);
	else
		controller_down(u, &u->dev[0x80], button);
	return;
}

static void
on_controller_up(Uint8 button)
{
	Uxn *u;
	if(focused) {
		u = &focused->u;
		controller_up(u, &u->dev[0x80], button);
	}
	return;
}

/* =============================================== */

static int
init(void)
{
	if(SDL_Init(SDL_INIT_VIDEO) < 0)
		return error("Init", SDL_GetError());
	SDL_GetCurrentDisplayMode(0, &DM);
	WIDTH = (DM.w >> 3 << 3) - 0x20;
	HEIGHT = (DM.h >> 3 << 3) - 0x80;
	printf("%dx%d\n", WIDTH, HEIGHT);
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
console_deo(Varvara *a, Uint8 addr, Uint8 value)
{
	int i;
	if(a == menu) {
		if(addr == 0x19) {
			sendcmd(value);
			return;
		}
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
		if(p == 0xf) remvv(prg);
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
	int i, anchor = 0;
	Uint32 begintime = 0, endtime = 0, delta = 0;
	if(argc == 2 && argv[1][0] == '-' && argv[1][1] == 'v')
		return !fprintf(stdout, "Porporo - Varvara Multiplexer, 29 Nov 2023.\n");
	if(!init())
		return error("Init", "Failure");
	ram = (Uint8 *)calloc(0x10000 * RAM_PAGES, sizeof(Uint8));
	menu = setvv(0, 200, 150, "bin/menu.rom", 0);
	/* load from arguments */
	for(i = 1; i < argc; i++) {
		Varvara *a;
		if(argv[i][0] == '-') {
			i += 1;
			a = order_push(setvv(i, anchor, 0, argv[i], 1));
			a->lock = 1;
			continue;
		}
		a = order_push(setvv(i, anchor + 0x10, 0x10, argv[i], 1));
		anchor += a->screen.w + 0x10;
	}
	/* event loop */
	while(1) {
		SDL_Event event;
		if(!begintime)
			begintime = SDL_GetTicks();
		else
			delta = endtime - begintime;
		if(delta < 40)
			SDL_Delay(40 - delta);
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
		/* screen vector */
		for(i = 0; i < olen; i++) {
			Varvara *v = order[i];
			Uxn *u = &v->u;
			Uint8 *address = &u->dev[0x20];
			Uint16 vector = PEEK2(address);
			if(vector)
				uxn_eval(u, vector);
			if(v->screen.x2) {
				screen_redraw(&v->screen);
				reqdraw |= 1;
			}
		}
		/* refresh */
		if(reqdraw) {
			if(reqdraw & 2)
				clear(pixels);
			redraw(pixels);
			reqdraw = 0;
		}
		begintime = endtime;
		endtime = SDL_GetTicks();
	}
	quit();
	return 0;
}