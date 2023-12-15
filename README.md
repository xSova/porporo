# Porporo

Porporo is an experimental operating system specification for [Varvara](https://wiki.xxiivv.com/site/varvara.html), written in TAL and ANSI C. This is a work in progress, for more details [follow the development during december](https://rabbits.srht.site/decadv/).

## Build

To build the Uxn emulator, you must install [SDL2](https://wiki.libsdl.org/) for your distro. If you are using a package manager:

```sh
sudo pacman -Sy sdl2             # Arch
sudo apt install libsdl2-dev     # Ubuntu
sudo xbps-install SDL2-devel     # Void Linux
brew install sdl2                # OS X
```

To build Porporo and the required roms:

```
make run
```

Since parts of Porporo are built with uxn itself, we need to have a partial implementation of the varvara ecosystem(`src/uxncli`), and an assembler(`src/uxnasm`). Building porporo begins by building these two tools.

## Controls

- `F1`, lock varvara.
- `F2`, center varvara.
- `F4`, exit varvara.
- `F5`, soft-reboot varvara.
- `d`, draw mode.
- `m`, move mode.

### Menu.rom

- `tab`, see all files.
- `esc`, exit.
- `mouse1`, or `enter`, run file.
- `mouse2`m or `shift+enter`, send filename.

## Wallpaper

The wallpaper rom expects a file named `.wallpaper` in the [chr format](https://wiki.xxiivv.com/site/chr_format.html) that is large enough to fill the screen. The way I do it is by, first getting the screen size(let's say `1328x640`), creating a `wallpaper.tga` image at that size, converting it with [tgachr](https://git.sr.ht/~rabbits/uxn-utils/tree/main/item/cli/tgachr/tgachr.tal), generating a file named `wallpapera6x50.chr`, and finally renaming it to `.wallpaper`. Voila!

```
uxncli ~/roms/tgachr.rom wallpaper.tga
mv wallpapera6x50.chr .wallpaper
```

## Messages

Porporo listens special console messages types:

- `0xff` run command
- `0xfe` set action type

## Potato

Potato is the menubar rom that holds some of the state of porporo in its zero-page:

- `0x00*` action vector
- `0x02` action value 

