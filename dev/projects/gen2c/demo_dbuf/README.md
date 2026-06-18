# GEN2DBuf — double buffering (PAGE2) on the GEN2 card

*[← POM1 documentation index](../../../../doc/README.md)*

Tiny **C** program that exercises the **double buffering** of Uncle Bernie's
GEN2 colour card: a 16×16 block bounces across the entire HIRES 280×192
screen **without flicker or tearing**.

The card has two HIRES framebuffers — page 1 (`$2000`) and page 2 (`$4000`).
Instead of drawing on the visible page (you'd see the half-rendered frame),
we draw the next frame into the **hidden** page and then flip the display to
it. The viewer only ever sees complete frames.

API used (see `dev/lib/gen2c/gen2.h`):

- `gen2_set_draw_page(p)` — picks where **all** primitives write (page 1 or
  2). Set **once per frame**: this re-derives the scanline tables (the
  per-pixel hot paths themselves do not cost a single extra cycle).
- `gen2_show_page()` — displays the current draw page (the `$C254`/`$C255`
  soft switch).

The loop: `set_draw_page(hidden)` → erase + draw the frame → `show_page()`
→ swap the page. The mode (graphics/hires/full) is re-asserted each frame,
which also covers the DevBench's deferred card plug.

## Build

```sh
make            # -> "software/Graphic HGR/GEN2DBuf.bin" (+ .txt Woz-hex)
make clean
```

The `Makefile` reuses the POM1 Bench `cl65` invocation: linker config
`dev/cc65/apple1_gen2_c.cfg` (code + C stack at `$6000-$BEFF`, above **both**
GEN2 framebuffers `$2000`/`$4000`) + the gen2c per-family modules + the
shared `gen2_blit.s` + `apple1io.c`/`apple1io_asm.s`, origin `$6000`.

## Run

- **DevBench → POM1 Bench**: new sketch, target *C / GEN2 HGR*, paste the
  source, compile, upload.
- **CLI**:
  ```sh
  build/POM1 --preset 11 \
      --load 6000:"software/Graphic HGR/GEN2DBuf.bin" --run 6000
  ```

## Tuning

- `STEP` (pixels/frame) speeds the block up or down.
- `BALL` (block size); bounce limits `XMAX`/`YMAX` are derived from it.

Double buffering also works in **LORES** (`gen2_set_draw_page` redirects
both HIRES and LORES primitives); HIRES is the most visually striking, so
that's what's shown here.
