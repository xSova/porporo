SRC=src/uxn.c src/devices/system.c src/devices/console.c src/devices/screen.c src/devices/controller.c src/devices/mouse.c src/devices/file.c src/devices/datetime.c
TMP=src/roms/menu.c src/roms/potato.c src/roms/wallpaper.c
RELEASE_flags=-std=c89 -Os -DNDEBUG -g0 -s -Wall -Wno-unknown-pragmas
DEBUG_flags=-std=c89 -DDEBUG -Wall -Wno-unknown-pragmas -Wpedantic -Wshadow -Wextra -Werror=implicit-int -Werror=incompatible-pointer-types -Werror=int-conversion -Wvla -g -Og -fsanitize=address -fsanitize=undefined
ASM=bin/uxnasm
CLI=bin/uxncli

.PHONY: all dest run lint install uninstall format clean

all: bin/uxnasm bin/uxncli bin/porporo

run: all bin/log.rom
	@ bin/porporo bin/log.rom

install: all
	@ cp bin/porporo ~/bin/

uninstall:
	@ rm -f ~/bin/porporo

format:
	@ clang-format -i src/porporo.c src/devices/*

lint:
	@ ${CLI} ~/roms/uxnlin.rom src/core/menu.tal
	@ ${CLI} ~/roms/uxnlin.rom src/core/wallpaper.tal
	@ ${CLI} ~/roms/uxnlin.rom src/core/potato.tal
	@ ${CLI} ~/roms/uxnlin.rom src/core/log.tal

clean:
	@ rm -f bin/*
	@ rm -f src/roms/*

bin/uxnasm: src/utils/uxnasm.c
	@ mkdir -p bin
	@ cc ${RELEASE_flags} src/utils/uxnasm.c -o ${ASM}
	
bin/uxncli: src/uxncli.c
	@ mkdir -p src/roms
	@ cc ${RELEASE_flags} ${SRC} src/uxncli.c -o ${CLI}

bin/format-c.rom: src/utils/format-c.tal
	@ ${ASM} src/utils/format-c.tal bin/format-c.rom
	
src/roms/menu.c: bin/format-c.rom src/core/menu.tal src/core/menu.assets.tal
	@ ${ASM} src/core/menu.tal bin/menu.rom
	@ ${CLI} bin/format-c.rom bin/menu.rom > src/roms/menu.c
	
src/roms/potato.c: bin/format-c.rom src/core/potato.tal src/core/potato.assets.tal
	@ ${ASM} src/core/potato.tal bin/potato.rom
	@ ${CLI} bin/format-c.rom bin/potato.rom > src/roms/potato.c
	
src/roms/wallpaper.c: bin/format-c.rom src/core/wallpaper.tal
	@ ${ASM} src/core/wallpaper.tal bin/wallpaper.rom
	@ ${CLI} bin/format-c.rom bin/wallpaper.rom > src/roms/wallpaper.c
	
bin/porporo: ${SRC} ${TMP} src/porporo.c
	@ cc ${RELEASE_flags} ${SRC} src/porporo.c -L/usr/local/lib -lSDL2 -o bin/porporo
	
bin/log.rom: src/core/log.tal src/core/log.assets.tal
	@ ${ASM} src/core/log.tal bin/log.rom
	