# lib/sid — P-LAB A1-SID sound card primitives

Equates + initialisation + note-trigger helpers for the P-LAB A1-SID
(MOS 6581 / 8580 at `$C800-$CFFF`). Replaces the inline-everywhere
register storage you see in `dev/projects/sid_piano/` and unblocks
jingles + SFX in any future game.

## Files

- **`sid.inc`** — pure equates: per-voice register addresses (V1, V2,
  V3), waveform masks (`SID_TRI`, `SID_SAW`, `SID_PULSE`, `SID_NOISE`,
  `SID_GATE`), filter/volume globals.
- **`sid_init.asm`** — `sid_init`: zero all 29 visible registers, set
  master volume to max, install a generic ADSR for voice 1.
- **`sid_notes.inc`** — generated 96-note frequency table (C0..B7) plus
  `NOTE_<NAME><OCTAVE>` equates. Re-emit via
  `python3 tools/build_sid_notes.py > dev/lib/sid/sid_notes.inc` if the
  CPU clock changes (locked to 1.022727 MHz today).
- **`sid_play.asm`** — `sid_v1_note` (A = note index → set V1 freq),
  `sid_v1_gate` (A = waveform mask → trigger ADSR), `sid_v1_off`
  (release).

## Public symbols

| Symbol | Type | Description |
|---|---|---|
| `SID_V1_FREQLO`..`SID_V3_SR` | equate | per-voice 7-register block |
| `SID_VOLUME`, `SID_FCLO`, `SID_FCHI`, `SID_RES_FILT` | equate | globals |
| `SID_PADDLE_X`, `SID_PADDLE_Y` | equate | A1-SID paddle inputs |
| `SID_GATE`, `SID_TRI`, `SID_SAW`, `SID_PULSE`, `SID_NOISE` | equate | waveform / control bits |
| `SID_FILT_LP`, `_BP`, `_HP`, `SID_VOICE3_OFF` | equate | $C818 mode bits |
| `sid_notes_lo`, `sid_notes_hi` | data | 96-byte each, freq tables |
| `NOTE_C0`..`NOTE_B7` | equate | 0..95 note indices |
| `sid_init` | routine | silence + defaults |
| `sid_v1_note` | routine | A = note → V1 freq |
| `sid_v1_gate` | routine | A = waveform → trigger |
| `sid_v1_off` | routine | gate off |

ZP usage: zero. All routines use only A/X/Y and direct SID register access.

## Use

```asm
.include "apple1.inc"
.include "sid.inc"
.include "sid_notes.inc"
.include "sid_init.asm"
.include "sid_play.asm"

main:
        JSR sid_init           ; silence + max volume + V1 ADSR

        ; --- Play a C4 triangle for 250 ms ---
        LDA #NOTE_C4
        JSR sid_v1_note
        LDA #SID_TRI
        JSR sid_v1_gate
        LDA #250
        JSR delay_ms_a         ; lib/apple1/delay.asm
        JSR sid_v1_off

        ; --- Then E4 ---
        LDA #NOTE_E4
        JSR sid_v1_note
        LDA #SID_TRI
        JSR sid_v1_gate
        LDA #250
        JSR delay_ms_a
        JSR sid_v1_off
```

In your project Makefile:

    LIB := -I ../../lib/apple1 -I ../../lib/sid

## Required preset

A1-SID must be plugged. POM1 presets that ship with SID enabled:

- `--preset 6` — A1-SID alone
- `--preset 11` — Juke-Box (coexists with SID)
- `--preset 12` / `--preset 14` — multiplexing fantasy

If you load a SID program under a preset without the card, all the
register stores are no-ops and you hear silence (no error).

## Polyphony

`sid_v1_*` only touches voice 1. For multi-voice tunes, copy the three
routines into your project and rewrite each `SID_V1_*` reference to
`SID_V2_*` or `SID_V3_*`. A future revision could parameterise on a
voice-base ZP slot, but most jingles use one voice — premature for now.

## Note table accuracy

Equal-temperament tuning anchored on A4 = 440 Hz. Worst-case rounding
error (lowest octave) is ~0.04 % (one cent of a semitone), inaudible.
The table covers 8 octaves comfortably under the SID's 16-bit range
(B7 sid_value = $FD2F, headroom for register tricks).
