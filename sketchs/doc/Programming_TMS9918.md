# P-LAB TMS9918 Programming (6502 assembly)

The **P-LAB Apple-1 Graphic Card** wraps a TMS9918A VDP: 256×192 pixels, 32
hardware sprites, 16 KB of dedicated VRAM accessed through a two-port I/O
window at `$CC00` (data) and `$CC01` (control). The chip's silicon timing is
strict, the sprite engine has subtle corner cases, and several details of the
P-LAB wiring shape what counts as portable code. This guide combines the
standard programming model with the silicon-handling guidance you need
**before** optimising any VRAM loop.

**Related docs**

| Doc | Use |
|-----|-----|
| [`Programming_Apple1_ASM.md`](Programming_Apple1_ASM.md) | Base 6502 / cc65 toolchain, text mode, common patterns |
| [`Programming_TMS9918C.md`](Programming_TMS9918C.md) | C (cc65) version of this guide |
| [`TMS9918-SPRITE_BEST_PRACTICES.md`](TMS9918-SPRITE_BEST_PRACTICES.md) | Operational sprite checklist |
| [`TMS9918-SPRITE_INIT.md`](TMS9918-SPRITE_INIT.md) | Sprite initialisation reference |
| [`CLI.md`](../../doc/CLI.md) | `--silicon-strict` / `--no-silicon-strict` flags |
| [`APPLE1DEV.md`](APPLE1DEV.md) §4 TMS9918 | Agent summary and pad / strict cross-links |

**Hardware target** — Apple-1 + TMS9918 card (P-LAB Graphic Card) + CodeTank
daughterboard (piggyback). Equivalent POM1 preset: **#9 — P-LAB Apple-1 with
TMS9918 (CodeTank daughterboard)** (see [`README.md`](../../README.md) § Machine
Presets).

POM1 emulation is faithful enough to develop in text and bitmap modes
(Modes 1 and 2) without surprises. The sprite engine and the VRAM-timing
model have discrepancies that only show on real silicon — they are
documented inline as **Bug N°k** call-outs so you can audit your code
against the canonical reference work. The fidelity status of each item
appears in the appendix table.

---

## Part 1 — The card

### 1. Characteristics

- **Resolution**: 256×192 pixels (Graphics I: 32×24 characters of 8×8 px).
- **VRAM**: 16 KB **separate** from main RAM (communication via I/O only).
- **I/O**: `$CC00` (data) and `$CC01` (control).
- **Frame rate**: NTSC = **59.94 Hz** exactly (60 × 1000/1001), not round
  60 Hz. PAL = 50 Hz exact. See §17 below for the timing implication on SAT
  multiplexing.

#### Usable memory map (preset #9)

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

> **TODO silicon**: `$2000-$3FFF` theoretically free on silicon if no HGR
> card — to be confirmed on the target. `$8000-$BFFF` is free without a
> microSD / CFFA1 / JukeBox card.

#### Bench references

- **"OK everywhere" (silicon + POM1)**: `TMS_Logo v1.7` — LOGO interpreter
  in Mode 2 bitmap, no sprites. Validates the VRAM, registers, and graphics
  modes subsystem.
- **"OK on POM1, broken on silicon" (historical)**: pre-strict `A1Galaga` —
  Mode 1 + sprites animated at 60 Hz. On silicon the title screen used to
  exhibit ghost sprites and parasitic checkerboards. Resolved by the
  silicon-strict pass documented in §24.

### 2. Graphics I memory map (the sweet spot for tile games)

VRAM tables in Mode 0 (Graphic I):

| Table | VRAM address | Size | Contents |
|-------|-------------|--------|---------|
| Pattern table | `$0000-$07FF` | 2048 B | 256 8×8 glyphs (8 bytes each) |
| Name table | `$1800-$1AFF` | 768 B | 32×24 character codes |
| Colour table | `$2000-$201F` | 32 B | **One entry per group of 8 characters** |
| Sprite attr | `$1B00-$1B7F` | 128 B | 32 sprites × 4 bytes |
| Sprite pattern | `$3800-$3FFF` | 2048 B | 256 sprite patterns |

---

## Part 2 — Bring-up

### 3. Initialisation sequence

```asm
init_vdp:
        ; 1. Program the 8 registers
        LDX #$00
@rl:    LDA vdp_regs,X
        STA $CC01             ; value
        TXA
        ORA #$80              ; register-write flag
        STA $CC01             ; register number + $80
        INX
        CPX #$08
        BNE @rl

        ; 2. Upload patterns (see §4)
        ; 3. Upload the colour table
        ; 4. Clear the name table
        ; 5. Disable sprites (see §10 — MANDATORY)

vdp_regs:
        .byte $00, $C0, $06, $80, $00, $36, $07, $01
        ; R0=mode, R1=16K+display on+GfxI, R2=name@$1800, R3=colour@$2000,
        ; R4=pattern@$0000, R5=sprite attr@$1B00, R6=sprite pattern@$3800, R7=backdrop black
```

<a name="bug-n3-16k-mode"></a>
**Silicon detail — R1 bit 7 selects 4K vs 16K (Bug N°3)**

R1 bit 7 selects memory mode on the real chip:

- 0 → 4K mode (TMS9928-compatible). Only the first 4 KB of VRAM are
  accessible — addresses are truncated to 12 bits.
- 1 → 16K mode (TMS9918A standard).

At reset, R1 = `$00` → 4K mode. If you initialise R1 **without** setting
bit 7, real silicon stays in 4K and any VRAM addressing ≥ `$1000` loops
back onto the first 4 KB. Pattern Table at `$0800`, Name Table at `$1800`,
sprite tables at `$1B00 / $3800` → **all corrupted** on silicon.

POM1 ignores R1 bit 7 (`TMS9918.cpp` treats VRAM as an unconditional 16 KB
buffer), so code that forgets bit 7 still appears to work on the emulator
and only breaks on the target. Always OR `$80` when writing R1.

**Recommended `R1` constants** (illustrative — `dev/lib/tms9918/tms9918.inc`
ships only the `VDP_DATA` / `VDP_CTRL` port and timing equates, not these R1
bit names; define them locally if you want symbolic names):

```asm
VDP_R1_BASE = $80        ; bit 7 = 16K, MANDATORY on silicon
VDP_R1_DISP = $40        ; display on
VDP_R1_IRQ  = $20        ; IRQ enable
VDP_R1_M1   = $10        ; Mode 1 (text)
VDP_R1_M2   = $08        ; Mode 2 (bitmap) — combined with M3 via R0 bit 1
VDP_R1_LRG  = $02        ; sprites 16×16
VDP_R1_MAG  = $01        ; sprites magnified ×2
```

Typical safe values: `$E0` (display on + IRQ enable + 16K, Mode 0), `$C2`
(display on + 16K + sprites 16×16, bit 1 = LRG), `$F0` (display on + IRQ +
16K + Mode 1 text). `$C0` is the canonical Graphics-I value (16K + display
on, 8×8 sprites) — perfectly valid. The trap is omitting bit 7 entirely
(any R1 value `< $80`), which drops the chip back to 4K mode.

### 4. VRAM address latch and the two-write protocol

```asm
; To write at VRAM address V:
        LDA #<V
        STA $CC01             ; low byte
        LDA #>V
        ORA #$40              ; write flag (without bit $40 = read)
        STA $CC01             ; high byte | $40

; Then write the data (auto-increment):
        LDA data
        STA $CC00
        LDA data2
        STA $CC00             ; goes to V+1 automatically
```

