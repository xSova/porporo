#include <SDL2/SDL.h>
#include <stdio.h>

#include "uxn.h"
#include "devices/system.h"
#include "devices/screen.h"
#include "devices/controller.h"
#include "devices/mouse.h"
#include "devices/file.h"
#include "devices/datetime.h"
#include "roms/menu.c"
#include "roms/wallpaper.c"
#include "roms/potato.c"

/*
Copyright (c) 2023 Devine Lu Linvega

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

/* clang-format off */

enum Action { NORMAL, MOVE, DRAW };
typedef struct { int x, y, mode; } Point2d;
static Uint8 *ram, cursor_icn[] = {
	0xfe, 0xfc, 0xf8, 0xf8, 0xfc, 0xce, 0x87, 0x02, 
	0xff, 0xff, 0xc3, 0xc3, 0xc3, 0xc3, 0xff, 0xff, 
	0x18, 0x18, 0x18, 0xff, 0xff, 0x18, 0x18, 0x18};
static Uint32 *pixels, palette[] = {0xeeeeee, 0x000000, 0x77ddcc, 0xffbb44};
static int WIDTH, HEIGHT, reqdraw, olen;
static Varvara varvaras[RAM_PAGES], *order[RAM_PAGES], *wallpaper, *menu, *focused, *potato;
static Point2d camera, drag, cursor;
static enum Action action;
static SDL_DisplayMode DM;
static SDL_Window *gWindow = NULL;
static SDL_Renderer *gRenderer = NULL;
static SDL_Texture *gTexture = NULL;

/* clang-format on */

/* = DRAWING ===================================== */

