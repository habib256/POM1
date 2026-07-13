# game_rogue_x2 (GEN2) — the ×2 COLOUR variant of the HGR Rogue port

*[← POM1 documentation index](../../doc/README.md)*

Same game as [`../game_rogue/`](../game_rogue/README.md) (same logic,
same 16×10 dungeon, same HUD), but the playfield renders at **×2 in
colour**: 28×32 px cells, coloured tiles and sprites, a 56×64 px demon.
The 4 HUD text rows at the bottom (rows 20-23) are **byte-identical**
to the ×1 build — same text engine, same ×1 icons; the empty-bag modal
and the GAME OVER screen even hash-match the ×1 build frame-for-frame.

## How it fits

A 16×10 map at 28 px/cell is 448 px wide — too big for HGR's 280. The
playfield therefore shows an **8×5-cell viewport, CENTRE-LOCKED on the
player**: the hero pins viewport cell (3, 2) and the world scrolls
around them. The camera origin may hang past the map edges — off-map
cells render black (repainted once per camera move via `vp_force`).
Every player move shifts the whole window (`force_dirty_all`); free
actions (bump, rest, dagger flight) keep the camera still, so the
delta renderer keeps skipping clean cells there. FOV, visibility and
all game logic still run on the full 16×10 grid.

## Colour

Colours are **baked into the assets** at generation time
(`tools/build_rogue_hgr_assets.py`, ×2 section): every playfield asset
is doubled to 28 px wide (4 HGR bytes/row) and masked to one pixel
parity + palette bit — doubled pixels are 2-px runs, so one parity
survives per pair and the shape stays intact at half density (the
`hgr_sprite16` lesson). Blit positions are always 4-byte-aligned, so
byte-column parity is fixed and the masks can be precomputed — **zero
runtime cost**. Palette (TMS tint → HGR artifact colour): hero white
(full density), undead green, ghost violet, skeleton blue, death
orange, troll green, boss **orange**; stairs green, door orange, pit
violet; items per kind (hero **blue** like the TMS light-blue, zombie
and troll green, ghost violet, skeleton and dagger white, sword blue,
armor/ring/torch orange). **Hits flash by flipping the NTSC palette
bit** (`hgr_blit2` mode 3, PALFLIP): the same pixels swap colour family
for one frame — the orange demon flashes green, the blue hero flashes
violet.

## Double buffering

Every turn renders to the **hidden HGR page** and flips ($C254/$C255,
two soft-switch reads) — moves never show a repaint in progress. The
delta machinery survives paging untouched: the dirty snapshots
(`prev_vis`/`ent_prev`) are kept **per page** and swapped at flip time,
so `render_tiles` always compares against "this page's last frame";
the HUD repaints once per page when it changes (`hud_again`); the
off-map border blanks once per page after a camera move (`vp_force`
2-render countdown). Modals, splash screens and prompts draw straight
onto the visible page (`draw_to_front`) — the empty-bag modal still
hash-matches the ×1 build — and `redraw_game` re-renders the back page
on exit (`force_dirty_all` fills both pages' snapshots).

## Coloured text

`hgr_text8`'s artifact-colour attributes (`ht_cm_ev/od` + `ht_cbit`,
white = pass-through) tint the byte-aligned text: the HUD rows are
colour-coded (DEPTH orange, ATK/DEF blue, HP green, timers/XP violet;
white restored after the HUD so prompts and modals stay white — the
empty-bag modal still hash-matches the ×1 build). The title page draws
a **×2 "ROGUE" banner in orange** (`putc8_x2`: the HGR-order bbfont
doubled via `dblnib` — a pure bit-doubler — with the straddling pair
split across the two output bytes) over a green title body.

## Rendering

`render_tiles` walks the 40 viewport cells with per-cell probes
(`dirty_test_a` / `vis_test_a`) instead of the ×1 build's row shift
registers; dirty cells blit through `blit_tile_x2_a` /
`blit_sprite_x2_a` (128 B assets via the lib's `hgr_blit4`). The boss
is two 4-byte-wide column halves (`boss_hgr_x2_l/_r`), drawn when its
2×2-cell footprint fits the viewport. Entities off-window are skipped
(`vp_cell_pos` sets carry). HUD icons and the inventory-modal
pictograms keep the ×1 pack — both asset packs link in (~4.2 KB total).

## Build & run

```bash
cd sketchs/gen2/game_rogue_x2
make            # -> "software/Graphic HGR"/HGR_RogueX2.{bin,txt}
./../../../build/POM1 --preset 11 \
  --load 6000:"software/Graphic HGR/HGR_RogueX2.bin" --run 6000    # 6000R
```

`make assets` regenerates both packs from the TMS art. Controls,
credits and gameplay reference: see the ×1 README.
