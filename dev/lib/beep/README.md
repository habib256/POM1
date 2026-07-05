# dev/lib/beep — 1-bit "beeper" SFX (ACI speaker)

*[← dev/lib index](../README.md)*  ·  SID synth track: [`../sid`](../sid/README.md)  ·  pinned by [`../test/micro/t11_beep_sfx.s`](../test/micro/t11_beep_sfx.s)

Sound effects on the Apple 1 with **no sound chip**: toggle the ACI output
flip-flop and let the deck speaker click. In POM1 a **read** of any address in
`$C000–$C0FF` **except `$C081`** (the tape-IN poll) flips the speaker level
(`Memory.cpp` "ACI_toggle" bus handle → `CassetteDevice::toggleOutput`). Reading
it at a steady interval makes a square wave — the same trick the GEN2
*A-1-CrazyCycle* chiptune uses. `$C030` is the canonical toggle address.

This is the **beeper half** of POM1's sound story (the [`sid`](../sid/README.md)
card is the synth half), and the export target for the in-app beeper SFX editor:
an editor "sound effect" is exactly one of the data tables below plus a `JSR`.

## Files

| File | Role |
|---|---|
| [`beep.inc`](beep.inc) | `BEEP_TOGGLE = $C030` + the SFX data-format spec. Idempotent include guard. |
| [`beep_sfx.asm`](beep_sfx.asm) | the data-driven player: `sfx_start` / `sfx_tick` / `sfx_active` / `sfx_play`. |
| [`beep_sfx_bank.inc`](beep_sfx_bank.inc) | starter bank — `sfx_coin` (up-blip), `sfx_laser` (down sweep), `sfx_hit` (buzz + rest + thud). |
| [`beep_sfx_bank50.inc`](beep_sfx_bank50.inc) | **50-cue bank** — pickups, lasers, explosions, movement, UI, bells, alarms, sci-fi, magic, water, fail (`sfx_coin`…`sfx_gameover`). The Beeper SFX editor loads the same 50 in its bank browser (mirror: `src/sfxbeep/SfxBank.h`). |

## SFX data format

An SFX is a flat list of 2-byte **steps**, terminated by a step whose length is 0:

```asm
        .byte period, length     ; step 0
        .byte period, length     ; step 1
        ...
        .byte $00,    $00        ; terminator (length 0 ends it)
```

- **period** → pitch (inner delay count; larger = lower note). `0` = **REST**
  (silent gap for the step's duration — no toggle).
- **length** → duration, in speaker half-cycles.
- A pitch **sweep** is just successive steps with a rising/falling `period`
  (see `sfx_laser`).

## Player ABI

| Entry | In | Does |
|---|---|---|
| `sfx_start` | `X`=lo, `Y`=hi of a table | arm it (no sound yet) |
| `sfx_tick` | — | play ONE step (a short burst), then advance. **Non-blocking across steps**: call once per frame; no-op when idle → safe to call every frame. Clobbers A,X,Y. |
| `sfx_active` | — | `A` ≠ 0 while still playing (test after `sfx_tick`) |
| `sfx_play` | `X`=lo, `Y`=hi | **blocking**: run every step now (title jingles / death stings) |

A single burst still blocks for its own (short) duration — 1-bit sound is
CPU-toggled, there is no timer. `sfx_tick` bounds that to one step so a game
keeps running between them.

**Zero page**: owns `sfx_ptr` / `sfx_per` / `sfx_len` (`.exportzp`'d). A program
sharing the [`apple1/zp.inc`](../apple1/zp.inc) pool must not alias these three.

## Usage

```asm
        .include "beep.inc"
        .include "beep_sfx_bank.inc"   ; or your own tables
        .import  sfx_play, sfx_start, sfx_tick, sfx_active

        ; one-shot (blocks): a pickup jingle
        LDX #<sfx_coin
        LDY #>sfx_coin
        JSR sfx_play

        ; non-blocking: arm once, drive per frame from the game loop
        LDX #<sfx_laser
        LDY #>sfx_laser
        JSR sfx_start
game_frame:
        ; ... game logic ...
        JSR sfx_tick                   ; advances the SFX one step, no-op when done
        JMP game_frame
```

## Requirements

- **ACI enabled** (Hardware menu / an ACI preset).
- **No audio-stream tape** (mp3/ogg/wav) inserted: while a stream plays,
  `CassetteDevice::fillAudioBuffer()` does not mix the live pulse queue, so
  toggles are silent. An empty deck or a program tape is fine.

## Test

[`../test/micro/t11_beep_sfx.s`](../test/micro/t11_beep_sfx.s) pins the table
machine (step stride, terminator → idle, REST handling) headless via the RAM
mailbox — the audible toggling is eyeballed in the app.
