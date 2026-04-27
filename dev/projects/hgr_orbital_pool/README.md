# HGR Orbital Pool — gravitational billiards

Aim, charge, fire — bend the trajectory of your ball around 1..3
gravity wells to hit the target. 32×24 cell grid (7×8-pixel cells),
three levels, on the GEN2 Color Graphics Card. Companion of
`tms9918_orbital_pool` (same gameplay, TMS9918 char-cell rendering).

Controls: `A`/`Z` rotate aim (16 directions, wraps); `S`/`X` adjust
power (1..7); `SPACE` fires; `R` resets the shot.

## Hardware

- Machine: Apple 1 (8 KB DRAM)
- Cards: GEN2 HGR
- Recommended POM1 preset: TODO — pick a GEN2 HGR preset.

## Sources

- `HGR_OrbitalPool.asm` — main entry, loads at `$0280`
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

1. POM1 → Presets → GEN2 HGR preset (TODO).
2. File → Load → `software/hgr/HGR_OrbitalPool.txt`.
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: TODO.
