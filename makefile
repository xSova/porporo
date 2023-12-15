SRC=src/uxn.c src/devices/system.c src/devices/console.c src/devices/screen.c src/devices/controller.c src/devices/mouse.c src/devices/file.c src/devices/datetime.c
TMP=src/roms/menu.c src/roms/potato.c src/roms/wallpaper.c
RELEASE_flags=-std=c89 -Os -DNDEBUG -g0 -s -Wall -Wno-unknown-pragmas
DEBUG_flags=-std=c89 -DDEBUG -Wall -Wno-unknown-pragmas -Wpedantic -Wshadow -Wextra -Werror=implicit-int -Werror=incompatible-pointer-types -Werror=int-conversion -Wvla -g -Og -fsanitize=address -fsanitize=undefined
ASM=bin/uxnasm
CLI=bin/uxncli

.PHONY: all dest run install uninstall format clean

all: bin/uxnasm bin/uxncli bin/porporo

run: all bin/log.rom
	@ bin/porporo bin/log.rom
install: all
	@ cp bin/porporo ~/bin/
uninstall:
	@ rm -f ~/bin/porporo
format:
	@ clang-format -i src/porporo.c src/devices/*
clean:
	@ rm -f bin/*
	@ rm -f src/roms/*

bin/uxnasm: src/uxnasm.c
	@ mkdir -p bin
	@ cc ${RELEASE_flags} src/uxnasm.c -o ${ASM}
bin/format-c.rom: src/utils/format-c.tal
	@ ${ASM} src/utils/format-c.tal bin/format-c.rom
bin/menu.rom: src/utils/menu.tal src/utils/menu.assets.tal
	@ ${ASM} src/utils/menu.tal bin/menu.rom
bin/potato.rom: src/utils/potato.tal src/utils/potato.assets.tal
	@ ${ASM} src/utils/potato.tal bin/potato.rom
bin/wallpaper.rom: src/utils/wallpaper.tal
	@ ${ASM} src/utils/wallpaper.tal bin/wallpaper.rom
bin/uxncli: src/uxncli.c
	@ cc ${RELEASE_flags} ${SRC} src/uxncli.c -o ${CLI}
roms: ${ASM} ${CLI} bin/format-c.rom bin/menu.rom bin/potato.rom bin/wallpaper.rom
	@ mkdir -p src/roms
	@ ${CLI} bin/format-c.rom bin/menu.rom > src/roms/menu.c
	@ ${CLI} bin/format-c.rom bin/potato.rom > src/roms/potato.c
	@ ${CLI} bin/format-c.rom bin/wallpaper.rom > src/roms/wallpaper.c
bin/porporo: roms ${SRC} ${TMP} src/porporo.c
	@ cc ${RELEASE_flags} ${SRC} ${TMP} src/porporo.c -L/usr/local/lib -lSDL2 -o bin/porporo
bin/log.rom: src/utils/log.tal src/utils/log.assets.tal
	@ ${ASM} src/utils/log.tal bin/log.rom
	