static void
draw_pixel(int x, int y, Uint32 color)
{
	if(x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
		pixels[y * WIDTH + x] = color;
}

static void
draw_icn(int x, int y, Uint8 *sprite, Uint32 color)
{
	int y2 = y + 8, h;
	for(; y < y2; y++, sprite++)
		for(h = 0; h < 8; h++)
			if(*sprite << h & 0x80)
				draw_pixel(x + h, y, color);
}

static void
draw_line(int ax, int ay, int bx, int by, Uint32 color)
{
	int dx = abs(bx - ax), sx = ax < bx ? 1 : -1;
	int dy = -abs(by - ay), sy = ay < by ? 1 : -1;
	int err = dx + dy, e2;
	for(;;) {
		draw_pixel(ax, ay, color);
		if(ax == bx && ay == by) break;
		e2 = 2 * err;
		if(e2 >= dy) err += dy, ax += sx;
		if(e2 <= dx) err += dx, ay += sy;
	}
}

static void
draw_borders(int x1, int y1, int x2, int y2, Uint32 color)
{
	int x, y;
	for(y = y1 - 1; y < y2 + 1; y++) {
		draw_pixel(x1 - 2, y, color), draw_pixel(x2, y, color);
		draw_pixel(x1 - 1, y, color), draw_pixel(x2 + 1, y, color);
	}
	for(x = x1 - 2; x < x2 + 2; x++) {
		draw_pixel(x, y1 - 2, color), draw_pixel(x, y2, color);
		draw_pixel(x, y1 - 1, color), draw_pixel(x, y2 + 1, color);
	}
}

static void
draw_connections(Varvara *a, Uint32 color)
{
	int i, x1, x2, y1, y2;
	for(i = 0; i < a->clen; i++) {
		Varvara *b = a->routes[i];
		if(b && b->live) {
			x1 = a->x + 1 + camera.x + a->screen.w, y1 = a->y - 2 + camera.y;
			x2 = b->x - 2 + camera.x, y2 = b->y + b->screen.h + 1 + camera.y;
			if(a->y < b->y)
				y1 = a->y + a->screen.h + 1 + camera.y, y2 = b->y - 2 + camera.y;
			draw_line(x1, y1, x2, y2, color);
		}
	}
}

static void
draw_window(Varvara *p)
{
	Uint32 color;
	Screen *scr = &p->screen;
	int x, y, row, relrow, x2, y2, w = scr->w, h = scr->h, x1 = p->x, y1 = p->y;
	if(!p->lock) {
		x1 += camera.x, y1 += camera.y, color = palette[(1 + action) & 0x3];
		draw_borders(x1, y1, x1 + w, y1 + h, color);
		if(p->clen) draw_connections(p, color);
	}
	x2 = x1 + w, y2 = y1 + h;
	for(y = y1; y < y2; y++) {
		row = y * WIDTH, relrow = (y - y1) * w;
		for(x = x1; x < x2; x++)
			if(x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
				pixels[row + x] = scr->pixels[relrow + x - x1];
	}
}

/* = OPTIONS ===================================== */

static void
por_focus(Varvara *a)
{
	if(focused == a) return;
	if(focused)
		mouse_move(&focused->u, &focused->u.dev[0x90], 0x8000, 0x8000);
	focused = a;
}

static void
por_raise(Varvara *v)
{
	int i, last = olen - 1;
	Varvara *a, *b;
	if(v == order[last])
		return;
	for(i = 0; i < last; i++) {
		if(v == order[i]) {
			a = order[i], b = order[last];
			order[i] = b, order[last] = a;
			reqdraw = 1;
			return;
		}
	}
}

static Varvara *
por_push(Varvara *p, int x, int y, int lock)
{
	if(p) {
		p->x = x, p->y = y, p->lock = lock, p->live = 1, reqdraw = 1;
		order[olen++] = p;
	}
	return p;
}

static void
por_pop(Varvara *p)
{
	if(!p) return;
	p->clen = 0, p->live = 0, reqdraw = 1;
	por_raise(p);
	olen--;
}

static Varvara *
por_init(Varvara *v, int eval)
{
	screen_resize(&v->screen, 0x10, 0x10);
	POKE2(&v->u.dev[0x22], WIDTH)
	POKE2(&v->u.dev[0x24], HEIGHT)
	if(eval)
		uxn_eval(&v->u, PAGE_PROGRAM);
	reqdraw = 1;
	return v;
}

static Varvara *
por_spawn(int id, char *rom, int eval)
{
	Varvara *v;
	if(id == -1 || id > RAM_PAGES) return 0;
	v = &varvaras[id];
	v->u.id = id, v->u.ram = ram + id * 0x10000;
	system_boot_rom(v, &v->u, rom, 0);
	return por_init(v, eval);
}

static Varvara *
por_prefab(int id, Uint8 *rom, int length, int eval)
{
	Varvara *v;
	if(id == -1 || id > RAM_PAGES) return 0;
	v = &varvaras[id];
	v->u.id = id, v->u.ram = ram + id * 0x10000;
	system_boot_img(&v->u, rom, length, 0);
	return por_init(v, eval);
}

static int
por_alloc(void)
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
por_within(Varvara *p, int x, int y)
{
	Screen *s = &p->screen;
	int x1 = p->x, y1 = p->y;
	if(p->lock)
		x1 -= camera.x, y1 -= camera.y;
	return p->live && x > x1 && x < x1 + s->w && y > y1 && y < y1 + s->h;
}

static Varvara *
por_pick(int x, int y, int force)
{
	int i;
	for(i = olen - 1; i > -1; --i) {
		Varvara *p = order[i];
		if((!p->lock || force) && por_within(p, x, y))
			return p;
	}
	return 0;
}

static void
por_pickfocus(int x, int y)
{
	int i;
	for(i = olen - 1; i > -1; --i) {
		Varvara *p = order[i];
		if(por_within(p, x, y) && (!p->lock || p == potato)) {
			por_focus(p);
			return;
		}
	}
	por_focus(0);
}

static void
por_connect(Varvara *a, Varvara *b)
{
	int i;
	if(a && b && a != b) {
		reqdraw = 1;
		for(i = 0; i < a->clen; i++)
			if(b == a->routes[i]) {
				a->clen = 0;
				return;
			}
		a->routes[a->clen++] = b;
	}
}

static void
por_restart(Varvara *v, int soft)
{
	if(v) {
		system_boot_rom(v, &v->u, v->rom, soft);
		por_init(v, 1);
	}
}

static void
por_lock(Varvara *v)
{
	if(!v || v == wallpaper || v == menu) return;
	if(!v->lock) {
		v->lock = 1, focused = 0;
		v->x += camera.x, v->y += camera.y;
	} else {
		v->lock = 0;
		v->x -= camera.x, v->y -= camera.y;
	}
	mouse_move(&v->u, &v->u.dev[0x90], 0x8000, 0x8000);
	reqdraw = 1;
}

static void
por_center(Varvara *v)
{
	if(!v || v->lock) return;
	v->x = -camera.x + WIDTH / 2 - v->screen.w / 2;
	v->y = -camera.y + HEIGHT / 2 - v->screen.h / 2;
	reqdraw = 1;
}

static void
por_close(Varvara *v)
{
	if(!v || v == wallpaper || v == menu) return;
	por_pop(v);
}

static void
por_setaction(enum Action a)
{
	Uint16 vector = PEEK2(&potato->u.ram[0x0000]);
	action = a & 0x3, reqdraw = 1;
	potato->u.ram[0x0002] = a;
	uxn_eval(&potato->u, vector);
}

static void
por_menu(int x, int y)
{
	if(menu->live)
		por_pop(menu);
	menu->u.dev[0x0f] = 0;
	uxn_eval(&menu->u, PAGE_PROGRAM);
	por_setaction(NORMAL);
	drag.mode = 0;
	por_push(menu, x, y, 0);
}

static void
load_theme(void)
{
	Uint8 buf[6];
	FILE *f = fopen(".theme", "rb");
	if(f) {
		fread(&buf, 1, 6, f);
		screen_palette(palette, buf);
		fclose(f);
	}
}

/* = COMMAND ===================================== */

static int cmdlen;
static char cmd[0x40];

static void
send_cmd(Varvara *dest, char c)
{
	int i;
	if(c < 0x20) {
		focused = por_push(por_spawn(por_alloc(), cmd, 0), menu->x, menu->y, 0);
		for(i = 0; i < menu->clen; i++)
			por_connect(focused, menu->routes[i]);
		if(focused)
			uxn_eval(&focused->u, PAGE_PROGRAM);
		cmdlen = 0;
		return;
	}
	if(cmdlen >= 0x3f) {
		cmdlen = 0;
		return;
	}
	cmd[cmdlen++] = c, cmd[cmdlen] = 0;
}

void
send_msg(Varvara *dest, Uint8 type, Uint8 value)
{
	Uint8 *address;
	Uint16 vector;
	if(type == 0xff) {
		send_cmd(dest, value);
		return;
	}
	if(type == 0xfe) {
		por_setaction(value);
		return;
	}
	if(dest) {
		address = &dest->u.dev[0x10];
		vector = PEEK2(address);
		dest->u.dev[0x12] = value;
		dest->u.dev[0x17] = type;
		if(vector)
			uxn_eval(&dest->u, vector);
	}
}

/* = MOUSE ======================================= */

static void
on_mouse_move(int x, int y)
{
	Uxn *u;
	int relx = x - camera.x, rely = y - camera.y;
	cursor.x = x, cursor.y = y, cursor.mode = 1, reqdraw = 1;
	if(focused == potato) {
		mouse_move(&potato->u, &potato->u.dev[0x90], x - potato->x, y - potato->y);
		cursor.mode = 0;
		por_pickfocus(relx, rely);
		return;
	} else if(action == DRAW)
		return;
	if(!drag.mode)
		por_pickfocus(relx, rely);
	if(!focused) {
		if(drag.mode) {
			camera.x += x - drag.x, camera.y += y - drag.y;
			drag.x = x, drag.y = y;
		}
		return;
	}
	if(action) {
		if(drag.mode) {
			focused->x += x - drag.x, focused->y += y - drag.y;
			drag.x = x, drag.y = y;
		}
		return;
	}
	u = &focused->u;
	if(focused->lock)
		relx = x, rely = y;
	mouse_move(u, &u->dev[0x90], relx - focused->x, rely - focused->y);
	if(PEEK2(&u->dev[0x90])) /* draw mouse when no mouse vector */
		cursor.mode = 0;
}

static void
on_mouse_down(int button, int x, int y)
{
	Uxn *u;
	if((!focused || action) && focused != potato) {
		if(button > 1) {
			if(action)
				por_setaction(NORMAL);
			else
				por_menu(x - camera.x, y - camera.y);
			return;
		}
		drag.mode = 1, drag.x = x, drag.y = y;
		return;
	}
	u = &focused->u;
	mouse_down(u, &u->dev[0x90], button);
	por_raise(focused);
}

static void
on_mouse_up(int button, int x, int y)
{
	Uxn *u;
	if((!focused || action) && focused != potato) {
		if(action == DRAW) {
			Varvara *a = por_pick(drag.x - camera.x, drag.y - camera.y, 0);
			Varvara *b = por_pick(x - camera.x, y - camera.y, 0);
			por_connect(a, b);
		}
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
	if(!focused) {
		camera.x += x << 4, camera.y -= y << 4, reqdraw = 1;
		return;
	}
	u = &focused->u;
	mouse_scroll(u, &u->dev[0x90], x, y);
}

/* = CONTROL ===================================== */

static Uint8
get_fkey(SDL_Event *e)
{
	switch(e->key.keysym.sym) {
	case SDLK_F1: return 0x01;
	case SDLK_F2: return 0x02;
	case SDLK_F3: return 0x03;
	case SDLK_F4: return 0x04;
	case SDLK_F5: return 0x05;
	case SDLK_F6: return 0x06;
	case SDLK_F7: return 0x07;
	case SDLK_F8: return 0x08;
	}
	return 0;
}

static Uint8
get_button(SDL_Event *e)
{
	switch(e->key.keysym.sym) {
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
get_key(SDL_Event *e)
{
	int sym = e->key.keysym.sym;
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
on_controller_special(char c, Uint8 fkey)
{
	Varvara *v = por_pick(cursor.x, cursor.y, 1);
	if(v) {
		switch(fkey) {
		case 1: por_lock(v); return;
		case 2: por_center(v); return;
		case 4: por_close(v); return;
		case 5: por_restart(v, 1); return;
		}
	}
	switch(c) {
	case 0x1b: por_setaction(NORMAL); return;
	case 'd': por_setaction(action == DRAW ? NORMAL : DRAW); return;
	case 'm': por_setaction(action == MOVE ? NORMAL : MOVE); return;
	}
}

static void
on_controller_input(char c)
{
	Uxn *u;
	if(!focused || action) {
		on_controller_special(c, 0);
		return;
	}
	u = &focused->u;
	controller_key(u, &u->dev[0x80], c);
	return;
}

static void
on_controller_down(Uint8 key, Uint8 button, Uint8 fkey)
{
	Uxn *u;
	if(fkey || !focused || action) {
		on_controller_special(key, fkey);
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
	if(!focused)
		return;
	u = &focused->u;
	controller_up(u, &u->dev[0x80], button);
	return;
}

/* =============================================== */

static void
emu_end(void)
{
	free(ram), free(pixels);
	SDL_DestroyTexture(gTexture), gTexture = NULL;
	SDL_DestroyRenderer(gRenderer), gRenderer = NULL;
	SDL_DestroyWindow(gWindow), gWindow = NULL;
	SDL_Quit();
	exit(0);
}

static int
init(void)
{
	if(SDL_Init(SDL_INIT_VIDEO) < 0)
		return system_error("Init", SDL_GetError());
	SDL_GetCurrentDisplayMode(0, &DM);
	WIDTH = (DM.w >> 3 << 3) - 0x20;
	HEIGHT = (DM.h >> 3 << 3) - 0x80;
	printf("%dx%d[%02xx%02x]\n", WIDTH, HEIGHT, WIDTH >> 3, HEIGHT >> 3);
	gWindow = SDL_CreateWindow("Porporo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
	if(gWindow == NULL)
		return system_error("Window", SDL_GetError());
	gRenderer = SDL_CreateRenderer(gWindow, -1, 0);
	if(gRenderer == NULL)
		return system_error("Renderer", SDL_GetError());
	gTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, WIDTH, HEIGHT);
	if(gTexture == NULL)
		return system_error("Texture", SDL_GetError());
	pixels = (Uint32 *)malloc(WIDTH * HEIGHT * sizeof(Uint32));
	if(pixels == NULL)
		return system_error("Pixels", "Failed to allocate memory");
	SDL_ShowCursor(0);
	return 1;
}

void
graph_deo(Varvara *a, Uint8 addr, Uint8 value)
{
	int i;
	if(addr == 0x18) {
		if(!a->clen)
			send_msg(0, a->u.dev[0x17], value);
		else
			for(i = 0; i < a->clen; i++)
				send_msg(a->routes[i], a->u.dev[0x17], value);
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
		if(p > 0x7 && p < 0xe) {
			screen_palette(prg->screen.palette, &u->dev[0x8]);
			screen_change(&prg->screen, 0, 0, prg->screen.w, prg->screen.h);
		}
		if(p == 0xf) por_pop(prg);
		break;
	case 0x10: graph_deo(prg, addr, value); break;
	case 0x20: screen_deo(prg, u->ram, &u->dev[d], p); break;
	case 0xa0: file_deo(0, u->ram, &u->dev[d], p); break;
	case 0xb0: file_deo(1, u->ram, &u->dev[d], p); break;
	}
}

int
main(int argc, char **argv)
{
	int i, anchor = 0;
	Uint32 begintime = 0, endtime = 0, delta = 0;
	/* Read flags */
	if(argc == 2 && argv[1][0] == '-' && argv[1][1] == 'v')
		return !fprintf(stdout, "Porporo - Varvara Multiplexer, 17 Dec 2023.\n");
	if(!init())
		return system_error("Init", "Failure");
	/* Boot */
	ram = (Uint8 *)calloc(0x10000 * RAM_PAGES, 1);
	load_theme();
	menu = por_prefab(0, menu_rom, sizeof(menu_rom), 0);
	wallpaper = por_push(por_prefab(1, wallpaper_rom, sizeof(wallpaper_rom), 1), 0, 0, 1);
	potato = por_push(por_prefab(2, potato_rom, sizeof(potato_rom), 1), 0x10, 0x10, 1);
	for(i = 1; i < argc; i++) {
		Varvara *a = por_push(por_spawn(i + 2, argv[i], 1), anchor + 0x12, 0x38, 0);
		anchor += a->screen.w + 0x10;
	}
	/* Game Loop */
	while(1) {
		SDL_Event e;
		if(!begintime)
			begintime = SDL_GetTicks();
		else
			delta = endtime - begintime;
		if(delta < 30)
			SDL_Delay(30 - delta);
		while(SDL_PollEvent(&e) != 0) {
			switch(e.type) {
			case SDL_QUIT: emu_end(); break;
			case SDL_MOUSEWHEEL: on_mouse_wheel(e.wheel.x, e.wheel.y); break;
			case SDL_MOUSEMOTION: on_mouse_move(e.motion.x, e.motion.y); break;
			case SDL_MOUSEBUTTONDOWN: on_mouse_down(SDL_BUTTON(e.button.button), e.motion.x, e.motion.y); break;
			case SDL_MOUSEBUTTONUP: on_mouse_up(SDL_BUTTON(e.button.button), e.motion.x, e.motion.y); break;
			case SDL_TEXTINPUT: on_controller_input(e.text.text[0]); break;
			case SDL_KEYDOWN: on_controller_down(get_key(&e), get_button(&e), get_fkey(&e)); break;
			case SDL_KEYUP: on_controller_up(get_button(&e)); break;
			case SDL_WINDOWEVENT: reqdraw = 1; break;
			}
		}
		/* Screen Vector */
		for(i = 0; i < olen; i++) {
			Varvara *v = order[i];
			Uxn *u = &v->u;
			Uint8 *address = &u->dev[0x20];
			Uint16 vector = PEEK2(address);
			if(vector)
				uxn_eval(u, vector);
			if(v->screen.x2) {
				screen_redraw(&v->screen);
				reqdraw = 1;
			}
		}
		/* Draw */
		if(reqdraw) {
			for(i = 0; i < olen; i++)
				draw_window(order[i]);
			if(cursor.mode)
				draw_icn(cursor.x, cursor.y, &cursor_icn[action * 8], palette[1 + action]);
			SDL_UpdateTexture(gTexture, NULL, pixels, WIDTH * sizeof(Uint32));
			SDL_RenderCopy(gRenderer, gTexture, NULL, NULL);
			SDL_RenderPresent(gRenderer);
			reqdraw = 0;
		}
		begintime = endtime;
		endtime = SDL_GetTicks();
	}
	emu_end();
	return 0;
}