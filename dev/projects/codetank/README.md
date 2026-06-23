# P-LAB CodeTank — cartridge menus

*[← dev/ index](../../README.md)*

*Tutorials: [TMS9918 assembly guide](../../../sketchs/doc/Programming_TMS9918.md) · [TMS9918 C guide](../../../sketchs/doc/Programming_TMS9918C.md).*

CodeTank is a ROM **daughterboard of the P-LAB TMS9918 Graphic Card**: a
single 32 kB 28c256 whose lower/upper 16 kB half is jumper-mapped to
`$4000-$7FFF`. It has no edge connector, address decoder, or standalone bus
presence of its own — it rides the TMS9918 card's slot, so enabling CodeTank in
POM1 cascade-plugs the TMS9918 and unplugging the TMS9918 cascade-unplugs it.

This group holds only the **launcher menus** + per-game bank-layout cfgs that
compose the cartridge ROMs. The actual games/demos they dispatch to are plain
TMS9918 programs that live as standalone DevBench sketches under
[`../../../sketchs/tms9918/`](../../../sketchs/tms9918/) — CodeTank just packages
them at fixed bank offsets.

Each menu directory is named after the cartridge ROM it ships in:

| Project             | ROM / role                                               |
|---------------------|----------------------------------------------------------|
| `game1_menu/`       | GAME1 **upper**-bank launcher ($4000-$40FF) → Galaga/Sokoban/Snake |
| `game3_menu/`       | GAME3 upper-bank launcher → Life/Mandel/Plasma           |
| `bank_cfgs/`        | per-game ld65 **bank-layout** cfgs (`apple1_*_codetank_bank.cfg`) — pin each game at its fixed cartridge offset |

The games/demos themselves are standalone DevBench programs under
[`../../../sketchs/tms9918/`](../../../sketchs/tms9918/); each keeps only its
own **standalone** `*_codetank.cfg` (run-in-place at the load address it would
use on its own). The cartridge-only bank-layout variants live here in
`bank_cfgs/` so the composition glue stays out of `sketchs/`. (Run-in-place
full-bank programs like Rogue/Nyan have a single cfg that doubles as both, so
it stays with the sketch.)

Not every bank has a menu — some hold a single run-in-place program selected
by the lower/upper jumper:

- **GAME1 upper bank** — TMS LOGO V2.6 (jumper Upper → `4000R` boots it directly).
- **GAME2** — Rogue alone (lower) / Nyan (upper); jumper-selected, no menu.

The cartridge ROMs (`roms/codetank/*.rom`) are assembled by
**`tools/build_codetank_rom.py`** (`--rom=1|2|3`), which pulls these menus
plus the TMS9918 cart sources and lands the result under `roms/codetank/`.
(The TEST and GAME4/LightCorridor carts were retired June 2026.)

## Creating a new game ROM entry

A cart game is just a TMS9918 sketch given a fixed bank slot. Two cases:

- **Menu game (shares an upper bank with siblings)** — needs a bank-layout cfg.
  Copy an existing template from `bank_cfgs/` (e.g. `apple1_snake_codetank_bank.cfg`)
  to `apple1_<game>_codetank_bank.cfg` and edit the `CODE:` line: set `start` to
  the game's fixed cartridge offset and `size` to its reserved slot (the standalone
  `sketchs/tms9918/<game>/*_codetank.cfg` keeps the run-in-place `$4000`/`$4000`
  full-bank layout — the bank cfg only differs in those two fields). Disambiguate
  when the same program is pinned at a different offset on another cart by adding a
  `_<rom>` infix — `apple1_life_codetank_game3_bank.cfg` is GAME3's Life slot.
  Add a corresponding entry to the menu's `codetank_menu.asm` /
  `codetank_game3_menu.asm` dispatch table.
- **Full-bank run-in-place program (owns a whole lower/upper bank, no menu)** —
  needs **no** bank cfg: its standalone `*_codetank.cfg` already loads at `$4000`
  and fills 16 kB, so it doubles as the cartridge layout (Rogue, Nyan, LOGO V2.6).

Then wire it into `tools/build_codetank_rom.py`: add module-level `*_ASM` /
`*_BANK_CFG` constants for the source + cfg (mirroring the `GALAGA_*` /
`LIFE_*` blocks) and reference them from the target ROM's assembly list. Bank
overflow is caught by the tool's `slot()` boundary check, not by ld65.
