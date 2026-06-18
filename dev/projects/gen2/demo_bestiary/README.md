# HGR Bestiary

*[← POM1 documentation index](../../../../doc/README.md)*

A browser for the pre-baked **HGR sprite bestiary** (`dev/lib/gen2/sprites/`) on
Uncle Bernie's GEN2 Color Graphics Card. It pages through six of the
SCROLL-O-SPRITES fantasy-adventure categories — the GEN2 HGR mirror of the
TMS9918 sprites `TMS_Rogue` uses on the TMS side:

| Category | Sprites |
|----------|---------|
| CREATURES | 8 |
| TROLLKIND | 4 |
| UNLIVING  | 8 |
| FAUNA     | 13 |
| MAGICK    | 15 |
| MUSIC     | 6 |

Each category is drawn as a **6×4 grid** of its 16×16 sprites; the category name
prints to the Apple-1 text terminal. **Any key** advances to the next category
(wraps); **ESC** quits to Wozmon.

Before this, only `hgr_symbols` consumed one category of the shipped bestiary —
this gives six of the remaining categories a home in one program. (The library
ships 15 categories / 242 sprites = 11.3 KB; the 8 KB dual-bank hardware can't
hold them all at once, so this browser carries a curated fantasy subset that
fits the single 4 KB `$E000` bank. Swap the `cat_*` tables + the included
category `.asm`/`.inc` for a different selection.)

## Blit

Byte-aligned **STA fast path**: the sprites sit at HGR byte columns, so each
16×16 sprite is 16 rows × 3 bytes stored straight into the framebuffer through
the `hgr_lo`/`hgr_hi` scanline table — no per-pixel plotting, no bit shifting.
One generalised `draw_sprite` (category data base passed in zero page) serves
every category.

## Build / run

```
make            # -> "software/Graphic HGR/HGR_Bestiary.{bin,txt}"
make clean
```

Run in POM1 with the GEN2 card (preset 11): load the `.txt`, then `E000R`.
The intro/category text prints at emulated terminal speed, so the first grid
appears a moment after the text scrolls by.

## Source of truth

Sprite pixels are edited on the **TMS9918** side
(`dev/lib/tms9918/sprites_<cat>.asm`); `tools/build_hgr_sprites.py` regenerates
the HGR mirror this project includes. The Makefile carries refresh rules for the
six categories it uses.
