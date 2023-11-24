#include <SDL2/SDL.h>
#include <stdio.h>

/*
Copyright (c) 2020 Devine Lu Linvega

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

#define HOR 60
#define VER 32
#define PAD 2
#define SZ (HOR * VER * 16)

typedef unsigned char Uint8;

#define GATEMAX 128
#define WIREMAX 256
#define WIREPTMAX 128
#define PORTMAX 32
#define INPUTMAX 12
#define OUTPUTMAX 12

typedef struct Connection {
	unsigned char ap, bp;
	struct Program *a, *b;
} Connection;

typedef struct Program {
	char *name;
	int x, y, w, h;
	int clen;
	Connection out[0x100];
	struct Program *input, *output;
} Program;

typedef enum { INPUT,
	OUTPUT,
	POOL,
	BASIC } GateType;

typedef struct {
	int x, y;
} Point2d;

typedef struct Wire {
	int id, polarity, a, b, len, flex;
	Point2d points[WIREPTMAX];
} Wire;

typedef struct Gate {
	int id, polarity, locked, inlen, outlen, channel, note, sharp;
	Point2d pos;
	GateType type;
	Wire *inputs[PORTMAX], *outputs[PORTMAX];
} Gate;

typedef struct Noton {
	int alive, frame, channel, octave, glen, wlen;
	unsigned int speed;
	Gate gates[GATEMAX];
	Wire wires[WIREMAX];
	Gate *inputs[INPUTMAX], *outputs[OUTPUTMAX];
} Noton;

typedef struct Brush {
	int down;
	Point2d pos;
	Wire wire;
} Brush;

static int WIDTH = 8 * HOR + 8 * PAD * 2;
static int HEIGHT = 8 * (VER + 2) + 8 * PAD * 2;
static int ZOOM = 2, GUIDES = 1;

static Program programs[0x10];
static int plen;

static Noton noton;
static Brush brush;

/* clang-format off */

static Uint32 theme[] = {0xffffff, 0xffb545, 0x72DEC2, 0x000000, 0xeeeeee};

Uint8 icons[][8] = {
    {0x38, 0x7c, 0xee, 0xd6, 0xee, 0x7c, 0x38, 0x00}, /* gate:input */
    {0x38, 0x44, 0x92, 0xaa, 0x92, 0x44, 0x38, 0x00}, /* gate:output */
    {0x38, 0x44, 0x82, 0x92, 0x82, 0x44, 0x38, 0x00}, /* gate:output-sharp */
    {0x38, 0x7c, 0xfe, 0xfe, 0xfe, 0x7c, 0x38, 0x00}, /* gate:binary */
    {0x10, 0x00, 0x10, 0xaa, 0x10, 0x00, 0x10, 0x00}, /* guide */
    {0x00, 0x00, 0x00, 0x82, 0x44, 0x38, 0x00, 0x00}, /* eye open */
    {0x00, 0x38, 0x44, 0x92, 0x28, 0x10, 0x00, 0x00}, /* eye closed */
    {0x10, 0x54, 0x28, 0xc6, 0x28, 0x54, 0x10, 0x00}  /* unsaved */
};

