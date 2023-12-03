# Porporo

Porporo is a [Varvara](https://wiki.xxiivv.com/site/varvara.html) operating system, written in ANSI C.

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
