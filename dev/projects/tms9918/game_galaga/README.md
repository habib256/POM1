# TMS Galaga — hardware-sprite shoot-'em-up

*[← POM1 documentation index](../../../../doc/README.md)*

Hardware-sprite Galaga on the P-LAB TMS9918 Graphic Card: a player ship
at the bottom plus three increasingly tough alien types hovering in
formation that periodically dive at the player. HP per type 2 / 4 / 6,
three player lives.

Four linker-config variants ship for different deployment scenarios:

- `apple1_galaga.cfg` — stock cassette/`.txt` load at `$0280`.
- `apple1_galaga_codetank.cfg` — full 16 kB CodeTank ROM image at `$4000`
  (Galaga alone in a bank).
- `apple1_galaga_codetank_bank.cfg` — 7 424 B lower-bank slot at `$4100`
  inside the GAME1 multi-game menu bank. **Makefile default**
  (`CFG ?= apple1_galaga_codetank_bank.cfg`); this is the layout that ships
  in `roms/codetank/Codetank_GAME1.rom` (menu → Galaga `$4100`, Sokoban
  `$6200`, Snake `$7600`; TMS LOGO V2.6 in the upper bank).
- `apple1_galaga_codetank_8k.cfg` — 8 kB lower-bank slot at `$4000`, a
  roomier alternative (pairs with Sokoban at `$6000` in a custom bank).

## Sprite layout

22 distinct 16×16 sprite patterns share the TMS9918 sprite-pattern table
(each 16×16 sprite consumes 4 of the 32 pattern slots). The full map
lives at the top of `TMS_Galaga.asm` (lines ~82-105) under
*Sprite pattern names*; ASCII-art pixel maps for every glyph follow
inline alongside the data tables (search for `; --- Sprite patterns ---`
and the `.byte %...` rows). Animation frames use the `_ALT` slots
(`P_ENEMY*_ALT`, `P_EXP_ALT`, `P_PLAYER_TH`); the 32×32 super-boss is
stitched from four quadrants (`P_SUPER_{TL,TR,BL,BR}`).

## Hardware

- Machine: Apple 1 (4 KB DRAM for the cassette build)
- Cards: P-LAB TMS9918 (CodeTank for the ROM variants)
- Recommended POM1 preset: 9 (P-LAB TMS9918 + CodeTank).

## Sources

- `TMS_Galaga.asm` — main entry, loads at `$0280` (or `$4000`/`$4100`
  for the CodeTank variants)
- `apple1_galaga.cfg` — cassette config (`CODE` at `$0280`)
- `apple1_galaga_codetank.cfg` — standalone CodeTank ROM (`$4000`, 16 kB)
- `apple1_galaga_codetank_bank.cfg` — slot inside the menu bank (`$4100`,
  Makefile default)
- `emit_TMS_Galaga_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/m6502/`, `dev/lib/tms9918/`

## Build

    make                          # default = CodeTank menu-bank slot ($4100) → ../../../../software/Graphic TMS9918/TMS_Galaga.{bin,txt}
    make CFG=apple1_galaga.cfg    # stock cassette build at $0280

Override the linker config from the command line:

    make CFG=apple1_galaga_codetank.cfg

## Run in POM1

1. POM1 → Presets → preset 9 (P-LAB TMS9918 + CodeTank).
2. File → Load → `software/Graphic TMS9918/TMS_Galaga.txt`.
3. Wozmon `\` prompt: type `280R` (cassette build), or — when running the
   `roms/codetank/Codetank_GAME1.rom` cartridge — `4000R` to bring up the
   GAME1 menu and pick **1 = Galaga**. Rebuild that ROM with
   `python3 tools/build_codetank_rom.py --rom=1`.

## Silicon Strict mode

The shipped binary is **NOP-padded for `Silicon Strict` ON** (see Hardware
menu → Silicon Strict / `--silicon-strict`). 219 NOPs across
`render_sprites`, `render_super_n`, `hide_all_sprites`, `draw_hud`,
`draw_title_tms`, `draw_help_sprites`, `draw_victory_tms`,
`draw_wave_clear_tms`, `draw_str_tms`, `emit_2digit_tms`, `plot_star`, and
`init_vdp` cover every back-to-back VDP write that would otherwise drop a
byte under the silicon timing window (cf. [`dev/Programming_TMS9918.md`](../../../Programming_TMS9918.md#bug-n1-vram-timing)
§17 Bug N°1 + §25 for the full bringup notes and patching toolchain).

Inline `hide_slot_4` was factored out as a JSR helper to free ~100 B for
the NOP padding. Without it the patched binary overflows the 7 424 B
menu-bank slot.

The shipped `roms/codetank/Codetank_GAME1.rom` packs Galaga into the
7 424 B menu-bank slot at `$4100` (alongside Sokoban `$6200` and Snake
`$7600`), so the NOP padding above must fit that slot — hence the
`hide_slot_4` factoring. If you rebuild a custom bank and need more
headroom, `apple1_galaga_codetank_8k.cfg` gives Galaga a full 8 kB at
`$4000`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