The two `$CC01` writes go through an **internal flip-flop**: the first
byte sets the low address, the second sets the high address plus the
read/write select. The flip-flop has consequences for IRQ-safe code —
see §19.

---

## Part 3 — Drawing

### 5. Graphics I tile workflow

Mode 0 (Graphic I) is the default starting point for tile-based games. The
32×24 name table indexes the pattern table, the colour table provides
foreground / background colours by groups of 8 characters, and sprites
overlay on top. The trick to getting "free" per-tile colours uses the
group-of-8 structure — see §6.

### 6. Colour-group trick for free per-tile colours

The colour table has **one fg/bg colour per group of 8 characters**. Place
each tile type at the first character of its group:

| Tile | Char code | Group | Colour |
|-------|-----------|--------|---------|
| Tile 0 | 0 | 0 (chars 0-7) | Colour A |
| Tile 1 | 8 | 1 (chars 8-15) | Colour B |
| Tile 2 | 16 | 2 (chars 16-23) | Colour C |
| Tile 3 | 24 | 3 (chars 24-31) | Colour D |

Each tile then has its own colour for free. Intermediate chars (1-7,
9-15, …) stay unused.

TMS Sokoban uses this technique for 7 coloured tiles (grey wall, red
target, yellow crate, green crate-on-target, blue player, medium-green
player-on-target).

### 7. Big tiles — 4×4-cell pieces

For larger sprites, use multiple characters per piece:

- **2×2 chars** (16×16 px): 4 glyphs per piece, fits in 1 colour group.
- **3×3 chars** (24×24 px): 9 glyphs, spans 2 groups — force both groups
  to the same colour.
- **4×4 chars** (32×32 px): 16 glyphs = **exactly 2 groups** → 1 group
  triplet per piece type.

TMS Connect 4 uses 4×4 (32×32 px per token): a 28×24-cell board = 224×192
pixels, filling the screen almost entirely. Three group triplets (empty,
red, yellow) × 16 glyphs = 48 chars, 7 colour entries.

### 8. Colour byte format

`(fg << 4) | bg` — fg and bg are palette indices (0-15).

TMS9918 palette:

| # | Colour |
|---|---------|
| 0 | Transparent (see R7 backdrop) |
| 1 | Black |
| 2 | Medium green |
| 3 | Light green |
| 4 | Dark blue |
| 5 | Light blue |
| 6 | Dark red |
| 7 | Cyan |
| 8 | Medium red |
| 9 | Light red |
| 10 | Dark yellow (wood) |
| 11 | Light yellow |
| 12 | Dark green |
| 13 | Magenta |
| 14 | Grey |
| 15 | White |

### 9. Delta rendering and screen independence

Delta rendering on TMS is trivial compared to HGR: compute the name-table
address `$1800 + row*32 + col`, latch it on `$CC01`, write a single
character code to `$CC00`. No clearing needed, no neighbour redraw.

The Apple 1 text screen and the TMS9918 window are **two independent
displays**. Convention: use the text screen for the title, the keyboard
prompt (QWERTY / AZERTY), and the victory messages; use TMS for the game
itself.

---

## Part 4 — Sprite engine

### 10. Disable sprites at init — mandatory

Graphics I always enables sprites. By default, the sprite attribute table
contains random values → garbage sprites appear. Writing `$D0` to the
first Y byte (address `$1B00`) stops the sprite chain:

```asm
        LDA #$00
        STA $CC01
        LDA #$5B              ; $1B | $40
        STA $CC01
        LDA #$D0
        STA $CC00
```

This is the standard nippur72 / Parmigiani convention and the first thing
every POM1 TMS9918 program does at boot.

### 11. Sprite attribute table layout

Each sprite occupies 4 bytes: Y, X, pattern name, colour. Y = `$D0`
terminates the active sprite chain. The first attribute table base is
selected by R5; the pattern base is selected by R6.

<a name="bug-n4-collision-overscan"></a>
**Silicon detail — collisions inside the visible window only (Bug N°4)**

The TMS9918's sprite engine rasters an internal area wider than the
visible screen (~32 px of overscan on each side, X = -32..-1 and 256..287).
Two sprites overlapping in that off-screen zone do **not** set the
collision bit on canonical reference hardware: openMSX's `SpriteChecker.cc`
explicitly clips collision to `[0, kScreenWidth)`, with guard bytes at
`$80` outside the 256-pixel window. POM1 matches this (May 2026 fix) —
collisions in overscan are clipped.

Use **software bounding-box** checks for off-screen sprite logic; don't
rely on the collision flag firing for off-screen overlaps.

### 12. Per-scanline sprite scan and raster splits

<a name="bug-n5-per-scanline"></a>
**Silicon detail — per-scanline scan (Bug N°5)**

The collision (bit 5) and 5S (bit 6) flags update **scanline by scanline**
during rendering, in real time as the beam traces the image. Polling the
status register mid-frame sees the exact state at the current beam
position.

POM1 invokes `scanSpritesForLine(line)` on every scanline crossed (verbatim
port of openMSX `SpriteChecker::checkSprites1`, line-major variant). The
sub-cycle order of SAT fetches and dummy reads after `$D0` is not modelled
by openMSX either — POM1 has the same resolution as the canonical
reference.

**Latency**: on silicon, a collision is detected as it happens (latency
< 1 ms). On POM1 mid-frame, the next call to `scanSpritesForLine`
publishes it; in practice, code that reads status after VBlank sees the
correct value either way.

<a name="bug-n10-raster-split"></a>
**Silicon detail — raster split via 5S (Bug N°10)**

The TMS9918 has no standard raster IRQ, but the 5S flag enables a
classical raster-split trick:

1. Place 5 invisible sprites (colour = 0) aligned at a target Y — e.g.
   Y = 95 mid-screen.
2. Tight-poll bit 6 of the status register:

   ```asm
   @wait_split: LDA $CC01
                AND #$40
                BEQ @wait_split
   ```

3. Bit 6 arms exactly when the beam crosses the 5th line. At that point
   you can change R7 (rainbow backdrop), R5 (second SAT for the bottom
   half), R4 (different pattern set per screen half).

POM1 implements this at line-major granularity. The exact sub-scanline
timing is not modelled by openMSX either, so cycle-precise splits remain
beyond the canonical reference; for typical mid-screen splits the
behaviour is silicon-correct.

### 13. Status register reads are destructive

**Silicon detail (still Bug N°5)** — the silicon clears bit 7 (F), bit 6
(5S), and bit 5 (C) on **every** read of the status register. POM1
reproduces this (`statusReg &= ~0xE0`). Therefore:

- **Never read status mid-frame "just to check the collision"** — bit 7
  (VBlank flag) will be cleared before your sync routine catches it, the
  game misses its frame, music and animations desync.
- **Snapshot once per frame**: read status into a register or RAM cell,
  then test the bits.

```asm
@wait_vbl: LDA $CC01      ; ONE read per frame
           BPL @wait_vbl  ; wait for bit 7 (auto-cleared here)
           TAY            ; save
           AND #$20       ; bit 5?
           BNE @collision ; collision detected during the frame
           TYA
           AND #$40       ; bit 6?
           BNE @overflow_5s
```

<a name="bug-n6-last-sprite-index"></a>
**Silicon detail — status bits 0..4 = last sprite scanned (Bug N°6)**

On the standard TMS9918A, status bits 0..4 contain the SAT index of the
**last sprite scanned** for the current line, updated as the scan
proceeds. When bit 6 (5S) latches, those bits freeze at the 5th sprite's
index. Otherwise they hold the index of the last sprite walked — usually
the `$D0` terminator or sprite 31.

