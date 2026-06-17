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

Each menu directory is named after the cartridge ROM it ships in
(`Codetank_GAMEn.rom` / `Codetank_TEST.rom`):

| Project             | ROM / role                                               |
|---------------------|----------------------------------------------------------|
| `game1_menu/`       | GAME1 lower-bank launcher ($4000-$40FF) → Galaga/Sokoban/Snake/Life |
| `game1_menu_upper/` | GAME1 upper-bank launcher → Tetris + LOGO (legacy design; the current GAME1 upper bank ships LOGO alone, so this menu is not wired into `build_codetank_rom.py`) |
| `game3_menu/`       | GAME3 upper-bank launcher → Life/Mandel/Plasma           |
| `test_menu/`        | TEST upper-bank launcher → Clone/Split (silicon-bug demos) |

The 5 shipped cartridge ROMs (`roms/codetank/*.rom`) are assembled by
**`tools/build_codetank_rom.py`**, which pulls these menus plus the TMS9918
cart sources and lands the result under `roms/codetank/`.
