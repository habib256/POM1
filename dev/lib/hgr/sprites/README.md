# lib/hgr/sprites — SCROLL-O-SPRITES, GEN2 HGR flavor

Auto-generated mirror of `dev/lib/tms9918/sprites_*.asm`. Same labels, same
sprite catalog, but the bytes are pre-baked in the GEN2 HGR pixel format
(16 rows × 3 bytes = 48 B per sprite, bit 0 = leftmost pixel, bit 7 stays
clear for the NTSC group selector).

Regenerate via:

```
python3 tools/build_hgr_sprites.py             # all 14 categories
python3 tools/build_hgr_sprites.py --only fauna
python3 tools/build_hgr_sprites.py --check     # exit 1 if anything is stale
```

## Project integration pattern

Each category produces two files:

- `sprites_<cat>_hgr.asm` — sprite data with per-sprite labels (e.g.
  `sym_at_pat`, `fauna_dragon_pat`) plus a contiguous `<cat>_hgr_data`
  base label for index-based loops (`<cat>_hgr_data + idx*48`).
- `sprites_<cat>_hgr.inc` — companion header with immediate-mode constants
  (`<CAT>_HGR_COUNT`, `<CAT>_HGR_BYTES_PER_SPRITE`). Pulled in via
  `.include` because cc65 cannot resolve `.import`ed symbols in
  immediate addressing mode (`CMP #N`).

Recommended project Makefile + .asm pattern (see
`dev/projects/hgr_symbols/` for a working example):

```makefile
LIB := -I ../../lib/apple1 -I ../../lib/hgr -I ../../lib/hgr/sprites
```

```asm
.include "sprites_<cat>_hgr.inc"          ; constants
.include "sprites/sprites_<cat>_hgr.asm"  ; data block, inlined
```

The two libraries (`dev/lib/tms9918/sprites_<cat>.asm` for TMS9918, this
directory for HGR) intentionally share the per-sprite label set so a
project can swap targets without touching its rendering code — only the
included file changes. Linking BOTH at once will trigger duplicate-symbol
errors on every shared label; pick one per build.

## Why HGR sprites are pre-baked

The TMS9918 stores 16×16 sprites as four 8×8 quarter-blocks (TL/BL/TR/BR,
32 B total). HGR uses a row-major scanline layout with a 7-pixel-per-byte
NTSC artifact-color encoding (bit 7 reserved). On-the-fly conversion in
6502 would burn cycles and code on every blit, and a 6502 cannot do it
faster than a precomputed table. Bake at build time, blit at runtime.

The TMS source remains the canonical edit point — when a sprite's pixels
change, edit the `.asm` under `dev/lib/tms9918/sprites_*.asm`, then rerun
`tools/build_hgr_sprites.py` to refresh this directory.
