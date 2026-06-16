# TMS SilBench — TMS9918 silicon benchmark suite (29 tests)

Replacement for the legacy `tms9918_siltest`. SilBench is a **menu-driven
visual + transcribable** benchmark battery for the P-LAB TMS9918
Graphic Card. Each of the 29 tests:

1. Renders a clean visual on the TMS9918 screen so an operator can
   confirm what the chip actually drew.
2. Prints a single line on the Apple-1 native PIA display in a fixed
   format that an operator can transcribe and diff between a Replica-1
   + P-LAB silicon stack and a POM1 silicon-strict run:

   ```
   Tnn NAME............ <RESULT> <16-bit hex value>
   ```

This makes it the canonical regression fixture for the May 2026 silicon
model rolled into POM1 (sprite cloning meisei port, hybrid mode dispatch,
6/10 text borders, MSX1 color-0 collision, status-bits-in-blank, etc.).

## Why a new project (and not siltest v3)

`tms9918_siltest` proves Bugs N°1..N°11 from `dev/SILICONBUGS.md` with
20 mostly-text tests. SilBench:

- Adds **visuals** so each test result is verifiable by eye, not only
  by the printed flag value.
- Covers **all the May 2026 model additions**: hybrid mode rendering
  (M1+M2 vertical bars, M1+M3 → text, M3+M2 → multicolor), 6/10
  asymmetric text borders, sprite cloning per meisei `vdp.c:591-670`,
  MSX1 color-0 collide, status bits 0..4 = $1F when blank.
- Ships an **interactive menu** so an operator on real silicon can
  isolate one test for transcription instead of waiting through all 29.

## Test matrix (29 tests)

| # | Name | Mode | Type | What it proves |
|---|---|---|---|---|
| T01 | GFX1 RENDER | I | Visual | 32×24 cell render with custom patterns + colour groups |
| T02 | GFX2 RENDER | II | Visual | 256×192 bitmap pixels via the lib m2 helpers |
| T03 | MULTI RENDER | III | Visual | 64×48 colour blocks (4×4 px each) |
| T04 | TEXT 6/10 BORDER | Text | Visual | 6-px left / 10-px right asymmetric borders (mai 2026 fix) |
| T05 | HYBRID M1+M2 BARS | Hybrid 5 | Visual | "Vertical bars glitch" — meisei vdp.c:480-488 |
| T06 | HYBRID M1+M3 TEXT | Hybrid 3 | Visual | M3 cleared → text fallback |
| T07 | HYBRID M2+M3 MULTI | Hybrid 6 | Visual | M3 cleared → multicolor fallback |
| T08 | SPRITE 8x8 | I | Visual | 8x8 unmagnified sprites |
| T09 | SPRITE 16x16 | I | Visual | 16x16 sprites (R1 bit 1) |
| T10 | SPRITE MAG | I | Visual | Magnification ×2 (R1 bit 0) |
| T11 | 4-PER-LINE CAP | I | Programmatic | 6 sprites at same Y, status reads 0x44 (5S + idx 4) |
| T12 | SPRITE CLONING | II | Visual | Mode II + R4=$00 → cascading clones (meisei) |
| T13 | EARLY CLOCK | I | Visual | Bit 7 of colour byte → sprite shifted -32 px |
| T14 | COLLISION | I | Programmatic | 2 overlapping sprites → status bit 5 latched |
| T15 | COLOR-0 COLLIDE | I | Programmatic | TMS9918A MSX1: color 0 sprites still collide |
| T16 | SPRITE PRIORITY | I | Visual | Sprite 0 paints last (= on top) |
| T17 | PALETTE 16 GRID | I | Visual | 16 colour cells, one per palette index |
| T18 | COLOR-0 TRANSPARENT | I | Visual | Char with FG=0 → backdrop bleeds through |
| T19 | F-FLAG TIMING | — | Programmatic | Bit 7 rises at scanline 192, not at frame end |
| T20 | 5S STICKY | I | Programmatic | Bit 6 stays set until $CC01 read |
| T21 | C STICKY | I | Programmatic | Bit 5 stays set until $CC01 read |
| T22 | STATUS BITS 0..4 | I | Programmatic | Last-walked-sprite index when 5S not latched |
| T23 | STATUS READ CLEAR | I | Programmatic | Reading $CC01 atomic-clears bits 5/6/7 |
| T24 | VBLANK BANDWIDTH | I | Programmatic | 768 bytes written during VBlank, all land |
| T25 | STRICT DROPS | I + sprites | Programmatic | Tight back-to-back writes drop in active display |
| T26 | R1.7 4K MASK | — | Programmatic | Bit 7 of R1 toggles 4K vs 16K addressing |
| T27 | AUTO-INC WRITE | — | Programmatic | Sequential STA $CC00 walks VRAM forward |
| T28 | READ PRE-FETCH | — | Programmatic | LDA $CC00 returns pre-fetched byte, advances |
| T29 | FLIPFLOP RESET | — | Programmatic | Reading $CC01 atomic-resets the 1st/2nd-write latch |

