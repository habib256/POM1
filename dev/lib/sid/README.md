# lib/sid — P-LAB A1-SID sound card primitives

*[← dev/lib index](../README.md)*

Equates + initialisation + note-trigger helpers for the P-LAB A1-SID
(MOS 6581 / 8580 at `$C800-$CFFF`). Replaces the inline-everywhere
register storage you see in `sketchs/apple1/sid_piano/` and unblocks
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
  (release). The low-level "poke one note" primitives.
- **`sid_player.asm`** — a **data-driven single-voice sequencer** (the SID
  analogue of [`beep/beep_sfx.asm`](../beep/README.md), and the runtime the SID
  tracker editor exports to). `sid_play_init` / `sid_play_start` (X:Y = song) /
  `sid_play_tick` (advance one frame, non-blocking) / `sid_play_active` /
  `sid_play_stop`. Song = 3-byte rows `[note, ctrl, frames]` (note `$FE`=gate
  off, `$FF`=tie; `frames`=0 ends). Self-contained (bundles the note table).
  Pinned by [`../test/micro/t12_sid_player.s`](../test/micro/t12_sid_player.s).

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

- `--enable sid` — A1-SID alone
- `--preset 10` / `--preset 12` — multiplexing fantasy (A1-SID + other cards)

If you load a SID program under a preset without the card, all the
register stores are no-ops and you hear silence (no error).

## Polyphony

`sid_v1_*` only touches voice 1. For multi-voice tunes, copy the three
routines into your project and rewrite each `SID_V1_*` reference to
`SID_V2_*` or `SID_V3_*`. A future revision could parameterise on a
voice-base ZP slot, but most jingles use one voice — premature for now.

### Multi-voice pattern (voice-base offset)

The three voice blocks are **identical and 7 registers apart**:
`SID_V1_FREQLO = $C800`, `SID_V2_FREQLO = $C807`, `SID_V3_FREQLO = $C80E`
(`SID_BASE + voice*7`). Each register keeps a fixed offset inside its block
(`FREQLO`=+0, `FREQHI`=+1, `PWMLO`=+2, `PWMHI`=+3, `CR`=+4, `AD`=+5,
`SR`=+6 — see `sid.inc`). So one routine drives any voice by computing the
block base once into a ZP pointer (`sid.inc`'s equates are zero-ZP; you
supply your own slot pair) and indexing with `Y`:

```asm
; ZP: vbase_lo / vbase_hi — set to SID_BASE + voice*7 before calling.
; Worked once for voice N (N = 0,1,2): vbase = $C800 + N*7.
;   voice 0 → $C800   voice 1 → $C807   voice 2 → $C80E

; --- set this voice's 16-bit frequency from note index in A ---
voice_note:                 ; A = note index, vbase_lo/hi = voice block
        TAX
        LDA sid_notes_lo,X
        LDY #0              ; +0 = FREQLO
        STA (vbase_lo),Y
        LDA sid_notes_hi,X
        INY                 ; +1 = FREQHI
        STA (vbase_lo),Y
        RTS

; --- gate this voice (A = waveform mask, e.g. SID_TRI) ---
voice_gate:                 ; A = waveform mask
        ORA #SID_GATE
        LDY #4             ; +4 = CR (gate + waveform)
        STA (vbase_lo),Y
        RTS

; --- release this voice ---
voice_off:
        LDA #0
        LDY #4
        STA (vbase_lo),Y
        RTS
```

Per-voice state (note pointer, envelope phase, duration counter) lives in
your own arrays indexed by voice number; only `vbase_lo/hi` and `Y` differ
between voices. ADSR (`AD`=+5, `SR`=+6) and pulse width (`PWMLO/HI`=+2/+3)
follow the same offset table if a voice needs a custom envelope.

## Note table accuracy

Equal-temperament tuning anchored on A4 = 440 Hz. Worst-case rounding
error (lowest octave) is ~0.04 % (one cent of a semitone), inaudible.
The table covers 8 octaves comfortably under the SID's 16-bit range
(B7 sid_value = $FD2F, headroom for register tricks).
