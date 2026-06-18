# SILICONBUGS.md — Coding for real Apple-1 + TMS9918 silicon

This document catalogues the **POM1 ↔ silicon divergences** observed (or suspected) on the TMS9918 subsystem, in particular the **sprite engine**. It is designed as a practical logbook: each bug states what the silicon does, what POM1 does, the impact on the code, and the fix pattern.

**See also**

| Doc | Use |
|-----|--------|
| [`Programming_Apple1_ASM.md`](Programming_Apple1_ASM.md) §6 | TMS9918 game tutorial (VBlank polling, sprite init) |
| [`APPLE1DEV.md`](APPLE1DEV.md) §4 TMS9918 | Agent summary + pad / strict links |
| [`doc/CLI.md`](../doc/CLI.md) | `--silicon-strict` / `--no-silicon-strict` |
| [`CLAUDE.md`](../CLAUDE.md) *Testing* | TMS9918 `ctest` list (`tms9918_*`) |

POM1 emulation is faithful enough to develop in text/bitmap (Modes 1 and 2). The sprite engine has discrepancies that only show on real silicon — this doc documents them so we don't fall into them again.

---

## 0. Rigour table — modelling status per bug

POM1 silicon-strict models the TMS9918 behaviours documented in Nouspikel + openMSX. **Not all models are equally verifiable**: some are verbatim ports of reference code (very solid), others are plausible approximations whose real silicon ground truth remains uncertain. This table gives the honest status of each bug to inform the confidence that can be placed in POM1 as a pre-deployment validation tool.