static unsigned char font[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x08, 
	0x00, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x7f, 0x22, 0x22, 0x22, 0x7f, 0x22, 
	0x00, 0x08, 0x7f, 0x40, 0x3e, 0x01, 0x7f, 0x08, 0x00, 0x21, 0x52, 0x24, 0x08, 0x12, 0x25, 0x42, 
	0x00, 0x3e, 0x41, 0x42, 0x38, 0x05, 0x42, 0x3d, 0x00, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x08, 0x10, 0x10, 0x10, 0x10, 0x10, 0x08, 0x00, 0x08, 0x04, 0x04, 0x04, 0x04, 0x04, 0x08, 
	0x00, 0x00, 0x2a, 0x1c, 0x3e, 0x1c, 0x2a, 0x00, 0x00, 0x00, 0x08, 0x08, 0x3e, 0x08, 0x08, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 
	0x00, 0x3e, 0x41, 0x41, 0x41, 0x41, 0x41, 0x3e, 0x00, 0x08, 0x18, 0x08, 0x08, 0x08, 0x08, 0x1c, 
	0x00, 0x7e, 0x01, 0x01, 0x3e, 0x40, 0x40, 0x7f, 0x00, 0x7e, 0x01, 0x01, 0x3e, 0x01, 0x01, 0x7e, 
	0x00, 0x11, 0x21, 0x41, 0x7f, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x40, 0x40, 0x3e, 0x01, 0x01, 0x7e, 
	0x00, 0x3e, 0x41, 0x40, 0x7e, 0x41, 0x41, 0x3e, 0x00, 0x7f, 0x01, 0x01, 0x02, 0x04, 0x08, 0x08, 
	0x00, 0x3e, 0x41, 0x41, 0x3e, 0x41, 0x41, 0x3e, 0x00, 0x3e, 0x41, 0x41, 0x3f, 0x01, 0x02, 0x04, 
	0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x08, 0x10, 
	0x00, 0x00, 0x08, 0x10, 0x20, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x1c, 0x00, 0x00, 
	0x00, 0x00, 0x10, 0x08, 0x04, 0x08, 0x10, 0x00, 0x00, 0x3e, 0x41, 0x01, 0x06, 0x08, 0x00, 0x08, 
	0x00, 0x3e, 0x41, 0x5d, 0x55, 0x45, 0x59, 0x26, 0x00, 0x3e, 0x41, 0x41, 0x7f, 0x41, 0x41, 0x41, 
	0x00, 0x7e, 0x41, 0x41, 0x7e, 0x41, 0x41, 0x7e, 0x00, 0x3e, 0x41, 0x40, 0x40, 0x40, 0x41, 0x3e, 
	0x00, 0x7c, 0x42, 0x41, 0x41, 0x41, 0x42, 0x7c, 0x00, 0x3f, 0x40, 0x40, 0x7f, 0x40, 0x40, 0x3f, 
	0x00, 0x7f, 0x40, 0x40, 0x7e, 0x40, 0x40, 0x40, 0x00, 0x3e, 0x41, 0x50, 0x4e, 0x41, 0x41, 0x3e, 
	0x00, 0x41, 0x41, 0x41, 0x7f, 0x41, 0x41, 0x41, 0x00, 0x1c, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1c, 
	0x00, 0x7f, 0x01, 0x01, 0x01, 0x01, 0x01, 0x7e, 0x00, 0x41, 0x42, 0x44, 0x78, 0x44, 0x42, 0x41, 
	0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7f, 0x00, 0x63, 0x55, 0x49, 0x41, 0x41, 0x41, 0x41, 
	0x00, 0x61, 0x51, 0x51, 0x49, 0x49, 0x45, 0x43, 0x00, 0x1c, 0x22, 0x41, 0x41, 0x41, 0x22, 0x1c, 
	0x00, 0x7e, 0x41, 0x41, 0x7e, 0x40, 0x40, 0x40, 0x00, 0x3e, 0x41, 0x41, 0x41, 0x45, 0x42, 0x3d, 
	0x00, 0x7e, 0x41, 0x41, 0x7e, 0x44, 0x42, 0x41, 0x00, 0x3f, 0x40, 0x40, 0x3e, 0x01, 0x01, 0x7e, 
	0x00, 0x7f, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x41, 0x41, 0x41, 0x41, 0x41, 0x42, 0x3d, 
	0x00, 0x41, 0x41, 0x41, 0x41, 0x22, 0x14, 0x08, 0x00, 0x41, 0x41, 0x41, 0x41, 0x49, 0x55, 0x63, 
	0x00, 0x41, 0x22, 0x14, 0x08, 0x14, 0x22, 0x41, 0x00, 0x41, 0x22, 0x14, 0x08, 0x08, 0x08, 0x08, 
	0x00, 0x7f, 0x01, 0x02, 0x1c, 0x20, 0x40, 0x7f, 0x00, 0x18, 0x10, 0x10, 0x10, 0x10, 0x10, 0x18, 
	0x00, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x18, 0x08, 0x08, 0x08, 0x08, 0x08, 0x18, 
	0x00, 0x08, 0x14, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 
	0x00, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x01, 0x3e, 0x41, 0x7d, 
	0x00, 0x00, 0x00, 0x40, 0x7e, 0x41, 0x41, 0x7e, 0x00, 0x00, 0x00, 0x3e, 0x41, 0x40, 0x41, 0x3e, 
	0x00, 0x00, 0x00, 0x01, 0x3f, 0x41, 0x41, 0x3f, 0x00, 0x00, 0x00, 0x3e, 0x41, 0x7e, 0x40, 0x3f, 
	0x00, 0x00, 0x00, 0x3f, 0x40, 0x7e, 0x40, 0x40, 0x00, 0x00, 0x00, 0x3f, 0x41, 0x3f, 0x01, 0x7e, 
	0x00, 0x00, 0x00, 0x40, 0x7e, 0x41, 0x41, 0x41, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x08, 0x08, 
	0x00, 0x00, 0x00, 0x7f, 0x01, 0x01, 0x02, 0x7c, 0x00, 0x00, 0x00, 0x41, 0x46, 0x78, 0x46, 0x41, 
	0x00, 0x00, 0x00, 0x40, 0x40, 0x40, 0x40, 0x3f, 0x00, 0x00, 0x00, 0x76, 0x49, 0x41, 0x41, 0x41, 
	0x00, 0x00, 0x00, 0x61, 0x51, 0x49, 0x45, 0x43, 0x00, 0x00, 0x00, 0x3e, 0x41, 0x41, 0x41, 0x3e, 
	0x00, 0x00, 0x00, 0x7e, 0x41, 0x7e, 0x40, 0x40, 0x00, 0x00, 0x00, 0x3f, 0x41, 0x3f, 0x01, 0x01, 
	0x00, 0x00, 0x00, 0x5e, 0x61, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x3f, 0x40, 0x3e, 0x01, 0x7e, 
	0x00, 0x00, 0x00, 0x7f, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x41, 0x41, 0x41, 0x42, 0x3d, 
	0x00, 0x00, 0x00, 0x41, 0x41, 0x22, 0x14, 0x08, 0x00, 0x00, 0x00, 0x41, 0x41, 0x41, 0x49, 0x37, 
	0x00, 0x00, 0x00, 0x41, 0x22, 0x1c, 0x22, 0x41, 0x00, 0x00, 0x00, 0x41, 0x22, 0x1c, 0x08, 0x08, 
	0x00, 0x00, 0x00, 0x7f, 0x01, 0x3e, 0x40, 0x7f, 0x00, 0x08, 0x10, 0x10, 0x20, 0x10, 0x10, 0x08, 
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08, 
	0x00, 0x00, 0x00, 0x30, 0x49, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 	
};

