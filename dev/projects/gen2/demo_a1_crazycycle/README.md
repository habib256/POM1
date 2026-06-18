# A-1-CrazyCycle — beam-raced demo for Uncle Bernie's *release* GEN2 card

*[← POM1 documentation index](../../../../doc/README.md)*

Validation demo for the `$C250-$C257` soft switches + HST0 flag on the release
card (spec: `doc/reference/ColorGraphicsCard_doc_for_Arnaud.pdf`, transcribed in
`doc/GEN2_RELEASE_questions.md`), with **2-voice music via the ACI**. This is the
"Bernie validation demo" deliverable from TODO Phase 5.

## Flow

1. **Latch init** — power-on state of the switches is *undefined* on real PLDs
   and the Apple-1 RESET never touches them (Q8): software initialises MIX_OFF,
   PAGE_ONE, HIRES by **reads** (the switches are read-only: a read toggles
   and returns HST0 in D7, a write is ignored).
2. **TEXT** — text page 1 ($0400) is filled with
   `Uncle Bernie HGR COLOR CARD` repeated across 40×24, then revealed
   (`$C251`) for ~3 s.
3. **UBERNIE image** — the HGR picture `sdcard/NONO/HGR/UBERNIE#062000` is
   embedded in the build as a **second hex zone at $2000** of the `.txt`
   (multi-zone Wozmon dump via `emit_woz.py extra_zones`): it is already
   in the framebuffer at start-up. Revealed by `$C250` (TEXT off → HIRES),
   ~3 s. (The `.bin` alone shows the DRAM power-on noise of the card.)
4. **Beam-raced bouncing window + music** — a 168×64 px rectangle through
   which the TEXT page is visible, moving and bouncing off the four edges:
   on each scanline of the band, `LDA $C251` is read at the exact cycle the
   beam enters the window and `LDA $C250` at the exact cycle it leaves
   (horizontal mid-scanline split — the flagship feature of the release card),
   while a 2-voice chiptune is output through the ACI's TAPE OUT. A keypress
   returns to Wozmon.

## Cycle-exact synchronisation (60 Hz: 262 lines × 65 cycles = 17030)

Three stages, detailed in source comments:

1. **WAITVBL** (coarse) — VBL detection via ORed double sampling of HST0 (the
   3-cycle notch burst, hcnt 13-15, cannot zero two reads spaced 4 cycles apart
   — Bernie's Listing 1); a blank that persists past the 25 cycles of an
   H-blank is the V-blank.
2. **Phase scan** (exact to ±0 cycle) — one HST0 sample every 66 cycles
   (one line + 1) slides by +1 cycle per line along the scanline; the 4th
   consecutive zero identifies the H-blank→live edge: its bus access touched
   hcnt 28 exactly (zeros at 25,26,27,28). The zero counter starts poisoned
   ($80) so a start mid-live-scan never triggers a false lock, and the scan
   runs on lines ~5-80, away from V-blank.
3. **Line lock** — with horizontal phase known, sample hcnt 45 (never blanked,
   never in the burst) every 65 cycles: 0 → live lines, 1 → V-blank, first
   following 0 = line 0.

Then the frame loop runs **free-run at exactly 17030 cycles/frame**, never
re-synced — zero jitter.

## Constant-cost moving window

- **Vertical**: pre/post-window burners with variable line counts (constant
  sum: `vpos` + `195-vpos`). `vpos` = vtab[fcnt], piecewise waveform (full
  traversal, fast double bounce at mid-height, return, small top bounce —
  256-frame period).
- **Horizontal**: every scanline goes through two 8-NOP slides via
  `JMP (ind)`; entry offsets (8-H and H) cancel — delays 2H and 16-2H,
  constant sum. `hoff` = htab[hidx], **separate 192-period counter** (wrap
  via balanced branch): lcm(256,192) = 1536 frames ≈ 25.6 s before the
  combined trajectory repeats.
- Taken branches in the timed loops are locked same-page by ld65 `.assert`
  (two adjustable "layout shims"); tables are page-aligned (4-cycle constant
  indexed reads).

## 2-voice music via the ACI (Bernie Q7)

Any `$C0xx` access toggles the ACI's TAPE OUT flip-flop — the SPEAKER `$C030`
convention from Apple II ports (the release card moved its switches to
`$C25x` precisely so that `$C0xx` stays available for the ACI; POM1's preset
11 plugs the ACI alongside GEN2, mirroring the real PCB with its jack
cut-out).

- **Tick per scanline slot**: each 65-cycle slot (both burner lines AND
  window scanlines — the tick fits in the 20-cycle gap between TEXT_ON and
  TEXT_OFF, with the countdown carried in Y) decrements a counter and
  toggles `$C030` on zero. Half-period = N slots → f = 7867/N Hz,
  cycle-accurate.
- **2 voices on 1 bit (virtual polyphony)**: the sequencer (branchless,
  index = fcnt>>2 into a 64-note table) alternates BASS and MELODY every 4
  frames (~66 ms) — the ear separates the walking bass
  (C3/G3 · A2/E3 · F2/C3 · G2/D3) from the arpeggios (I-vi-IV-V in C). A
  4-bar loop = 256 frames, in phase with the vertical bounce.
- The ACI **records** the tune while playing it (real behaviour):
  `--save-tape output.aci` on POM1 exit gives back a playable cassette.
  Accuracy verified by analysing the cassette pulse durations: exact
  plateaus at N×65 cycles, bass/melody sequence matching the 4 bars.

## Caveats

- **60 Hz only** — leave the HGR window's "50 Hz vertical" checkbox unchecked
  (the loop counts 17030 cycles; at 312 lines it would need 20280).
- **Real hardware**: free-run assumes 1 CPU cycle = 1 video cycle — true on
  SRAM replicas (Briel) and in POM1; on an original Apple-1 the DRAM refresh
  steals 4 cycles out of 65 and would drift the loop (caveat documented by
  Bernie). The HST0 poll reads `$C254` (PAGE_ONE): the program lives on
  page 1, so the poll toggle is always a no-op.
- `INC`/`DEC` zp avoided in timed code (POM1 counts them at 4 cycles, real
  6502 at 5) — replaced by LDA/ADC/STA, identical on both sides.

## Build & run

```bash
make          # → software/Graphic HGR/A-1-CrazyCycle.{bin,txt}
```

POM1: preset 11 (GEN2 HGR + ACI), load `A-1-CrazyCycle.txt` (the
`Graphic HGR/` folder auto-enables the card), then `E000R`. Or from CLI:

```bash
./build/POM1 --preset 11 --load 'E000:software/Graphic HGR/A-1-CrazyCycle.txt'
# (the .txt loads code + image; --save-tape music.aci to keep the tune)
```