Result column legend:
- **V** = visual test, ran without crashing
- **P** = programmatic, read-back value matched expected
- **F** = programmatic, read-back diverged (likely chip vs emulator difference)
- **?** = test not yet run

## Hardware

- Machine: Apple 1 (4 KB DRAM stock — only the ZP and stack used; the
  ROM image lives in the CodeTank window)
- Cards: P-LAB TMS9918 + CodeTank cartridge
- Recommended POM1 preset: any TMS9918+CodeTank preset (e.g. preset 7).

## Build

Default = CodeTank ROM image (~4.5 KB at `$4000-$7FFF`):

```
cd dev/projects/tms9918_silbench
make
```

Produces `software/Graphic TMS9918/TMS_SilBench.bin` + `.txt` (Woz hex).

To roll into a CodeTank ROM image:

```
python3 tools/build_codetank_rom.py --add software/Graphic TMS9918/TMS_SilBench.bin
```

(check `tools/build_codetank_rom.py --help` for the layout option.)

## Run in POM1

1. POM1 → Hardware → enable TMS9918 + CodeTank, jumper = lower.
2. Either flash the SilBench .bin into a CodeTank ROM (via
   `build_codetank_rom.py`) or `File → Load` the `.txt` directly into
   `$4000-$7FFF`.
3. Wozmon `\` prompt: `4000R`.
4. Banner + menu print on the Apple-1 display:
   - **A** — auto-run all 29 tests sequentially (~1 minute)
   - **1**..**9** — interactive single-test mode
   - **0** — re-print the test list
   - **ESC** — exit to Wozmon
5. Each test holds for ~1.5 s on the TMS9918 screen, then prints its
   line on the Apple-1 display.

## Run on Replica-1 + P-LAB silicon

Same flow as POM1: flash `TMS_SilBench.bin` into a CodeTank EEPROM
(28C256, lower jumper), boot, `4000R`, run all tests, **transcribe each
line** including the 16-bit hex value. Diff against the POM1
silicon-strict log — any mismatch flags either:

- a real divergence between POM1's emulation and the chip (file a POM1
  bug under `dev/SILICONBUGS.md`), or
- a test bug in SilBench (check the test source for hidden assumptions).

## Sources

- `TMS_SilBench.asm` — main + 29 test bodies + transcript helpers
- `apple1_silbench.cfg` — stock 4 KB layout (overflows today)
- `apple1_silbench_codetank.cfg` — CodeTank 16 KB layout (default build)
- `emit_TMS_SilBench_txt.py` — assemble + emit Woz hex `.txt`
- libs used: `dev/lib/apple1/` (print, ECHO equates), `dev/lib/tms9918/`
  (m1, 5strigger, pad12)

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
