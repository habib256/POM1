# TMS_SilTest — TMS9918 silicon-strict validation suite v2.0

A comprehensive 17-test battery for the P-LAB TMS9918 Graphic Card on
real Apple-1 / Replica-1 hardware. Every silicon behaviour catalogued
in `dev/SILICONBUGS.md` (Bug N°1 to N°11) is exercised, plus several
Nouspikel-derived extras (status sticky-on-read, color-0 sprite
collision, 5S latch first-occurrence). A 30-second sprite-multiplexing
stress benchmark closes the run.

**Output is on the Apple-1 native PIA display (`$D012` / Wozmon ECHO).**
The text scrolls as each test completes; the final block re-prints the
full results grid + stress numbers so the operator can transcribe
without losing earlier lines off the top of the screen. The TMS9918
itself is used only for the silicon scenarios under test (sprite
setups, raster polling, register writes) — visible to a second observer
watching the Graphic Card monitor while the Apple-1 display logs.

**Goal**: lockstep silicon ↔ POM1 silicon-strict comparison. Boot the
same binary on real hardware and on POM1 (preset 8 + `--silicon-strict`),
write down each row's result + 16-bit hex value, diff them. Any
divergence is a candidate POM1 bug to investigate. The recommended
workflow is **"validate software in POM1 strict before deploying to
silicon"** — this binary is the canonical regression fixture for that
flow.

## Quick start

### POM1
```
python3 tools/build_codetank_rom.py --rom=tools     # rebuilds the ROM
./POM1 --preset 8 --silicon-strict \
  --codetank-rom roms/codetank/Codetank_TOOLS.rom \
  --codetank-jumper lower
# In Wozmon: 4000R
```

### Real Apple-1 / Replica-1 + P-LAB TMS9918
Three delivery options; pick whichever fits your setup:

1. **CodeTank cartridge (recommended)**
   - Flash `roms/codetank/Codetank_TOOLS.rom` onto a 32 KB EEPROM (28C256).
   - Set the CodeTank jumper to **lower**.
   - Wozmon: `4000R`.

2. **microSD card**
   - Copy `software/tms9918/TMS_SilTest.bin` to the SD root.
   - Wozmon: `6000R` to boot the SD CARD OS, then `LOAD SILTEST`, then
     `280R`.

3. **Wozmon paste**
   - Open `software/tms9918/TMS_SilTest.txt` (Woz-hex format) on a host,
     paste it into the Apple-1's terminal, then `280R`.

> The CodeTank cartridge route is preferred because the binary lives in
> ROM — no Apple-1 RAM is consumed by the program itself, only by the
> tiny BSS (~50 bytes). microSD and Wozmon-paste both require the
> binary to fit in RAM ($0280-$1FFF on a stock 8 KB Apple-1; the binary
> is ~3.7 KB). On the Parmegiani / P-LAB Apple-1 dual-bank layout
> (4 KB at $0000-$0FFF + 4 KB at $E000-$EFFF, with a gap at $1000-$DFFF),
> only the **CodeTank** route works.

## What the screen shows

Boot lands you at the banner. As each test completes, a single line is
printed in the form:

```
T01 SLOT TBL ACT GFX12+SPR... D 0080
T02 VBLANK FREE BANDWIDTH... P 0000
T03 BLANK FREE BANDWIDTH... P 0000
T04 ACTIVE TEXT TIGHT BURST... P 0000
T05 ACTIVE MULTICOL TIGHT... P 0000
T06 R1B7 4K VS 16K MASK... Y 00A5
T07 OVERSCAN COLLIDE X<0... Y 0020
T08 STATUS BITS 0..4 LAST... Y 0004
T09 COLOR0 SPRITE COLLIDE... Y 0020
T10 5S LATCH FIRST OCCUR... Y 0044
T11 STATUS STICKY ON READ... Y 0020
T12 SPRITE SCAN IN BLANK... ? 0000   <- KEY OPEN QUESTION
T13 FLIPFLOP RESET ON READ... Y 00A5
T14 FRAME RATE NTSC CYCLES... M 0985
T15 RASTER SPLIT 5S VIS... Y 0000
T16 ILLEGAL MODE CLONE... N 0000
T17 MID FRAME R7 RAINBOW... Y 0000

STRESS 30S SAT BURST... 04C0 F=0708

===== SUMMARY =====
TXX  RES VALU
T01  D   0080
T02  P   0000
...
T17  Y   0000
STRESS DROPS=04C0 FRAMES=0708

DONE - POWER CYCLE TO RERUN
```

