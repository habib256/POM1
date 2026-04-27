# HGR9 Smiley16 Show — 28×28 emoji glyphs

Shows six 28×28 smiley glyphs (grin / laugh / wink / neutral / sad /
surprised) on the GEN2 Color Graphics Card. Glyphs are stored as 14×14
1-bit bitmaps and rendered at 2× with HGR "bridge" passes for NTSC
artifact safety. Glyph data is generated from a source AVIF by the
companion `_gen_smiley16.py` (run only when the source art changes).
Ninth entry in the HGR demo series.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: TODO — pick a GEN2 HGR preset.

## Sources

- `HGR9_Smiley16Show.asm` — main entry, loads at `$0280`
- `_gen_smiley16.py` — regenerates `dev/lib/hgr/smiley.inc` from the
  AVIF emoji sheet (no toolchain prerequisites; only run when the art
  changes)
- `emit_HGR9_Smiley16Show_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/hgr/` (`smiley.inc`, `hgr_tables.inc`)

## Build

    make                          # produces ../../../software/hgr/HGR9_Smiley16Show.{bin,txt}

By hand:

    ca65 -I ../../lib/apple1 -I ../../lib/hgr HGR9_Smiley16Show.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR9_Smiley16Show.o \
        -o ../../../software/hgr/HGR9_Smiley16Show.bin
    python3 emit_HGR9_Smiley16Show_txt.py

## Run in POM1

1. POM1 → Presets → GEN2 HGR preset (TODO).
2. File → Load → `software/hgr/HGR9_Smiley16Show.txt`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
