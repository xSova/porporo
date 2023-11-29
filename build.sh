#!/bin/bash

# format code
# clang-format -i src/porporo.c src/devices/*

SRC="src/uxn.c src/devices/system.c src/devices/screen.c src/devices/controller.c src/devices/mouse.c src/devices/file.c src/devices/datetime.c src/porporo.c"

# remove old
rm -f bin/porporo
mkdir -p bin

# debug(slow)
# cc -std=c89 -DDEBUG -Wall -Wno-unknown-pragmas -Wpedantic -Wshadow -Wextra -Werror=implicit-int -Werror=incompatible-pointer-types -Werror=int-conversion -Wvla -g -Og -fsanitize=address -fsanitize=undefined $SRC -L/usr/local/lib -lSDL2 -o bin/porporo

# build(fast)
cc $SRC -std=c89 -Os -DNDEBUG -g0 -s -Wall -Wno-unknown-pragmas -L/usr/local/lib -lSDL2 -o bin/porporo

# roms
cc src/uxnasm.c -o bin/uxnasm
bin/uxnasm etc/menu.tal bin/menu.rom
bin/uxnasm etc/wallpaper.tal bin/wallpaper.rom
bin/uxnasm etc/log.tal bin/log.rom

# run
./bin/porporo bin/log.rom