### Result codes

| Code | Meaning |
|------|---------|
| `P`  | Pass — auto-test confirmed expected silicon behaviour |
| `F`  | Fail — auto-test got unexpected result (typically: drops where none expected) |
| `D`  | Drops detected — burst test saw silicon-correct dropping (expected on T01) |
| `Y`  | Yes — visual / status-bit-set scenario confirmed |
| `N`  | No — visual / status-bit-clear scenario confirmed |
| `M`  | Measured — for T14 (frame-rate count); see numeric value |
| `?`  | Unknown / not run |

### Numeric value (last 4 hex chars)

Test-specific 16-bit datum:

| Test | Value meaning |
|------|---|
| T01-T05 | drop count from the 256-byte burst (0000 = no drops) |
| T06     | $00A5 if 4K mode honoured (high bits truncate); `00xx` otherwise |
| T07     | status snapshot — `xx20` = bit 5 set (overscan collision) |
| T08     | status snapshot — low 5 bits should be 4 |
| T09     | status snapshot |
| T10     | status snapshot — bit 6 + low 5 = 4 → first 5S occurred at slot 4 |
| T11     | `<1st read><2nd read>` — `2000` typical (1st: bit 5 set, 2nd: cleared) |
| T12     | status snapshot — `0020` = silicon scans during blank |
| T13     | `00A5` if flip-flop correctly reset on readControl |
| T14     | 16-bit cycle count between two F-flag rises — `0985` ≈ 59.94 Hz NTSC |
| T15-T17 | `0000` (visual tests, no numeric) |
| Stress  | drops = total mismatches across ~1800 frames; frames = 16-bit count |

## Test-by-test reference

| # | Test | Bug ref | Type | Expected silicon | Notes |
|---|------|---------|------|------------------|-------|
| T01 | Slot-table active Mode 0 + sprites burst | N°1 | auto | D, drops≥1 | Galaga damiers reproduction |
| T02 | VBlank free bandwidth | N°1 §VBlank | auto | P, 0 drops | confirms 4.3 ms VBlank window |
| T03 | Display-blanked free bandwidth | N°1 §blanked | auto | P, 0 drops | confirms permanent window when R1.6=0 |
| T04 | Active text mode tight burst | N°1 §text | auto | P (mostly) | text mode bandwidth wider |
| T05 | Active multicolor tight burst | N°1 §multic | auto | P (mostly) | multicolor bandwidth wider |
| T06 | R1.7 4K vs 16K mask | N°3 | auto | Y, $A5 | 4K-mode silicon truncates high addr bits |
| T07 | Overscan collision (X<0) | N°4 | auto | Y, bit 5 set | early-clock collision off-screen |
| T08 | Status bits 0..4 = last sprite | N°6 | auto | Y, bits=4 | bit 6 = 0 + bits 0..4 = terminator slot |
| T09 | Color-0 (transparent) collision | Nouspikel | auto | Y, bit 5 set | collision from pattern bits, color irrelevant |
| T10 | 5S latch first-occurrence | Nouspikel | auto | Y, bits=4 | 1st 5S at Y=50 wins over 2nd at Y=100 |
| T11 | Status sticky on read | N°5 §destructive | auto | Y, sticky | 1st read returns set, 2nd returns clear |
| T12 | Sprite scan in display blank | **N°7 OPEN** | auto | **?** | KEY UNKNOWN — silicon answers |
| T13 | Flip-flop reset on readControl | N°9 | auto | Y, $A5 | confirms IRQ-friendly status read |
| T14 | Frame rate measurement | N°11 | auto | M, ~$0985 | 16-bit cycle count between F flags |
| T15 | Raster split 5S visible | N°10 | visual | Y if split | TMS9918 monitor: 2-color split mid-screen |
| T16 | Illegal-mode sprite cloning | N°8 | visual | Y on TMS NMOS | ghost sprites at top of screen |
| T17 | Mid-frame R7 rainbow | progressive | visual | Y if 2-band | top half color A, bottom color B |
| stress | 30s sprite-multiplexing burst | N°1 | auto | drops>0 | live confirmation that strict-mode drops in active display |

