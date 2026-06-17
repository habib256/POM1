# A1-IO RTC Clock — text mode

*[← POM1 documentation index](../../../../doc/README.md)*

Centred date+time line driven by the P-LAB A1-IO & RTC card (DS3231).
Forty-column layout, English month names (`JANUARY..DECEMBER`), 5-second
refresh. Reads broadcast registers from the emulated ATMEGA32 over the
65C22 VIA at `$2000-$200F`.

## Hardware

- Machine: Apple 1 (4 KB DRAM is enough)
- Cards: P-LAB A1-IO & RTC
- Recommended POM1 preset: 8 (P-LAB I/O Board & RTC).

## Sources

- `RtcClock.asm` — main entry, loads at `$0280`
- libs used: `dev/lib/apple1/`, `dev/lib/a1io/`

## Build

    make                          # produces ../../../../software/a1io_rtc/RtcClock.bin

By hand:

    ca65 -I ../../../lib/apple1 -I ../../../lib/a1io RtcClock.asm
    ld65 -C ../../../cc65/apple1_4k.cfg RtcClock.o -o ../../../../software/a1io_rtc/RtcClock.bin

## Run in POM1

1. POM1 → Presets → the A1-IO & RTC card (`--enable rtc`).
2. File → Load → `software/a1io_rtc/RtcClock.bin` (or the matching `.txt`).
3. Wozmon `\` prompt: type `280R`.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
