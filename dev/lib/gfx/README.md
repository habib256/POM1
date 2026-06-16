# dev/lib/gfx — card-neutral graphics primitives (factoring axis 1)

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
| `gfx_draw.c` | `gfx_line` / `gfx_rect` / `gfx_circle` / `gfx_ellipse` (shared) |
| `gfx_num.c` | `gfx_utoa` / `gfx_itoa` / `gfx_hexstr` (shared string builders) |
| `gfx_backend_gen2.c` | GEN2 backend — wraps `gen2_hgr_plot/hline/vline`, 280×192 |
| `gfx_backend_tms.c` | TMS9918 backend — wraps `screen2_plot`, 256×192 |

Each program links **`gfx_draw.o` + `gfx_num.o` + exactly one backend**.

## What stays per-card (deliberately NOT unified here)

- **Number *drawing position*.** GEN2 draws digits as a pixel-addressed graphics
  font (`gen2_hgr_puts` at x,y); TMS bitmap draws at 8px char cells
  (`screen2_putc`); TMS text mode at name-table cells. Only the *conversion*
  (`gfx_utoa`/`gfx_hexstr`) is shared — each card still draws the string.
- **GEN2's hot HUD path.** `gen2_hgr_putu_field` keeps its hand-written asm
  `gen2_utoa` (avoids cc65's runtime 16-bit soft-divide). Do **not** reroute it
  through `gfx_utoa` — that would regress the flicker-free HUD primitive.
- Card-specific strengths untouched: GEN2 double-buffering / NTSC colour / blit,
  TMS hardware sprites / collision / true colour. Those are axis 3 territory.

## Rewiring checklist (the actual de-duplication — DO AFTER a clean build)

The files above are **additive**: until the steps below are applied, the card
libs still carry their own copies, so nothing is broken but nothing is removed
yet. To complete the factoring (remove the duplicates), once the toolchain is
available and a sample build is green:

1. **GEN2 — `dev/lib/gen2c/gen2.c`:** replace the bodies of `gen2_hgr_line`,
   `gen2_hgr_rect`, `gen2_hgr_circle` with one-line forwards to `gfx_line` /
   `gfx_rect` / `gfx_circle` (include `gfx.h`). Keep `gen2_hgr_hline` /
   `gen2_hgr_vline` as the real primitives (they ARE the backend). `gen2_hgr_putx`
   → build via `gfx_hexstr` then `gen2_hgr_puts`. Optionally add a `gen2_hgr_ellipse`
   thin wrapper (new capability).
2. **TMS — `dev/apple1-videocard-lib/lib/screen2.c`:** replace `screen2_line`,
   `screen2_circle`, `screen2_ellipse_rect` bodies with forwards to `gfx_line` /
   `gfx_circle` / `gfx_ellipse`. Drop the local `kEllipseCos64/Sin64` tables and
   `math_abs` (now in `gfx_draw.c`). `screen2_filled_rect` can stay but the
   fast-fill gap is an axis-1.5 follow-up (GEN2 has `fill_pixrect`; TMS does not).
   Route `printlib` decimal/hex through `gfx_utoa`/`gfx_hexstr` (keep the
   `pl_putc_fn` callback wrappers).
3. Keep the old public names as wrappers so existing programs/demos compile
   unchanged (`gen2_hgr_line`, `screen2_line`, …).

## Build integration

The layer ships as **two cc65 library archives** built by the `Makefile` here:

```bash
make -C dev/lib/gfx           # -> gfx-gen2.lib + gfx-tms.lib
make -C dev/lib/gfx check     # compile-verify every TU against both backends
make -C dev/lib/gfx clean
```

**Why an archive, not a list of `.c` on the link line.** `ld65` pulls only the
modules a program *references* out of a `.lib`. Until the destructive rewiring
lands (`gen2.c` / `screen2.c` forwarding to `gfx_*`), nothing references
`gfx_line`/`gfx_circle`/`gfx_utoa`/…, so linking `gfx-<card>.lib` adds **0 bytes**
(measured: `GEN2Snake.bin` and the videocard `demo.bin` are byte-identical with
vs without the lib on their link line). Force-listing the `.c` sources instead
force-links the whole layer — **+2888 bytes** of dead code (cc65's soft
mul/divide for the ellipse + `gfx_num`). So the archive is the only *purely
additive* way to wire gfx into a size-sensitive build path before rewiring.

To wire a program:

- Add `-I dev/lib/gfx` to its cc65 include path (so it can `#include "gfx.h"`).
- Put the matching archive **after** the program's own sources on the cl65/ld65
  line: GEN2 → `gfx-gen2.lib`; TMS9918 bitmap → `gfx-tms.lib`.
- GEN2 builds still need `-I dev/lib/gen2c`; TMS builds `-I dev/apple1-videocard-lib/lib`.

Wired touch points (copy these as the template for the rest):

- **`src/Pom1BenchHost.cpp`** — the Bench's *Uncle Bernie GEN2 HGR (C)* line.
  The Bench compiles the gfx sources **from source** (like `gen2.c`) so edits
  apply live and a sketch can immediately `#include "gfx.h"` and draw vectors;
  its binaries are throwaway, so the dead-code trade-off doesn't matter there.
- **`dev/projects/gen2_vectors_demo/Makefile`** — GEN2 example (links `gfx-gen2.lib`).
- **`dev/apple1-videocard-lib/demos/demo/Makefile`** — TMS example (links `gfx-tms.lib`);
  `dev/apple1-videocard-lib/Makefile` exposes `make gfx` to build the TMS archive.

## Verification

Compile-verified 2026-06-16 (cc65 6502, Homebrew):

- `make check` builds both archives → every shared TU compiles against both backends.
- End-to-end link smoke passed for **both** cards: a consumer that references
  `gfx_line`/`rect`/`circle`/`ellipse` + `gfx_utoa`/`itoa`/`hexstr` links cleanly
  against the GEN2 runtime (`gen2.c` → `gfx_plot`/`hline`/`vline` resolve to
  `gen2_hgr_plot`/…) and against the TMS runtime (`screen2.c` → `screen2_plot`),
  with no unresolved or duplicate symbols.

Still open (the *destructive* rewiring — separate TODO items, NOT done here):
swap `gen2_hgr_line`/`screen2_line` bodies to forward to `gfx_*` and run
`tools/test_gfx_regress.py` to confirm pixel-identical output, then `ctest -R gfx`
plus the `gen2_*` / `tms9918_*` smokes stay green.
