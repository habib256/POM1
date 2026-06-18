# _template — minimal Apple-1 "hello world" (copy me)

*[← POM1 documentation index](../../doc/README.md)*

The smallest useful Apple-1 program, in both **assembly** and **C**. Copy this
whole folder, rename, and start editing. Sorts to the top of `dev/projects/`
thanks to the leading `_`.

## Assembly (`Hello.asm`)

```bash
make                    # -> ../../../software/Apple-1 demos/Hello.bin
```

By hand:

```bash
ca65 -I ../lib/apple1 Hello.asm -o Hello.o
ld65 -C ../cc65/apple1_4k.cfg Hello.o -o Hello.bin
```

Run in POM1: **File > Load Memory > Hello.bin**, then `280R`.

## C (`hello.c`)

```bash
cl65 -t none -Oirs -C ../cc65/apple1_c.cfg -I ../lib/apple1c \
     hello.c ../lib/apple1c/apple1io.c ../lib/apple1c/apple1io_asm.s \
     -o hello.bin
```

Run in POM1: **File > Load Memory > hello.bin**, then `0300R`.

## Even easier — the POM1 Bench

Skip the command line: *DevBench → POM1 Bench*, **New → Apple-1 dual 4K/8K (text)
— start here**, then **Upload**. The Bench drops in the same starter and runs it
in one click (it needs `cc65` installed — it'll tell you how if it's missing).

## Next steps

- Assembly → [`dev/Programming_Apple1_ASM.md`](../../Programming_Apple1_ASM.md),
  playbook [`dev/APPLE1DEV.md`](../../APPLE1DEV.md).
- C → [`dev/Programming_Apple1_C.md`](../../Programming_Apple1_C.md).
- Add graphics: HGR (`dev/lib/gen2c/`), TMS9918 (`dev/lib/tms9918c/`),
  or GT-6144 (`dev/projects/gt6144/gt6144_demo_hello/`).

## Hardware

- Machine: Apple 1 (stock 4 KB is enough)
- Cards: none
- Recommended POM1 preset: **4** (Apple-1 with ACI & BASIC cassette) — any text preset works.

## License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository [LICENSE](../../../LICENSE)).
