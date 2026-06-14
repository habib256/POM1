# GEN2 Release Card — Developer Guide (the "Bernie SDK")

Everything needed to write or port software for Uncle Bernie's GEN2 Color
Graphics Card on the Apple-1, using POM1 as the development machine.

**Audience:** Uncle Bernie (whose hardware dev rig is down) and anyone porting
Apple II HGR software to the Apple-1. **Hardware truth:** Bernie's PDF
`doc/ColorGraphicsCard_doc_for_Arnaud.pdf`, transcribed with full Q&A status
in [`GEN2_RELEASE_questions.md`](GEN2_RELEASE_questions.md) (Q1–Q10 all
resolved). **Reference implementation:** `dev/projects/a1_crazycycle/` —
beam-raced bouncing window + 2-voice ACI music, every cycle accounted for.

---

## 1. The card in one page

Full Apple II video subsystem on the Apple-1 expansion connector:

| Mode | Resolution | Memory |
|------|-----------|--------|
| TEXT  | 40×24, B&W, full-ASCII 2716 char gen | page 1 `$0400`, page 2 `$0800` |
| LORES | 40×48, 16 colours | same pages as TEXT |
| HIRES | 280×192, NTSC artifact colour (6 colours) | page 1 `$2000-$3FFF`, page 2 `$4000-$5FFF` |
| MIXED | graphics + bottom 4 TEXT rows | `$C253` |

- **RAM expansion (Q9):** the card carries 48 KB DRAM covering `$0000-$BFFF`
  (CPU writes reach it through the VMA write-through latch) and the
  motherboard keeps `$E000-$EFFF` — Bernie quotes **54 KB** total. POM1
  preset 12 models this configuration (48 KB + the `$E000` bank).
- **Timing (Q4):** 65 CPU cycles/scanline; **262 lines @ 60 Hz** or
  **312 @ 50 Hz** (vertical jumper; NTSC colour either way). Lines 0-191
  live; visible bytes at h-counter 25-64. **~4200 cycles of V-blank budget**
  before the beam re-lights.
- **Dual monitor:** the Apple-1 terminal (`$D012`) keeps working — text UI on
  the terminal, graphics on the GEN2 output.

## 2. Soft switches `$C250-$C257` — READ-ONLY (Q1/Q2/Q6)

```
$C250  TEXT_OFF   graphics on          $C254  PAGE_ONE   $0400 / $2000
$C251  TEXT_ON    text (kills gfx)     $C255  PAGE_TWO   $0800 / $4000
$C252  MIX_OFF    full screen          $C256  LORES_ON   RES latch = LORES
$C253  MIX_ON     bottom 4 rows text   $C257  HIRES_ON   RES latch = HIRES
```

The single most important difference from the Apple II:

> **Only a READ changes a switch.** The read also returns the HST0 blank
> flag in bit 7. **Writes are ignored** (a write would clash the card's D7
> bus driver). Use `LDA $C25x` / `BIT $C25x`, never `STA`.

Decode is `SEL = $Cxxx & !A11 & A9 & A4`: the eight switches mirror every
8 bytes across `$C2xx/$C3xx/$C6xx/$C7xx` wherever A4=1. Use the canonical
`$C250-$C257` block.

**Power-on (Q8):** the latch state is **indeterminate** (PLD POR is
untrustworthy) and **Apple-1 RESET never touches it**. Initialise every
switch your program relies on, always. (POM1's documented cold state is
GRAPHICS+HIRES+PAGE1 — do not rely on it on real hardware.)

## 3. HST0 — the blank flag (Q3), and how to sync with it

Bit 7 of any decoded `$C25x` read:

```
1 = blanking   (H-blank: hcnt 0-24 of every line, or V-blank: lines 192+)
0 = live scan  (hcnt 25-64 of lines 0-191)
EXCEPTION: 0 during the 3-cycle colour burst (hcnt 13-15) — even in V-blank.
```

Three rules of thumb:

1. **Poll a switch you are already in** — the poll read toggles it; polling
   `$C254` (PAGE_ONE) from a page-1 program is a no-op toggle.
