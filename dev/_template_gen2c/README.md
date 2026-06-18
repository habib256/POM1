# `_template_gen2c` — minimal GEN2 HGR C starter

The smallest GEN2 HGR program in C. About 10 lines of `main()`. Copy this
folder to start a new colour-graphics project.

## Build

```sh
make            # builds main.bin (origin $6000)
make clean
```

## Run

POM1 → preset 11 (Uncle Bernie GEN2 HGR Color) → Load Memory → `main.bin`,
then type `6000R` in Wozmon.

## What's tiny here

The Makefile links **only the GEN2 families this program calls**: CORE +
TEXT + RECT. ld65 strips at the `.o` file granularity, so dropping the
PIXEL / SPRITES / GEOM / LORES families saves real bytes vs the all-in-one
`GEN2C_ALL_SRCS` variable. See `dev/lib/gen2c/gen2c.mk` for the list.

If you need pixel plotting or sprite blits later, swap the SRCS line in the
Makefile for the umbrella variant (commented in the Makefile).

## Further reading

- `dev/Programming_C_Quickstart.md` — beginner cheat sheet (function
  chooser, top pitfalls).
- `dev/Programming_GEN2C.md` — GEN2 C reference.
- `dev/lib/gen2c/gen2.h` — the in-source function chooser comment block.
