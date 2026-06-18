# GT-6144 Hello — clear + centred 2-line text

*[← POM1 documentation index](../../../../doc/README.md)*

Smallest possible program for the SWTPC GT-6144 Graphic Terminal (1976,
$98.50). Clears the 64×96 monochrome framebuffer (Intel 2102 bistable
SRAM powers up with random bits — visible as the classic "snow" of
random rectangles) and draws two centred lines with a 3×5-pixel font:

    APPLE-1
    GT-6144

## Hardware

- Machine: Apple 1 (stock 4 KB)
- Cards: SWTPC GT-6144 (write-only port at `$D00A`)
- Recommended POM1 preset: 5 (Apple-1 + SWTPC GT-6144).

## Sources

- `GT1_Hello.asm` — main entry, `.org $0300`
- `gt6144.cfg` — local linker config (`CODE` at `$0300`, 4 KB cap)
- libs used: `dev/lib/apple1/`, `dev/lib/gt6144/`

## Build

    make                          # produces ../../../../software/Graphic gt-6144/GT1_Hello.bin

By hand:

    ca65 -I ../../../lib/apple1 -I ../../../lib/gt6144 GT1_Hello.asm
    ld65 -C gt6144.cfg GT1_Hello.o -o ../../../../software/Graphic gt-6144/GT1_Hello.bin

## Run in POM1

1. POM1 → Presets → preset 5 (Apple-1 + SWTPC GT-6144).
2. File → Load → `software/Graphic gt-6144/GT1_Hello.txt`.
3. Wozmon `\` prompt: type `300R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
