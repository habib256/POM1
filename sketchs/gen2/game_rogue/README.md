# game_rogue (GEN2) — Roguelike for Uncle Bernie's GEN2 HGR card

*[← POM1 documentation index](../../doc/README.md)*

HGR port of the P-LAB TMS9918 roguelike
([`sketchs/tms9918/game_rogue/`](../../tms9918/game_rogue/README.md) — read
that README for the full gameplay reference: MVP1-MVP5 features, controls,
items, XP progression, boss fight). The game logic — dungeon generation,
Bergström recursive-shadowcasting FOV, monster AI, combat, the 26-slot
inventory, timed buffs, hidden pits, the depth-13 demon — is carried over
**verbatim**; only the video layer was rewritten for the 280×192 HGR bitmap.

Runs on the "GEN2 HGR Color" 48 KB machine (POM1 preset 11) as a single
binary at `$6000` (chess model), drawing into HGR page 1. Entry: `6000R`.

## What changed vs the TMS9918 version

| | TMS9918 | GEN2 HGR |
|---|---|---|
| Playfield cell | 16×16 px (2×2 name-table chars) | 14×16 px (2 HGR bytes × 16 rows) |
| Text | name-table font chars | 8×8 `bbfont_ascii5f` glyphs, byte-aligned on a 32-col window (byte cols 4..35) |
| Entities | hardware sprites (SAT) | soft OR-blits over the tile layer, every repaint |
| Hurt flash | red sprite colour | inverted-box blit (silhouette dark, cell lit) |
| Colour | per-char colour table + per-sprite SAT colour | NTSC artifact colour of the bit patterns (walls read violet/white) |
| Boss visual | 4 hardware sprites, 32×32 | one 28×32 (4-byte × 32-row) blit |
| Repaint | full name-table stream per turn | **dirty-tracked**: only cells lit this frame OR last frame (`prev_vis` snapshot) are redrawn |
| Memory | CodeTank cartridge `$4000` + Parmigiani high bank `$E000` | flat 48 KB: code `$6000`, map/pools at `$0280-$046F` |

The TMS9918 helper names (`WRT_DATA_REG`, `name_at_rc`, `vdp_set_write`,
`clear_name_table`, `render_map`, `place_all_sprites`, …) are kept as an HGR
**compatibility layer** (macros + routines at the top / middle of
`HGR_Rogue.asm`), which is what lets ~5000 lines of game logic assemble
untouched. The byte-aligned engines underneath come from the shared lib:
`hgr_putc8` (**`dev/lib/gen2/hgr_text8.asm`**) emulates the VDP
auto-increment write pointer — a stream of chars walks the 32-wide text
window and wraps to the next row, so every TMS print loop works as-is —
and the tile/entity/boss copies ride `hgr_blit2`/`hgr_blit4`
(**`dev/lib/gen2/hgr_blit2.asm`**, OR / inverted-FLASH / STORE).

Dirty-tracking details: the per-turn redraw set is
`(vis XOR prev_vis) | ent_prev | ent_now` — the FOV **ring delta** plus
every cell an entity touched last frame or will touch this frame (an
entity pre-scan at the top of `place_all_sprites` fills `ent_now`,
FOV-gated like the draws; the thrown dagger is included, being the one
entity that can sit on an unlit cell). Cells lit on both frames with no
entity involvement are skipped entirely — a typical move repaints
~12-18 cells instead of the whole lit union. Fresh levels and modal
exits force a full repaint (`force_dirty_all` primes `prev_vis` AND
`ent_prev` to $FF — under the XOR formula `ent_prev` is the force
channel). The HUD is repaint-elided too: `update_hud` caches the 11
displayed values (`hud_cache`) and skips its ~130-glyph + 5-icon
repaint when nothing changed and no transient prompt (`hud_msg`) or
full wipe (`hud_force`, set by `clear_name_table`) intervened. The
dagger-flight frames ride the same dirty pass (~2 cells per frame)
instead of a forced full redraw.

## Build

```bash
cd sketchs/gen2/game_rogue
make            # -> "software/Graphic HGR"/HGR_Rogue.{bin,txt}
```

Run it:

```bash
./build/POM1 --preset 11 --load 6000:"software/Graphic HGR/HGR_Rogue.bin" --run 6000
```

or File → Load Memory the `.txt`/`.bin` from `software/Graphic HGR/`
(auto-enables the GEN2 card), then `6000R` from Wozmon. Also discoverable
in the DevBench (`.sketch.json` wires the cfg).

Headless smoke (deterministic — the PRNG seeds from the injection cycle):

```bash
./build/POM1 --preset 11 --load 6000:"software/Graphic HGR/HGR_Rogue.bin" --run 6000 \
  --paste-at-cycle 3000000 " " --paste-at-cycle 6000000 " " \
  --dump-after-cycles 14000000 --dump-gen2-frame /tmp/rogue.png
```

## Assets

`rogue_assets_hgr.inc` is **generated** by
`tools/build_rogue_hgr_assets.py` from the TMS art (the 8 tiles of
`tileset_rogue.inc`, the 14 inline 16×16 sprite patterns of
`TMS_Rogue.asm`, and `sprites_boss.asm`), converted 16→14 px wide
(crop one column per side) and repacked 7 px/byte, bit 0 leftmost,
palette bit clear. `make assets` re-runs it. Same credits as the TMS
version: Quale's SCROLL-O-SPRITES (CC-BY-3.0) + Hexany Ives' Monster
Menagerie (CC0).

## Files

```
HGR_Rogue.asm          main source (logic verbatim from TMS_Rogue.asm +
                       HGR compat layer + blit engine + render_tiles)
rogue_assets_hgr.inc   generated HGR tiles/sprites/boss (do not edit)
apple1_rogue_hgr.cfg   single-region cfg ($6000 code, low-RAM pools)
Makefile               ca65 + ld65 + emit_gen2_txt.py
```

## Controls

Identical to the TMS version: IJKL move, `B` bag, `T` throw dagger,
`.` rest, `H`/`?` help, any key at the title starts (`B` at the title =
hidden boss-room cheat). Reach depth 13 and kill the demon to win.
