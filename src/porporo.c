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

enum Action {
	NORMAL,
	MOVE,
	DRAW
};
enum Action action;

typedef struct {
	int x, y, mode;
} Point2d;

static Uint8 *ram, cursor_icn[] = {0xff, 0xfe, 0xfc, 0xf8, 0xfc, 0xee, 0xc7, 0x82};
static Uint32 *pixels, theme[] = {0xffb545, 0x72DEC2, 0x000000, 0xffffff, 0xeeeeee};
static int WIDTH, HEIGHT, reqdraw, olen;
static Varvara varvaras[RAM_PAGES], *order[RAM_PAGES], *menu, *focused;
static Point2d camera, drag, cursor;

static SDL_DisplayMode DM;
static SDL_Window *gWindow = NULL;
static SDL_Renderer *gRenderer = NULL;
static SDL_Texture *gTexture = NULL;

/* = DRAWING ===================================== */

static void
drawclear(Uint32 *dst, Uint32 color)
{
	int i, l = WIDTH * HEIGHT;
	for(i = 0; i < l; i++) dst[i] = color;
}

static void
drawpixel(Uint32 *dst, int x, int y, Uint32 color)
{
	if(x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
		dst[y * WIDTH + x] = color;
}

static void
drawicn(Uint32 *dst, int x, int y, Uint8 *sprite, Uint32 color)
{
	int v, h;
	for(v = 0; v < 8; v++)
		for(h = 0; h < 8; h++)
			if((sprite[v] >> (7 - h)) & 0x1)
				drawpixel(dst, x + h, y + v, color);
}

static void
drawline(Uint32 *dst, int ax, int ay, int bx, int by, Uint32 color)
{
	int dx = abs(bx - ax), sx = ax < bx ? 1 : -1;
	int dy = -abs(by - ay), sy = ay < by ? 1 : -1;
	int err = dx + dy, e2;
	for(;;) {
		drawpixel(dst, ax, ay, color);
		if(ax == bx && ay == by) break;
		e2 = 2 * err;
		if(e2 >= dy) err += dy, ax += sx;
		if(e2 <= dx) err += dx, ay += sy;
	}
}

static void
drawborders(Uint32 *dst, int x1, int y1, int x2, int y2, Uint32 color)
{
	int x, y;
	for(y = y1 - 1; y < y2 + 1; y++) {
		drawpixel(dst, x1 - 2, y, color), drawpixel(dst, x2, y, color);
		drawpixel(dst, x1 - 1, y, color), drawpixel(dst, x2 + 1, y, color);
	}
	for(x = x1 - 2; x < x2 + 2; x++) {
		drawpixel(dst, x, y1 - 2, color), drawpixel(dst, x, y2, color);
		drawpixel(dst, x, y1 - 1, color), drawpixel(dst, x, y2 + 1, color);
	}
}

static void
drawconnections(Uint32 *dst, Varvara *a, Uint32 color)
{
	int i, x1, x2, y1, y2;
	for(i = 0; i < a->clen; i++) {
		Varvara *b = a->routes[i];
		if(b && b->live) {
			x1 = a->x + 1 + camera.x + a->screen.w, y1 = a->y - 2 + camera.y;
			x2 = b->x - 2 + camera.x, y2 = b->y - 2 + camera.y;
			drawline(dst, x1, y1, x2, y2, color);
		}
	}
}

static void
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

static void
drawvarvara(Uint32 *dst, Varvara *p)
{
	int w = p->screen.w, h = p->screen.h, x = p->x, y = p->y;
	Uint32 color;
	if(!p->lock) {
		x += camera.x, y += camera.y, color = theme[2 - action];
		drawborders(dst, x, y, x + w, y + h, color);
		if(p->clen) drawconnections(dst, p, color);
	}
	drawscreen(pixels, &p->screen, x, y);
}

static void
redraw(Uint32 *dst)
{
	int i;
	if(reqdraw & 2) drawclear(pixels, theme[4]);
	for(i = 0; i < olen; i++) drawvarvara(dst, order[i]);
	if(cursor.mode) drawicn(pixels, cursor.x, cursor.y, cursor_icn, theme[cursor.mode]);
	reqdraw = 0;
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
focus(Varvara *a)
{
	if(focused == a) return;
	if(focused) mouse_move(&focused->u, &focused->u.dev[0x90], 0x8000, 0x8000);
	focused = a;
}

static void
raise(Varvara *v)
{
	int i, last = olen - 1;
	Varvara *a, *b;
	for(i = 0; i < olen - 1; i++) {
		if(v == order[i]) {
			a = order[i], b = order[last];
			order[i] = b, order[last] = a;
			return;
		}
	}
}

static Varvara *
push(Varvara *p)
{
	if(p)
		order[olen++] = p, p->live = 1;
	return p;
}

static void
pop(Varvara *p)
{
	if(p) {
		p->clen = 0, p->live = 0, reqdraw |= 2;
		raise(p);
		olen--;
	}
}

static void
showmenu(int x, int y)
{
	if(menu->live)
		pop(menu);
	menu->u.dev[0x0f] = 0;
	uxn_eval(&menu->u, 0x100);
	menu->x = x, menu->y = y;
	drag.mode = 0, reqdraw |= 2;
	action = NORMAL, reqdraw |= 1;
	push(menu);
}

static Varvara *
spawn(int id, int x, int y, char *rom, int eval)
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
	screen_resize(&p->screen, 0x10, 0x10);
	p->u.dev[0x22] = WIDTH >> 8;
	p->u.dev[0x23] = WIDTH;
	p->u.dev[0x24] = HEIGHT >> 8;
	p->u.dev[0x25] = HEIGHT;
	if(eval)
		uxn_eval(&p->u, 0x100);
	return p;
}

static int
alloc(void)
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
within(Varvara *p, int x, int y)
{
	Screen *s = &p->screen;
	int xx = p->x, yy = p->y;
	return p->live && x > xx && x < xx + s->w && y > yy && y < yy + s->h;
}

static Varvara *
pick(int x, int y)
{
	int i;
	for(i = olen - 1; i > -1; --i) {
		Varvara *p = order[i];
		if(!p->lock && within(p, x, y))
			return p;
	}
	return 0;
}

static void
center(Varvara *v)
{
	if(v) {
		reqdraw |= 2;
		v->x = -camera.x + WIDTH / 2 - v->screen.w / 2;
		v->y = -camera.y + HEIGHT / 2 - v->screen.h / 2;
	}
}

static void
lock(Varvara *v)
{
	if(v && !v->lock) {
		v->lock = 1, focused = 0;
		v->x += camera.x, v->y += camera.y;
	} else {
		int i;
		for(i = olen - 1; i > -1; i--) {
			Varvara *a = order[i];
			if(a->lock) {
				a->lock = 0;
				a->x -= camera.x, a->y -= camera.y;
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
	if(within(menu, x, y)) {
		focus(menu);
		return;
	}
	for(i = olen - 1; i > -1; --i) {
		Varvara *p = order[i];
		if(within(p, x, y) && !p->lock) {
			focus(p);
			return;
		}
	}
	focus(0);
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
restart(Varvara *v)
{
	if(v) {
		screen_wipe(&v->screen);
		system_boot(&v->u, 1);
		system_load(&v->u, focused->rom);
		uxn_eval(&v->u, PAGE_PROGRAM);
		reqdraw |= 1;
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
		/* TODO: Handle invalid rom */
		focused = push(spawn(alloc(), menu->x, menu->y, cmd, 1));
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
	int relx = x - camera.x, rely = y - camera.y;
	cursor.mode = 0;
	if(action == DRAW) {
		cursor.x = x, cursor.y = y, cursor.mode = 2, reqdraw |= 1;
		return;
	}
	if(!drag.mode)
		pickfocus(relx, rely);
	if(!focused) {
		if(drag.mode) {
			camera.x += x - drag.x, camera.y += y - drag.y;
			drag.x = x, drag.y = y, reqdraw |= 2;
		}
		cursor.x = x, cursor.y = y, cursor.mode = 2, reqdraw |= 1;
		return;
	}
	if(action) {
		if(drag.mode) {
			focused->x += x - drag.x, focused->y += y - drag.y;
			drag.x = x, drag.y = y, reqdraw |= 2;
		}
		cursor.x = x, cursor.y = y, cursor.mode = 1, reqdraw |= 1;
		return;
	}
	u = &focused->u;
	mouse_move(u, &u->dev[0x90], relx - focused->x, rely - focused->y);
	if(!PEEK2(&u->dev[0x90])) /* draw mouse when no mouse vector */
		cursor.x = x, cursor.y = y, cursor.mode = 2, reqdraw |= 1;
}

static void
on_mouse_down(int button, int x, int y)
{
	Uxn *u;
	if(!focused && button > 1) {
		showmenu(x - camera.x, y - camera.y);
		return;
	}
	if(!focused || action) {
		drag.mode = 1, drag.x = x, drag.y = y;
		return;
	}
	u = &focused->u;
	mouse_down(u, &u->dev[0x90], button);
	raise(focused);
}

static void
on_mouse_up(int button, int x, int y)
{
	Uxn *u;
	if(action == DRAW) {
		Varvara *a = pick(drag.x - camera.x, drag.y - camera.y);
		Varvara *b = pick(x - camera.x, y - camera.y);
		if(a && b && a != b)
			connect(a, b);
		drag.mode = 0;
		return;
	}
	if(!focused || action) {
		drag.mode = 0;
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
	case 0x1b: action = NORMAL, reqdraw |= 1; return;
	case 'd': action = action == DRAW ? NORMAL : DRAW, reqdraw |= 1; return;
	case 'm': action = action == MOVE ? NORMAL : MOVE, reqdraw |= 1; return;
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
	case SDLK_F1: lock(focused); return;
	case SDLK_F2: center(focused); return;
	case SDLK_F4: pop(focused); return;
	case SDLK_F5: restart(focused); return;
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
	SDL_ShowCursor(0);
	drawclear(pixels, theme[4]);
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
		if(p == 0xf) pop(prg);
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
	menu = spawn(0, 200, 150, "bin/menu.rom", 0);
	/* load from arguments */
	for(i = 1; i < argc; i++) {
		Varvara *a;
		if(argv[i][0] == '-') {
			i += 1;
			a = push(spawn(i, anchor, 0, argv[i], 1));
			a->lock = 1;
			continue;
		}
		a = push(spawn(i, anchor + 0x10, 0x10, argv[i], 1));
		anchor += a->screen.w + 0x10;
	}
	/* event loop */
	while(1) {
		SDL_Event event;
		if(!begintime)
			begintime = SDL_GetTicks();
		else
			delta = endtime - begintime;
		if(delta < 30)
			SDL_Delay(30 - delta);
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
				if(event.window.event == SDL_WINDOWEVENT_EXPOSED) reqdraw |= 1;
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
			redraw(pixels);
			SDL_UpdateTexture(gTexture, NULL, pixels, WIDTH * sizeof(Uint32));
			SDL_RenderCopy(gRenderer, gTexture, NULL, NULL);
			SDL_RenderPresent(gRenderer);
		}
		begintime = endtime;
		endtime = SDL_GetTicks();
	}
	quit();
	return 0;
}