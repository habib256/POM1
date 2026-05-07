# TMS_SilTest — Silicon validation suite for the P-LAB TMS9918 card

A 16 KB Apple-1 binary that probes four silicon-discriminating behaviours
of the TMS9918A and prints the results in plain text on the TMS9918 video
output. Run on real silicon, write down the results, compare to POM1 in
silicon-strict mode.

The point isn't "POM1 vs silicon, who wins" — it's to **resolve a few
open questions** where POM1 currently makes a guess that needs silicon
confirmation, in particular **Bug N°7 (sprite engine during display blank)**.

## How to load on real Apple-1 + TMS9918

The binary loads at **`$0280`**. Three options to bring it in:

### Option 1 — Wozmon paste (most universal)

```
> Wozmon prompt: paste software/tms9918/TMS_SilTest.txt verbatim
> When the prompt returns: 280R
```

The `.txt` file is Woz-hex format (`addr: bytes...`), 10 KB, ~150 lines.
Any Apple-1 with a serial keyboard interface or a cassette deck can
ingest it.

### Option 2 — Cassette (ACI / Apple Cassette Interface)

```
> C100R              (boot the ACI Woz monitor)
> 280.D14R           (load range from cassette)
> 280R               (run)
```

Note: the cassette image needs to be made first (use POM1's
`tools/wav_from_aci.py` or equivalent).

### Option 3 — microSD (P-LAB SD card)

```
> 6000R              (boot the SD CARD OS)
> LOAD SILTEST       (auto-binds to address $0280 from the .bin)
> 280R               (run)
```

## What you'll see

After ~3 seconds of black screen (the tests run on TMS9918 graphic state,
not text), the screen flips back to text mode and shows:

```
   TMS9918 SILICON VALIDATOR V1.0
        PARMIGIANI VALIDATION SUITE

  TEST                            BIT 5

  T1 SPRITE SCAN IN BLANK    Y/N: ?
  T2 OVERSCAN COLLISION X<0  Y/N: ?
  T3 /INT TO /IRQ WIRING     Y/N: ?
  T4 5S LATCH FIRST-OF-FRAME Y/N: ?

  Y = STATUS BIT 5 SET (COLLISION)
  N = STATUS BIT 5 CLEAR (NO COLL)
  T3 Y = /INT FIRES IRQ HANDLER

  POM1 REFERENCE: T1=N T2=Y T3=Y T4=Y
  T1 IS THE OPEN QUESTION:
    IF SILICON SAYS Y, POM1 NEEDS FIX
    IF SILICON SAYS N, POM1 IS CORRECT

  DONE - POWER CYCLE FOR RERUN
```

The four `?` markers will be either `Y` or `N`. **Write them down.**

## What each test means

### T1 — Sprite scan during display blank (THE OPEN QUESTION)

**Setup**: Two opaque sprites, fully overlapping at Y=80, X=100, colour 15.
Display blanked (R1.6=0). Wait two frames. Read status register.

**What it tells you**: Does the sprite engine continue to scan the SAT
(and latch collision/5S in status bits 5/6) when the display is blanked?

The TMS9918A datasheet is **ambiguous** on this point. The MSX BiFi
reference suggests yes, but I've never seen it confirmed on a real
TI-99/4A or P-LAB Apple-1 board. POM1 currently says **no** (skips
sprite scan when R1.6=0).

| Silicon answers | What it means |
|---|---|
| `Y` (bit 5 set) | Silicon scans sprites during blank. POM1 needs to be patched. |
| `N` (bit 5 clear) | Silicon doesn't scan. POM1 is correct. |

This is the most valuable result of this test suite.

### T2 — Overscan collision

**Setup**: Two early-clock sprites at X=10 (real X = -22, off-screen
left), Y=50, opaque pattern. Wait. Read status.

**What it tells you**: Does the silicon detect sprite-sprite collision
in the overscan zone `[-32, 0)` ? Nouspikel says yes (the chip rasters
into the overscan), and POM1 was patched in May 2026 to extend its
clip range to `[-32, 288)` to match. This test confirms.

Expected silicon: `Y` (bit 5 set). Expected POM1 strict: `Y`.

### T3 — /INT to /IRQ wiring

**Setup**: Enable VDP IRQ (R1.5=1), clear status, install an IRQ handler
that sets a marker byte. CLI. Spin a few hundred ms. SEI. Check marker.

**What it tells you**: Is the TMS9918's `/INT` pin actually wired to
the 6502's `/IRQ` line on **this specific Apple-1 + TMS9918 setup**?
The P-LAB schematic specifies it, but board variants exist that omit
the wire (or have it cut). POM1 always returns `Y` because the wiring
is built into the emulator.

| Silicon | What it means |
|---|---|
| `Y` | Wiring works. Programs using CLI + WAI for VBlank sync will work. |
| `N` | Wire missing or cut. Always poll status bit 7 instead. |

### T4 — 5S latch first-occurrence

**Setup**: 5 sprites at Y=50 (slots 0..4), 5 OTHER sprites at Y=100
(slots 5..9). Wait. Read status.

**What it tells you**: When two 5-sprite-overflow conditions fire in the
same frame, the silicon latches the **first** one encountered (lowest
scanline = topmost). After the latch, bits 0..4 should equal `4` (the
SAT index of the 5th sprite at Y=50, encountered first). Confirms the
"latch is sticky on first occurrence" behaviour from Nouspikel.

Expected silicon: `Y`. POM1 strict: `Y`.

## Comparison sheet

Once you have the silicon results, fill this in:

```
                            POM1   silicon  match?
T1 Sprite scan in blank     N      ____     ____
T2 Overscan collision       Y      ____     ____
T3 /INT to IRQ wiring       Y      ____     ____
T4 5S latch first-of-frame  Y      ____     ____
```

If T1 silicon = Y, ping me — POM1 needs to be updated to scan the SAT
during blank. The fix is small (~10 lines in `TMS9918::advanceCycles`)
but I want silicon confirmation before committing.

## Build (for reference)

```
cd dev/projects/tms9918_siltest
make
```

Produces:
- `software/tms9918/TMS_SilTest.bin`  — 2.7 KB raw 6502 binary
- `software/tms9918/TMS_SilTest.txt`  — Woz-hex paste-friendly format

Both binaries load at `$0280`. The build is silicon-strict-clean (every
back-to-back VDP write is gapped via `JSR tms9918_pad12` for the
silicon-correct 12c floor in Mode I + sprites — cf.
`dev/SILICONBUGS.md` Bug N°1).

## Files

- `TMS_SilTest.asm`         — main program + 4 tests + IRQ handler
- `apple1_siltest.cfg`      — cc65 linker config (16 KB DRAM)
- `Makefile`                — build script
- `emit_TMS_SilTest_txt.py` — Woz-hex emitter wrapper
- `README.md`               — this file

Linked libraries:
- `dev/lib/tms9918/tms9918_text.asm` — text-mode driver + 1 KB charmap
- `dev/lib/tms9918/tms9918_pad.asm`  — silicon-strict pad helpers

Total binary: ~2.7 KB (program + 1 KB embedded font).