POM1 reproduces this. Before treating bits 0..4 as "5th sprite index",
check that bit 6 is set — otherwise the value is the terminator slot or
the last-active slot.

### 14. Sprite engine during display blank

<a name="bug-n7-sprite-blank"></a>
**Silicon detail — sprite engine during blank (Bug N°7)**

Behaviour during `R1 & $40 == 0` (display blanked) is non-trivial. The
datasheet is ambiguous; MSX reference (BiFi) suggests the sprite engine
keeps scanning during blank but the compositor emits nothing.

POM1 (May 2026) skips the sprite scan in blank, consistent with meisei
`vdp.c:437` (`mode = 9` → blank case = `memset bd 256` with no sprite),
and forces status bits 0..4 to `$1F` per the same reference. As a
consequence, code that blanks the display then waits for a collision
latch via status will **not** see bit 5 set on POM1 — a behaviour to
confirm on silicon.

If your code depends on collision detection during blank, treat it as
unspecified and re-enable display before testing status.

### 15. Sprite cloning and the M3 + R4 trigger

<a name="bug-n8-sprite-cloning"></a>
**Silicon detail — sprite cloning (Bug N°8)**

On TI / NMOS TMS9918A silicon, certain register combinations cause the
**Y coordinate** of sprites with index 8..31 to be polluted by low bits
of R3 (colour table base) and partial bits in the addressing path,
producing vertical ghost clones in the top 64-line stratum of the
screen. These clones consume per-line sprite slots and trigger the
collision bit like legitimate sprites. The MSX demo *Alankomaat*
(Bandwagon group) deliberately exploits this effect.

