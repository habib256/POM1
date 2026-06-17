# TMS Split — palette split mid-frame via 5th-sprite-overflow trigger

*[← POM1 documentation index](../../../../doc/README.md)*

Smallest possible consumer for `dev/lib/tms9918/tms9918_5strigger.asm`.
Demonstrates that, on P-LAB stock (no /INT wired), the 6502 can still
schedule a mid-frame event by placing 5 invisible sprites at a chosen
scan line and polling the 5S overflow flag.

What the demo renders:

- 32 vertical stripes (each cell = `(col & $1F) << 3` so each column
  maps to its own colour group)
- Upper half (rows 0..11): a "cool" palette — greens, blues, cyans
- Lower half (rows 12..23): a "warm" palette — reds, oranges, yellows
- The split fires at scan line 96 on every frame:
  1. `WAIT_VBLANK` — clears stale 5S/F/C flags
  2. `push_palette_top` — write 32 cool colours to the colour table
  3. `arm_5s_trigger(96)` — place 5 sprites at Y=95 (= scan line 96)
  4. `WAIT_5S` — busy-loop until raster crosses line 96
  5. `push_palette_bot` — overwrite the colour table with warm colours
     before the chip reads it for the lower half

Caveat (documented in the source): the swap is not pixel-perfect —
the 32-byte colour table upload takes ~512 c, so the cool→warm
transition spreads across rows 12..14. For a 1-line split, reduce
the colour-table writes (e.g. update only entry 0) or unroll the
upload to fit one scan line.

## Hardware

- Machine: Apple 1 (stock 4 KB DRAM)
- Card: P-LAB TMS9918 Graphic Card
- Recommended POM1 preset: 6 (P-LAB TMS9918 + CodeTank), or the P-LAB Multiplexing Fantasy preset 7

## Sources

- `TMS_Split.asm` — main entry, two palette tables inlined
- `apple1_split.cfg` — 4 KB stock layout with a tiny BSS region at
  `$0F80-$0FFF` (the lib's `tms9918_5strigger` declares 1 byte BSS)
- `emit_TMS_Split_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`, `dev/lib/tms9918/` (m1 + 5strigger + pad)

## Build

    make                      # → ../../../../software/Graphic TMS9918/TMS_Split.{bin,txt}

## Run in POM1

1. POM1 → Hardware → plug TMS9918.
2. File → Load → `software/Graphic TMS9918/TMS_Split.txt`.
3. Wozmon `\` prompt: `280R`.
4. Watch for the horizontal palette boundary around row 12.
5. ESC returns to Wozmon.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
