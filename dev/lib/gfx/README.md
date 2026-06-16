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

## Build integration (per project)

- Add `dev/lib/gfx` to the cc65 include path (`-I dev/lib/gfx`).
- Compile `gfx_draw.c` + `gfx_num.c` + the matching `gfx_backend_*.c`.
- GEN2 projects also need `-I dev/lib/gen2c`; TMS projects `-I dev/apple1-videocard-lib/lib`.
- Touch points that build these libs today:
  - the Bench cl65 line in `src/Pom1BenchHost.cpp` (compiles `gen2.c`);
  - per-project Makefiles / `emit_*` scripts under `dev/projects/*`;
  - `dev/apple1-videocard-lib/Makefile`.

## Verification (REQUIRED before claiming axis 1 done)

This code is **not yet compile-verified** — it was authored while the sandbox
shell was unavailable. Before relying on it:

```bash
# 1. assemble the shared TUs against each backend (needs cc65: cl65/ca65/ld65)
cl65 -t none -c -I dev/lib/gfx -I dev/lib/gen2c \
     dev/lib/gfx/gfx_draw.c dev/lib/gfx/gfx_num.c dev/lib/gfx/gfx_backend_gen2.c
cl65 -t none -c -I dev/lib/gfx -I dev/apple1-videocard-lib/lib \
     dev/lib/gfx/gfx_draw.c dev/lib/gfx/gfx_num.c dev/lib/gfx/gfx_backend_tms.c
# 2. build a sample GEN2 project (e.g. a1_crazycycle) and a TMS bitmap demo,
#    swapping their line/circle calls to gfx_* ; run the gfx regression test
#    (tools/test_gfx_regress.py) to confirm pixel-identical output.
# 3. ctest -R gfx   (and the gen2_* / tms9918_* smokes) must stay green.
```
