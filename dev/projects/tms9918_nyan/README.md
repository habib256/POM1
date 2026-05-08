# TMS Nyan — Nyan Cat in TMS9918 Multicolor mode (6502 port)

6502 port of [J.B. Langston's TMS9918 Nyan Cat demo](https://github.com/jblang/TMS9918A/blob/master/examples/nyan.asm)
for the P-LAB Apple-1 TMS9918 Graphic Card. The original artwork is
[*Passan Kiskat* by Dromedaar Vision](http://www.dromedaar.com/);
jblang assembled the 12-frame TMS9918 multicolor animation and the
Z80 driver. This port keeps the static artwork bit-for-bit and
recreates the cat's bob with a 2-frame delta toggle.

## What it exercises in POM1

- **Mode III (Multicolor) — 64×48 colored 4×4 blocks.** The mode every
  TMS9918 demo claims to support and almost none actually use. Each
  cell is encoded as 8 bytes in the pattern table; the chip selects
  2 of those 8 bytes per cell-row using `(cell_y & 3) * 2`, so a single
  pattern entry covers 4 distinct cells stacked vertically.
- **6-section name-table fan-out** — built once at boot. Section S
  (cell rows `4S..4S+3`) uses pattern entries `(32S + cell_x)`. This
  matches jblang's `TmsMulticolor` initialisation byte-for-byte; the
  visual relies on it.
- **Sprites disabled at boot** — `Y=$D0` written to `SAT[0]` at $1B00.
  The TMS9918 silicon-strict slot table relaxes from ~12c (Mode III
  with sprites) to ~6c (Mode III, no sprites), which keeps the per-byte
  VDP write loop comfortably above the gating floor.
- **Polling V-blank sync.** P-LAB stock leaves /INT floating
  (SILICONBUGS Bug N°2), so we spin on bit 7 of `$CC01` between
  frames.
- **Delta-encoded animation.** The full 18 KB animation cannot fit on
  the stock 4 KB Apple-1, so we ship one full frame plus a 102-entry
  delta table to a mid-cycle anchor frame. Toggling the two anchors
  every 8 vsyncs gives the cat its bob and approximates the rainbow
  scroll without the extended RAM footprint.

## Build & run

    make                      # → ../../../software/tms9918/TMS_Nyan.{bin,txt}

In POM1:

1. Hardware → plug the TMS9918 card (auto-enabled when loading from
   `software/tms9918/`).
2. File → Load → `software/tms9918/TMS_Nyan.txt`.
3. Wozmon `\` prompt: type `280R`.
4. The cat appears mid-screen, dark-blue starfield around it. After
   about a second the bob animation kicks in.
5. ESC at any point returns to Wozmon.

## What's NOT ported (vs the Z80 original)

- **PT3 music** (the Karbofos chiptune cover). jblang drives an AY-3
  via `UseAY = 1`; this port targets the bare TMS9918 Graphic Card
  with no audio path. A future variant with the P-LAB A1-SID could
  carry a 6581-tracker version of the theme — see how
  `tms9918_split` / Galaga share the bus today.
- **All 12 frames.** Stock 4 KB Apple-1 can't hold the full 18 KB
  animation. We pick 2 anchor frames (#0 and #6) and toggle. The bob
  is recognisable; the rainbow scroll is two-state instead of cycling.
  An 8 KB Apple-1 (or Juke-Box ROM page) could carry all 12 frames —
  see `apple1_nyan.cfg` for the easy edit (bump `CODE` size, append
  more frames to `nyan_data.asm`).
- **Z180 hardware-multiply optimisations.** Not relevant — there's no
  fixed-point math in this demo, only bulk VRAM writes.

## Memory map

    $0000-$002F  ZP (~48 B): VDP write addr, frame state, vsync count,
                 source pointer for bulk upload, name-table init counters
    $0100-$01FF  6502 stack
    $0280-$0FFF  CODE + data (~3.5 KB available, ~2.5 KB used)
                 - code (~700 B)
                 - nyan_frame0 (1 536 B static base)
                 - 4 delta tables (4 × 102 = 408 B)

## Mode III VRAM layout

    $0000-$05FF  Pattern table (1 536 B = 192 entries × 8 B)
    $1800-$1AFF  Name table (768 B fan-out)
    $1B00        Sprite attribute table — Y=$D0 sentinel = sprites off
    (no colour table — Mode III pattern bytes ARE the colour values)

## Animation data sourcing

`nyan_data.asm` is auto-generated from
[jblang's `nyan/nyan.bin`](https://github.com/jblang/TMS9918A/raw/master/examples/nyan/nyan.bin)
(18 432 bytes = 12 frames × 1 536 B). The Python block in this
project's commit message picks frames 0 and 6 (mid-cycle, max delta
contrast — frame 6 differs from frame 0 in 102 of 1 536 bytes). Pick
a different anchor pair or add more anchors by re-running the
generation script in the project commit history.

## Sources

- `TMS_Nyan.asm` — Mode III init + 6-section name table + animation loop
- `nyan_data.asm` — auto-generated frame 0 + delta tables
- `apple1_nyan.cfg` — ld65 config (4 KB stock layout)
- `emit_TMS_Nyan_txt.py` — assemble + emit Woz hex `.txt`

## Author / License

VERHILLE Arnaud, 2026. Algorithm and frame-format derive from
J.B. Langston (Z80 port, MIT licence — see jblang/TMS9918A);
artwork is [*Passan Kiskat*](http://www.dromedaar.com/) by
Dromedaar Vision.
