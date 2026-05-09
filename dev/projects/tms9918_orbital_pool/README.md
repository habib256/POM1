# TMS Orbital Pool — gravitational billiards

Aim, charge, fire — bend the trajectory of your ball around 1..3
gravity wells to hit the target. 32×24 char-cell grid (native 8×8 TMS
chars), three levels, on the P-LAB TMS9918 Graphic Card. Companion of
[`hgr_orbital_pool`](../hgr_orbital_pool/) (same level data and
controls, GEN2 HGR rendering at 7×8-pixel cell resolution).

Controls: `A`/`Z` rotate aim (16 directions, wraps); `S`/`X` adjust
power (1..7); `SPACE` fires; `R` resets the shot.

## Implementation note vs. the HGR port

Both ports share the same level layouts, the same 16-direction aim LUT
and the same 1..7 power scale. The integration loops (`apply_gravity`,
ball stepping) are independent re-implementations rather than a shared
lib — this port factors the multiply through `umult_8x8` + `lsr_16_x`
helpers, the HGR port inlines a (>>4)-shift variant. Visually similar
gameplay, distinct code. If you re-tune one, re-validate the other.

## Hardware

- Machine: Apple 1 (4 KB DRAM)
- Cards: P-LAB TMS9918
- Recommended POM1 preset: TODO — pick a TMS9918 preset.

## Sources

- `TMS_OrbitalPool.asm` — main entry, loads at `$0280`
- `emit_TMS_OrbitalPool_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/`

## Build

    make                          # produces ../../../software/tms9918/TMS_OrbitalPool.{bin,txt}

By hand:

    ca65 -I ../../lib/apple1 TMS_OrbitalPool.asm
    ld65 -C ../../cc65/apple1_4k.cfg TMS_OrbitalPool.o \
        -o ../../../software/tms9918/TMS_OrbitalPool.bin
    python3 emit_TMS_OrbitalPool_txt.py

## Run in POM1

1. POM1 → Presets → TMS9918 preset (TODO).
2. File → Load → `software/tms9918/TMS_OrbitalPool.txt`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
