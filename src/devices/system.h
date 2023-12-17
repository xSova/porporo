/*
Copyright (c) 2022 Devine Lu Linvega, Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

#define SYSTEM_VERSION 2
#define SYSTEM_DEIMASK 0x0030
#define SYSTEM_DEOMASK 0xff38

#define RAM_PAGES 0x10

void system_inspect(Uxn *u);
int system_error(char *msg, const char *err);
int system_boot_img(Uxn *u, Uint8 *img, int length, int soft);
int system_boot_rom(Varvara *v, Uxn *u, char *filename, int soft);

Uint8 system_dei(Uxn *u, Uint8 addr);
void system_deo(Uxn *u, Uint8 *d, Uint8 port);
