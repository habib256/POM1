# HGR Orbital Pool — gravitational billiards

Aim, charge, fire — bend the trajectory of your ball around 1..3
gravity wells to hit the target. 32×24 cell grid (7×8-pixel cells),
three levels, on the GEN2 Color Graphics Card. Companion of
[`tms9918_orbital_pool`](../tms9918_orbital_pool/) (same level data and
controls, TMS9918 char-cell rendering).

Controls: `A`/`Z` rotate aim (16 directions, wraps); `S`/`X` adjust
power (1..7); `SPACE` fires; `R` resets the shot.

## Implementation note vs. the TMS9918 port

Both ports share the same level layouts, the same 16-direction aim LUT
and the same 1..7 power scale. The integration loops (`apply_gravity`,
ball stepping) are independent re-implementations rather than a shared
lib — pull strength uses `(dx * pull_lut[d2]) >> 4` here, and the
companion port has its own variant tuned for the 8×8 char-cell grid.
Visually similar gameplay, distinct code. If you re-tune one,
re-validate the other.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: 12 (Uncle Bernie's GEN2 HGR Color).

## Sources

- `HGR_OrbitalPool.asm` — main entry, loads at `$E000`
- `emit_HGR_OrbitalPool_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`

## Build

    make                          # produces ../../../software/hgr/HGR_OrbitalPool.{bin,txt}

By hand:

    ca65 -I ../../lib/apple1 HGR_OrbitalPool.asm
    ld65 -C ../../cc65/apple1_gen2.cfg HGR_OrbitalPool.o \
        -o ../../../software/hgr/HGR_OrbitalPool.bin
    python3 emit_HGR_OrbitalPool_txt.py

## Run in POM1

1. POM1 → Presets → preset 12 (Uncle Bernie's GEN2 HGR Color).
2. File → Load → `software/hgr/HGR_OrbitalPool.txt`.
3. Wozmon `\` prompt: type `E000R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