POM1 implements a verbatim port of meisei's algorithm (author "hap",
published in [openMSX issue #593](https://github.com/openMSX/openMSX/issues/593),
described as *"the only known correct implementation, tested side-by-side
against real MSX1"*).

The cloning trigger condition is:

```
isCloningActive = (R0 & 2) ∧ ((R4 & 3) ≠ 3)
```

That is: M3 set **and** R4 lacks at least one of bits 0-1. R6 plays no
role (contrary to earlier POM1 approximations). Cloning can therefore
engage in perfectly legal Mode II if R4 has a low bit cleared — exactly
the configuration hap's reference BASIC program exhibits.

**Recommendation**: don't exploit cloning for portable code. Always stick
to the 4 documented modes (Mode 0, 1, 2, 3). To exceed 32 sprites, use
classical SAT-swap multiplexing at VBlank (cf. authentic flicker).

#### Thermal drift (edge case, not modelled)

Original TMS9918A clones fade progressively as the chip heats up
(documented "hair-dryer" effect). Block 1 clones disappear first, then
Block 2. Toshiba and Yamaha V9938 fix the addressing paths at the factory
— **no cloning**, code that relies on it doesn't work. POM1 simulates
permanent "cold" cloning; emulator consensus (openMSX, meisei) does not
model thermal drift.

### 16. Chip type and the Toshiba carve-out

POM1 exposes a runtime setter `TMS9918::setChipType()` and an
`enum class ChipType` covering TMS9918A (default), TMS9929A, TMS9118 /
9128 / 9129, T7937A, T6950 (see `TMS9918.h`). Toshiba variants (T7937A,
T6950) short-circuit sprite cloning (`isCloningActive` returns false) —
matches meisei's `!toshiba` guard and openMSX's TMS-vs-Toshiba dispatch.
V99x8 (MSX2) is not modelled; the default stays TMS9918A for the
Apple-1 + P-LAB Graphic Card target.

---

## Part 5 — Timing and synchronisation

### 17. The slot-table model and silicon-strict mode

<a name="bug-n1-vram-timing"></a>
**Silicon detail — VRAM access timing (Bug N°1)**

The TMS9918A is **slave to its own video signal**: the pixel scan and
DRAM refresh have absolute priority. The CPU only receives VRAM access
windows opened by the internal sequencer. When the CPU initiates an
access to `$CC00` or `$CC01`:

1. **Physical preparation delay**: ~28 VDP ticks (≈ 1.3 µs, openMSX
   `Delta::D28`).
2. **Wait for the next free slot**: variable depending on position in the
   scanline and the active mode.

If the CPU sends the next byte **before the previous latch has drained**,
the chip silently overwrites the previous one (openMSX
`tooFastCallback` with `allowTooFastAccess = false`). No error reported.
This is the primary cause of Galaga-class checkerboards on real silicon.

#### openMSX slot-table model (source of truth)

POM1 v6.x replaced the old min-distance threshold with a **verbatim port**
of openMSX's model (`src/video/VDPAccessSlots.cc`). A TMS9918 NTSC
scanline is 1368 VDP ticks (≈ 63.7 µs, openMSX `TICKS_PER_LINE`). For each
mode, openMSX publishes the exact list of CPU slot positions in the line:

- `slotsMsx1ScreenOff` — display blanked (R1.6 = 0). Slots every ~8 ticks
  for most of the line.
- `slotsMsx1Gfx12` — Mode I (Graphic 1) + Mode II (Graphic 2). 19
  slots/line; bursty pattern `4, 12, 20, 28, 116, 124, 132, 140, 220,
  348, 476, 604, 732, 860, 988, 1116, 1236, 1244, 1364`.
- `slotsMsx1Gfx3` — Multicolor (Mode 3). 51 slots/line.
- `slotsMsx1Text` — Text (Mode 1). 91 slots/line, dense for the first
  half.

On every CPU access to `$CC00 / $CC01`:

1. POM1 computes `linePosTicks = (frameCycleCounter * 21) % 1368` (1 6502
   cycle ≈ 21 ticks at 1.022727 MHz).
2. Lookup in the active table: `slotTick = smallest slot ≥ linePosTicks
   + 28`.
3. `pendingDrainCycles = ceil((slotTick - linePosTicks) / 21)` 6502
   cycles.
4. During this drain, any other access is silently overwritten.

openMSX does not distinguish sprites-on vs sprites-off on MSX1 —
`slotsMsx1Gfx12` aggregates both (the slot pattern already reflects the
worst case with sprites). The historic SAT[0] = `$D0` scan is therefore
dropped.

#### POM1 vs openMSX divergence: free VBlank

openMSX picks the active table from `(R1.6, mode)` only — the vertical
position in the frame has no effect. This means that during the ~70 NTSC
VBlank lines with display ON, openMSX imposes the Gfx12 table whereas on
silicon sprite-scan + pixel-fetch don't happen and VRAM bandwidth is
free.

POM1 corrects this in `selectSlotTable()`: if `frameCycleCounter >=
kActiveDisplayCycles` (i.e. in VBlank), it switches to
`slotsMsx1ScreenOff` regardless of R1.6. The silicon-correct VBlank-batch
idiom therefore Just Works:

```asm
@wait_vbl: BIT $CC01
           BPL @wait_vbl     ; spin until F flag set
           ; ~4554 6502 cycles of CPU-free bandwidth available
```

Without this divergence, programs like Rogue (which stack uploads during
VBlank) would see phantom drops on POM1 even though they run on real
silicon.

#### Progressive per-scanline rendering

POM1 keeps a persistent `framebuffer[320×240]` on the `TMS9918` side,
painted line by line in `advanceCycles` as the beam crosses each
scanline. Silicon-correct consequences:

- **R7 (backdrop) changed mid-frame** → the L/R border bands of the
  following scanlines use the new colour; already-rasterised lines keep
  the old one. "Rainbow" effect possible.
- **R1.6 (display blank) toggled mid-frame** → active lines = rendered
  pixels, blank lines = backdrop alone.
- **VRAM modified mid-frame** → only lines rendered AFTER the
  modification see the new pattern/SAT/colour. Earlier lines are frozen.
- **R5/R6/R4 changed mid-frame** → next-line render uses the new
  pointers (split-screen attribute changes).

Pinned by `tests/tms9918_per_scanline_test.cpp` Phase F.

#### Silicon worst case per mode

| Mode | Worst inter-slot gap (ticks) | 6502 cycles | Worst-case total (D28 prep + gap) |
|---|--:|--:|--:|
| Display blanked | 16 | 0.8c | 2c (D28 dominated) |
| Text (M1) | 16 | 0.8c | 2c |
| Multicolor (M2) | 88 | 4.2c | 5c |
| **Graphic I/II** | **128** | **6.1c** | **7.5c** |

The **empirical Tetris floor (11c gap)** sits ~50% above the worst
Gfx12 (7.5c). Galaga's pre-patch 4c bursts sit below the floor → expected
drops, as on silicon.

#### 6502 patterns benchmarked against the slot model

At 1.022 MHz: 1 cycle ≈ 0.978 µs.

| 6502 pattern | Gap between writes | Verdict |
|---|---|---|
| `STA $CC00` ; `STA $CC00` (back-to-back) | 4c | **KO** — below floor |
| `LDA #x` ; `STA $CC00` ; `LDA #y` ; `STA $CC00` | 6c | **KO** — below floor |
| `STA $CC00` ; `NOP×2` ; `STA $CC00` (old strict 8c) | 8c | **borderline** |
| `STA $CC00` ; `JSR tms9918_pad18` ; `STA $CC00` | 16c | **OK** (3 bytes, ratio winner) |
| `STA $CC00` ; `NOP×6` ; `STA $CC00` | 16c | **OK** (6 NOPs = 6 bytes) |
| Tetris loop (`LDA / STA $CC00 / DEX / BNE`) | 11c | **OK** (silicon-validated) |
| Loop `LDA tab,X / STA $CC00,X / DEX / BNE` | 14c | **OK** comfortable |

`JSR tms9918_pad18` (12c pad → 16c STA→STA gap, 3 bytes at the call site)
is the dense winner: 4c/byte ratio, twice as dense as NOP×6 (2c/byte).
`pad24` is an optional cushion; `pad40` is a legacy paranoid variant
(superseded by the 12c contract — do not use in new code).

#### Init trick: blank the display during massive uploads

To load Pattern Table, Name Table, Colour Table at program start
**without timing constraints**, set `R1` bit 6 = 0 (display OFF) before
the upload, then turn it back on afterwards. During the blank, VRAM
bandwidth is fully available (only the ~2 µs preparation delay applies) →
tight loops are fine:

```asm
; Before VRAM init
LDA #$80          ; R1 = 16K + display OFF + everything else at 0
STA VDP_CTRL
LDA #$81          ; reg 1
STA VDP_CTRL

; ... tight VRAM-loading loops (Pattern, Name, Colour tables) ...

; Final display
LDA #$E2          ; R1 = 16K + display ON + IRQ + sprites 16x16
STA VDP_CTRL
LDA #$81
STA VDP_CTRL
```

#### Recommended fixes (in order of preference)

**Option A — `WRT_DATA_REG` / `WRT_DATA_VAL` macros on the lib side
(recommended)**

`dev/lib/tms9918/tms9918.inc` provides (12c contract — 16c STA→STA gap):

```asm
.import tms9918_pad18, tms9918_pad24

.macro WRT_DATA_REG             ; A already loaded, 16c STA→STA gap on exit
        STA     VDP_DATA        ; 4c
        JSR     tms9918_pad18   ; 12c pad — next write lands 12c later
.endmacro

.macro WRT_DATA_VAL val          ; immediate val, 16c STA→STA gap on exit
        LDA     #val            ; 2c
        STA     VDP_DATA        ; 4c
        JSR     tms9918_pad18   ; 12c, 3 bytes
.endmacro
```

**Option B — Indexed addressing (free 1-cycle bonus)**

Replace `STA $CC00` with `STA $CC00,X` (X = 0). Cost: +1 cycle (4c →
5c) without a NOP. Two indexed STAs back-to-back = 10 cycles → OK.

**Option C — Table-driven loop rewrite**

Precompute every frame a SAT table in RAM (40 bytes), then upload it with
a `LDA tab,X / STA $CC00 / NOP / NOP / INX / BNE` loop (4 + 4 + 2 + 2 +
2 + 3 = 17c between writes). Simpler to reason about than inline writes.

#### POM1 silicon-strict mode

When `Silicon Strict` is active (default outside the Multiplexing Fantasy
presets), POM1 **drops** bytes if `$CC00 / $CC01` accesses arrive faster
than the slot-table model — same behaviour as too-fast access on real
silicon. Toggle: Hardware menu or [`CLI.md`](../../doc/CLI.md)
(`--silicon-strict` / `--no-silicon-strict`).

POM1 exposes a complete diagnostic infrastructure when strict is ON:

- **Status bar**: live `STRICT — drops:N` counter next to the
  STRICT/FANTASY tag.
- **stderr trace**: the first 60 drops are traced one line per drop with
  PC, value, vramAddr, remaining drain, position in the line, active
  table, port (D = `$CC00` / C = `$CC01`), phase (active/vblank). Format:

  ```
  [TMS9918 DROP #N] D val=B0 vramAddr=1100 latch2=0 drain=4c linePos=1152
  nextSlot=1236 tbl=Gfx12 vblank=0 frameCycle=120 R1=C0 PC=$5A04
  ```

- **Menu Hardware → Dump TMS9918 drop diagnostics**: emits the full
  histogram on stderr (total + port/phase/table breakdown + top-16 PC).
  Reset via the adjacent *Reset TMS9918 drop counter*.

To target a silicon-safe program: strict ON → run the program → *Dump
diagnostics* → fix the top PC in the list (probably a STA in a tight
loop) → retest → iterate.

#### The pad helpers

The helper `tms9918_pad18: rts` (1 byte) costs 12 cycles via JSR (3
bytes at the site). 4 c/B ratio at the site, twice as dense as NOP. For a
16c STA-STA gap, the JSR replaces 6 NOPs (saving: 3 bytes/site).

`tms9918_pad24: jsr pad12 / rts` chains for exotic pads (24c in 3 bytes
at the site, 4-byte helper). Both are defined in
`dev/lib/tms9918/tms9918_pad.asm` and exported via `.export`; each
TMS9918 project's linker config loads the lib via `EXTRA_ASM`
(`Makefile.common`) or directly via `tools/build_codetank_rom.py`, which
auto-detects the dependency.

#### Diagnostic case study — Galaga `render_sprites`

Pre-patch extract from `sketchs/tms9918/game_galaga/TMS_Galaga.asm`
("hidden" player slot):

```asm
LDA #HIDDEN_Y      ; 2c
STA VDP_DATA       ; 4c  → write #1
LDA #$00           ; 2c   → 2 cycles elapsed
STA VDP_DATA       ; 4c  → write #2 (gap = 6c)  KO
STA VDP_DATA       ; 4c  → write #3 (gap = 4c)  KO
STA VDP_DATA       ; 4c  → write #4 (gap = 4c)  KO
```

This pattern repeats for every SAT slot (10 slots × 4 bytes = 40 bytes /
frame, 60 Hz). On silicon several bytes are lost every frame → corrupted
SAT → ghost sprites at random positions and patterns (the famous
parasitic checkerboards).

#### Why TMS_Logo passes on silicon

The LOGO interpreter computes one pixel at a time via a bytecode loop →
between two VRAM writes there are ~50 to 200 cycles of 6502 arithmetic.
Timing is respected by accident thanks to the interpreter's slowness.

#### Why Galaga's "text" passes despite the SAT corruption

Galaga uses **Mode 0 (Graphic I)** — not Text Mode (the two are
incompatible with sprites). The title-screen "text" rendering is done
with tiles in the Pattern Table, written once at startup via a slow
loop. Any lost bytes are drowned among the 768 Name Table entries,
barely visible. Conversely, the SAT (40 bytes) is rewritten at 60 Hz in
a tight loop → corrupted → ghost sprites immediately visible.

#### Historical sub-bug: `statusReg` bit 7 sticky as VBlank proxy (fixed)

Before 2026-04-30, running Galaga via CodeTank with `Silicon Strict` ON
triggered **no** byte drops even though the `STA $CC00` patterns 4-6
cycles apart are visibly KO on real silicon.

Root cause: `TMS9918::requiredAccessCycles()` used
`(statusReg & 0x80) != 0` as a "we're in VBlank" proxy to relax the
access window to 2 cycles. But that bit is sticky-until-`readControl`:
it arms on every VBlank and stays latched until an `LDA $CC01`. Galaga
**never** reads `$CC01` (0 occurrences in the source) → from frame 1
the bit stays at 1 → `requiredAccessCycles()` returned 2 cycles for the
whole frame → all Galaga writes went through.

Fix:

```cpp
if ((regs[1] & 0x40) == 0 || frameCycleCounter >= kActiveDisplayCycles) return 2;
```

with `kActiveDisplayCycles = (kCyclesPerFrame * 192) / 262` (≈ 12 490
cycles at 1× — 192 active scanlines out of 262 in the NTSC frame).
During active display → strict 8c window; during physical VBlank →
relaxed 2c window.

The new gating reflects silicon better and introduces no false positives.

### 18. Frame synchronisation — polling recommended, IRQ also works

<a name="bug-n2-int-irq"></a>
**P-LAB wiring detail (Bug N°2)** — the P-LAB Apple-1 card **wires** the
TMS9918 `/INT` pin to the 6502 `/IRQ` line (trace verified on real
hardware by Parmigiani). Nino Porcino's nippur72 software never uses it:
the canonical pattern is **polling bit 7 of `$CC01`** (simpler, portable,
independent of the I flag, no `$CC01` flip-flop reentrancy pitfall):

```asm
@vblank_wait:
    LDA VDP_CTRL    ; read status
    BPL @vblank_wait ; bit 7 = 0 → not yet VBlank
    ; here we're in VBlank, status auto-cleared by the read
```

But since the line is wired, an IRQ-on-VBlank handler also works on real
hardware — provided a valid handler is installed at vector `$FFFE` that
reads `$CC01` atomically (cf. the flip-flop convention in §19).

POM1 reflects this faithfully: `irqStrapped == true` by default,
`TMS9918::irqAsserted()` returns `R1.5 ∧ status.7`, and the IRQ aggregator
in `Memory::advanceCycles` pulls `/IRQ` accordingly. Reading `$CC01`
clears the frame flag → IRQ releases on the next tick. As on real
hardware, the request stays **inert until the program does `CLI`** (the I
flag masks the IRQ at reset): Nippur72 polling-only code is unaffected.

`TMS9918::setIrqStrapped(false)` models a card where `/INT` would be
left floating. `irqAsserted()` then returns `false` unconditionally — the
VDP can no longer trigger a frame IRQ regardless of R1.5.

#### Standard polling idiom

```asm
        ; Wait for next VBlank — silicon-correct polling pattern
        BIT $CC01             ; drain stale F flag (clears bits 5/6/7)
@v_wait:
        BIT $CC01
        BPL @v_wait           ; bit 7 = 0 → not yet VBlank
        ; here: we're in VBlank, ~4 554 cycles of VRAM bandwidth
        ; available before the next frame's active display
```

Or with the shared `WAIT_VBLANK` macro in `dev/lib/tms9918/tms9918.inc`:

```asm
.include "tms9918.inc"

        WAIT_VBLANK           ; expands to the pattern above, 7 bytes
        ; ... upload SAT, sprites, animations …
```

Polling is the pattern that the Nippur72 libs target and that all POM1
games (Galaga, Sokoban, Snake, Life, Rogue, Asteroids, Connect4, etc.)
use.

#### Polling side effect on bits 5/6

Reading `$CC01` clears bits 5 (collision), 6 (5S overflow), **and** 7
(F flag) together. If you use those flags, read them **before** the
`WAIT_VBLANK`, or snapshot them into a variable (see §13 idiom).

For games that only read F (the majority), the 5/6 clobber has no
visible consequence.

### 19. The control-port flip-flop and IRQ safety

<a name="bug-n9-flipflop"></a>
**Silicon detail — `$CC01` flip-flop (Bug N°9)**

Writing a VDP register via `$CC01` takes **two bytes**: 1st = data,
2nd = `$80 | regnum`. The VDP uses an internal flip-flop (latch) to
track which byte to expect next.

If a hardware interrupt occurs **between** the two bytes and the IRQ
routine also writes to `$CC01`, the flip-flop desyncs: the main
routine's 1st byte is interpreted as the IRQ routine's 2nd byte → VDP
register corruption.

**Documented silicon solution**: the IRQ routine must read `$CC01`
(status register) first. That **atomically resets** the flip-flop. Any
following write restarts cleanly with a 1st byte.

POM1 reproduces this: `readControl()` sets `latchIsSecond = false`, and
`writeData()` / `readData()` likewise.

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

Because the `/INT` line is wired on P-LAB (§18, default
`irqStrapped = true`), as soon as a program does `CLI` with R1.5 = 1 this
IRQ actually fires and the above convention becomes critical. Nippur72
software stays safe because it polls without ever unmasking the IRQ.

### 20. Why no IRQ on stock P-LAB code

Case that **deadlocks** if you assume `/INT` is unwired:

```asm
        LDA #$E0              ; R1 = display ON + IRQ enable + 16K
        STA $CC01
        LDA #$81              ; reg 1
        STA $CC01
        CLI                   ; allow /IRQ
@loop:  JMP @loop             ; wait for frame IRQ at $FFFE
```

If you write IRQ-based code, install a valid handler at vector `$FFFE`
that reads `$CC01` atomically (§19). POM1 defaults to `irqStrapped = true`
to match the verified P-LAB wiring; some hypothetical non-wired card
would be modelled with `setIrqStrapped(false)` and then the code above
would deadlock.

#### Community FPGA strap (out of scope)

Some users have modified their Apple-1 replica / P-LAB replica-1 to bridge
`int_n_o` (VDP) → `irq_n` (6502) with an inverter. This mod **is not
stock P-LAB**; the default already covers the canonical wiring. If you
target stock P-LAB, simply poll — that's it.

### 21. NTSC frame rate — 59.94 Hz, not 60 Hz

<a name="bug-n11-ntsc"></a>
**Silicon detail — true NTSC rate (Bug N°11)**

Analog NTSC = **59.94 Hz** exactly (60 × 1000/1001), not round 60 Hz.
PAL = exact 50 Hz. The non-integer NTSC rate causes a progressive
**phase drift** on SAT-multiplexing routines.

`POM1_CPU_CYCLES_PER_FRAME_1X_60HZ` in `CpuClock.h` anchors POM1 at
**17 062 cycles/frame ≈ 59.94 Hz** (May 2026, was 17 045 = round 60 Hz).
Formula: `(1001 × 1022727 + 30000) / 60000`. The ~0.1% drift is
eliminated, which matters for fine audio demos and timing-critical SAT
multiplexing.

Pinned by `tests/tms9918_per_scanline_test.cpp` Phase E.

---

## Part 6 — Validation and deployment

### 22. Emulator → silicon bringup protocol

Six-step checklist to port a POM1 program to silicon without surprises:

1. **Measure the real usable RAM** — confirm `$0200-$1FFF` free (should
   be). Test `$2000-$3FFF` (free only without HGR). Test `$8000-$BFFF`
   (free without microSD / CFFA1 / JukeBox).
2. **Check VDP init** — R1 must have bit 7 = 1 (16K). R0/R2/R3/R4/R5/R6/R7
   per the chosen mode. Re-check the register names in
   `dev/lib/tms9918/tms9918.inc`.
3. **Audit all VRAM write loops** — every consecutive `STA VDP_DATA` pair
   must have **≥ 8 cycles** between writes during active display. Prefer
   `STA $CC00,X` or the `WRT_DATA_*` macros. VRAM init loops at program
   start can ignore this rule if the display is blanked (R1 bit 6 = 0)
   during loading.
4. **Prefer polling over `/INT`** — polling bit 7 of the status register
   remains the recommended pattern (simple, independent of the I flag, no
   `$CC01` flip-flop reentrancy pitfall). The `/INT` line is wired on
   P-LAB (§18), so an IRQ-on-VBlank handler also works — but only with a
   valid `$FFFE` vector and an atomic `$CC01` read.
5. **No collision in overscan** — software bounding-box for off-screen
   sprites.
6. **Progressive test** — text only → static bitmap → 1 still sprite →
   animated SAT → full SAT 60 Hz. At each step, validate on silicon
   before adding complexity.

### 23. Reproducible test suite (Tests A-E)

Minimal suite to characterise a new silicon setup. All programs are
loaded via Wozmon and launched manually.

#### Test A — Tight SAT loop (must fail on silicon)

Writes 32 SAT entries without NOPs:

```asm
;     LDA #$00 / STA $CC01 / LDA #$5B / STA $CC01     ; addr=$1B00 write
; @lp LDA #$50 / STA $CC00 / LDA #$00 / STA $CC00 / STA $CC00 / STA $CC00 / DEX / BNE @lp
```

Expected on silicon: corrupted SAT, sprites at random positions.
Expected on POM1 (strict OFF): 32 sprites aligned at Y = `$50`, X = `$00`,
name = `$00`.

#### Test B — Same loop with NOPs (must pass everywhere)

Same as Test A, interleaving `NOP / NOP` between each `STA $CC00`.
Expected: identical on both targets.

#### Test C — Sprite at Y = `$D0` (terminator)

SAT[0] = (Y = `$50`, X = `$00`, name = 1, colour = `$0F`), SAT[1] =
(Y = `$D0`, …), SAT[2] = (Y = `$50`, X = `$20`, name = 1, colour =
`$0F`). Sprite #2 must **not** appear.
Expected: 1 visible sprite (slot 0). Identical on both targets if silicon
is OK.

#### Test D — 5S overflow

5 sprites aligned at Y = 50, X = 0, 16, 32, 48, 64. Read status after
VBlank.
Expected: bit 6 set, low 5 bits = 4. Identical on both targets.

#### Test E — Overscan collision (expected difference)

2 sprites with early-clock (colour bit 7 = 1), X = 10 → actual X = -22,
Y = 50. Solid pattern. Read status after VBlank.
Expected on silicon: bit 5 set (off-screen collision detected on
historical Nouspikel reference).
Expected on POM1 + openMSX reference: bit 5 = 0 (collision clipped to
visible window — see §11 Bug N°4).

This test calibrates the canonical-reference choice on your silicon.

#### POM1 test coverage

State 2026-04-30:

- TMS9918-dedicated ctest tests: `tms9918_sprite_status`,
  `tms9918_silicon_strict_runtime`, `tms9918_per_scanline`,
  `tms9918_advanced_silicium`.
- ~20 6502-asm sub-tests in the `TMS_SilBench` ROM, auto-verifiable.
- Standalone visual interactive tests (`tms9918_clone` T12,
  `tms9918_split`).
- Galaga-class stress benchmark (~30 sec).
- 6-phase final demo (~30 sec) with real fauna sprites.

### 24. Defensive checklist (reference card to print)

Keep handy for any new TMS9918 code intended for silicon:

- [ ] `R1` initialised with **bit 7 = 1** (= 16K mode). Typical value:
      `$E0` or `$F0`.
- [ ] **≥ 8 cycles between 2× VRAM accesses** in Graphic mode with
      active sprites (NOPs, indexed addressing, or a loop with 6+ cycles
      of intervening instructions).
- [ ] For massive uploads (init Pattern / Name / Colour Table):
      **blank the display** (R1 bit 6 = 0) during the upload then
      re-enable it.
- [ ] VBlank waited via `LDA $CC01 ; BPL …` (polling), with IRQ used
      only when intentionally wiring the `$FFFE` vector and reading
      `$CC01` atomically in the handler.
- [ ] No `WAI` and no `CLI` + IRQ handler for VBlank synchronisation
      unless you specifically need IRQ-driven sync (P-LAB supports it
      but polling stays preferred).
- [ ] **ONE single status read per frame** (the read clears bits 5, 6,
      and 7 atomically).
- [ ] Any IRQ routine that touches the VDP must **read status first**
      (resets the 1st/2nd-byte flip-flop).
- [ ] Collision tested but **never in the overscan zone** (use software
      bounding-box off-screen — Bug N°4 clips collisions to the visible
      window).
- [ ] **`$D0` terminator** placed after the last active SAT slot
      (otherwise 32 sprites scanned per frame, perf and spurious 5S).
- [ ] Before treating status bits 0..4 as "5th sprite index", check
      **bit 6 is set** (otherwise the value is the terminator slot or
      the last sprite walked — Bug N°6).
- [ ] Don't exploit the **raster split via 5S hack** beyond line-major
      granularity (Bug N°10 — POM1 matches openMSX's resolution, finer
      sub-scanline splits are not modelled).
- [ ] Don't exploit the **sprite cloning** of illegal hybrid modes
      (Bug N°8) — refused by Toshiba / Yamaha clones, fades with
      thermal drift on real NMOS.
- [ ] Documented modes only: Mode 0 (Graphic I), Mode 1 (Text), Mode 2
      (Graphic II / Bitmap), Mode 3 (Multicolor). **No hybrid
      combination** unless you understand the meisei dispatch (§25).

### 25. Patching tooling — `silicon_strict_patch.py`

`sketchs/tms9918/game_galaga/TMS_Galaga.asm` is the first TMS9918
game reference fully ported under `Silicon Strict` ON. The procedure is
reusable across the other games in the repo (Sokoban, Snake, Connect4,
Maze3D).

Reusable script: **`tools/silicon_strict_patch.py`** (inserts
`JSR tms9918_pad18` at detected sites). Idempotent — `--unpatch` strips
the v1 (NOPs) and v2 (JSR) markers before fresh reinsertion.

```bash
# Patch in place
python3 tools/silicon_strict_patch.py path/to/Game.asm

# Dry-run (count without writing)
python3 tools/silicon_strict_patch.py path/to/Game.asm --dry-run

# Strip-only (revert without reinsertion)
python3 tools/silicon_strict_patch.py path/to/Game.asm --unpatch
```

Applied rules (cumulative, deterministic order):

| Case | Detected pattern | v2 insertion (`JSR tms9918_pad18`) | Bytes added |
|---|---|---|--:|
| **A** | `ST? VDP_*` adjacent to `ST? VDP_*` | `JSR tms9918_pad18` between | 3 |
| **B** | `ST? VDP_* / LDA #imm / ST? VDP_*` | `JSR tms9918_pad18` BEFORE the LDA | 3 |
| **C** | `ST? VDP_* / LDA <zp/abs/zp,X> / ST? VDP_*` | `JSR tms9918_pad18` BEFORE the LDA | 3 |

The patcher injects `.import tms9918_pad18` once at the top of every
patched file (for projects that don't include `tms9918.inc`). The
`tms9918_pad18 / pad24` helper lives in `dev/lib/tms9918/tms9918_pad.asm`
and is linked automatically by `Makefile.common` (via `EXTRA_ASM`),
`emit_woz.py` (auto-detection), and `build_codetank_rom.py`
(auto-detection).

`ST?` covers `STA / STX / STY` (Galaga uses all three). Cross-port
(`VDP_DATA → VDP_CTRL` or vice versa): the window is unique for both
ports — the matcher covers `VDP_(DATA|CTRL)` indiscriminately.

**No skip annotation — strict means strict.** Earlier versions of the
patcher honoured a `; SILICON_STRICT_SKIP` comment to exempt a routine
from pad injection. This escape hatch was removed (May 2026) for two
reasons:

1. Substring-match footgun: a comment mentioning the directive name
   (e.g. *"do not add SILICON_STRICT_SKIP here"*) silently disabled
   injection on the entire routine — the `hide_slot_4` incident in
   Galaga where hours were lost chasing a cycle regression when the
   pads simply hadn't been emitted.
2. A "strict mode" with per-routine exemptions is a hollow promise: a
   build that passes strict no longer guarantees the silicon contract,
   because the auditor can no longer distinguish audited routines from
   exempted ones.

Routines that need particular padding (cross-JSR cushions, VBlank entry
sync pad, cross-caller cushion at the start of `init_vdp_g*`) must inline
their `JSR tms9918_pad{12,40}` explicitly. The patcher detects these
manual pads via `is_existing_pad` and doesn't inject on top.

#### Patched project inventory (state at 2026-04-30)

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

#### Byte-saving refactor — `hide_slot_4`

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

Refactored as `hide_slot_4` (22 B helper with built-in NOP padding) plus
10× `JSR hide_slot_4` (3 B each). Net saving ≈ 100 B on the ROM slot,
indispensable to fit the full NOP coverage.

#### Hidden `LDA <zp 3c>` bridges

Typical sites missed if case C is not covered:

| ASM site | Pattern | Silicon-strict symptom |
|---|---|---|
| `render_sprites @show_p` | `STA VDP_DATA / LDA player_x / STA VDP_DATA` | Player ship flickers 1/2 frame |
| `plot_star` final write | `STA VDP_CTRL / LDA temp3 / STA VDP_DATA` | Star not plotted → sparse starfield |
| `render_sprites @en_paint X` | `STA VDP_DATA / LDA enemy_x / STA VDP_DATA` | Enemy flickers (but `enemy_x,X` 4c → OK) |

`player_x`, `temp3` etc. are in zero page (`.res 1` in the ZEROPAGE
segment). `LDA zp` = 3 cycles → gap 4 + 3 + 4 = **11c** from the start of
the previous STA, i.e. **7c** between the two VDP latches. Adding 1 NOP
(2c) is enough to reach 9c.

#### `STA VDP_CTRL / STX VDP_CTRL` bridges (2-byte address write)

`draw_str_tms` (and clones) did `STA VDP_CTRL / STX VDP_CTRL` direct (4c
gap). The initial matcher targeted only `STA VDP_CTRL` — `STX` and `STY`
were ignored → 2nd half of the address-write dropped → all strings
written by `draw_str_tms` ended at random VRAM offsets (splattered text).
Resolved by extending the matcher to `ST[AXY] VDP_(DATA|CTRL)`.

#### ROM slot — why the `dualslot8k` layout

Full coverage on Galaga = 219 NOPs = 219 bytes added. The historic
menu-bank layout (`build_codetank_rom.py --layout=menu`) reserved only
**7 424 B** for the Galaga slot (`$4100-$5DFF`) — Galaga with patches
was 7 419 B → 5 B of margin, untenable once all cases are covered.

Solution: `--layout=dualslot8k`, which offers **8 192 B** per slot and
sacrifices the interactive menu plus Snake/Life:

```
Lower bank ($4000-$7FFF):
  $4000-$5FFF  Galaga  (8 kB, 760 B padding with full patch)
  $6000-$7FFF  Sokoban (8 kB, 3 410 B padding)
Upper bank:
  Tetris launcher + payload (unchanged)
```

No menu — Wozmon `4000R` launches Galaga, `6000R` launches Sokoban.
Published under `roms/codetank/Codetank_GAME1.rom`.

Builder:

```bash
python3 tools/build_codetank_rom.py --layout=dualslot8k -o roms/codetank/Codetank_GAME1.rom
```

Cfgs: `apple1_galaga_codetank.cfg` / `apple1_sokoban_codetank.cfg` (DevBench,
16 KB CodeTank @ `$4000`) and `apple1_*_codetank_bank.cfg` slots for
`tools/build_codetank_rom.py`.

#### Visual validation

POM1 `--preset 9 --terminal --silicon-strict`, `4000R`, pick QWERTY:

- Full splash page `A1GALAGA / APPLE-1 TMS9918 / BY VERHILLE ARNAUD`.
- 3 alien sprites SCOUT / FIGHTER / BOSS with HP labels.
- Clean keyboard menu `1 QWERTY (A D S) / 2 AZERTY (Q D S) / SPACE FIRE`.
- Gameplay: HUD `SCORE/LIVES/W:01`, 6-8 star starfield scrolling smoothly,
  player ship + enemies without flicker.

Reference screenshots in `screenshots/pom1_latest.png` (captured via
TerminalCard ESC S after `--terminal`).

---

## Part 7 — Reference

### 26. TMS9918 register quick reference

| Reg | Bits | Role |
|---|---|---|
| R0 | bit 1 = M3 (Mode bit 3); bit 0 = External VDP (unused) | Mode selection |
| R1 | bit 7 = **16K**; bit 6 = Display ON; bit 5 = IRQ Enable; bit 4 = M1; bit 3 = M2; bit 1 = sprites 16×16; bit 0 = sprites ×2 mag | Mode + display |
| R2 | bits 3-0 = Name Table base × `$400` | Name Table address |
| R3 | Colour Table base (Mode 0: ×`$40`; Mode 2: bit 7 + mask) | Colour Table address |
| R4 | bits 2-0 = Pattern Table base × `$800` (Mode 0); bit 2 + mask (Mode 2) | Pattern Table address |
| R5 | bits 6-0 = Sprite Attr Table base × `$80` | SAT address |
| R6 | bits 2-0 = Sprite Pattern Table base × `$800` | Sprite Patterns address |
| R7 | bits 7-4 = FG (text); bits 3-0 = BG / Backdrop | Colours |

Status register (read `$CC01`, **destructive**: clears F/5S/C on every
read):

| Bit | Name | Role |
|---|---|---|
| 7 | F | Frame flag — set at start of VBlank, triggers `/INT` if R1 bit 5 = 1 |
| 6 | 5S | Fifth sprite overflow — set when > 4 sprites on one scanline |
| 5 | C | Coincidence / collision — set when 2 sprites overlap (bit-pattern, colour 0 included) |
| 4-0 | 5S index | SAT index of the **first** identified 5th sprite — valid **only if bit 6 = 1** |

**Documented graphics modes (M1, M2, M3)**:

| M1 | M2 | M3 | Mode | Description | Sprites |
|---|---|---|---|---|---|
| 0 | 0 | 0 | **Mode 0** Graphic I | 32×24 tiles, 32 colour groups (8 patterns/group) | Yes |
| 0 | 0 | 1 | **Mode 2** Graphic II | Full 256×192 bitmap, colour per 8 px | Yes |
| 1 | 0 | 0 | **Mode 1** Text | 40×24 chars, 6×8 glyph, FG/BG via R7 | **No** |
| 0 | 1 | 0 | **Mode 3** Multicolor | 64×48 coloured blocks | Yes |

Any other combination (M1+M2, M2+M3, M1+M3, M1+M2+M3) = **illegal hybrid
mode**. See §27 for what POM1 (and silicon) do in those cases.

### 27. Hybrid modes and rare configurations

POM1 ports meisei's dispatch for the illegal mode combinations
(May 2026):

- **M3 + M1** → fallback text (M3 ignored).
- **M3 + M2** → fallback multicolor.
- **M1 + M2** (and all-three after M3 XOR) → "static vertical bars"
  glitch: 4 px text-colour + 2 px backdrop, ×40, independent of VRAM.
- Sprites are OFF in mode 1 and mode 5 (text-derived) per meisei
  `if (~mode & 1)`.

Affected programs: *Lotus F3* (MSX1 palette), *Illusions* demo,
dvik/joyrex `scr5.rom`.

**Text mode borders** — 6 px left / 10 px right asymmetric per TMS9918A
datasheet + meisei `vdp.c:475-510` (fixed May 2026 — was 8/8 symmetric
before).

**VRAM power-on init** — `$FF` even / `$00` odd bistable per meisei
`vdp.c:212-217` (fixed May 2026 — was all-zero before). MSX1 silicon.
MSX2 settles to all-`$FF`; MSX2-targeted programs (e.g. *Universe:
Unknown* final) are slightly glitchy at boot — behaviour consistent with
MSX1 hardware.

**Colour-0 sprite collision** — POM1 collides. openMSX MSX1
(`isMSX1VDP()`) collides always (`VDP.hh:201-208`). meisei collides.
Consensus match on TMS9918A behaviour — the "???" mention in the openMSX
comments refers only to V99x8 (which have a toggle).

**Precise HBlank** — public API `TMS9918::inHBlank()` (`TMS9918.cpp`,
verbatim port of openMSX `VDP::getHR()` `VDP.hh:948-961`). TMS9918A NTSC
constants: `HBLANK_LEN_TXT = 404`, `HBLANK_LEN_GFX = 312` ticks;
`getLeftSprites` 282/258 ticks (text/gfx); `getRightBorder = leftSprites
+ 960/1024`. Lets callers test whether the beam is in HBlank.

### 28. Pixel and scanline timing reference

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
| Exact frame rate | 59.94 Hz (NTSC) / 50 Hz (PAL) | POM1 anchors at 17 062 cycles/frame (§21) |

**Tick → cycle ratio**: 21 ticks/cycle. openMSX `TICKS_PER_SECOND =
3 579 545 × 6 = 21 477 270`, 6502 = 1 022 727 → exact ratio
21.0000029 (drift = 3 ticks/sec). Imperceptible.

### 29. Implementation examples

- `sketchs/tms9918/game_sokoban/TMS_Sokoban.asm` — 47 levels, 8×8
  tiles with 7 colours.
- `sketchs/tms9918/game_galaga/TMS_Galaga.asm:2593-2600` — polling
  pattern commented in place.

### 30. POM1 fidelity status table

POM1 silicon-strict models the TMS9918 behaviours documented in
Nouspikel and openMSX. Not all models are equally verifiable: some are
verbatim ports of reference code (very solid), others are plausible
approximations whose real silicon ground truth remains uncertain. This
table gives the honest status of each bug to inform the confidence
placed in POM1 as a pre-deployment validation tool.

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
| **Colour-0 sprite collision** | 🟢 SOLID | POM1 = collide. openMSX MSX1 (`isMSX1VDP()`) = collide always (`VDP.hh:201-208`). meisei = collide. **Consensus match** on TMS9918A behaviour — the "???" mention in the openMSX comments refers only to V99x8 (which have a toggle). |
| **Hybrid mode rendering** | 🟢 SOLID | **meisei dispatch port (May 2026)**. M3+M1 → fallback text (M3 ignored). M3+M2 → fallback multicolor. M1+M2 (and all-three after M3-XOR) → "static vertical bars" glitch (4 px text-color + 2 px backdrop, ×40, independent of VRAM). Affected programs: Lotus F3 (MSX1 palette), Illusions demo, dvik/joyrex `scr5.rom`. Sprites OFF in mode 1/5 (text-derived) per meisei `if (~mode&1)`. |
| **Text mode borders** | 🟢 SOLID | **6 px left / 10 px right** asymmetric per TMS9918A datasheet + meisei vdp.c:475-510 (fixed May 2026 — was 8/8 symmetric before). |
| **VRAM power-on init** | 🟢 SOLID | **`$FF` even / `$00` odd** bistable per meisei vdp.c:212-217 (fixed May 2026 — was all-zero before). MSX1 silicon. MSX2 settles to all-`$FF`, MSX2-targeted programs (e.g. *Universe: Unknown* final) slightly glitchy at boot — behaviour consistent with MSX1 hardware. |

**Legend**:

- 🟢 SOLID — deterministic implementation based on canonical reference
  (datasheet, openMSX source).
- 🟡 UNVERIFIED — plausible model reproducing the documented behaviour,
  but not confronted with real silicon.
- ⚠️ APPROXIMATION — imperfect model that produces a visible/observable
  effect but may diverge from silicon in the details.
- 🔑 OPEN — silicon behaviour genuinely unknown, POM1 picks a side.
- 🔴 NOT MODELLED — silicon behaviour known but POM1 doesn't simulate it.

### 31. External resources

- [openMSX `VDPAccessSlots.cc`](https://github.com/openMSX/openMSX) —
  source of truth for the slot-table model (Bug N°1).
- [openMSX issue #593](https://github.com/openMSX/openMSX/issues/593) —
  hap's published cloning algorithm (Bug N°8).
- Sean Riddle's TMS9918 hardware notes and the original TI datasheet.
- Nouspikel's TMS9918A pages — historical reference for register
  semantics.
- meisei `vdp.c` — author hap, second canonical reference for sprite
  cloning, hybrid mode dispatch, text-mode borders, VRAM power-on init.

---

*Bug N°1 (timing) confirmed on silicon via Galaga. Bugs N°2 to N°11
derived from static analysis of `TMS9918.cpp` cross-referenced with the
TI / Texas Instruments / BiFi MSX / openMSX / meisei references — to
validate on silicon case by case (Tests A to E above).*