/* clang-format on */

static SDL_Window *gWindow = NULL;
static SDL_Renderer *gRenderer = NULL;
static SDL_Texture *gTexture = NULL;
static Uint32 *pixels;

#pragma mark - Helpers

static int
distance(Point2d a, Point2d b)
{
	return (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
}

static Point2d *
setpt2d(Point2d *p, int x, int y)
{
	p->x = x;
	p->y = y;
	return p;
}

static Point2d *
clamp2d(Point2d *p, int step)
{
	return setpt2d(p, p->x / step * step, p->y / step * step);
}

static Point2d
Pt2d(int x, int y)
{
	Point2d p;
	setpt2d(&p, x, y);
	return p;
}

static Program *
addprogram(int x, int y, int w, int h, char *n)
{
	Program *p = &programs[plen++];
	p->x = x, p->y = y, p->w = w, p->h = h, p->name = n;

	return p;
}

static void
connectports(Program *a, Program *b, unsigned char ap, unsigned char bp)
{
	Connection *c = &a->out[a->clen++];
	c->ap = ap, c->bp = bp;
	c->a = a, c->b = b;
	a->output = b;
	b->input = a;
}

#pragma mark - Generics

static Gate *
nearestgate(Noton *n, Point2d pos)
{
	int i;
	for(i = 0; i < n->glen; ++i) {
		Gate *g = &n->gates[i];
		if(distance(pos, g->pos) < 50)
			return g;
	}
	return NULL;
}

static Gate *
gateat(Noton *n, Point2d pos)
{
	int i;
	for(i = 0; i < n->glen; ++i) {
		Gate *g = &n->gates[i];
		if(pos.x == g->pos.x && pos.y == g->pos.y)
			return g;
	}
	return NULL;
}

static void
flex(Wire *w)
{
	int i;
	if(w->len < 3 || !w->flex || noton.frame % 15 != 0)
		return;
	for(i = 1; i < w->len - 1; ++i) {
		Point2d *a = &w->points[i - 1];
		Point2d *b = &w->points[i];
		Point2d *c = &w->points[i + 1];
		b->x = (a->x + b->x + c->x) / 3;
		b->y = (a->y + b->y + c->y) / 3;
	}
	w->flex--;
}

#pragma mark - Options

static void
selchan(Noton *n, int channel)
{
	n->channel = channel;
	printf("Select channel #%d\n", n->channel);
}

static void
modoct(Noton *n, int mod)
{
	if((n->octave > 0 && mod < 0) || (n->octave < 8 && mod > 0))
		n->octave += mod;
	printf("Select octave #%d\n", n->octave);
}

static void
modspeed(Noton *n, int mod)
{
	if((n->speed > 10 && mod < 0) || (n->speed < 100 && mod > 0))
		n->speed += mod;
	printf("Select speed #%d\n", n->speed);
}

static void
pause(Noton *n)
{
	n->alive = !n->alive;
	printf("%s\n", n->alive ? "Playing.." : "Paused.");
}

static void
reset(Noton *n)
{
	int i, locked = 0;
	for(i = 0; i < n->wlen; i++)
		n->wires[i].len = 0;
	for(i = 0; i < n->glen; i++) {
		n->gates[i].inlen = 0;
		n->gates[i].outlen = 0;
		if(n->gates[i].locked)
			locked++;
	}
	n->wlen = 0;
	n->glen = locked;
	n->alive = 1;
}

/* Add/Remove */

static Wire *
addwire(Noton *n, Wire *temp, Gate *from, Gate *to)
{
	int i;
	Wire *w = &n->wires[n->wlen];
	w->id = n->wlen++;
	w->polarity = -1;
	w->a = from->id;
	w->b = to->id;
	w->len = 0;
	w->flex = 4;
	for(i = 0; i < temp->len; i++)
		setpt2d(&w->points[w->len++], temp->points[i].x, temp->points[i].y);
	printf("Add wire #%d(#%d->#%d) \n", w->id, from->id, to->id);
	return w;
}

static Gate *
addgate(Noton *n, GateType type, int polarity, Point2d pos)
{
	Gate *g = &n->gates[n->glen];
	g->id = n->glen++;
	g->polarity = polarity;
	g->channel = 0;
	g->note = 0;
	g->sharp = 0;
	g->inlen = 0;
	g->outlen = 0;
	g->type = type;
	setpt2d(&g->pos, pos.x, pos.y);
	printf("Add gate #%d \n", g->id);
	return g;
}

/* Wiring */

static int
extendwire(Brush *b)
{
	if(b->wire.len >= WIREPTMAX)
		return 0;
	if(distance(b->wire.points[b->wire.len - 1], b->pos) < 20)
		return 0;
	setpt2d(&b->wire.points[b->wire.len++], b->pos.x, b->pos.y);
	return 1;
}

static int
beginwire(Brush *b)
{
	Gate *gate = nearestgate(&noton, b->pos);
	Point2d *p = gate ? &gate->pos : &b->pos;
	b->wire.polarity = gate ? gate->polarity : -1;
	b->wire.len = 0;
	setpt2d(&b->wire.points[b->wire.len++], p->x + 4, p->y + 4);
	return 1;
}

static int
abandon(Brush *b)
{
	b->wire.len = 0;
	return 1;
}

static int
endwire(Brush *b)
{
	Wire *newwire;
	Gate *gatefrom, *gateto;
	if(b->wire.len < 1)
		return abandon(b);
	gatefrom = nearestgate(&noton, b->wire.points[0]);
	if(!gatefrom || gatefrom->outlen >= PORTMAX)
		return abandon(b);
	if(gatefrom->type == OUTPUT)
		return abandon(b);
	gateto = nearestgate(&noton, b->pos);
	if(!gateto || gateto->inlen >= PORTMAX)
		return abandon(b);
	if(gateto->type == INPUT || gatefrom == gateto)
		return abandon(b);
	setpt2d(&b->pos, gateto->pos.x + 4, gateto->pos.y + 4);
	extendwire(b);
	newwire = addwire(&noton, &b->wire, gatefrom, gateto);
	gatefrom->outputs[gatefrom->outlen++] = newwire;
	gateto->inputs[gateto->inlen++] = newwire;
	return abandon(b);
}

#pragma mark - Draw

static void
putpixel(Uint32 *dst, int x, int y, int color)
{
	if(x >= 0 && x < WIDTH - 8 && y >= 0 && y < HEIGHT - 8)
		dst[(y + PAD * 8) * WIDTH + (x + PAD * 8)] = theme[color];
}

static void
line(Uint32 *dst, int ax, int ay, int bx, int by, int color)
{
	int dx = abs(bx - ax), sx = ax < bx ? 1 : -1;
	int dy = -abs(by - ay), sy = ay < by ? 1 : -1;
	int err = dx + dy, e2;
	for(;;) {
		putpixel(dst, ax, ay, color);
		if(ax == bx && ay == by)
			break;
		e2 = 2 * err;
		if(e2 >= dy)
			err += dy, ax += sx;
		if(e2 <= dx)
			err += dx, ay += sy;
	}
}

static void
drawicn(Uint32 *dst, int x, int y, Uint8 *sprite, int fg)
{
	int v, h;
	for(v = 0; v < 8; v++)
		for(h = 0; h < 8; h++) {
			int ch1 = (sprite[v] >> (7 - h)) & 0x1;
			if(ch1)
				putpixel(dst, x + h, y + v, fg);
		}
}

static void
drawwire(Uint32 *dst, Wire *w, int color)
{
	int i;
	if(w->len < 2)
		return;
	for(i = 0; i < w->len - 1; i++) {
		Point2d *p1 = &w->points[i];
		Point2d *p2 = &w->points[i + 1];
		line(dst, p1->x, p1->y, p2->x, p2->y, (int)(noton.frame / 3) % w->len != i ? w->polarity + 1 : color);
	}
}

static void
drawconnection(Uint32 *dst, Program *p, int color)
{
	int i;
	for(i = 0; i < p->clen; i++) {
		Connection *c = &p->out[i];
		int x1 = p->x + p->w + 3, y1 = p->y + 3;
		int x2 = c->b->x - 5, y2 = c->b->y + 3;
		line(dst, x1, y1, x2, y2, 3);
	}
}

static void
drawguides(Uint32 *dst)
{
	int x, y;
	for(x = 0; x < HOR; x++)
		for(y = 0; y < VER; y++)
			drawicn(dst, x * 8, y * 8, icons[4], 4);
}

static void
drawui(Uint32 *dst)
{
	int bottom = VER * 8 + 8;
	drawicn(dst, 0 * 8, bottom, icons[GUIDES ? 6 : 5], GUIDES ? 1 : 2);
}

static void
linerect(Uint32 *dst, int x1, int y1, int x2, int y2, int color)
{
	int x, y;
	for(y = y1; y < y2; y++)
		putpixel(dst, x1, y, color), putpixel(dst, x2, y, color);
	for(x = x1; x < x2; x++)
		putpixel(dst, x, y1, color), putpixel(dst, x, y2, color);
	putpixel(dst, x2, y2, color);
}

static void
drawrect(Uint32 *dst, int x1, int y1, int x2, int y2, int color)
{
	int x, y;
	for(y = y1; y < y2; y++)
		for(x = x1; x < x2; x++)
			putpixel(dst, x, y, color);
}

static void
drawtext(Uint32 *dst, int x, int y, char *t, int color)
{
	char c;
	x -= 8;
	while((c = *t++))
		drawicn(dst, x += 8, y, &font[(c - 0x20) << 3], color);
}

static void
drawbyte(Uint32 *dst, int x, int y, unsigned char byte, int color)
{
	int hb = '0' - 0x20 + (byte >> 0x4);
	int lb = '0' - 0x20 + (byte & 0xf);
	drawicn(dst, x += 8, y, &font[hb << 3], color);
	drawicn(dst, x += 8, y, &font[lb << 3], color);
}

static void
drawprogram(Uint32 *dst, Program *p)
{
	drawrect(dst, p->x, p->y, p->x + p->w, p->y + p->h, 4);
	linerect(dst, p->x, p->y, p->x + p->w, p->y + p->h, 3);
	drawtext(dst, p->x + 2, p->y + 2, p->name, 3);
	/* ports */
	drawicn(dst, p->x - 8, p->y, icons[0], 1);
	drawicn(dst, p->x + p->w, p->y, icons[0], 2);
	drawbyte(dst, p->x - 8, p->y - 9, 0x12, 3);
	drawbyte(dst, p->x + p->w - 8, p->y - 9, 0x18, 3);
}

static void
clear(Uint32 *dst)
{
	int i, j;
	for(i = 0; i < HEIGHT; i++)
		for(j = 0; j < WIDTH; j++)
			dst[i * WIDTH + j] = theme[0];
}

static void
redraw(Uint32 *dst)
{
	int i;
	clear(dst);
	if(GUIDES)
		drawguides(dst);
	for(i = 0; i < noton.wlen; i++)
		drawwire(dst, &noton.wires[i], 2);
	drawwire(dst, &brush.wire, 3);

	for(i = 0; i < plen; i++)
		drawprogram(dst, &programs[i]);

	for(i = 0; i < plen; i++)
		drawconnection(dst, &programs[i], 3);

	drawui(dst);
	SDL_UpdateTexture(gTexture, NULL, dst, WIDTH * sizeof(Uint32));
	SDL_RenderClear(gRenderer);
	SDL_RenderCopy(gRenderer, gTexture, NULL, NULL);
	SDL_RenderPresent(gRenderer);
}

/* operation */

static void
savemode(int *i, int v)
{
	*i = v;
	redraw(pixels);
}

static void
selectoption(int option)
{
	switch(option) {
	case 0:
		savemode(&GUIDES, !GUIDES);
		break;
	}
}

static void
run(Noton *n)
{
	int i;
	for(i = 0; i < n->wlen; ++i)
		flex(&n->wires[i]);
	n->frame++;
}

/* options */

static int
error(char *msg, const char *err)
{
	printf("Error %s: %s\n", msg, err);
	return 0;
}

static void
modzoom(int mod)
{
	if((mod > 0 && ZOOM < 4) || (mod < 0 && ZOOM > 1)) {
		ZOOM += mod;
		SDL_SetWindowSize(gWindow, WIDTH * ZOOM, HEIGHT * ZOOM);
	}
}

static void
quit(void)
{
	free(pixels);
	SDL_DestroyTexture(gTexture);
	gTexture = NULL;
	SDL_DestroyRenderer(gRenderer);
	gRenderer = NULL;
	SDL_DestroyWindow(gWindow);
	gWindow = NULL;
	SDL_Quit();
	exit(0);
}

#pragma mark - Triggers

static void
domouse(SDL_Event *event, Brush *b)
{
	setpt2d(&b->pos, (event->motion.x - (PAD * 8 * ZOOM)) / ZOOM, (event->motion.y - (PAD * 8 * ZOOM)) / ZOOM);
	switch(event->type) {
	case SDL_MOUSEBUTTONDOWN:
		if(event->motion.y / ZOOM / 8 - PAD == VER + 1) {
			selectoption(event->motion.x / ZOOM / 8 - PAD);
			break;
		}
		if(event->button.button == SDL_BUTTON_RIGHT)
			break;
		b->down = 1;
		if(beginwire(b))
			redraw(pixels);
		break;
	case SDL_MOUSEMOTION:
		if(event->button.button == SDL_BUTTON_RIGHT)
			break;
		if(b->down) {
			if(extendwire(b))
				redraw(pixels);
		}
		break;
	case SDL_MOUSEBUTTONUP:
		if(event->button.button == SDL_BUTTON_RIGHT) {
			if(b->pos.x < 24 || b->pos.x > HOR * 8 - 72)
				return;
			if(b->pos.x > HOR * 8 || b->pos.y > VER * 8)
				return;
			if(!gateat(&noton, *clamp2d(&b->pos, 8)))
				addgate(&noton, BASIC, -1, *clamp2d(&b->pos, 8));
			redraw(pixels);
			break;
		}
		b->down = 0;
		if(endwire(b))
			redraw(pixels);
		break;
	}
}

static void
dokey(Noton *n, SDL_Event *event)
{
	switch(event->key.keysym.sym) {
	case SDLK_EQUALS:
	case SDLK_PLUS:
		modzoom(1);
		break;
	case SDLK_UNDERSCORE:
	case SDLK_MINUS:
		modzoom(-1);
		break;
	case SDLK_BACKSPACE:
		reset(n);
		break;
	case SDLK_SPACE:
		pause(n);
		break;
	case SDLK_UP:
		modoct(n, 1);
		break;
	case SDLK_DOWN:
		modoct(n, -1);
		break;
	case SDLK_LEFT:
		modspeed(n, 5);
		break;
	case SDLK_RIGHT:
		modspeed(n, -5);
		break;
	case SDLK_1:
		selchan(n, 0);
		break;
	case SDLK_2:
		selchan(n, 1);
		break;
	case SDLK_3:
		selchan(n, 2);
		break;
	case SDLK_4:
		selchan(n, 3);
		break;
	case SDLK_5:
		selchan(n, 4);
		break;
	case SDLK_6:
		selchan(n, 5);
		break;
	case SDLK_7:
		selchan(n, 6);
		break;
	case SDLK_8:
		selchan(n, 7);
		break;
	case SDLK_9:
		selchan(n, 8);
		break;
	}
}

static int
init(void)
{
	if(SDL_Init(SDL_INIT_VIDEO) < 0)
		return error("Init", SDL_GetError());
	gWindow = SDL_CreateWindow("Noton", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH * ZOOM, HEIGHT * ZOOM, SDL_WINDOW_SHOWN);
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

int
main(int argc, char **argv)
{
	Uint32 begintime = 0;
	Uint32 endtime = 0;
	Uint32 delta = 0;
	Program *a, *b, *c, *d;
	(void)argc;
	(void)argv;

	noton.alive = 1;
	noton.speed = 40;
	noton.channel = 0;
	noton.octave = 2;

	if(!init())
		return error("Init", "Failure");

	a = addprogram(150, 30, 120, 120, "console.rom");
	b = addprogram(320, 120, 120, 120, "print.rom");
	c = addprogram(10, 130, 100, 70, "keyb.rom");
	d = addprogram(190, 170, 100, 80, "debug.rom");

	connectports(a, b, 0x12, 0x18);
	connectports(c, a, 0x12, 0x18);
	connectports(d, b, 0x12, 0x18);

	while(1) {
		SDL_Event event;
		if(!begintime)
			begintime = SDL_GetTicks();
		else
			delta = endtime - begintime;
		if(delta < noton.speed)
			SDL_Delay(noton.speed - delta);
		if(noton.alive) {
			run(&noton);
			redraw(pixels);
		}
		while(SDL_PollEvent(&event) != 0) {
			if(event.type == SDL_QUIT)
				quit();
			else if(event.type == SDL_MOUSEBUTTONUP ||
				event.type == SDL_MOUSEBUTTONDOWN ||
				event.type == SDL_MOUSEMOTION) {
				domouse(&event, &brush);
			} else if(event.type == SDL_KEYDOWN)
				dokey(&noton, &event);
			else if(event.type == SDL_WINDOWEVENT)
				if(event.window.event == SDL_WINDOWEVENT_EXPOSED)
					redraw(pixels);
		}
		begintime = endtime;
		endtime = SDL_GetTicks();
	}
	quit();
	return 0;
}