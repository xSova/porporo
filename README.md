# Porporo

Porporo is a [Varvara](https://wiki.xxiivv.com/site/varvara.html) operating system.

## Roms

- `menu.rom` spawned on right-click.
- `wallpaper.rom` locked by default on start, expects `.wallpaper`.
- `log.rom` normal.

## Controls

- `d`, draw mode.
- `m`, move mode.
- `F1`, lock varvara.
- `F2`, center varvara.
- `F4`, exit varvara.
- `F5`, soft-reboot varvara.

## TODOs

- File device pointers should be assoc with Varvaras
- Map connection data to porporo's ram
- Automatic reqdraw, use screen
- Fail if cannot find menu?

Window Manager
	- only draw program screens that are visible
	- Global pointer, Hide pointer
	- Transluscent windows

Menu.rom
	- Navigate into folders
	- MODE: Only display roms
	- MODE: All files mode

Log.rom
	- Toggle binary/ascii mode
	- Scroll buffer
	- Support theme

Misc
	- Fixed time program
