# tms9918_rogue — Roguelike for the P-LAB TMS9918

A Berlin-Interpretation roguelike for the Apple-1 + P-LAB TMS9918
Graphic Card. Custom 8x8 tileset sliced from Quale's SCROLL-O-SPRITES
(CC-BY-3.0), 16x16 hardware sprite for the player, dual-bank Parmigiani
RAM layout (low bank for code, high bank for the map buffer).

Ships as `roms/codetank/Codetank_GAMES2.rom` — load via POM1's
`--codetank-rom` and run with `4000R` from Wozmon.

## MVP roadmap

- **MVP1 (done)** — vi-key movement (HJKL / QZSD on AZERTY), 16x16
  hardware-sprite player (Quale's `char_adventurer`), collision against
  a RAM map buffer.
- **MVP2 (done)** — LFSR-16 PRNG seeded from time-to-keypress,
  random gen dispatcher (coin-flip between big-room and two-rooms +
  L-corridor; connectivity guaranteed by construction). Each room's
  perimeter is scanned post-hoc and TILE_EMPTY perimeter cells become
  TILE_DOOR — that's the visual cue that a corridor cuts through the
  wall. Edge-door wraps (cols 0/15, rows 0/9) regenerate a sibling
  big-room at the same depth, with the entry door re-stamped opposite
  the player's exit. Bresenham FOV (8\*FOV_RADIUS rays to the player's
  box-perimeter) with opaque walls + opaque doors — every room is a
  fresh "scene", no peeking through thresholds. **Still TODO**:
  recursive shadowcasting (Bresenham has slope artifacts), persistent
  fog-of-war (currently full re-flood every turn — "torchlight" mode,
  no remembered terrain), and bit-packing of map+vis buffers.
- **MVP3 (done)** — 16-slot monster pool at `$E300` (UNDEAD/GHOST/
  SKELETON/DEATH/TROLL, depth-keyed type pool + per-depth HP & damage
  bonuses; TROLL flees the player via `ai_troll`, gated to depth 5+),
  8-slot item pool at `$E380` for food drops, bump-to-attack
  combat with per-frame red-flash on both hits, HP HUD on row 20
  (rows 21-23 free for MVP4 expansion), stairs-down advances depth,
  permadeath → "YOU DIED ON LEVEL N" + 1.3 s deaf-time + JMP $4000
  cartridge cold-start. Food drops where a monster died, walking onto
  the cell heals FOOD_HEAL HP (capped at HP_MAX).
