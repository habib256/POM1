# game_rogue — Roguelike for the P-LAB TMS9918

*[← POM1 documentation index](../../doc/README.md)*

A Berlin-Interpretation roguelike for the Apple-1 + P-LAB TMS9918
Graphic Card. Custom 8x8 tileset sliced from Quale's SCROLL-O-SPRITES
(CC-BY-3.0), 16x16 hardware sprite for the player, dual-bank Parmigiani
RAM layout (low bank for code, high bank for the map buffer).

Ships as `roms/codetank/Codetank_GAME2.rom` — load via POM1's
`--codetank-rom` and run with `4000R` from Wozmon.

## Shipped features

- **MVP1** — vi-key movement (HJKL / QZSD on AZERTY), 16x16
  hardware-sprite player (Quale's `char_adventurer`), collision against
  a RAM map buffer.
- **MVP2** — LFSR-16 PRNG seeded from time-to-keypress,
  random gen dispatcher (coin-flip between big-room and two-rooms +
  L-corridor; connectivity guaranteed by construction). Each room's
  perimeter is scanned post-hoc and TILE_EMPTY perimeter cells become
  TILE_DOOR — that's the visual cue that a corridor cuts through the
  wall. Edge-door wraps (cols 0/15, rows 0/9) regenerate a sibling
  big-room at the same depth, with the entry door re-stamped opposite
  the player's exit. **FOV uses Björn Bergström's recursive
  shadowcasting** (RogueBasin 2002): the plane around the player is
  partitioned into 8 octants and each is processed by `cast_octant`,
  which scans rows of increasing depth and shrinks the visible cone
  on every wall encounter. All slope numerators / denominators stay
  ≤ 2\*TORCH_RADIUS+1 = 15, so the cross-products fit in one byte and
  the per-cell comparison only needs a 4-bit×4-bit multiply. Walls
  AND doors block sight — every room is a fresh "scene", no peeking
  through thresholds, and no slope artifacts. Persistent fog-of-war
  was deliberately retired in favour of pure torchlight: `compute_fov`
  wipes `vis_buffer` and re-paints from scratch every move, so cells
  that leave the player's radius plunge back into darkness immediately.
- **MVP3** — 16-slot monster pool at `$E300` (UNDEAD/GHOST/
  SKELETON/DEATH/TROLL, depth-keyed type pool + per-depth HP & damage
  bonuses; TROLL flees the player via `ai_troll`, gated to depth 5+),
  8-slot item pool at `$E380` for food drops, bump-to-attack
  combat with per-frame red-flash on both hits, HP HUD on row 20
  (rows 21-23 free for MVP4 expansion), stairs-down advances depth,
  permadeath → "YOU DIED ON LEVEL N" + 1.3 s deaf-time + JMP $4000
  cartridge cold-start. Food drops where a monster died, walking onto
  the cell heals FOOD_HEAL HP (capped at HP_MAX).
- **MVP4 — inventory-first command set + timed-buff equipment**:
  26-slot inventory at `$E3A0` (one slot per letter A..Z, 8 B per slot),
  8 item categories (weapon, armor, ring, potion, scroll, food, dagger,
  torch). Each category has a single sub-type that matches its on-screen
  sprite — a Quale outfit / magick / food / exploration pictogram:
  - **SWORD** (weapon, ATK +2 for `WEAPON_DURATION` = 20 turns) — `outfit_sword_pat`
  - **TUNIC** (armor, DEF +1 for `ARMOR_DURATION` = 30 turns) — `outfit_tunic_pat`
  - **AMULET** (ring, +1 HP every `RING_REGEN_PERIOD` = 5 turns,
    expires after `RING_DURATION` = 15 turns) — `outfit_amulet_pat`
  - **POTION** (heal +5 HP, capped at `hp_max`) — `food_bottle_pat`
  - **SCROLL** (full-map reveal) — `magic_scroll_pat`
  - **RATION** (food, +`FOOD_HEAL` = 3 HP) — meat sprite
  - **DAGGER** (thrown, dmg 2, `THROW_RANGE` = 8) — `outfit_dagger_pat`
  - **TORCH** (FOV doubles from `FOV_RADIUS` = 3 to `TORCH_RADIUS` = 7
    for `TORCH_DURATION` = 50 turns) — `expl_torch_pat`

  Pickup is automatic on contact; the player only ever explicitly
  **inspects / uses** inventory slots or **throws** daggers:
  - `I` — open the **inventory** modal (full list with `[L] QxNAME UTIL`
    rows, one 16×16 sprite per slot). From inside the modal, **type a
    letter** to activate that slot directly — the modal closes and the
    effect fires; any other key dismisses. Slot activation uses
    type-driven dispatch:
    - `WEAPON` / `ARMOR` / `RING` / `TORCH` → activate the buff (free
      action). The slot is **consumed** and the matching ZP timer
      (`weapon_timer` / `armor_timer` / `ring_timer` / `torch_timer`)
      is set to its category duration. Re-using a fresh item of the
      same kind overwrites the timer — no stacking, latest activation
      wins. Each turn `finish_turn` ticks the four timers down; on
      weapon/armor expiry it calls `recompute_player_stats` so the
      boost drops cleanly. Ring expiry clears `RING_F_REGEN` from
      `ring_flags` so the regen tick stops firing.
    - `FOOD` (ration) → +`FOOD_HEAL` HP (cap `hp_max`), consumes the
      slot, costs a turn.
    - `POTION` → +5 HP (cap `hp_max`), costs a turn.
    - `SCROLL` → one-shot full-map reveal modal (press any key to
      return), costs a turn.
    - `DAGGER` → not "used"; throw it with `T`.
  - `T` — **throw** a dagger. Single-prompt: directly asks for a
    direction (vi keys); the dagger slot is auto-picked via
    `find_dagger_slot` (the bag's first `ITEM_T_DAGGER`, since dagger
    is the only throwable type). Cell-by-cell animation at ~80 ms/frame,
    range `THROW_RANGE` = 8. The dagger **vanishes on impact** —
    monster hit (deals `INV_VALUE` damage = 2), wall / door / stairs /
    pit, OOB, or end of range. No floor drop. Costs a turn on a
    completed throw; free action with `NO DAGGER` if the bag is empty
    or silent if the direction key isn't recognised.
  - **HUD** (3 rows below the playfield):
    - **Row 20** — `DEPTH NNN                  HP HH/HM` — current
      floor and live HP / runtime cap.
    - **Row 21** — `ATK:NN DEF:NN             XP:NNN` — recomputed
      `player_dmg` / `player_def` (combat reads these; incoming hits
      subtract `player_def`, floor 0, before damaging HP) plus the
      kill counter.
    - **Rows 22-23** — one 16×16 item sprite + 2-digit countdown per
      ACTIVE buff (sword / tunic / amulet / torch); inactive buffs are
      blank. **Left-packed**: icons at pixel x = 0 / 32 / 64 / 96
      (cols 0-1, 4-5, 8-9, 12-13), with each 2-digit countdown sitting
      on **row 23** (the very bottom) immediately right of its icon
      (cols 2-3, 6-7, 10-11, 14-15). Cols 16-31 of rows 22-23 stay
      blank — free for future status pickups. The legacy
      `WPN:NN ARM:NN RNG:NN TRC:NN` text row + `W:_ A:_ R:_`
      equipped-letter row were both dropped.
  - **XP-driven progression**: `player_xp` increments by 1 per slain
    monster (saturates at 255). Three independent countdowns drive
    level-ups in `award_xp`, all reset on `init_inventory`:
    - every  5 kills → `+1 hp_max` (and `+1` current HP — small heal-on-up),
    - every 10 kills → `+1 xp_atk_bonus`, folded into `player_dmg`,
    - every 20 kills → `+1 xp_def_bonus`, folded into `player_def`.
  - **Items spawn at level gen**: `spawn_level_items` scatters 1..3
    typed items per floor; cumulative thresholds out of 32 give food
    ≈ 22 %, dagger ≈ 16 %, torch ≈ 16 %, weapon ≈ 12 %, potion ≈ 9 %,
    scroll ≈ 9 %, armor ≈ 9 %, ring ≈ 6 %. The legacy "monster drops
    food on death" path is preserved.
  - **Pickup is auto** on contact; if the bag is full the item stays
    on the floor. Stackables (food, daggers, potions, scrolls, torches)
    merge into an existing slot via `INV_QTY++` so a fistful of daggers
    takes one letter.
  - **Boss room at depth 13** (MVP5): descending the stairs from
    depth 12 routes into `gen_boss_room` + `spawn_boss` instead of
    triggering `win_screen` immediately. The boss arena is a fixed
    big-room (cols 1..14 × rows 1..8) with **no doors / no stairs /
    no items / no pits** — once the player descends, the only exit is
    to kill the demon (or die trying). Player spawns at (7, 8); the
    boss anchor sits at (7, 4) with a 32×32 visual that occupies cells
    (7,4)/(8,4)/(7,5)/(8,5) — 4 hardware sprites tiled together
    (slots 56/60/64/68 in the pattern table) painted from a single
    `MON_TYPE_BOSS` pool entry. Sprite art is Hexany Ives'
    "Monster Menagerie" `creature_024.png` (CC0); the conversion lives
    in `tools/extract_hexany_boss_sprite.py` and emits
    `sprites_boss.asm`. The boss has **base HP 15 / dmg 4** scaled by
    the depth-13 spawn bonuses (`+depth/3` HP, `+depth/6` dmg) → 19 HP
    / 6 dmg; with expected gear (TUNIC + XP DEF) the bite lands at
    ~3 HP/turn, gating the win behind real positioning. AI is
    greedy-chase like UNDEAD but routed through a 2x2-aware
    `apply_boss_step` that checks all four prospective footprint
    cells and treats "player inside the new footprint" as the
    bump-attack. Killing the boss tail-calls `win_screen` —
    "DUNGEON CONQUERED" + score takeover; dying to the boss shows
    "KILLED BY THE DEMON" on the death screen.
- **Hidden pit traps** — `spawn_level_pits` scatters 0..2 `TILE_TRAP_PIT`
  cells per floor (1d3 roll) on random `TILE_EMPTY` tiles. Pits start
  hidden: render_map paints them as blank floor (high bit clear + tile
  id == `TILE_TRAP_PIT` ⇒ "blank, same as out-of-FOV cells"). Stepping
  onto a pit calls `trigger_pit`: the player loses `PIT_DMG = 3` HP
  (saturated at 0), the sprite flashes red for the next frame, and the
  cell flips to "revealed" via the high bit on its `map_buffer` byte
  so it now renders as the pit pictogram. Pits are **not consumed** —
  re-stepping costs HP again, so you have to remember the layout. Pits
  drift back to "hidden" the moment they leave the player's FOV
  (`strip_invisible_pit_reveals`, run as the tail of `compute_fov`),
  forcing a real "do I remember where it was" play. Monsters trigger
  the same trap (`PIT_DMG` off `MON_HP`), thrown daggers stop dead on
  the cell, and dying on a pit shows `KILLED BY A PIT` on the death
  screen via `last_attacker = LAST_ATTACKER_PIT`.

## TODO

(MVP1-MVP4 + bit-packed buffers all shipped — open candidates live in
`dev/TODO6502.md` under hygiene / WIP.)

## Files

```
TMS_Rogue.asm        main source (boots, init VDP, game loop)
tileset_rogue.inc    generated by tools/extract_quale_8x8_tiles.py
apple1_rogue.cfg     cartridge cfg ($4000-$7FFF + dual-bank high RAM)
Makefile             drives ca65 + ld65
```

The 8 KB Parmigiani DRAM target (code at `$0280`, Wozmon-hex `.txt`)
was abandoned once MVP4 outgrew the 3.5 KB low-bank CODE budget; the
cartridge build ($4000-$7FFF, 16 KB) is now the only supported path.

## Memory layout (Parmigiani dual-bank, preset 9)

The standard P-LAB / Replica Apple-1 splits 8 KB user RAM into
two banks with a hole in the middle:

- **Low bank**: `$0000-$0FFF` (4 KB). Holds ZP + stack + code + tileset
  patterns + colour table. Cartridge variant skips this entirely
  since code lives in ROM at `$4000-$7FFF`.
- **Hole**: `$1000-$7FFF`. Reads return `$FF`, writes dropped under
  silicon-strict mode. **Do not put variables here** — they will
  silently fail to persist.
- **High bank**: `$E000-$EFFF` (4 KB). Linker-managed `MAPSEG` hosts
  `map_buffer` (80 B, **bit-packed**, 2 cells per byte — low nibble =
  even col, high nibble = odd col; bits 0..2 = dense `TILE_*` id, bit
  3 = pit-reveal flag) + `vis_buffer` (20 B, bit-packed, one bit per
  cell — 8 cells per byte, row R uses bytes 2R / 2R+1). After MAPSEG
  ends at `$E064`, three absolutely-addressed pools: `monsters` at
  `$E300` (16 slots × 8 B = 128 B), `items` at `$E380` (8 slots ×
  4 B = 32 B), and `inventory` at `$E3A0` (26 slots × 8 B = 208 B).
  `$E470-$EFFF` (~3 KB) is free for future content.

## Build

Cartridge (the only supported target):
```bash
python3 tools/build_codetank_rom.py --rom=2     # writes roms/codetank/Codetank_GAME2.rom
./build/POM1 --preset 9 --codetank-rom roms/codetank/Codetank_GAME2.rom
# Then 4000R from Wozmon to start.
```

Build the ROM image (assembles `TMS_Rogue.asm` from this directory and links
it into the `$4000` lower bank of `roms/codetank/Codetank_GAME2.rom`):
```bash
python3 tools/build_codetank_rom.py --rom=2
```
This sketch is also discoverable in DevBench (mono-source `.sketch.json`);
there is no per-project `Makefile` — it migrated out of `dev/projects/` into
`sketchs/tms9918/game_rogue/`.

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

Movement is fixed IJKL — the same physical keys on QWERTY and AZERTY
keyboards — bound once when the title screen is dismissed (any key
starts).

- `I` / `J` / `K` / `L` — north / west / south / east
- `B` — show the **bag** (inventory modal). Type a slot letter `A..Z` from
  inside the modal to activate the item directly; any other key
  dismisses. Weapons / armor / rings / torches arm timed buffs; food /
  potions heal; scrolls reveal the map one-shot; daggers are thrown with
  `T` instead.
- `T` — **throw** a dagger. Auto-picks the first dagger in your bag,
  then asks for a direction (IJKL). The dagger flies in that
  direction until it hits a monster (deals damage), an obstacle
  (wall / door / stairs / pit), or runs out of range — and **vanishes
  on impact**. No floor drops. Free action with `NO DAGGER` if the bag
  has none, or silent if the direction key isn't recognised.
- `.` — **rest** one turn. No movement, but monsters take their turn,
  the regen amulet pulses (if active), and every buff timer ticks down.
  Useful when an amulet is on and you want the +1 HP heal without
  giving up positioning.
- `?` / `H` — open the **help** modal (free action). Static reference
  card listing the movement keys, every command, and the gameplay rules
  — so the player on a real cartridge doesn't need this README.
- Stepping onto `TILE_STAIRS_DOWN` advances to a deeper level (depth++,
  harder monster pool); walking off a screen-edge `TILE_DOOR` warps to
  a sibling big-room at the same depth, spawning at the opposite edge.
- Stepping onto a hidden `TILE_TRAP_PIT` costs `PIT_DMG` HP and reveals
  the pit pictogram on that cell. Pits are not consumed, so re-stepping
  costs HP again — and the reveal lapses the moment the pit leaves your
  FOV, so you have to remember its location. Daggers thrown across a
  pit stop on it (no jump-overs). Dying on a pit shows `KILLED BY A PIT`.

## Credits

- Custom tileset and sprite art: Quale's SCROLL-O-SPRITES (May 2013,
  CC-BY-3.0). See `dev/lib/tms9918/sprites_*.asm` headers for the
  full attribution chain.
- Depth-13 boss sprite: Hexany Ives' SCROLL-O-SPRITES "Monster
  Menagerie" (`creature_024.png`, CC0). See
  https://hexany-ives.itch.io/hexanys-monster-menagerie. The 32×32
  source is sliced into 4 × 16×16 TMS9918 sprite slots by
  `tools/extract_hexany_boss_sprite.py` → `sprites_boss.asm`.
- TMS9918 driver + silicon-strict timing macros:
  `dev/lib/tms9918/tms9918m1.asm`.
