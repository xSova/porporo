
SRC=src/uxn.c src/devices/system.c src/devices/screen.c src/devices/controller.c src/devices/mouse.c src/devices/file.c src/devices/datetime.c src/porporo.c
RELEASE_flags=-std=c89 -Os -DNDEBUG -g0 -s -Wall -Wno-unknown-pragmas -L/usr/local/lib -lSDL2
DEBUG_flags=-std=c89 -DDEBUG -Wall -Wno-unknown-pragmas -Wpedantic -Wshadow -Wextra -Werror=implicit-int -Werror=incompatible-pointer-types -Werror=int-conversion -Wvla -g -Og -fsanitize=address -fsanitize=undefined -L/usr/local/lib -lSDL2

.PHONY: all debug dest rom run install uninstall format clean

all: dest bin/uxnasm bin/porporo

dest:
	@ mkdir -p bin
rom:
	@ bin/uxnasm src/utils/menu.tal bin/menu.rom
	@ bin/uxnasm src/utils/wallpaper.tal bin/wallpaper.rom
	@ bin/uxnasm src/utils/log.tal bin/log.rom
run: bin/uxnasm bin/porporo rom
	@ bin/porporo : bin/wallpaper.rom bin/log.rom
install: bin/uxnasm bin/porporo
	@ cp bin/porporo ~/bin/
uninstall:
	@ rm -f ~/bin/porporo
format:
	@ clang-format -i src/porporo.c src/devices/*
clean:
	@ rm -f bin/*

bin/uxnasm: src/uxnasm.c
	@ cc ${RELEASE_flags} src/uxnasm.c -o bin/uxnasm
bin/porporo: ${SRC} src/porporo.c
	@ cc ${DEBUG_flags} ${SRC} -o bin/porporo
