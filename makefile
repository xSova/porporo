SRC=src/uxn.c src/devices/system.c src/devices/console.c src/devices/screen.c src/devices/controller.c src/devices/mouse.c src/devices/file.c src/devices/datetime.c
RELEASE_flags=-std=c89 -Os -DNDEBUG -g0 -s -Wall -Wno-unknown-pragmas
DEBUG_flags=-std=c89 -DDEBUG -Wall -Wno-unknown-pragmas -Wpedantic -Wshadow -Wextra -Werror=implicit-int -Werror=incompatible-pointer-types -Werror=int-conversion -Wvla -g -Og -fsanitize=address -fsanitize=undefined
ASM=bin/uxnasm

.PHONY: all dest run install uninstall format clean

all: dest bin/uxnasm bin/uxncli bin/porporo bin/menu.rom bin/potato.rom bin/wallpaper.rom bin/log.rom

dest:
	@ mkdir -p bin
run: all
	@ bin/porporo bin/log.rom
install: all
	@ cp bin/porporo ~/bin/
uninstall:
	@ rm -f ~/bin/porporo
format:
	@ clang-format -i src/porporo.c src/devices/*
clean:
	@ rm -f bin/*

bin/uxnasm: src/uxnasm.c
	@ cc ${RELEASE_flags} src/uxnasm.c -o bin/uxnasm
bin/uxncli: src/uxncli.c
	@ cc ${RELEASE_flags} ${SRC} src/uxncli.c -o bin/uxncli
bin/porporo: ${SRC} src/porporo.c
	@ cc ${DEBUG_flags} ${SRC} src/porporo.c -L/usr/local/lib -lSDL2 -o bin/porporo

bin/menu.rom: src/utils/menu.tal src/utils/menu.assets.tal
	@ ${ASM} src/utils/menu.tal bin/menu.rom
bin/potato.rom: src/utils/potato.tal src/utils/potato.assets.tal
	@ ${ASM} src/utils/potato.tal bin/potato.rom
bin/wallpaper.rom: src/utils/wallpaper.tal
	@ ${ASM} src/utils/wallpaper.tal bin/wallpaper.rom
bin/log.rom: src/utils/log.tal src/utils/log.assets.tal
	@ ${ASM} src/utils/log.tal bin/log.rom
	