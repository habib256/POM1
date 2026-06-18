# `_template_tms9918c` — minimal TMS9918 sprite C starter

The smallest TMS9918 program with sprites in C, using the no-tearing shadow
workflow from the start. About 18 lines of `main()`.

## Build

```sh
make
make clean
```

## Run

POM1 → preset 9 (TMS9918 CodeTank) → Load Memory → `main.bin`, then type
`4000R` in Wozmon.

## Why the shadow workflow

`tms_set_sprite()` writes one attribute at a time to VRAM `$3B00`. If the
beam happens to read the SAT mid-write, you see torn sprites. The shadow API
(`tms_shadow_set` / `tms_shadow_move` / `tms_shadow_clear`) updates a 128-byte
RAM mirror instead, and `tms_shadow_flush()` writes the whole table in one
burst (`tms_fast.s`). Combined with a VBlank wait, it's flicker-free.

A beginner should NEVER call `tms_set_sprite()` per frame.

## Linker variables

The Makefile only links the families this program calls:
`TMS9918C_CORE_SRCS + SCREEN1_SRCS + SPRITES_SRCS + APPLE1_SRCS`. `SCREEN2`,
`VSYNC`, `PRINTLIB`, `RANDOM`, `INTERRUPT` and `SCREEN_EXT` are skipped. See
`dev/lib/tms9918c/tms9918c.mk` for every variable.

## Further reading

- `dev/Programming_C_Quickstart.md` — beginner cheat sheet.
- `dev/Programming_TMS9918C.md` — TMS9918 C reference, including the
  silicon-handling that leaks through (TMS_IO_DELAY, silicon-strict mode).
- `dev/lib/tms9918c/sprite_shadow.h` — shadow API docs.
