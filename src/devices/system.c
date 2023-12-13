#include <stdio.h>
#include <stdlib.h>

#include "../uxn.h"
#include "system.h"

/*
Copyright (c) 2022-2023 Devine Lu Linvega, Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

static char *
scpy(char *src, char *dst, int len)
{
	int i = 0;
	while((dst[i] = src[i]) && i < len - 2) i++;
	dst[i] = 0;
	return dst;
}

static void
system_print(Stack *s, char *name)
{
	Uint8 i;
	fprintf(stderr, "%s ", name);
	for(i = 0; i < 9; i++) {
		Uint8 pos = s->ptr - 4 + i;
		fprintf(stderr, !pos ? "[%02x]" : i == 4 ? "<%02x>" :
                                                   " %02x ",
			s->dat[pos]);
	}
	fprintf(stderr, "\n");
}

int
system_error(char *msg, const char *err)
{
	fprintf(stderr, "%s: %s\n", msg, err);
	fflush(stderr);
	return 0;
}

int
system_load(Varvara *v, Uxn *u, char *filename)
{
	int l, i = 0;
	FILE *f = fopen(filename, "rb");
	if(!f)
		return system_error("Init", "Failed to load rom.");
	l = fread(&u->ram[PAGE_PROGRAM], 0x10000 - PAGE_PROGRAM, 1, f);
	while(l && ++i < RAM_PAGES)
		l = fread(u->ram + 0x10000 * i, 0x10000, 1, f);
	fclose(f);
	scpy(filename, v->rom, 0x40);
	return 1;
}

void
system_boot(Uxn *u, int soft)
{
	int i;
	for(i = 0x100 * soft; i < 0x10000; i++)
		u->ram[i] = 0;
	for(i = 0x0; i < 0x100; i++)
		u->dev[i] = 0;
	u->wst.ptr = 0;
	u->rst.ptr = 0;
}

void
system_inspect(Uxn *u)
{
	system_print(&u->wst, "wst");
	system_print(&u->rst, "rst");
}

/* IO */

Uint8
system_dei(Uxn *u, Uint8 addr)
{
	switch(addr) {
	case 0x4: return u->wst.ptr;
	case 0x5: return u->rst.ptr;
	default: return u->dev[addr];
	}
}

void
system_deo(Uxn *u, Uint8 *d, Uint8 port)
{
	Uint8 *ram;
	Uint16 addr;
	switch(port) {
	case 0x3:
		ram = u->ram;
		addr = PEEK2(d + 2);
		if(ram[addr] == 0x1) {
			Uint16 i, length = PEEK2(ram + addr + 1);
			Uint16 a_page = PEEK2(ram + addr + 1 + 2), a_addr = PEEK2(ram + addr + 1 + 4);
			Uint16 b_page = PEEK2(ram + addr + 1 + 6), b_addr = PEEK2(ram + addr + 1 + 8);
			int src = (a_page % RAM_PAGES) * 0x10000, dst = (b_page % RAM_PAGES) * 0x10000;
			for(i = 0; i < length; i++)
				ram[dst + (Uint16)(b_addr + i)] = ram[src + (Uint16)(a_addr + i)];
		}
		break;
	case 0x4:
		u->wst.ptr = d[4];
		break;
	case 0x5:
		u->rst.ptr = d[5];
		break;
	case 0xe:
		system_inspect(u);
		break;
	}
}
