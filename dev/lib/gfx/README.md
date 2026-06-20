# dev/lib/gfx — card-neutral graphics primitives (factoring axis 1)

*[← POM1 documentation index](../../../doc/README.md)*

One implementation of the **card-independent** 2D logic shared by the GEN2 HGR
card and the P-LAB TMS9918 card. Only the per-pixel store and the screen width
differ between the two; the geometry and the integer→ASCII conversions are
identical, and were previously written twice (geometry) or three times
(numbers). This is **axis 1** of the GEN2/TMS9918 library factoring.

## Why a link-time backend (not a function pointer)

Parmigiani's *"one board at a time"* rule (CLAUDE.md) means a single binary ever
talks to exactly **one** video card. So the backend symbols (`gfx_plot`,
`gfx_hline`, `gfx_vline`, `gfx_width`, `gfx_height`) are resolved by the **linker**
to the one card the program links against — a direct `JSR`, no per-pixel
indirection, no runtime dispatch. A GEN2 program links `gfx_backend_gen2`; a
TMS9918 program links `gfx_backend_tms`. They never coexist, so there is no
ambiguity to resolve at runtime.

## Files

| File | Role |
|------|------|
| `gfx.h` | card-neutral API + the backend contract |
| `gfx_line.c` | `gfx_line` (shared geometry) |
| `gfx_rect.c` | `gfx_rect` (shared geometry) |
| `gfx_circle.c` | `gfx_circle` (shared geometry) |
| `gfx_ellipse.c` | `gfx_ellipse` (shared geometry) |
| `gfx_num_dec.c` | `gfx_utoa` / `gfx_itoa` (shared int→ASCII, decimal) |
| `gfx_num_hex.c` | `gfx_hexstr` (shared int→ASCII, hex, divide-free) |
| `gfx_text.c` | **axis 3** — shared 8×8 cell-cursor façade (`gfx_gotoxy` / `gfx_putc` / `gfx_text` / `gfx_putu` / `gfx_puti` / `gfx_putx`); owns the cursor + advance/wrap, draws via the per-card cell backend |
| `gfx_text_backend_gen2.c` | GEN2 cell backend — `gfx_cell_glyph` → `gen2_hgr_puts8` (native 8×8), 35×24; `gfx_cell_color` is a no-op (8×8 cells are white) |
| `gfx_text_backend_tms.c` | TMS9918 cell backend — `gfx_cell_glyph` → `screen2_putc`, 32×24; `gfx_cell_color` maps to the per-cell `FG_BG` attribute |
| `gfx_backend_gen2.c` | GEN2 backend — wraps `gen2_hgr_plot/hline/vline`, 280×192 |
| `gfx_backend_gen2_rect.c` | GEN2 `gfx_filled_rect` / `gfx_clear` — wraps `gen2_hgr_fill_pixrect` / `gen2_hgr_clear` (split out so a lines-only program dead-strips the fill path) |
| `gfx_backend_tms.c` | TMS9918 backend — wraps `screen2_plot`, 256×192 |
| `gfx_backend_tms_rect.c` | TMS9918 `gfx_filled_rect` / `gfx_clear` — wraps `screen2_filled_rect` / `screen2_clear` (same dead-strip split; `gfx_clear` ignores `color`, TMS bitmap clear is always 0) |

Each program links **only the `gfx_*` modules it references (ld65 dead-strips
the rest) + exactly one backend**.

## Positioned text — the axis-3 cell-cursor façade (`gfx_text.c`)

`gfx_text.c` adds a card-NEUTRAL 8×8 **cell cursor** so a program can position
text/numbers and compile for either card by backend choice alone:

```c
gfx_gotoxy(2u, 2u);  gfx_text("SCORE ");  gfx_putu(score);   /* same on both cards */
```

The shared layer owns the cursor, the advance/wrap, and the number formatting
(via `gfx_utoa`/`gfx_itoa`/`gfx_hexstr`); each card supplies just `gfx_cell_glyph`
(one 8×8 blit) + `gfx_cell_color` + the `gfx_text_cols`/`gfx_text_rows` extent.
GEN2 maps a cell to `gen2_hgr_puts8` (35×24); TMS to `screen2_putc` (32×24).
The `sketchs/portable/hello_gfx_text/` demo builds the SAME source for both
cards and pins the façade.

