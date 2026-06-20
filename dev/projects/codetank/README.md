# P-LAB CodeTank — cartridge menus

*[← POM1 documentation index](../../../doc/README.md)*

CodeTank is a ROM **daughterboard of the P-LAB TMS9918 Graphic Card**: a
single 32 kB 28c256 whose lower/upper 16 kB half is jumper-mapped to
`$4000-$7FFF`. It has no edge connector or address decoder of its own — it
rides the TMS9918 (see the daughterboard rule in `CLAUDE.md`).

This group holds only the **launcher menus** that ship inside the cartridge
ROMs. The actual games/demos they dispatch to are plain TMS9918 programs and
live under [`../tms9918/`](../tms9918/) with their `*_codetank*.cfg` link
variants — CodeTank just packages them.

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