| Bug | Status | Justification |
|---|---|---|
| **N°1** Slot-table timing | 🟢 SOLID | Tables verbatim copied from `openMSX VDPAccessSlots.cc`, `getTab` algorithm + Delta D28 identical. Test `tms9918_silicon_strict_runtime` (35 assertions). |
| **N°2** /INT → /IRQ | 🟢 SOLID | The P-LAB card **wires** /INT → /IRQ (trace verified on real hardware by Parmigiani); the Nippur72 software doesn't use it (polling $CC01). POM1 default = `irqStrapped=true` → `irqAsserted()` = R1.5 ∧ status.7; the IRQ stays inert until the program does `CLI`. Toggle `setIrqStrapped(false)` for a hypothetical non-wired card. |
| **N°3** R1.7 4K/16K | 🟢 SOLID | Mask `0x0FFF` vs `0x3FFF` depending on R1.7. Datasheet confirms. Pinned by T06. |
| **N°4** Collision range | 🟢 SOLID | **Visible-only `[0, kScreenWidth)`** per openMSX `SpriteChecker.cc:187-191` (*"Sprites that are partially off-screen position can collide, but only on the in-screen pixels. Sprites cannot collide in the left or right border, only in the visible screen area"*). Confirmed by meisei vdp.c:587-589 (guard bytes at 0x80 outside 256). Fixed May 2026 (was `[-32, 288)` per Nouspikel — which contradicted both canonical refs). Pinned by T07 + ctest. |
| **N°5** Per-scanline scan | 🟢 SOLID | Port of openMSX `SpriteChecker::checkSprites1` line-major (`SpriteChecker.cc:87`). Sub-cycle silicon details (exact order of SAT fetches, dummy reads after $D0) **not modelled by openMSX either** — POM1 has the same resolution as the canonical reference. Pinned by tests T07-T11, T18-T20 + ctest. |
| **N°6** Status bits 0..4 | 🟢 SOLID | "Last sprite walked" — simple logic confirmed by Nouspikel. T08 + ctest test 6. |
| **N°7** Sprite scan in blank | 🟢 SOLID | POM1 skips the sprite scan in blank (consistent with meisei `vdp.c:437` `mode=9` → blank case = `memset bd 256` with no sprite). **May 2026**: POM1 also forces status bits 0..4 to `$1F` in blank (per meisei vdp.c:437 `statuslow=0x1f`) instead of preserving the last value. T12. |
| **N°8** Sprite cloning | 🟢 SOLID | **Verbatim port of meisei `vdp.c:591-670`** (May 2026, hap — *"the only known correct implementation, tested side-by-side against real MSX1"*). Trigger condition = `M3 set ∧ (R4 & 3) ≠ 3` (R6 plays **no** role, contrary to POM1's earlier approximation). Algorithm by 4 blocks of 64 lines with `clonemask[6]` modulating `yc = (line & cm[0]) - ((y ^ cm[1]) | cm[2])`. Sprites 0..7 never cloned (preprocessed in HBlank). |
| **N°9** Flipflop reset | 🟢 SOLID | `latchIsSecond = false` in readControl. Datasheet + Nouspikel confirm. T13 + ctest. |
| **N°10** Raster split 5S | 🟢 SOLID | Mechanism via per-scanline scan (Bug N°5). Line-major granularity = openMSX. Exact sub-scanline timing is not modelled by openMSX either → POM1 reaches the limit of the canonical reference. For cycle-precise splits the model would have to be extended beyond the emulator state of the art. T15. |
| **N°11** NTSC 59.94 Hz | 🟢 SOLID | Exact constant 17062c = floor(1.022727 MHz / 59.94). Math verified. T14. |
| **Precise HBlank** | 🟢 SOLID | Covered by the slot-table ported from openMSX (Bug N°1) **and** public API `TMS9918::inHBlank()` (TMS9918.cpp, verbatim port of openMSX `VDP::getHR()` VDP.hh:948-961). TMS9918A NTSC constants: `HBLANK_LEN_TXT=404`, `HBLANK_LEN_GFX=312` ticks; `getLeftSprites` 282/258 ticks (text/gfx); `getRightBorder` = leftSprites + 960/1024. Lets callers test whether the beam is in HBlank. |
| **N°8 thermal drift** | 🔴 NOT MODELLED | Hot NMOS silicon → clones disappear. **Emulator consensus**: neither openMSX nor meisei nor POM1 model it. Out of scope. |
| **Tick→cycle drift** | 🟢 SOLID | 21 ticks/cycle. openMSX `TICKS_PER_SECOND = 3579545×6 = 21477270`, 6502 = 1022727 → **exact ratio 21.0000029** (drift = 3 ticks/sec). Imperceptible. (The earlier doc claim of 20.97 was an error — corrected May 2026.) |
| **Chip type dispatch** | 🟢 SOLID | `enum class TMS9918::ChipType` covers TMS9918A (default), TMS9929A, TMS9118/9128/9129, T7937A, T6950 (TMS9918.h). Runtime setter `setChipType()`. Toshiba (T7937A/T6950) short-circuits sprite cloning (`isCloningActive` returns false) — matches meisei `!toshiba` guard and openMSX TMS-vs-Toshiba dispatch. V99x8 not modelled (POM1 has no MSX2 target). Default stays TMS9918A for the Apple-1 + P-LAB Graphic Card target. |
| **Color-0 sprite collision** | 🟢 SOLID | POM1 = collide. openMSX MSX1 (`isMSX1VDP()`) = collide always (`VDP.hh:201-208`). meisei = collide. **Consensus match** on TMS9918A behaviour — the "???" mention in the openMSX comments refers only to V99x8 (which have a toggle). |
| **Hybrid mode rendering** | 🟢 SOLID | **meisei dispatch port (May 2026)**. M3+M1 → fallback text (M3 ignored). M3+M2 → fallback multicolor. M1+M2 (and all-three after M3-XOR) → "static vertical bars" glitch (4 px text-color + 2 px backdrop, ×40, independent of VRAM). Affected programs: Lotus F3 (MSX1 palette), Illusions demo, dvik/joyrex `scr5.rom`. Sprites OFF in mode 1/5 (text-derived) per meisei `if (~mode&1)`. |
| **Text mode borders** | 🟢 SOLID | **6 px left / 10 px right** asymmetric per TMS9918A datasheet + meisei vdp.c:475-510 (fixed May 2026 — was 8/8 symmetric before). |
| **VRAM power-on init** | 🟢 SOLID | **`$FF` even / `$00` odd** bistable per meisei vdp.c:212-217 (fixed May 2026 — was all-zero before). MSX1 silicon. MSX2 settles to all-`$FF`, MSX2-targeted programs (e.g. *Universe: Unknown* final) slightly glitchy at boot — behaviour consistent with MSX1 hardware. |

**Legend**:
- 🟢 SOLID — deterministic implementation based on canonical reference (datasheet, openMSX source)
- 🟡 UNVERIFIED — plausible model reproducing the documented behaviour, but not confronted with real silicon
- ⚠️ APPROXIMATION — imperfect model that produces a visible/observable effect but may diverge from silicon in the details
- 🔑 OPEN — silicon behaviour genuinely unknown, POM1 picks a side
- 🔴 NOT MODELLED — silicon behaviour known but POM1 doesn't simulate it

**Test coverage** (figures = state 2026-04-30):
- TMS9918-dedicated ctest tests: `tms9918_sprite_status`, `tms9918_silicon_strict_runtime`, `tms9918_per_scanline`, `tms9918_advanced_silicium` (the ~19 ctest tests of the time also covered the other subsystems — not just the VDP)
- ~20 6502-asm sub-tests in the TMS_SilBench ROM, auto-verifiable
- standalone visual interactive tests (`tms9918_clone` T12, `tms9918_split`)
- Galaga-class stress benchmark (~30 sec)
- 6-phase final demo (~30 sec) with real fauna sprites

---

## 1. Context & hardware target

- **Target platform**: Apple-1 + TMS9918 card (P-LAB Graphic Card) + CodeTank daughterboard (piggyback).
- **Equivalent POM1 preset**: **#7** — *P-LAB Apple-1 with TMS9918 (CodeTank daughterboard)* (see [`README.md`](../README.md) § Machine Presets).
- **"OK everywhere" reference (silicon + POM1)**: `TMS_Logo v1.7` — LOGO interpreter in Mode 2 bitmap, no sprites. Validates the VRAM/registers/graphics-modes subsystem.
- **"OK on POM1, broken on silicon" reference**: `A1Galaga` — Mode 1 + sprites animated at 60 Hz. On silicon, sprite artefacts and parasitic checkerboards appear around the legitimate sprites from the title screen onwards.

### Usable memory map (preset **#7**)

```
$0000-$00FF   Zero page (Apple-1 standard)
$0100-$01FF   Stack
$0200-$1FFF   User RAM (~3.5 KB usable after stack/zp)            ← only GUARANTEED block
$2000-$3FFF   Free on the CodeTank side (mutex with GEN2 HGR if card present) — TODO measure on silicon
$4000-$7FFF   CodeTank 16 KB ROM (Lower OR Upper jumper)
$CC00 / $CC01 TMS9918 DATA / CTRL
$E000-$EFFF   User RAM Hi 4k
$FF00-$FFFF   Wozmon + vectors
```

> **TODO silicon**: `$2000-$3FFF` theoretically free on silicon if no HGR card — to be confirmed on the target. `$8000-$BFFF` is free without a microSD/CFFA1/JukeBox card.

---

## 2. BUG N°1 — VRAM timing (primary cause of the Galaga artefacts)

### What the silicon does

The TMS9918A is **slave to its own video signal**: the pixel scan + DRAM refresh have absolute priority. The CPU only receives **VRAM access windows** opened by the internal sequencer. When the CPU initiates an access to `$CC00` or `$CC01`:

1. **Physical preparation delay**: ~28 VDP ticks (≈ 1.3 µs, openMSX `Delta::D28`).
2. **Wait for the next free slot**: variable depending on position in the scanline and the active mode.

If the CPU sends the next byte **before the previous latch has drained**, the chip silently overwrites (openMSX `tooFastCallback` with `allowTooFastAccess=false`). No error reported. This is the primary cause of the Galaga checkerboards on real silicon.

### openMSX slot-table model (source of truth)

POM1 v6.x replaced the min-distance threshold with a **verbatim port of the openMSX model** (`src/video/VDPAccessSlots.cc`). A TMS9918 NTSC scanline is 1368 VDP ticks (≈ 63.7 µs, openMSX `TICKS_PER_LINE`). For each mode, openMSX publishes the exact list of CPU slot positions in the line:

- `slotsMsx1ScreenOff` — display blanked (R1.6=0). Slots every ~8 ticks for most of the line.
- `slotsMsx1Gfx12` — Mode I (Graphic 1) + Mode II (Graphic 2). 19 slots/line; bursty pattern `4, 12, 20, 28, 116, 124, 132, 140, 220, 348, 476, 604, 732, 860, 988, 1116, 1236, 1244, 1364`.
- `slotsMsx1Gfx3` — Multicolor (Mode 3). 51 slots/line, intermediate between Gfx12 and Text.
- `slotsMsx1Text` — Text (Mode 1). 91 slots/line, dense for the first half.

On every CPU access to `$CC00/$CC01`:
1. POM1 computes `linePosTicks = (frameCycleCounter * 21) % 1368` (1 6502 cycle ≈ 21 ticks at 1.022727 MHz).
2. Lookup in the active table: `slotTick = smallest slot ≥ linePosTicks + 28`.
3. `pendingDrainCycles = ceil((slotTick - linePosTicks) / 21)` 6502 cycles.
4. During this drain, any other access is silently overwritten.

**Important**: openMSX **does not distinguish sprites-on/off** on MSX1 — `slotsMsx1Gfx12` aggregates the two cases (the slot pattern already reflects the worst case with sprites). The historic SAT[0]=$D0 scan is therefore dropped.

### POM1 vs openMSX divergence: free VBlank

openMSX picks the active table only from `(R1.6, mode)` — the vertical position in the frame has no effect. This means that during the ~70 NTSC VBlank lines with display ON, openMSX imposes the Gfx12 table whereas on silicon **sprite-scan + pixel-fetch don't happen**: VRAM bandwidth is free.

POM1 corrects this in `selectSlotTable()`: if `frameCycleCounter >= kActiveDisplayCycles` (i.e. in VBlank), it switches to `slotsMsx1ScreenOff` regardless of R1.6. This enables the silicon-correct idiom:

```asm
@wait_vbl: BIT $CC01
           BPL @wait_vbl     ; spin until F flag set
           ; ~4554 6502 cycles of CPU-free bandwidth available
```

Without this divergence, programs like Rogue (which stack uploads during VBlank) would see phantom drops on POM1 even though they run on real silicon.

### Progressive per-scanline rendering (rainbow demos) ✅ IMPLEMENTED

POM1 keeps a persistent `framebuffer[320×240]` on the `TMS9918` side, painted line by line in `advanceCycles` as the beam crosses each scanline. Silicon-correct consequences:

- **R7 (backdrop) changed mid-frame** → the L/R border bands of the following scanlines use the new colour. Already-rasterised lines keep the old one. "Rainbow" effect possible.
- **R1.6 (display blank) toggled mid-frame** → "active" lines = rendered pixels, "blank" lines = backdrop alone.
- **VRAM modified mid-frame** → only lines rendered AFTER the modification see the new pattern/SAT/color. Earlier lines are frozen.
- **R5/R6/R4 changed mid-frame** → next-line render uses the new pointers (split-screen attribute changes).

Cf. `TMS9918::renderActiveLine`, `paintLeftRightBorderForActiveLine`, `paintTopBorder`, `paintBottomBorder`. The helpers `renderGfxILineRaw` / `renderGfxIILineRaw` / `renderTextLineRaw` / `renderMulticolorLineRaw` / `renderSpritesLineRaw` do the raw per-scanline work, shareable between live (progressive) and legacy (snapshot).

Test pin: `tests/tms9918_per_scanline_test.cpp` Phase F validates that a mid-frame R7 change produces two different border colours depending on the vertical zone.

### Silicon worst case per mode (measured via slot-table)

| Mode | Worst inter-slot gap (ticks) | 6502 cycles | Worst-case total (D28 prep + gap) |
|---|--:|--:|--:|
| Display blanked | 16 | 0.8c | 2c (D28 dominated) |
| Text (M1) | 16 | 0.8c | 2c |
| Multicolor (M2) | 88 | 4.2c | 5c |
| **Graphic I/II** | **128** | **6.1c** | **7.5c** |

The **empirical Tetris floor (11c gap)** sits ~50% above the worst silicon Gfx12 (7.5c) — comfortable. Galaga (4c bursts) is below the floor → expected drops, as on silicon.

### What POM1 does

`writeData()` in `TMS9918.cpp:60-67` performs the write instantly, with no bandwidth constraint:
```cpp
void TMS9918::writeData(uint8_t value) {
    latchIsSecond = false;
    vram[vramAddr & 0x3FFF] = value;
    readAheadBuffer = value;
    vramAddr = (vramAddr + 1) & 0x3FFF;
    snapshotDirty = true;
}
```
Every byte gets through, whatever the rhythm. **Code can spam `STA $CC00` 1 cycle apart, POM1 will say "OK".**

### Impact on code (silicon worst-case Gfx12 = ~7.5c, pad12 OK)

At 1.022 MHz Apple-1: **1 cycle ≈ 0.978 µs**. The slot-table model gives a Gfx12 worst-case of ~7.5c (D28 prep + 128 ticks worst gap). All the patterns below are judged against this floor — Tetris (11c) passes comfortably, Galaga (4c bursts) drops systematically.

| 6502 pattern | Gap between writes | Slot-model verdict | Note |
|---|---|---|---|
| `STA $CC00` ; `STA $CC00` (back-to-back) | 4c | **KO** | below the floor |
| `LDA #x` ; `STA $CC00` ; `LDA #y` ; `STA $CC00` | 6c | **KO** | below the floor |
| `STA $CC00` ; `NOP×2` ; `STA $CC00` (old strict 8c) | 8c | **borderline** | depends on phase |
| `STA $CC00` ; `JSR tms9918_pad12` ; `STA $CC00` | 16c | **OK** (3 bytes, ratio winner) |
| `STA $CC00` ; `NOP×6` ; `STA $CC00` | 16c | **OK** (6 NOPs = 6 bytes) |
| Tetris loop (`LDA / STA $CC00 / DEX / BNE`) | 11c | **OK** (silicon-validated) |
| Loop `LDA tab,X / STA $CC00,X / DEX / BNE` | 14c | **OK** comfortable |

The choice of **`JSR tms9918_pad12`** (16c gap, 3 bytes at the site) remains optimal: 4c/byte ratio, twice as dense as NOP×6 (2c/byte). `pad24` and `pad40` are larger cushions useful for routines called from multiple call sites (Phase 7 of the runtime test).

### Diagnosing a program that drops

POM1 exposes a complete diagnostic infrastructure when `Silicon Strict` is ON:

- **Status bar**: live `STRICT — drops:N` counter next to the STRICT/FANTASY tag.
- **stderr trace**: the first 60 drops are traced one line per drop with PC, value, vramAddr, remaining drain, position in the line, active table, port (D=$CC00 / C=$CC01), phase (active/vblank). Format:
  ```
  [TMS9918 DROP #N] D val=B0 vramAddr=1100 latch2=0 drain=4c linePos=1152
  nextSlot=1236 tbl=Gfx12 vblank=0 frameCycle=120 R1=C0 PC=$5A04
  ```
- **Menu Hardware → Dump TMS9918 drop diagnostics**: emits the full histogram on stderr:
  - total + port/phase/table breakdown
  - top-16 PC (mid-instruction; the `STA` is at `PC-3` for `STA abs` / `STA abs,X`)
  - reset via the adjacent `Reset TMS9918 drop counter`
- **CLI**: see [`doc/CLI.md`](../doc/CLI.md) — `--silicon-strict` / `--no-silicon-strict` at boot.

To target a silicon-safe program, the workflow is: strict ON → run the program → `Dump diagnostics` → fix the top PC in the list (probably a STA in a tight loop) → retest → iterate.

### Dense padding — `JSR tms9918_pad12` (preferred)

The helper `tms9918_pad12: rts` (1 byte) costs 12 cycles via JSR (3 bytes at the site). **4 c/B ratio at the site**, twice as dense as NOP (2 c/B). For a 16c STA-STA gap, the JSR replaces 6 NOPs (saving: 3 bytes/site).

`tms9918_pad24: jsr pad12 / rts` chains for exotic pads (24c in 3 bytes at the site, 4-byte helper). Both are defined in `dev/lib/tms9918/tms9918_pad.asm` and exported via `.export`; each TMS9918 project's linker config loads the lib via `EXTRA_ASM` (Makefile.common) or directly via `tools/build_codetank_rom.py` which auto-detects the dependency.

### Real case — `render_sprites` in Galaga

Extract from `dev/projects/tms9918_galaga/TMS_Galaga.asm:2551-2557` ("hidden" player slot):

```asm
LDA #HIDDEN_Y      ; 2c
STA VDP_DATA       ; 4c  → write #1
LDA #$00           ; 2c   → 2 cycles elapsed
STA VDP_DATA       ; 4c  → write #2 (gap = 6c ≈ 5.9 µs)  KO
STA VDP_DATA       ; 4c  → write #3 (gap = 4c ≈ 3.9 µs)  KO
STA VDP_DATA       ; 4c  → write #4 (gap = 4c ≈ 3.9 µs)  KO
```

This pattern is repeated for every SAT slot (10 slots × 4 bytes = 40 bytes / frame, 60 Hz). On silicon, several bytes are lost every frame → corrupted SAT → **ghost sprites at random positions/patterns** (the famous parasitic checkerboards).

### Why TMS_Logo passes on silicon

The LOGO interpreter computes one pixel at a time via a bytecode loop → between two VRAM writes there are ~50 to 200 cycles of 6502 arithmetic. Timing is respected **by accident** thanks to the interpreter's slowness.

### Why the Galaga text passes

Galaga actually uses **Mode 0 (Graphic I)** — not Text Mode (the two are incompatible with sprites). The title-screen "text" rendering is done with **tiles** in the Pattern Table, written **once** at startup via a slow loop. Any lost bytes are drowned among the 768 Name Table entries, barely visible.

Conversely, the **SAT (40 bytes)** is rewritten at **60 Hz in a tight loop** without NOPs → several bytes lost every frame → corrupted SAT → ghost sprites immediately visible.

### Init trick: blank the display during massive uploads

To load Pattern Table, Name Table, Color Table at program start **without timing constraints**, set `R1` bit 6 = 0 (display off) before the upload, then turn it back on afterwards. During the blank, VRAM bandwidth is fully available (only the 2 µs preparation delay applies) → tight loops OK.

```asm
; Before VRAM init
LDA #$80          ; R1 = 16K + display OFF + everything else at 0
STA VDP_CTRL
LDA #$81          ; reg 1
STA VDP_CTRL

; ... tight VRAM-loading loops (Pattern, Name, Color tables) ...

; Final display
LDA #$E2          ; R1 = 16K + display ON + IRQ + sprites 16x16
STA VDP_CTRL
LDA #$81
STA VDP_CTRL
```

### Recommended fixes (in order of preference)

**Option A — `WRT_DATA_REG` / `WRT_DATA_VAL` macros on the lib side (recommended)**

`dev/lib/tms9918/tms9918.inc` provides (paranoid 16c):
```asm
.import tms9918_pad12, tms9918_pad24

.macro WRT_DATA_REG             ; A already loaded, 16c gap on exit
        STA     VDP_DATA        ; 4c
        JSR     tms9918_pad12   ; 12c, 3 bytes — next write lands 16c later
.endmacro

.macro WRT_DATA_VAL val          ; immediate val, 16c gap on exit
        LDA     #val            ; 2c
        STA     VDP_DATA        ; 4c
        JSR     tms9918_pad12   ; 12c, 3 bytes
.endmacro
```

**Option B — Indexed addressing (free 1-cycle bonus)**

Replace `STA $CC00` with `STA $CC00,X` (X = 0). Cost: +1 cycle (4c → 5c) without a NOP. Two indexed STAs back-to-back = 10 cycles → OK.

**Option C — Table-driven loop rewrite**

Precompute every frame a SAT table in RAM (40 bytes), then upload it with a `LDA tab,X / STA $CC00 / NOP / NOP / INX / BNE` loop (4+4+2+2+2+3 = 17c between writes). Simpler to reason about than inline writes.

### Validation

On the emulator, these 3 options change nothing (POM1 is tolerant). On silicon, option A or C **must make the sprite artefacts disappear**. If not, the primary bug is elsewhere and bugs N°2 to N°7 must be investigated.

---

## 3. BUG N°2 — /INT line wired on the P-LAB card

### What the TMS9918 silicon does

When R1 bit 5 = 1 (interrupt enable) and bit 7 of the status register = 1 (frame flag), the TMS9918 pulls the `/INT` pin low (active-low). Reading $CC01 clears bit 7 → /INT released.

### What the P-LAB card does

**P-LAB wires /INT → /IRQ.** The VDP's `/INT` pin is connected to the 6502's `/IRQ` line — Parmigiani verified the trace on real hardware. Nino Porcino's software (**Nippur72** libs) never uses it: the canonical usage remains **polling via $CC01 bit 7** (simpler, portable, independent of the I flag):

```asm
@vblank_wait:
    LDA VDP_CTRL    ; read status
    BPL @vblank_wait ; bit 7 = 0 → not yet VBlank
    ; here we're in VBlank, status auto-cleared by the read
```

But since the line is present, an IRQ-on-VBlank handler (`CLI` + vector `$FFFE`) also works on real hardware.

### What POM1 does

✅ **P-LAB compliant by default**: `irqStrapped == true` (original state). `TMS9918::irqAsserted()` reflects the silicon — R1.5 ∧ status.7 — and the IRQ aggregator in `Memory::advanceCycles` pulls the 6502 `/IRQ` accordingly; reading $CC01 clears the frame flag → the IRQ releases on the next tick. As on real hardware, the request stays **inert until the program does `CLI`** (the I flag masks the IRQ at reset): Nippur72 polling-only code is therefore unaffected.

### Disabling the wiring (hypothetical non-wired card)

`TMS9918::setIrqStrapped(false)` models a card where /INT would be left floating. `irqAsserted()` then returns `false` unconditionally (the VDP can no longer trigger a frame IRQ), regardless of R1.5.

### Impact

- Apple-1 / Nippur72 code on **stock P-LAB** = polling-only → runs identically on silicon and POM1 (the hardware IRQ is present but masked, with no effect).
- MSX-port-style code with `LDA #$E0 / STA VDP_CTRL / CLI / loop` that depends on IRQ-on-VBlank → **works** on P-LAB as on POM1 default, provided a valid handler is installed at vector `$FFFE` that reads `$CC01` atomically (cf. the $CC01 flip-flop convention below). Polling remains recommended nonetheless.

### Portable workaround

**Polling bit 7 of the status register** remains the recommended pattern (independent of the I flag and the IRQ vector contents, no $CC01 flip-flop reentrancy pitfall). IRQ-on-VBlank is now a valid option since the line is wired.

---

## 4. BUG N°3 — R1 bit 7 (4K/16K mode) ignored by POM1

### What the silicon does

R1 bit 7 selects memory mode:
- 0 → 4K mode (compatible with the older TMS9928) — only the first 4 KB of VRAM are accessible, the VRAM address is truncated to 12 bits.
- 1 → 16K mode — the full 16 KB are accessible (TMS9918A standard mode).

At reset, **R1 = $00 → 4K mode**. If the code initialises R1 without setting bit 7, the silicon stays in 4K and any VRAM addressing ≥ $1000 loops back onto the first 4 KB. Consequence: Pattern Table at $1800, Name Table at $1B00, etc. → **everything is corrupted**.

### What POM1 does

`TMS9918.cpp` treats VRAM as an unconditional 16 KB buffer. R1 bit 7 is ignored in all paths. Code that forgets bit 7 → works on POM1, crashes on silicon.

### Code impact

Always initialise R1 with **bit 7 = 1**. Typical values:
- `$E0` = display on + IRQ enable + 16K (Mode 0)
- `$D0` = display on + 16K + sprites 16×16 (Mode 0, large sprites)
- `$F0` = display on + IRQ + 16K + Mode 1 (40-col text)
- `$C0` = display on + 16K (bit 7 absent here) → **WRONG**, to avoid

### Recommendation

Adopt a convention in `dev/lib/tms9918/tms9918.inc`:
```asm
VDP_R1_BASE = $80        ; bit 7 = 16K, MANDATORY on silicon
VDP_R1_DISP = $40        ; display on
VDP_R1_IRQ  = $20        ; IRQ enable (no effect on POM1)
VDP_R1_M1   = $10        ; Mode 1 (text)
VDP_R1_M2   = $08        ; Mode 2 (bitmap) — combined with M3 via R0 bit 1
VDP_R1_LRG  = $02        ; sprites 16×16
VDP_R1_MAG  = $01        ; sprites magnified ×2
```
And **always** OR `VDP_R1_BASE` when writing R1.

---

## 5. BUG N°4 — Sprite collision in overscan ✅ IMPLEMENTED

### What the silicon does

The TMS9918's sprite engine rasters an internal area **wider than the visible screen**: ~32 pixels of overscan on the left (x = -32..-1) and 32 on the right (x = 256..287). Two sprites overlapping in this off-screen area set the **bit 5 (collision) of the status register** even though nothing is visible.

### What POM1 does

**`scanSpritesForLine()` in `TMS9918.cpp` extends collision to `[-32, 288)`** since the move to the openMSX slot-table model (May 2026). The `tms9918_per_scanline` Phase D test pins this behaviour. Rendering (`renderSprites`) is still clipped to `[0, 256)` — the overscan doesn't appear visually, only in collision detection (silicon-correct).

### Behaviour pin

`tests/tms9918_per_scanline_test.cpp` Phase D: 2 sprites in early-clock (color bit 7 = 1, X=10 → actual X=-22), pattern $FF, Y=50. Read status after one frame → bit 5 must be set.

---

## 6. BUG N°5 — Sprite scan 1×/frame (at VBlank) vs per scanline ✅ IMPLEMENTED

### What the silicon does

The collision (bit 5) and 5S (bit 6) flags are updated **scanline by scanline** during rendering, in real time as the beam traces the image. Polling the status register **mid-frame** sees the exact state at the current beam position.

### What POM1 does

**`advanceCycles()` invokes `scanSpritesForLine(line)` on every scanline crossed** (May 2026). Verbatim port of the openMSX `SpriteChecker::checkSprites1` logic (line-major variant — POM1 walks the SAT once per scanline, openMSX's sprite-major loop optimisation brings nothing at 60 Hz). `scanSpritesForStatus` (1×/frame) remains as a VBlank fallback for frames where the display stayed blanked.

### Impact 1 — Delayed collision detection

Code that polls status mid-sprite-movement will see:
- **Silicon**: collision detected as soon as it happens (latency < 1 ms).
- **POM1**: collision detected only after the next VBlank, up to 16 ms later.

For most games this is not critical (status is read after VBlank). But **bug N°10 below (raster split via 5S)** depends entirely on this scanline-by-scanline update.

### Impact 2 — Destructive status read

The silicon **clears bit 7 (F), bit 6 (5S) AND bit 5 (C) on every read** of the status register. POM1 reproduces this behaviour (`TMS9918.cpp:115-122`: `statusReg &= ~0xE0`).

**Consequence: never read status mid-frame "just to check the collision"** — bit 7 (VBlank flag) will be cleared before the sync routine catches it, the game misses its frame, music/animation desync. Always group the status read in one place, after VBlank:

```asm
@wait_vbl: LDA VDP_CTRL    ; ONE read per frame
           BPL @wait_vbl   ; wait for bit 7 (auto-cleared here)
           TAY             ; save
           AND #$20        ; bit 5?
           BNE @collision  ; collision detected during the frame
           TYA
           AND #$40        ; bit 6?
           BNE @overflow_5s
```

### Recommendation

**Always read status after VBlank**, **never** mid-frame, unless the code intentionally exploits the raster split hack (see Bug N°10).

---

## 7. BUG N°6 — Status bits 0..4 don't reflect the "last sprite scanned" ✅ IMPLEMENTED

### What the silicon does

On the standard TMS9918A, status register bits 0..4 contain the SAT index of the **last sprite scanned**, updated line by line. If bit 6 (5S) is latched, it's the 5th sprite's index. Otherwise, it's the index of the last sprite processed (often the $D0 terminator or sprite 31).

### What POM1 does

**`scanSpritesForLine()` updates bits 0..4 = lastSpriteIdx** on each scanline (May 2026), where `lastSpriteIdx` is the SAT index of the last sprite walked for this line (terminator, or sprite 31, or whatever index found). When 5S latches, bits 0..4 freeze at the 5th sprite's index (silicon-correct).

### Behaviour pin

`tests/tms9918_per_scanline_test.cpp` Phase C: 4 sprites at Y=50, terminator at SAT[4]. After a full frame, status must have bits 0..4 = 4 (the terminator's index) and bit 6 = 0.

---

## 8. BUG N°7 — Sprite engine during blank (R1 bit 6 = 0)

### What the silicon does

Non-trivial behaviour — the datasheet is ambiguous. MSX reference (BiFi) suggests the sprite engine **keeps scanning** during the blank (so collision and 5S can latch) but the video compositor emits nothing. **To measure on silicon.**

### What POM1 does

`advanceCycles()` (`TMS9918.cpp:133-136`) **completely** skips the sprite scan when R1 bit 6 = 0:
```cpp
if (regs[1] & 0x40) {
    scanSpritesForStatus(vram.data(), regs.data(), statusReg);
}
```
No update of bits 5/6 during blank.

### Impact

Code that blanks the display (`R1 &= ~$40`) then waits for a collision latch via status **will never see bit 5 on POM1**. On silicon, to be confirmed.

### TODO silicon test

Short program: display 2 overlapping sprites, blank the screen, read status after a frame. If bit 5 is latched → silicon scans during blank → POM1 diverges.

---

## 9. BUG N°8 — Sprite Cloning ✅ MODELLED (verbatim meisei port, May 2026)

### What the silicon does

The TMS9918 exposes 8 valid combinations of the M1/M2/M3 bits (4 documented modes + 4 reserved/unused). When code forces **illegal combinations** (M1+M2 simultaneously active, or M3 + interference flags), the silicon signals no error but enters a **reproducible chaotic behaviour** zone:

- The **Y axis** of sprites with index 8 to 31 becomes polluted by the low bits of the SPGT offset (R6) and bits 5-6 of the Color Table (R3).
- Visible consequence: **vertical ghost clones** of the original sprites appear in the top stratum of the screen (Y = 0..63), forming an echo pattern in deformed tiles.
- These clones **consume slots** in the 4-sprites-per-scanline limit and **trigger the C bit (collision)** like legitimate sprites.
- The MSX demo **"Alankomaat"** (Bandwagon group) deliberately exploits this effect to display an impossible number of sprites.

### Thermal drift (edge case)

On original TMS9918A silicon (TI / NMOS), the clones **fade progressively as the chip heats up** (documented "hair-dryer" effect). Block 1 index clones disappear first, then Block 2. Clones from Toshiba or Yamaha V9938 have the addressing paths corrected at the factory — **no cloning, code that relies on it doesn't work**.

### What POM1 does

✅ **MODELLED — verbatim meisei port** (`src/vdp.c:591-670`, author **hap**, May 2026). hap published in [openMSX issue #593](https://github.com/openMSX/openMSX/issues/593) that this is *"the only known correct implementation, tested side-by-side to my MSX1, with all possible sprite Y and addressmasks"* — therefore **the** de-facto reference.

POM1 now exposes 2 independent predicates:

- **`isIllegalModeRegs(regs)`** — true if ≥ 2 of the M1/M2/M3 bits are active (reserved hybrid combinations). Used to decide the **BG playfield bypass** (backdrop-only) — behaviour unrelated to cloning.
- **`isCloningActive(regs)`** — true iff `(R0 & 2) ∧ ((R4 & 3) ≠ 3)` (meisei condition). This is the true silicon cloning trigger condition. **R6 plays no role** (contrary to POM1's earlier approximation).

Cloning can therefore engage in **perfectly legal Mode II** (M3 set, M1=M2=0) if R4 has at least one bit 0-1 cleared — this is exactly what hap's reference BASIC program (issue #593) does to exhibit the bug. Conversely, hybrid M1+M2 (without M3) is illegal but **does not clone**.

The `renderCloneSpritesLineRaw` algorithm (`TMS9918.cpp`) divides the frame into 4 blocks of 64 scanlines:

| Block | Lines | Condition | Effect on `cm[0..5]` |
|---|---|---|---|
| 0 | 0..63 | never (sprites 0..7 preprocessed in HBlank) | — |
| 1 | 64..127 | `R4 & 1 == 0` | `cm[0]=$3F`, `cm[5]=(~R3 << 1) & $40` |
| 2 | 128..191 | `R4 & 2 == 0` | `cm[1]=cm[4]=$80`, `cm[5]=(R3 << 1) & $80` |
| 3 | 192..255 | always (off-screen for POM1, omitted) | — |

For each sprite slot 8..31 (slots 0..7 are **unaffected**):

```
yc = (line & cm[0]) - ((y ^ cm[1]) | cm[2])
if (yc >= spriteH) yc = (line & cm[3]) - ((y ^ cm[4]) | cm[5])
if (yc < spriteH)  → render the sprite at yc with the standard pattern fetch + color path
```

This is the exact silicon algorithm (or the closest mathematical inversion hap could deduce after side-by-side comparison with real hardware).

### Residual limits

- **Block 3 (lines 192..255) omitted** — kScreenHeight=192, so these scanlines are never rendered. Sprites placed at Y_attr=192..254 that should wrap onto the visible frame are lost on the cloning side. Impact: demos that exploit top-of-screen sprite cloning via Y wrap. **TODO if needed**: extend the pipeline to handle cloning of Y_attr values above 192 and remap them onto the visible scanlines 0..63.
- **Thermal drift** not modelled (clones disappear block by block as the VDP heats up — block 1 first then block 2). hap describes the "blow dryer" test in the meisei comment. POM1 = permanent "cold" behaviour.
- **Toshiba / Yamaha V9938+** have factory-fixed addressing and **don't clone**. POM1 hard-codes "TI silicon" (no dispatch on chip kind).

⚠️ **Consequence for T16 of `tms9918_siltest`** (May 2026): T16 uses R0=$02 + R1=$D8, which sets M1+M2+M3 simultaneously. With the new meisei dispatch:
- mode = 7 → after M3 XOR → mode 5 → "vertical bars glitch" + **sprites OFF** (M1 set).
- isCloningActive gate = `!M1 && M3 && (R4&3)≠3` → false (M1 set).
- So T16 now displays: static vertical bars, **no sprites, no clones**.

The operator must therefore answer **N** to the "saw ghost sprites?" question — this is the correct silicon behaviour. T16 becomes a test of **hybrid mode 5** rather than cloning. The real cloning test is now `dev/projects/tms9918_clone/` (legal Mode II + R4=0 + sprite #31, hap-style — fires cloning without M1 to respect the sprite-engine gate).

### Recommendation

**Don't exploit this behaviour** for portable code. Always stick to the 4 documented modes (Mode 0, 1, 2, 3). To go beyond 32 sprites, use **classic SAT-swap multiplexing at VBlank** (cf. authentic flicker).

---

## 10. BUG N°9 — Control-port write flip-flop vs IRQ

### What the silicon does

Writing a VDP register via `$CC01` takes **two bytes**: 1st = data, 2nd = `$80 | regnum`. The VDP uses an **internal flip-flop** (latch) to track the state (1st or 2nd byte expected).

If a **hardware interrupt** occurs between the two bytes AND the IRQ routine also writes to `$CC01`, the flip-flop desyncs → the main routine's 1st byte is interpreted as the IRQ routine's 2nd byte → **VDP register corruption**.

**Documented silicon solution**: the IRQ routine must **read `$CC01` (status register) first**, which atomically RESETS the flip-flop. Any following write restarts cleanly with a 1st byte.

### What POM1 does

POM1 correctly simulates this reset: `readControl()` (`TMS9918.cpp:115-122`) does `latchIsSecond = false`, and `writeData()` / `readData()` (`TMS9918.cpp:60-76`) likewise. **POM1 is faithful here.**

### Impact

No behavioural divergence: code that follows the "read status at the start of the IRQ routine" convention runs on both targets. But code that **omits** this read will be buggy on **both** targets as soon as an IRQ hits between the 2 bytes.

### Recommendation

**Mandatory convention for any IRQ routine that touches the VDP**:
```asm
vdp_irq_handler:
    PHA               ; save A
    LDA VDP_CTRL      ; read status — ATOMIC flip-flop RESET
                      ;               AND clears bit 7 frame flag
    ; ... IRQ processing, VDP writes free ...
    PLA
    RTI
```

Note: on Apple-1 + TMS9918, **the /INT line is wired** to the 6502 /IRQ (Bug N°2, default `irqStrapped=true`). As soon as a program does `CLI` with R1.5=1, this IRQ actually fires and the above convention becomes critical — the handler MUST read $CC01 atomically. Nippur72 software stays safe because it polls without ever unmasking the IRQ (`CLI`).

---

## 11. BUG N°10 — Raster split hack via 5S (mid-scanline effects) ✅ IMPLEMENTED

### What the silicon does

The TMS9918 provides **no standard raster IRQ**, but period programmers worked around this limitation via a technique known as **"5S raster split"**:

1. Place 5 **invisible** sprites (color = 0) aligned at a target Y coordinate — for example Y = 95 (mid-screen).
2. The CPU launches a **tight wait loop** on bit 6 of the status register:
   ```asm
   @wait_split: LDA VDP_CTRL
                AND #$40
                BEQ @wait_split
   ```
3. At the exact instant the beam reaches Y = 95, the silicon detects 5 sprites on the scanline → **bit 6 (5S) arms**.
4. The CPU exits the loop and can then:
   - Modify R7 (backdrop colour) → "rainbow / gradient" effect.
   - Modify R5 (SAT base) → second set of 32 sprites for the bottom half.
   - Modify R4 (Pattern Table base) → different graphics set per screen half.

### What POM1 does

✅ **IMPLEMENTED**: `scanSpritesForLine` is invoked on every scanline crossed by frameCycleCounter (May 2026). Bit 6 arms exactly when the beam crosses the 5th line — the CPU's wait loop exits at the same position as on silicon.

### Behaviour pin

`tests/tms9918_per_scanline_test.cpp` Phase B: 5 sprites at Y=95, simulated bit 6 polling. Before the crossing (line ~50) bit 6 = 0; after (line ~110) bit 6 = 1 + low 5 bits = 4 (the 5th sprite's index).

---

## 12. BUG N°11 — 59.94 Hz NTSC frame rate vs 60 Hz POM1 ✅ IMPLEMENTED

### What the silicon does

Analog NTSC = **59.94 Hz** exactly (60 × 1000/1001), not round 60 Hz. PAL = exact 50 Hz. This non-integer frequency causes, on SAT multiplexing routines (cf. Bug N°6 authentic flicker), a progressive **phase drift** in group inversions.

### What POM1 does

`POM1_CPU_CYCLES_PER_FRAME_1X_60HZ` in `CpuClock.h` now anchors POM1 at **17062 cycles/frame ≈ 59.94 Hz** (May 2026, was 17045 = round 60 Hz). Formula: `(1001 × 1022727 + 30000) / 60000`. ~0.1% drift eliminated for fine audio demos and timing-critical SAT multiplexing.

### Behaviour pin

`tests/tms9918_per_scanline_test.cpp` Phase E: asserts `POM1_CPU_CYCLES_PER_FRAME_1X_60HZ == 17062`.

---

## 13. Emulator → silicon bringup protocol

6-step checklist to port a POM1 program to silicon without surprises:

1. **Measure the real usable RAM** — Confirm `$0200-$1FFF` free (should be). Test `$2000-$3FFF` (free only without HGR). Test `$8000-$BFFF` (free without microSD/CFFA1/JukeBox).

2. **Check VDP init** — R1 must have bit 7 = 1 (16K). R0/R2/R3/R4/R5/R6/R7 according to mode. Re-check the register names in `dev/lib/tms9918/tms9918.inc`.

3. **Audit all VRAM write loops** — Every consecutive `STA VDP_DATA` pair must have **≥ 8 cycles** between writes during active display. Prefer `STA $CC00,X` or the `WRT_DATA_NOP` macro. VRAM init loops at program start can ignore this rule if the display is blanked (`R1 bit 6 = 0`) during loading.

4. **Prefer polling over /INT** — Polling bit 7 of the status register remains the recommended pattern (simple, independent of the I flag, no $CC01 flip-flop reentrancy pitfall). The /INT line is wired on P-LAB (Bug N°2), so an IRQ-on-VBlank handler also works — but only with a valid `$FFFE` vector and an atomic $CC01 read.

5. **No collision in overscan** — Software bounding-box for off-screen sprites.

6. **Progressive test** — text only → static bitmap → 1 still sprite → animated SAT → full SAT 60 Hz. At each step, validate on silicon before adding complexity.

---

## 14. Reproducible tests to run on both targets

Minimal suite to characterise a new silicon setup. All programs are loaded via Wozmon and launched manually.

### Test A — Tight SAT loop (must fail on silicon)

Writes 32 SAT entries without NOPs:
```asm
;     LDA #$00 / STA $CC01 / LDA #$5B / STA $CC01     ; addr=$1B00 write
; @lp LDA #$50 / STA $CC00 / LDA #$00 / STA $CC00 / STA $CC00 / STA $CC00 / DEX / BNE @lp
```
**Expected on silicon**: corrupted SAT, sprites visible at random positions.
**Expected on POM1**: 32 sprites aligned at Y=$50, X=$00, name=$00.

### Test B — Same loop with NOPs (must pass everywhere)

Same as Test A, interleaving `NOP / NOP` between each `STA $CC00`.
**Expected**: identical on both targets.

### Test C — Sprite at Y=$D0 (terminator)

SAT[0] = (Y=$50, X=$00, name=1, color=$0F), SAT[1] = (Y=$D0, ...), SAT[2] = (Y=$50, X=$20, name=1, color=$0F). Sprite #2 must **not** appear.
**Expected**: 1 visible sprite (slot 0). Identical on both targets if silicon is OK.

### Test D — 5S overflow

5 sprites aligned at Y=50, X=0,16,32,48,64. Read status after VBlank.
**Expected**: bit 6 set, low 5 bits = 4. Identical on both targets.

### Test E — Overscan collision (expected difference)

2 sprites with early-clock (color bit 7 = 1), X=10 → actual X=-22, Y=50. Solid pattern. Read status after VBlank.
**Expected on silicon**: bit 5 set (off-screen collision detected).
**Expected on POM1**: bit 5 = 0 (collision clipped).

This test validates bug N°4 on your silicon target — useful for calibration.

---

## 15. Defensive checklist (reference card to print)

Keep handy for any new TMS9918 code intended for silicon:

- [ ] `R1` initialised with **bit 7 = 1** (= 16K mode). Typical value: `$E0` or `$F0`.
- [ ] **≥ 8 cycles between 2× VRAM accesses** in Graphic mode + active sprites (NOPs, indexed addressing, or a loop with 6+ cycles of intervening instructions).
- [ ] For massive uploads (init Pattern/Name/Color Table): **blank the display** (`R1` bit 6 = 0) during the upload then re-enable it.
- [ ] VBlank waited via `LDA $CC01 ; BPL ...` (polling), **not** via IRQ.
- [ ] No `WAI` and no `CLI` + IRQ handler for VBlank synchronisation.
- [ ] **ONE single status read per frame** (the read clears bits 5, 6 and 7 atomically).
- [ ] Any IRQ routine that touches the VDP must **read status first** (reset the 1st/2nd-byte flip-flop).
- [ ] Collision tested but **never in the overscan zone** (use software bounding-box off-screen).
- [ ] **`$D0` terminator** placed after the last active SAT slot (otherwise 32 sprites scanned per frame, perf and spurious 5S).
- [ ] Before masking status bits 0..4 as "5th sprite index", check **bit 6 is set** (otherwise the value is undefined).
- [ ] Don't exploit the **raster split via 5S hack** (Bug N°10) — broken visual on POM1.
- [ ] Don't exploit the **sprite cloning** of illegal hybrid modes (Bug N°8) — not simulated on POM1, refused by Toshiba/Yamaha clones.
- [ ] Documented modes only: Mode 0 (Graphic I), Mode 1 (Text), Mode 2 (Graphic II / Bitmap), Mode 3 (Multicolor). **No hybrid combination.**

---

## Appendix A — TMS9918 register quick reference

| Reg | Bits | Role |
|---|---|---|
| R0 | bit 1 = M3 (Mode bit 3); bit 0 = External VDP (unused) | Mode selection |
| R1 | bit 7 = **16K**; bit 6 = Display ON; bit 5 = IRQ Enable; bit 4 = M1; bit 3 = M2; bit 1 = sprites 16×16; bit 0 = sprites ×2 mag | Mode + display |
| R2 | bits 3-0 = Name Table base × $400 | Name Table address |
| R3 | Color Table base (Mode 0: ×$40; Mode 2: bit 7 + mask) | Color Table address |
| R4 | bits 2-0 = Pattern Table base × $800 (Mode 0); bit 2 + mask (Mode 2) | Pattern Table address |
| R5 | bits 6-0 = Sprite Attr Table base × $80 | SAT address |
| R6 | bits 2-0 = Sprite Pattern Table base × $800 | Sprite Patterns address |
| R7 | bits 7-4 = FG (text); bits 3-0 = BG / Backdrop | Colours |

Status register (read `$CC01`, **destructive**: clears F/5S/C on each read):

| Bit | Name | Role |
|---|---|---|
| 7 | F | Frame flag — set at the start of VBlank, triggers /INT if R1 bit 5=1 |
| 6 | 5S | Fifth sprite overflow — set when >4 sprites on one scanline |
| 5 | C | Coincidence/collision — set when 2 sprites overlap (bit-pattern, color=0 included) |
| 4-0 | 5S index | SAT index of the **first** identified 5th sprite — valid **only if bit 6=1** |

**Documented** graphics modes (M1, M2, M3):

| M1 | M2 | M3 | Mode | Description | Sprites |
|---|---|---|---|---|---|
| 0 | 0 | 0 | **Mode 0** Graphic I | 32×24 tiles, 32 color groups (8 patterns/group) | Yes |
| 0 | 0 | 1 | **Mode 2** Graphic II | Full 256×192 bitmap, color/8 pixels | Yes |
| 1 | 0 | 0 | **Mode 1** Text | 40×24 chars, 6×8 glyph, FG/BG via R7 | **No** |
| 0 | 1 | 0 | **Mode 3** Multicolor | 64×48 coloured blocks | Yes |

Any other combination (M1+M2, M2+M3, M1+M3, M1+M2+M3) = **illegal hybrid mode**, chaotic behaviour not emulated by POM1 (cf. Bug N°8 sprite cloning).

## Appendix B — Pixel and scanline timing

| Parameter | Value | Notes |
|---|---|---|
| Pixel clock | ~5.37 MHz (NTSC) | Pixel cycle = 186 ns |
| VDP memory cycle | ~372 ns (= 2 pixel cycles) | CPU access-window unit |
| Full scanline | 342 pixel cycles | Horizontal total |
| ↳ visible active zone | 256 pixel cycles | 256 px of display |
| ↳ margins + sync | ~28 pixel cycles | Left/right borders |
| ↳ HBLANK | ~58 pixel cycles | Horizontal blanking |
| Full frame | 262 scanlines (NTSC) / 313 (PAL) | |
| ↳ visible | 192 scanlines | Active display |
| ↳ VBLANK | ~70 lines (NTSC) / ~121 (PAL) | ~4.3 ms NTSC |
| Exact frame rate | 59.94 Hz (NTSC) / 50 Hz (PAL) | POM1 = fixed 60 Hz (cf. Bug N°11) |

## Appendix C — Emulator roadmap (future POM1 additions)

Improvements that **would bring POM1 closer to silicon** but are not implemented:

1. ~~**"Silicon timing strict" mode** — option that drops bytes if the VRAM write is too fast for the active mode (refines Bug N°1).~~ **✅ IMPLEMENTED**: `TMS9918::siliconStrictMode` — if enabled, `canAcceptAccess()` blocks accesses while `pendingDrainCycles` > 0; every accepted access recomputes the delay until the next CPU slot via the openMSX tables ported in §2 (Bug N°1). The behaviour is **phase-dependent** (display mode, sprites, VBlank, position in the scanline), not a single fixed "N cycles" threshold for the whole frame. Practical contract: *strict ON ⇒ aligned with this slot-table model*. Default ON for all presets except Multiplexing Fantasy. Toggle: menu *Hardware → Silicon Strict (TMS9918 timing)* and CLI [`doc/CLI.md`](../doc/CLI.md) (`--silicon-strict` / `--no-silicon-strict`). Status bar `STRICT` / `FANTASY`. Snapshot: bit `kFlagSiliconStrict` (bit 14). Main pin: `tests/tms9918_silicon_strict_runtime_test.cpp` (+ `tms9918_*` family in `CLAUDE.md` / `tests/CMakeLists.txt`).
2. **Scanline-by-scanline sprite scan** — call `scanSpritesForStatus()` mid-frame, not only at VBlank (resolves Bugs N°5 and N°10).
3. **59.94 Hz frame rate** — exact NTSC option (resolves Bug N°11).
4. **Sprite cloning — hybrid detection** — cloning (Bug N°8) is already modelled (meisei); remaining avenues are NMOS thermal drift (out of scope) and refinement of illegal hybrid modes outside test coverage.
5. **/INT → 6502 /IRQ wiring** — option in the preset to propagate the hardware IRQ (resolves Bug N°2).
6. **R1 bit 7 (4K/16K)** strict — option that masks VRAM addressing to 12 bits if bit 7=0 (resolves Bug N°3 via early detection).

These additions are not planned short-term — this doc serves first and foremost to **avoid** these pitfalls on the user-code side.

---

## 16. Appendix D — Bug N°1 sub-bug: `statusReg` bit 7 sticky as VBlank proxy (fixed)

### Symptom
Before 2026-04-30, running Galaga via CodeTank with `Silicon Strict` ON triggered
**no** byte drops even though the `STA $CC00` patterns at 4-6 cycles apart
are visibly KO on real silicon (cf. Bug N°1).

### Root cause
`TMS9918::requiredAccessCycles()` (TMS9918.cpp:74-77) used
`(statusReg & 0x80) != 0` as a *"we're in VBlank"* proxy to relax the
access window to 2 cycles. But this bit is **sticky-until-`readControl`**:
it arms on every VBlank and stays latched until an `LDA $CC01`. Galaga
**never** reads `$CC01` (0 occurrences in `dev/projects/tms9918_galaga/TMS_Galaga.asm`)
→ from frame 1 the bit stays at 1 for good → `requiredAccessCycles()`
returns 2 cycles for the whole frame → all Galaga writes go through.

### Fix (TMS9918.cpp + TMS9918.h)
The window depends on the physical beam position, not the latched flag.
Replaced by:

```cpp
if ((regs[1] & 0x40) == 0 || frameCycleCounter >= kActiveDisplayCycles) return 2;
```

with `kActiveDisplayCycles = (kCyclesPerFrame * 192) / 262` (≈ 12 490 cycles at
1× — 192 active scanlines out of 262 in the NTSC frame). During active display
→ strict 8c window, during physical VBlank → relaxed 2c window.

### Consequence for user code
**None**: the new gating reflects silicon better and introduces no
false positives. Programs that polled `$CC01` by discipline continue
to function exactly the same (the window never depended on bit 7 on
silicon).

---

## 17. Appendix E — Case study: porting Galaga to silicon-strict

`dev/projects/tms9918_galaga/TMS_Galaga.asm` is the first TMS9918 game
reference fully ported under `Silicon Strict` ON. Reusable bringup folder
for the other games in the repo (Sokoban, Snake, Connect4, Maze3D).

### Patching tooling

Reusable script: **`tools/silicon_strict_patch.py`** (inserting `JSR tms9918_pad12` at detected sites — cf. §2 6502 patterns / STA-STA gap). Idempotent — `--unpatch` strips the v1 (NOPs) and v2 (JSR) markers before fresh reinsertion.

```bash
# Patch in place
python3 tools/silicon_strict_patch.py path/to/Game.asm

# Dry-run (count without writing)
python3 tools/silicon_strict_patch.py path/to/Game.asm --dry-run

# Strip-only (revert without reinsertion)
python3 tools/silicon_strict_patch.py path/to/Game.asm --unpatch
```

Applied rules (cumulative, deterministic order):

| Case | Detected pattern | v2 insertion (`JSR tms9918_pad12`) | Bytes added |
|---|---|---|--:|
| **A** | `ST? VDP_*` adjacent to `ST? VDP_*` | `JSR tms9918_pad12` between | 3 |
| **B** | `ST? VDP_* / LDA #imm / ST? VDP_*` | `JSR tms9918_pad12` BEFORE the LDA | 3 |
| **C** | `ST? VDP_* / LDA <zp/abs/zp,X> / ST? VDP_*` | `JSR tms9918_pad12` BEFORE the LDA | 3 |

The patcher also injects `.import tms9918_pad12` once at the top of every patched file (for projects that don't include `tms9918.inc`). The `tms9918_pad12 / pad24` helper lives in `dev/lib/tms9918/tms9918_pad.asm` and is linked automatically by `Makefile.common` (via `EXTRA_ASM`), `emit_woz.py` (auto-detection), and `build_codetank_rom.py` (auto-detection).

`ST?` covers `STA` / `STX` / `STY` (Galaga uses all three).
Cross-port (`VDP_DATA → VDP_CTRL` or vice versa): the window is unique for
both ports — the matcher covers `VDP_(DATA|CTRL)` indiscriminately.

**No skip annotation — strict means strict.** Earlier versions of
the patcher honoured a `; SILICON_STRICT_SKIP` comment to
exempt a routine from pad injection. This *escape hatch* was
removed (May 2026) for two reasons:
1. Substring-match footgun: a comment mentioning the directive name
   (e.g. *"do not add SILICON_STRICT_SKIP here"*)
   silently disabled injection on the entire routine —
   the hide_slot_4 incident in Galaga where hours were lost
   chasing a cycle regression when the pads simply
   hadn't been emitted.
2. A "strict mode" with per-routine exemptions is a hollow
   promise: a build that passes strict no longer guarantees the
   silicon contract, because the auditor can no longer distinguish
   audited routines from exempted ones.

Routines that need particular padding (cross-JSR cushions,
VBlank entry sync pad, cross-caller cushion at the start of `init_vdp_g*`)
must inline their `JSR tms9918_pad{12,40}` explicitly. The patcher
detects these manual pads via `is_existing_pad` and doesn't inject on top.

### Patched project inventory (state at 2026-04-30)

All TMS9918 programs in the repo + the `lib/tms9918/` drivers have
been through the tool. Count of inserted NOPs:

| Project | Case A | Case B | Case C | Total |
|---|--:|--:|--:|--:|
| `tms9918_galaga` (incl. `hide_slot_4` refactor) | 62 | 193 | 14 | **269** |
| `tms9918_sokoban` | 2 | 16 | 0 | 18 |
| `tms9918_snake` | 2 | 14 | 1 | 17 |
| `tms9918_orbital_pool` | 0 | 9 | 1 | 10 |
| `tms9918_connect4` | 2 | 7 | 0 | 9 |
| `tms9918_logo` (Mode 2) | 0 | 7 | 0 | 7 |
| `tms9918_life` | 0 | 6 | 0 | 6 |
| `tms9918_maze3d` | 0 | 3 | 1 | 4 |
| `tms9918_chess` | 0 | 1 | 0 | 1 |
| `tms9918_codetank_menu` | 0 | 0 | 0 | 0 |
| `tms9918_codetank_menu_upper` | 0 | 0 | 0 | 0 |
| `lib/tms9918/tms9918m1.asm` | 0 | 3 | 1 | 4 |
| `lib/tms9918/tms9918m2.asm` | 0 | 5 | 1 | 6 |
| **Total** | **68** | **264** | **19** | **351** |

### Byte-saving refactor: `hide_slot_4`

Galaga had 10 inline occurrences of the "hide a SAT slot" pattern (16 B
each = 160 B total):

```asm
LDA #HIDDEN_Y
STA VDP_DATA
LDA #$00
STA VDP_DATA
STA VDP_DATA
STA VDP_DATA
```

Refactored as `hide_slot_4` (22 B helper with built-in NOP padding) + 10×
`JSR hide_slot_4` (3 B each). Net saving ≈ 100 B on the ROM slot,
indispensable to fit the full NOP coverage.

### Hidden `LDA <zp 3c>` bridges

Typical sites missed if case C is not covered:

| ASM site | Pattern | Silicon-strict symptom |
|---|---|---|
| `render_sprites @show_p` | `STA VDP_DATA / LDA player_x / STA VDP_DATA` | Player ship flickers 1/2 frame |
| `plot_star` final write | `STA VDP_CTRL / LDA temp3 / STA VDP_DATA` | Star not plotted → sparse starfield |
| `render_sprites @en_paint X` | `STA VDP_DATA / LDA enemy_x / STA VDP_DATA` | Enemy flickers (but `enemy_x,X` 4c → OK) |

`player_x`, `temp3` etc. are in zero page (`.res 1` in the ZEROPAGE segment).
`LDA zp` = 3 cycles → gap 4+3+4 = **11c** from the start of the previous STA, i.e.
**7c** between the two VDP *latches*. Adding 1 NOP (2c) is enough to reach 9c.

### `STA VDP_CTRL / STX VDP_CTRL` bridges (2-byte address-write)

`draw_str_tms` (and clones) did `STA VDP_CTRL / STX VDP_CTRL` direct (4c gap).
The initial matcher targeted only `STA VDP_CTRL` — `STX` and `STY` were
ignored → 2nd half of the address-write dropped → **all** strings written
by `draw_str_tms` ended at a random VRAM offset (splattered text).
Resolved by extending the matcher to `ST[AXY] VDP_(DATA|CTRL)`.

### ROM slot: why the `dualslot8k` layout

Full NOP coverage on Galaga = **219 NOPs** = 219 bytes added.
The historic menu-bank layout (`build_codetank_rom.py --layout=menu`)
reserved only **7 424 B** for the Galaga slot (`$4100-$5DFF`) — Galaga with patches
was 7 419 B → 5 B of margin, untenable once all cases are covered.

Solution: `--layout=dualslot8k`, which offers **8 192 B** per slot and
sacrifices the interactive menu + Snake/Life:

```
Lower bank ($4000-$7FFF):
  $4000-$5FFF  Galaga  (8 kB, 760 B padding with full patch)
  $6000-$7FFF  Sokoban (8 kB, 3 410 B padding)
Upper bank:
  Tetris launcher + payload (unchanged)
```

No menu — Wozmon `4000R` launches Galaga, `6000R` launches Sokoban. ROM published
under `roms/codetank/Codetank_GAME1.rom`.

### Builder

```bash
python3 tools/build_codetank_rom.py --layout=dualslot8k -o roms/codetank/Codetank_GAME1.rom
```

Cfgs: `apple1_galaga_codetank_8k.cfg` (link at `$4000`, 8 kB slot) and
`apple1_sokoban_codetank_8k.cfg` (link at `$6000`).

### Visual validation

POM1 `--preset 9 --terminal --silicon-strict`, `4000R`, pick QWERTY:
- Full splash page `A1GALAGA / APPLE-1 TMS9918 / BY VERHILLE ARNAUD`.
- 3 alien sprites SCOUT/FIGHTER/BOSS with HP labels.
- Clean keyboard menu `1 QWERTY (A D S) / 2 AZERTY (Q D S) / SPACE FIRE`.
- Gameplay: HUD `SCORE/LIVES/W:01`, 6-8 star starfield scrolling smoothly,
  player ship + enemies without flicker.

Reference screenshots in `screenshots/pom1_latest.png` (captured via
TerminalCard ESC S after `--terminal`).

---

*Last update: 2026-04-30. Bug N°1 (timing) confirmed on silicon via
Galaga. Sub-§16 bug fixed in POM1. Appendix E added with the full Galaga
bringup (toolchain + `dualslot8k` cfgs). Bugs N°2 to N°11 derived from
static analysis of `TMS9918.cpp` cross-referenced with the TI / Texas
Instruments / BiFi MSX / openMSX references — to validate on silicon
case by case (Tests A to E above).*
