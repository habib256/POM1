# SID Piano (AZERTY) — Claudio Parmigiani

The original P-LAB A1-SID keyboard piano by Claudio Parmigiani (2019),
ported to ca65 for POM1 with an AZERTY layout (DAW-standard middle/top
row mapping). Press a row key to play a note, `1`-`8` to change octave,
`P/O/I/A` to swap waveform (pulse / noise / triangle / sawtooth).

Two-segment layout per `piano.cfg`: the program at `$0600` plus a
fixed-position screen buffer at `$0850`.

## Hardware

- Machine: Apple 1 (4 KB DRAM is enough)
- Cards: P-LAB A1-SID
- Recommended POM1 preset: TODO — pick the SID preset.

## Sources

- `Claudio_PARMIGIANI_SID_PIANO_AZERTY.asm` — main entry, loads at `$0600`
- `piano.cfg` — local linker config (CODE `$0600`, SCREEN `$0850`)
- libs used: none (constants inlined in the source)

## Build

    make                          # produces ../../../software/sid/Claudio_PARMIGIANI_SID_PIANO_AZERTY.bin

By hand:

    ca65 Claudio_PARMIGIANI_SID_PIANO_AZERTY.asm
    ld65 -C piano.cfg Claudio_PARMIGIANI_SID_PIANO_AZERTY.o \
        -o ../../../software/sid/Claudio_PARMIGIANI_SID_PIANO_AZERTY.bin

## Run in POM1

1. POM1 → Presets → SID preset (TODO).
2. File → Load → `software/sid/Claudio_PARMIGIANI_SID_PIANO_AZERTY.bin`.
3. Wozmon `\` prompt: type `600R`.

## Author / License

Claudio Parmigiani, 2019 (original). AZERTY port: VERHILLE Arnaud, 2026.
License: TODO.
