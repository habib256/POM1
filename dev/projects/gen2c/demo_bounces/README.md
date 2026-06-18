# GEN2Bounces — fast XOR sprites + vectors + HUD (double buffering)

*[← POM1 documentation index](../../../../doc/README.md)*

A **C** demo combining five `dev/lib/gen2c` building blocks to show how to
animate full-screen fast on the GEN2 card: **four balls** (one large 48×48,
three small 16×16) bounce inside a frame **and collide pairwise**, with a
bounce counter HUD and an 8×8 caption.

Building blocks:

- **Fast XOR sprites (B)** — `gen2_hgr_blit7(..., GEN2_XOR)`. The balls are
  pre-packed in the **7px/byte** framebuffer format, so the blit XORs **whole
  bytes** instead of pixels. Erase = re-blit XOR at the same spot (background
  restored pixel-perfect). Trade-off: x is aligned to 7px (horizontal step of 7).
- **Double buffering (C)** — `gen2_set_draw_page` / `gen2_show_page`: draw the
  frame on the hidden page, then flip.
- **HUD number + 8×8 text (D)** — `gen2_hgr_putu_field` (auto-erasing
  fixed-width counter) + `gen2_hgr_puts8` (dense caption in the native 8×8 font).
- **Vectors (E)** — `gen2_hgr_rect` + `gen2_hgr_line` for the scenery.

## The three speed levers

1. **Don't redraw the scenery**: frame, separator, label, caption drawn ONCE
   per page.
2. **XOR erase** of balls: no clear-box to scrub.
3. **Byte-aligned blit** (`gen2_hgr_blit7`): ~7× fewer writes than a
   pixel-by-pixel blit. Measured on one scene (large + small ball):
   **42 → 223 frames** for the same cycle budget, i.e. **×5.3**.

## Ball-vs-ball collision

For each pair: if centres are closer than `R_i + R_j` **and** are approaching
(`dx·Δvx + dy·Δvy < 0`), swap velocities. All balls move at the same speed, so
swapping preserves the byte alignment.

## Build / Run

```sh
make            # -> "software/Graphic HGR/GEN2Bounces.bin" (+ .txt Woz-hex)
make clean
```

```sh
build/POM1 --preset 11 \
    --load 6000:"software/Graphic HGR/GEN2Bounces.bin" --run 6000
```

or DevBench → POM1 Bench → target *C / GEN2 HGR*, paste, compile, upload.