- **MVP4 (done) — three-key Rogue command set**: 26-slot inventory at
  `$E3A0` (one slot per letter A..Z, 8 B per slot), 7 item categories
  (weapon, armor, ring, potion, scroll, food, dagger). After the
  MVP4 simplification each category has a **single sub-type** that
  matches its on-screen sprite — a Quale outfit/magick/food pictogram:
  - **SWORD** (weapon, dmg 2) — `outfit_sword_pat`
  - **TUNIC** (armor, def 2) — `outfit_tunic_pat`
  - **AMULET** (ring, regen) — `outfit_amulet_pat`
  - **POTION** (heal +8 HP) — `food_bottle_pat`
  - **SCROLL** (full-map reveal) — `magic_scroll_pat`
  - **RATION** (food, +`FOOD_HEAL` HP) — meat sprite
  - **DAGGER** (thrown, dmg 2) — `outfit_dagger_pat`

  Pickup is auto on bump; the player only ever explicitly **inspects**,
  **uses**, or **throws**:
  - `I` — open the **inventory** modal (full-screen list with
    `[A] *NAME +V` / `(xQ)` formatting, `*` flags equipped slots).
    From inside the modal, **type a letter** to activate that slot
    directly (same dispatch as `E` below — the modal closes and the
    effect fires); any other key just dismisses. The `E` command
    keeps working from the playfield as a single-step shortcut.
  - `E` — **use** a slot, with type-driven dispatch:
    - `WEAPON` / `ARMOR` / `RING` → toggle equip (free action).
      Re-pressing `E` on the same slot un-equips; pressing it on a
      different slot of the same type replaces the previous one.
      The amulet manages `ring_flags`' `RING_F_REGEN` bit.
    - `FOOD` (ration) → +`FOOD_HEAL` HP (cap `HP_MAX`), consumes the
      slot, costs a turn.
    - `POTION` → +`INV_VALUE` HP (= 8, cap), costs a turn.
    - `SCROLL` → one-shot full-map reveal modal (press any key to
      return), costs a turn.
    - `DAGGER` → not "used"; throw it instead with `T`.
  - `T` — **throw** a dagger. Two-stage prompt: pick the slot, then
    a direction (vi keys). Cell-by-cell animation at ~80 ms/frame,
    range `THROW_RANGE = 8`. Stops on the first wall / door / monster /
    out-of-bounds; lands the dagger on the last empty cell (or on the
    monster's corpse). Costs a turn.
  - **HUD**: row 21 shows `W:_  A:_  R:_   ATK:NN DEF:NN` —
    equipped slot letters (or `-`) and the recomputed
    `player_dmg` / `player_def`. Combat reads `player_dmg` instead
    of the constant `PLAYER_DMG`, and incoming hits subtract
    `player_def` (floor 0) before damaging HP.
  - **Amulet of regeneration**: `RING_F_REGEN` makes `finish_turn`
    add +1 HP every `RING_REGEN_PERIOD` (= 5) turns.
  - **Items spawn at level gen**: `spawn_level_items` scatters 1..3
    typed items per floor (food 35%, dagger 20%, potion 15%, scroll
    10%, weapon 8%, armor 7%, ring 5%); the legacy "monster drops
    food on death" path is preserved.
  - **Pickup is auto** on contact; if the bag is full the item stays
    on the floor. Stackables (food, daggers) merge into an existing
    slot via `INV_QTY++` so a fistful of daggers takes one letter.

## Files

```
TMS_Rogue.asm                main source (boots, init VDP, game loop)
tileset_rogue.inc            generated by tools/extract_quale_8x8_tiles.py
apple1_rogue.cfg             DRAM dev cfg ($0280 + dual-bank high RAM)
apple1_rogue_codetank.cfg    cartridge cfg ($4000 + dual-bank high RAM)
emit_TMS_Rogue_txt.py        Wozmon-hex emitter (DRAM build only)
Makefile                     drives ca65 + ld65 (default cfg = DRAM)
```

## Memory layout (Parmigiani dual-bank, preset 8)

The standard P-LAB / Replica Apple-1 splits 8 KB user RAM into
two banks with a hole in the middle:

- **Low bank**: `$0000-$0FFF` (4 KB). Holds ZP + stack + code + tileset
  patterns + colour table. Cartridge variant skips this entirely
  since code lives in ROM at `$4000-$7FFF`.
- **Hole**: `$1000-$7FFF`. Reads return `$FF`, writes dropped under
  silicon-strict mode. **Do not put variables here** — they will
  silently fail to persist.
- **High bank**: `$E000-$EFFF` (4 KB). Linker-managed `MAPSEG` hosts
  `map_buffer` (160 B, 16×10 logical tiles, one byte per tile) +
  `vis_buffer` (160 B, parallel VIS_SEEN/VIS_VISIBLE bits). After
  MAPSEG ends at `$E0A0`, two absolutely-addressed pools:
  `monsters` at `$E300` (16 slots × 8 B = 128 B) and `items` at
  `$E380` (8 slots × 4 B = 32 B). `$E3A0-$EFFF` (~3.2 KB) is free
  for MVP4 inventory + equipment state.

## Build

DRAM dev (8 KB Parmigiani, low + high bank):
```bash
cd dev/projects/tms9918_rogue
make                                  # default cfg = apple1_rogue.cfg
# Loads as TMS_Rogue.txt at $0280, run with 280R from Wozmon.
```

Cartridge:
```bash
python3 tools/build_games2_rom.py     # writes roms/codetank/Codetank_GAMES2.rom
./build/POM1 --preset 8 --codetank-rom roms/codetank/Codetank_GAMES2.rom
# Then 4000R from Wozmon to start.
```

## Tileset regeneration

The pattern table in `tileset_rogue.inc` is a 256-char (2 KB) block of
8x8 patterns sliced from Quale's 16x16 sprites in
`dev/lib/tms9918/sprites_*.asm`. To pick different sprites or
quadrants, edit the `PALETTE` dict at the top of
`tools/extract_quale_8x8_tiles.py` and re-run:

```bash
python3 tools/extract_quale_8x8_tiles.py
```

The colour table (32 bytes at VRAM `$2000`) groups char IDs in
batches of 8 for the Mode-1 1-pair-per-group constraint:

| Group | Char IDs | Intended use              | Colour byte |
|-------|----------|---------------------------|-------------|
| 0     |  0..7    | stone walls/floors        | $E1 gray/blk |
| 1     |  8..15   | stairs-down + door        | $A1 dk-yel/blk |
| 2     | 16..23   | stairs-up (arch) + torch  | $B1 lt-yel/blk |
| 3     | 24..31   | items / magic             | $71 cyan/blk |
| 4     | 32..39   | font (' ' + punct)        | $F1 white/blk |
| 5+    | 40..255  | font (digits, A..Z)       | $F1 white/blk |

## Controls

The title screen lets you pick QWERTY (HJKL) or AZERTY (QZSD); the
game loop binds the four movement keys at runtime.

- `H` / `J` / `K` / `L` — west / south / north / east (QWERTY layout)
- `Q` / `Z` / `S` / `D` — west / north / south / east (AZERTY layout)
- `I` — show **inventory** (modal). Type a slot letter `A..Z` from
  inside the modal to activate the item directly; any other key
  dismisses.
- `E` — **use** a slot from the playfield (toggle-equip for
  weapon/armor/ring, consume for food/potion/scroll; auto-dispatches
  by type)
- `T` — **throw** a dagger (asks for direction next)
- `N` — regenerate the current level at the same depth (debug refresh,
  no turn cost — does not advance depth or cost HP).
- Stepping onto `TILE_STAIRS_DOWN` advances to a deeper level (depth++,
  harder monster pool); walking off a screen-edge `TILE_DOOR` warps to
  a sibling big-room at the same depth, spawning at the opposite edge.

## Credits

- Custom tileset and sprite art: Quale's SCROLL-O-SPRITES (May 2013,
  CC-BY-3.0). See `dev/lib/tms9918/sprites_*.asm` headers for the
  full attribution chain.
- TMS9918 driver + silicon-strict timing macros:
  `dev/lib/tms9918/tms9918m1.asm`.
