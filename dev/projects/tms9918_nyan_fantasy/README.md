# TMS Nyan Fantasy — full 12-frame Nyan Cat (POM1 Fantasy preset)

XXL variant of [`tms9918_nyan/`](../tms9918_nyan/) — same algorithm,
but ships the FULL 12-frame animation (18 432 bytes from
[jblang's `nyan/nyan.bin`](https://github.com/jblang/TMS9918A/raw/master/examples/nyan/nyan.bin))
embedded in the binary instead of a 2-frame delta. The cat bobs
smoothly and the rainbow scroll cycles through all 4 phases.

The 18 KB animation can't fit in a stock 4 KB Apple-1, so this build
**only loads in POM1's Multiplexing Fantasy preset** (12 or 14) where
the full 64 KB RAM is available. Uses `apple1_nyan_fantasy.cfg`, a
local trim of [`dev/cc65/pom1_fantasy.cfg`](../../cc65/pom1_fantasy.cfg).

## Run in POM1

1. Load **preset 15** (POM1 Multiplexing Fantasy 2026).
2. Hardware menu → plug the **TMS9918** Graphic Card (it's off by
   default in this preset; preset 12 has it on).
3. File → Load Memory → `software/tms9918/TMS_Nyan_Fantasy.txt`.
4. Wozmon `\` prompt → type `280R`.
5. Cat appears, bobs at ~20 fps with the full rainbow scroll.
6. ESC returns to Wozmon.

(Applesoft Lite + microSD ROMs may stay plugged — this project's
CODE region stops at $5FFF and doesn't collide with their
$6000-$9FFF span.)

## Build

    make                 # → ../../../software/tms9918/TMS_Nyan_Fantasy.{bin,txt}

Image size: ~19 KB (18 886 bytes). Loading the `.txt` via Wozmon hex
takes a moment — File → Load Memory is much faster.

## What changed vs the 4 KB variant

| | `tms9918_nyan/` | `tms9918_nyan_fantasy/` (here) |
|---|---|---|
| Frames | 2 (anchors + delta) | 12 (full animation) |
| Image size | 2.4 KB | ~19 KB |
| Apple-1 RAM | stock 4 KB | 64 KB (Fantasy preset) |
| Animation rate | 1 toggle / 8 vsyncs (~7.5 fps, jerky) | 1 advance / 3 vsyncs (~20 fps, smooth) |
| Per-frame VRAM cost | 102 byte writes (delta only) | 1 536 byte writes (full upload) |
| Linker config | `apple1_nyan.cfg` (stock 4 KB) | `apple1_nyan_fantasy.cfg` |

## Sources

- `TMS_Nyan_Fantasy.asm` — Mode III init + 12-frame cycle
- `nyan_frames.asm` — auto-generated 12 × 1 536 B baked-in
- `apple1_nyan_fantasy.cfg` — local trim of `dev/cc65/pom1_fantasy.cfg`
  (CODE = 24 KB instead of 32 KB; BASIC_RAM kept for symmetry).

## Author / License

VERHILLE Arnaud, 2026. Same lineage as the 4 KB variant — algorithm
and frame format from J.B. Langston (Z80 port, MIT licence — see
[jblang/TMS9918A](https://github.com/jblang/TMS9918A)); artwork is
[*Passan Kiskat*](http://www.dromedaar.com/) by Dromedaar Vision.