## Comparison sheet template

After running on silicon **and** POM1 strict, fill in both columns and
diff:

```
                                     POM1   silicon
T01 Slot-table active burst         D xxxx | D yyyy
T02 VBlank free bandwidth           P 0000 | P 0000
T03 Blank free bandwidth            P 0000 | P 0000
T04 Active text tight burst         _ _____| _ _____
T05 Active multicolor tight         _ _____| _ _____
T06 R1B7 4K/16K mask                Y 00A5 | _ _____
T07 Overscan collision              Y 002x | _ _____
T08 Status bits 0..4                Y 0004 | _ _____
T09 Color-0 collision               Y 0020 | _ _____
T10 5S latch first                  Y 0044 | _ _____
T11 Status sticky                   Y 2000 | _ _____
T12 Sprite scan in blank            N 0000 | _ _____    <-- KEY
T13 Flip-flop reset                 Y 00A5 | _ _____
T14 Frame rate (cycle count)        M ~985 | _ _____
T15 Raster split visible            Y      | _
T16 Illegal-mode clone              N      | _
T17 Mid-frame R7 rainbow            Y      | _

STRESS DROPS                        xxxx   | yyyy
STRESS FRAMES                       ~0708  | ~0708
```

If T12 silicon comes back **Y**, that confirms the TI-99/MSX BiFi
hypothesis ("silicon scans during blank") and POM1 needs a small fix
in `TMS9918::advanceCycles` (remove the `if (regs[1] & 0x40)` guard
on `scanSpritesForLine`). Bug N°7 in `dev/SILICONBUGS.md` is the
canonical reference.

## Build (for reference)

```
cd dev/projects/tms9918_siltest
make
```

Produces:
- `software/tms9918/TMS_SilTest.bin` — 3.7 KB raw 6502 binary, loads at `$0280`
- `software/tms9918/TMS_SilTest.txt` — Woz-hex paste-friendly form

The CodeTank-bank build is wired into `tools/build_codetank_rom.py
--rom=tools` and lives in `roms/codetank/Codetank_TOOLS.rom` (lower
half = TMS_SilTest at $4000-$7FFF, upper half reserved).

Both forms are silicon-strict-clean by construction: every back-to-back
VDP write is gapped via `JSR tms9918_pad12` for the silicon-correct
12c floor in Mode I + sprites (cf. `dev/SILICONBUGS.md` Bug N°1 §
"Modèle slot-table openMSX").

## Files

- `TMS_SilTest.asm`                 — main + 17 tests + stress + IRQ/IO helpers
- `apple1_siltest.cfg`              — cc65 linker config, standalone $0280 build
- `apple1_siltest_codetank_bank.cfg`— cc65 linker config, ROM-bank $4000 build
- `Makefile`                        — build script
- `emit_TMS_SilTest_txt.py`         — Woz-hex emitter wrapper
- `README.md`                       — this file

Linked libraries:
- `dev/lib/tms9918/tms9918_pad.asm` — silicon-strict pad helpers (pad12/pad24/pad40)

Total binary: ~3.7 KB (program + small constant strings; no embedded font
since output uses the Apple-1 PIA display).
