# GEN2Lores — LORES 40×48 colour demo on the GEN2 card

*[← POM1 documentation index](../../../../doc/README.md)*

Tiny **C** program for Uncle Bernie's GEN2 colour card that exercises the
**LORES** mode: a **40×48 grid of 7px×4px blocks, 16 true colours**. Unlike
HIRES (where colour is an NTSC artifact of the bit pattern), LORES gives
**real per-block colour** — clean, no half-density, no carrier mask. The
demo:

1. displays the **16 palette colours** as vertical bars,
2. draws a **white frame** around the screen,
3. paints a **rainbow diagonal** block by block,

then holds the image by re-asserting LORES in a loop (instantaneous — this
also covers the DevBench's *deferred* card plug).

It's a minimal example of the **"Uncle Bernie GEN2 HGR (C)"** target: same
gen2c runtime under `dev/lib/gen2c` (per-family modules, see `gen2c.mk`;
LORES lives in `gen2_lores.c`) + the apple1c text base under `dev/lib/apple1c`
(`woz_mon`). LORES API used: `gen2_lores_init` / `gen2_lores_clear` /
`gen2_lores_setblock` / `gen2_lores_hlin` / `gen2_lores_vlin` (see
`dev/lib/gen2c/gen2.h`).

## Build

```sh
make            # -> "software/Graphic HGR/GEN2Lores.bin" (+ .txt Woz-hex)
make clean
```

The `Makefile` reuses the POM1 Bench `cl65` invocation: linker config
`dev/cc65/apple1_gen2_c.cfg` (code + C stack at `$6000-$BEFF`, above the GEN2
framebuffers) + the gen2c per-family modules + `gen2_blit.s` +
`apple1io.c`/`apple1io_asm.s`, origin `$6000`. The LORES page shares RAM with
the text page (`$0400-$07FF`).

## Run

- **DevBench → POM1 Bench**: new sketch, target *C / GEN2 HGR*, paste the
  source, compile, upload.
- **CLI**:
  ```sh
  build/POM1 --preset 11 \
      --load 6000:"software/Graphic HGR/GEN2Lores.bin" --run 6000
  ```
- Loading the `.bin`/`.txt` from `software/Graphic HGR/` auto-plugs the GEN2
  card and opens its window (preset 12 = Apple-1 + GEN2 HGR).

## Headless verification

The rendering can be checked without the GUI (same pixels as the display):

```sh
build/POM1 --preset 11 \
    --load 6000:"software/Graphic HGR/GEN2Lores.bin" --run 6000 \
    --dump-after-cycles 2000000 --dump-gen2-frame /tmp/lores.png
```

→ PNG 280×192: 16 sharp colour bars, white frame (top/bottom/left/right
edges at 255,255,255), rainbow diagonal. Zero NTSC artefact.