2. **Mask the burst notch** by ORing two samples 4+ cycles apart
   (Bernie's Listing 1) — the notch is only 3 cycles wide:
   ```asm
   poll:  LDA GEN2_PAGE1
          ORA GEN2_PAGE1
          BMI in_blank        ; reliable: notch can't zero both reads
   ```
3. **V-blank vs H-blank:** an H-blank lasts 25 cycles; wait 26 cycles after
   a blanking edge and re-sample — still blank ⇒ V-blank.

The old Apple II `$C050` vaporlock is **dead** with the ACI present (Q7) —
HST0 replaces it, and is strictly more powerful (H *and* V information).

### Ready-made sync code — `dev/lib/gen2/`

```asm
        .include "gen2.inc"        ; equates + HST0 cheat-sheet
        ...
        .include "gen2_sync.asm"   ; (in CODE, behind a JMP)
        ...
        JSR gen2_waitvbl           ; coarse: returns just after VBL starts
        JSR gen2_beam_lock         ; exact: returns at (line 0, hcnt 55) ±0
```

`gen2_beam_lock` pins the beam to the cycle in three stages (coarse VBL →
66-cycle sliding phase scan that finds the H-blank edge exactly → 65-cycle
line scan that finds line 0). After it returns, a frame is exactly
**17030 cycles** (60 Hz): a free-running loop that consumes exactly that per
iteration stays locked forever — zero jitter, no re-sync. See
`A-1-CrazyCycle.asm` for the complete pattern (constant-time variable
delays via NOP-slides + `JMP (ind)`, balanced branches, link-time `.assert`
page guards).

### Mid-scanline splits (the flagship feature)

Reading TEXT_ON at the exact cycle the beam reaches column L, and TEXT_OFF
when it leaves, splits a single scanline into graphics | text | graphics.
Column ↔ cycle mapping: the visible window opens at hcnt 25, one byte
column per cycle. **End every split line back in graphics** — Bernie: the
colour burst must stay alive or the TV's colour killer may drop the next
line to B&W.

## 4. Porting Apple II software — the rules (Q7)

| Apple II habit | On the GEN2 Apple-1 |
|---|---|
| `$C050-$C057` soft switches (read **or write**) | rewrite to `$C250-$C257`, **reads only** |
| `$C019` VBL flag (IIe/IIc) | HST0 in bit 7 of any `$C25x` read (mind the burst notch) |
| `$C050` vaporlock / floating bus | dead — use HST0 |
| `$C030-$C03F` SPEAKER clicks | **keep them unchanged** — the ACI TAPE OUT flip-flop toggles on any `$C0xx` read and plays through its jack |
| other `$C0xx` I/O (keyboard `$C000/$C010`, paddles, annunciators…) | **remove/neutralise** — `$C0xx` belongs to the ACI here |
| keyboard `$C000` strobe `$C010` | Apple-1 PIA: data `$D010` (bit 7 = ready in `$D011`; reading `$D010` clears the strobe) |
| text/`COUT` to the Apple II screen | Apple-1 terminal via Wozmon `ECHO $FFEF` (dual monitor!) or GEN2 TEXT page writes |
| HGR page 2 `$4000` | identical (`$C255`) |
| 48 KB RAM assumption | fine — the card IS a 48 KB expansion (Q9) |
| DHGR / 80-col / AN3 | does not exist (Apple II, not IIe) |

Tooling: `dev/cc65/apple1_gen2.cfg` (CODE at `$E000`, HGR pages reserved),
libraries `dev/lib/gen2/` (this SDK), `dev/lib/hgr/` (scanline tables,
plot), `dev/lib/apple1/` (Wozmon/PIA equates, print). Multi-zone loading:
`emit_woz.py` bundles raw blobs (e.g. an 8 KB HGR image at `$2000`) into the
program's `.txt` via `extra_zones` — one Wozmon load installs everything.

## 5. Sound: the ACI is the speaker (Q7)

The release card moved its switches out of `$C0xx` precisely so the ACI
keeps the whole range. Any `$C0xx` read toggles the TAPE OUT flip-flop —
1-bit Apple II-style speaker audio out of the cassette jack. The
`A-1-CrazyCycle` demo plays a two-voice chiptune this way (per-scanline
countdown ticks, note half-period N scanlines → f = 7867/N Hz, bass/melody
alternation for virtual polyphony) — and since the ACI *records* what it
plays, `--save-tape tune.aci` keeps the music as a playable tape.

## 6. Developing with POM1

- **Preset 12** = Bernie's real machine: GEN2 (beam-raced, all modes,
  HST0-exact) + ACI + 48 KB. The `software/Graphic HGR/` folder auto-plugs
  the card on load.
- **Dev loop:**
  ```bash
  cd dev/projects/<yours> && make        # ca65 + ld65 + Woz-hex .txt
  ./build/POM1 --preset 12 --load 'E000:software/Graphic HGR/<P>.txt'
  ```
  `.txt` loads run automatically (trailing `E000R`). Useful flags:
  `--terminal` (telnet 127.0.0.1:6502 — drive the keyboard from a script,
  Ctrl-S grabs a PNG screenshot), `--save-tape` (keep ACI output),
  `--break <addr>`, `--step`, `--snapshot-save/load`. The GEN2 window has
  monitor tints, a live `$C25x` latch readout and the **50 Hz vertical**
  jumper checkbox.
- **Timing fidelity:** POM1's GEN2 scanner counts CPU cycles 1:1 — like an
  SRAM replica (Briel). An original Apple-1 steals 4/65 cycles for DRAM
  refresh: cycle-counted loops that free-run there must account for it
  (CPU → Settings has a DRAM-refresh stall toggle to test that case).
  Avoid `INC/DEC zp` in cycle-counted code for now: POM1 counts them one
  cycle short of real silicon (see `TODO.md` cycle-oracle item);
  `LDA/ADC/STA` sequences cost the same on both.
- **Web build:** the WASM build (`build-wasm/POM1.html`) runs the full GEN2
  + demo bundle in a browser — no install needed to try the card.

---

*POM1 GEN2 emulation status: Phases 0-3 shipped (soft switches, HST0,
beam-raced renderer with vertical and mid-scanline splits), pinned by four
ctest suites. See `TODO.md` → "Uncle Bernie GEN2".*