It is **additive and neutral**, NOT a replacement — it does not "level down" the
rich per-card text:

- **Colour.** TMS honours a per-cell `FG_BG` attribute via `gfx_cell_color`; GEN2
  8×8 cells are white (its colour is the NTSC artifact trick on the 16×16 doubled
  glyphs — reach for `gen2_hgr_puts_color` directly).
- **GEN2's hot HUD path.** `gen2_hgr_putu_field` keeps its hand-written asm
  `gen2_utoa` (avoids cc65's runtime 16-bit soft-divide). Do **not** reroute it
  through `gfx_putu`/`gfx_utoa` — that would regress the flicker-free HUD primitive.
- **16×16 doubled glyphs** (`gen2_hgr_puts`) and TMS **name-table text mode** stay
  card-specific; the façade is the lowest common denominator (monospaced 8×8).

## What stays per-card (deliberately NOT unified)

- Card-specific strengths untouched: GEN2 double-buffering / NTSC colour / blit /
  16×16 glyphs, TMS hardware sprites / collision / true colour. The façade above
  is the ONLY text unification — and it is additive (the rich APIs are intact).

## Rewiring (the de-duplication) — DONE 2026-06-17

The card libs now forward to `gfx_*`; their local copies are gone. The old
public names are kept as thin wrappers, so existing programs compile unchanged —
they just need `gfx-<card>.lib` on the link line now (see Build integration).

1. **GEN2 — `dev/lib/gen2c/gen2_geom.c`:** `gen2_hgr_line` / `gen2_hgr_rect` /
   `gen2_hgr_circle` are one-line forwards to `gfx_line` / `gfx_rect` /
   `gfx_circle` (pixel-identical — `gfx_line.c` / `gfx_rect.c` / `gfx_circle.c`
   were ported verbatim from them); the local `gen2_hgr_plot_clip` was removed
   (`gfx_circle` clips internally to the same bounds). `gen2_hgr_putx` builds via
   `gfx_hexstr` then `gen2_hgr_puts`. **Kept:** `gen2_hgr_hline` / `gen2_hgr_vline`
   (they ARE the backend) and the asm `gen2_utoa` for `gen2_hgr_putu_field`
   (flicker-free HUD). **Added:** `gen2_hgr_ellipse` → `gfx_ellipse`.
2. **TMS — `dev/lib/tms9918c/screen2_geom.c`:** `screen2_line` /
   `screen2_circle` / `screen2_ellipse_rect` forward to `gfx_line` / `gfx_circle`
   / `gfx_ellipse`; the local `kEllipseCos64/Sin64` tables, `math_abs`, and
   `screen2_clamp_*` were removed (now in `gfx_line.c` / `gfx_circle.c` /
   `gfx_ellipse.c`). `printlib.c` dec/hex route through `gfx_utoa` /
   `gfx_hexstr` (the `pl_putc_fn` wrappers stay; hex is now minimal-width, which
   matches `printlib.h`'s documented "no leading zeros" contract).
   `screen2_filled_rect` (in `screen2_ext.c`) was rewritten as a byte-aligned fast
   fill (axis 1.5, done) — the TMS analogue of GEN2's `fill_pixrect`.

**Cost note.** When the card libs were monolithic (`gen2.c` / `screen2.c`),
forwarding pulled the referenced `gfx_*` modules into every program. The
per-family split (`gen2_geom.c` / `screen2_geom.c` as their own `.o`) now lets
ld65 dead-strip the geometry forwards in a program that never draws vectors.
On GEN2 the monolithic cost was ≈ **+1.5 KB/binary** (the `gen2_hgr_ellipse` wrapper always drags
`gfx_ellipse`'s soft 16-bit mul/div + cos/sin tables; `gen2_hgr_putx` dragged the
decimal soft-`/10` of `gfx_utoa`/`itoa`, which are dead in GEN2 since it keeps
its asm `gen2_utoa`). Splitting `gfx_hexstr` into its own divide-free module
(`gfx_num_hex.c`, separate from `gfx_num_dec.c`) recovered that part: `gen2_hgr_putx`
now drags only `gfx_num_hex.c` and leaves the decimal soft-`/10` dead-stripped.
Everything still fits (no ROM overflow).

**Behaviour.** GEN2 output is pixel-identical (verbatim ports). On TMS,
`gfx_line`/`gfx_circle` are the GEN2-derived Bresenham variant (not screen2's
original): lines are visually identical for on-screen endpoints, circles may
differ by ≤1 px at some octant boundaries; the ellipse is byte-identical
(`gfx_ellipse` was ported from `screen2.c`).

## Build integration

The layer ships as **two cc65 library archives** built by the `Makefile` here:

```bash
make -C dev/lib/gfx           # -> gfx-gen2.lib + gfx-tms.lib
make -C dev/lib/gfx check     # compile-verify every TU against both backends
make -C dev/lib/gfx clean
```

**Why an archive, not a list of `.c` on the link line.** `ld65` pulls only the
modules a program *references* out of a `.lib`. The card libs now forward to
`gfx_*` (see Rewiring below), so a program references exactly the `gfx_*` modules
its draw calls reach and ld65 dead-strips the rest. Force-listing the `.c` sources
instead force-links the whole layer — **+2888 bytes** of dead code (cc65's soft
mul/divide for the ellipse + `gfx_num`). The per-family split (`gfx_num_hex.c`
separate from `gfx_num_dec.c`; `gfx_backend_*_rect.c` separate from the line
backend) keeps a lines-only or hex-only program from dragging the decimal
soft-`/10` or the fill path.

To wire a program:

- Add `-I dev/lib/gfx` to its cc65 include path (so it can `#include "gfx.h"`).
- Put the matching archive **after** the program's own sources on the cl65/ld65
  line: GEN2 → `gfx-gen2.lib`; TMS9918 bitmap → `gfx-tms.lib`.
- GEN2 builds still need `-I dev/lib/gen2c`; TMS builds `-I dev/lib/tms9918c`.

Wired touch points (copy these as the template for the rest):

- **`src/Pom1BenchHost.cpp`** — the Bench's *Uncle Bernie GEN2 HGR (C)* line.
  The Bench compiles the gfx sources **from source** (like `gen2.c`) so edits
  apply live and a sketch can immediately `#include "gfx.h"` and draw vectors;
  its binaries are throwaway, so the dead-code trade-off doesn't matter there.
- **`sketchs/gen2/demo_bounces/Makefile`** — GEN2 example (links `gfx-gen2.lib`).
- **`sketchs/tms9918/nino-democ/`** — TMS C menu demo (links `gfx-tms.lib` via DevBench).
- `dev/lib/gfx/Makefile` builds both archives via `ar65` (run from this dir).

## Verification

Compile-verified 2026-06-16 (cc65 6502, Homebrew):

- `make check` builds both archives → every shared TU compiles against both backends.
- End-to-end link smoke passed for **both** cards: a consumer that references
  `gfx_line`/`rect`/`circle`/`ellipse` + `gfx_utoa`/`itoa`/`hexstr` links cleanly
  against the GEN2 runtime (`gen2.c` → `gfx_plot`/`hline`/`vline` resolve to
  `gen2_hgr_plot`/…) and against the TMS runtime (`screen2.c` → `screen2_plot`),
  with no unresolved or duplicate symbols.

Done (2026-06-17): `gen2_hgr_line` / `gen2_hgr_rect` / `gen2_hgr_circle` /
`gen2_hgr_ellipse` (in `gen2_geom.c`) and `screen2_line` / `screen2_circle` /
`screen2_ellipse_rect` (in `screen2_geom.c`) now forward to `gfx_*`; the gen2c and
tms9918c demos link `gfx-<card>.lib` and the `make -C dev/projects` gate is green.
When touching the shared geometry, re-run `tools/test_gfx_regress.py` (pixel-identical
output) and `ctest -R gfx` plus the `gen2_*` / `tms9918_*` smokes.
