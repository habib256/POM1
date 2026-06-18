# Apple 1 6502 Assembly Programming

**Practical guide — text mode, HGR (GEN2 Color Graphics Card), TMS9918 (P-LAB Graphic Card)**  
VERHILLE Arnaud — 2026

This document summarises everything you need to program the Apple 1 emulated by POM1, in the three available video modes. It is drawn from the concrete experience of porting **Sokoban** (47-72 levels per mode) and **Connect 4** (all three modes).

**Related documents**

| File | Contents |
|---------|---------|
| [`APPLE1DEV.md`](APPLE1DEV.md) | Agent playbook: presets, deployment, CLI examples, short pitfalls |
| [`SILICONBUGS.md`](SILICONBUGS.md) | Real TMS9918 vs POM1, strict VRAM timing, sprites |
| [`doc/CLI.md`](../doc/CLI.md) | Exhaustive list of flags (`--preset`, `--silicon-strict`, …) |
| [`TODO6502.md`](TODO6502.md) | Project backlog under `dev/projects/` |

**Contents (titles §1–§11)** — toolchain · common hardware · 6502 pitfalls · text mode · HGR · TMS9918 · game patterns · zero page · reference implementations · external resources · checklist.

---

## 1. Toolchain and workflow

### Tools

- **ca65** — 6502 assembler from the cc65 suite
- **ld65** — linker, takes a `.cfg` to place segments and ZP
- **python3** — generates the Woz Monitor hex dump from the binary

**Install cc65**: `sudo apt install cc65` (Debian/Ubuntu) · `sudo dnf install cc65`
(Fedora) · `sudo pacman -S cc65` (Arch) · `brew install cc65` (macOS) ·
<https://cc65.github.io/> (Windows/other). Verify with `ca65 --version`.

> **Simplest way to get started**: the built-in **POM1 Bench** (*DevBench → POM1
> Bench*, desktop) edits, assembles/compiles (asm **or** C) and launches in one click, with
> a `HELLO WORLD` skeleton per target. Copy `dev/projects/_template/` for a
> minimal starting point. In C → [`Programming_Apple1_C.md`](Programming_Apple1_C.md).

### Standard commands

Sources live under `dev/projects/<name>/` (each has its own `Makefile`). Manual workflow if needed:

```bash
# Assemble
ca65 -o build/MyGame.o dev/projects/<name>/MyGame.asm

# Link with specific config
ld65 -C dev/cc65/apple1_4k.cfg -o build/MyGame.bin build/MyGame.o

# Binary → Woz Monitor conversion (hex dump 16 bytes per line, hex addresses)
python3 -c "
data = open('build/MyGame.bin','rb').read()
base = 0x0280
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    print(f'{base+i:04X}: ' + ' '.join(f'{b:02X}' for b in chunk))
" > software/<dir>/MyGame.txt
```

The compiled binary and its Woz hex `.txt` are deposited under `software/<dir>/` — that's where POM1 reads them (the card auto-enable hooks are wired to `software/Graphic HGR/`, `software/Graphic TMS9918/`, etc.).

### Loading into POM1

1. Pick a preset with the desired card — **Machine Presets** table in [`README.md`](../README.md) (e.g. TMS9918 + CodeTank → **preset 9**, GEN2 HGR → **12**).
2. (Or auto-enable: place the deliverable under `software/hgr/`, `software/tms9918/`, etc. — see [`APPLE1DEV.md`](APPLE1DEV.md) §8.)
3. **File > Load Memory** → select the `.txt`
4. In the Woz Monitor, type `280R` (or the linker's start address)

### Available linker configs

| Config | CODE range | Size | Typical use |
|--------|-----------|--------|---------------|
| `dev/cc65/apple1_4k.cfg` | `$0280-$127F` | 4 096 B | Text or TMS9918 games (VRAM off the bus) — default config |
| `dev/cc65/apple1_gen2.cfg` | `$E000-$EFFF` | 4 096 B | HGR games: code in the high bank (launch with `E000R`), the framebuffer `$2000-$3FFF` stays reserved (direct writes). Programs > 4 KB: dedicated split-bank cfg (cf. `hgr_chess/apple1_chess_hgr.cfg`, `games_sokoban/apple1_sok_hgr.cfg`) |
| `dev/cc65/pom1_fantasy.cfg` | configurable | — | Multiplexing Fantasy preset (POM1-only) |

Minimal `.cfg` syntax:

```
MEMORY {
    ZP:     start = $0000, size = $0023, type = rw, define = yes;
    CODE:   start = $0280, size = $1000, type = ro, define = yes, file = %O;
}
SEGMENTS {
    ZEROPAGE: load = ZP,   type = zp;
    CODE:     load = CODE, type = ro;
}
```

---

## 2. Apple 1 hardware common to all modes

### I/O addresses

| Address | Role |
|---------|------|
| `$D010` | `KBD` — keyboard data (bit 7 = 1 when key pressed, remaining 7 bits = ASCII) |
| `$D011` | `KBDCR` — keyboard control register, bit 7 = 1 when key available |
| `$D012` | `DSP` — display a character, bit 7 must be set; bit 7 on read = 0 when ready |
| `$FFEF` | `ECHO` — Woz Monitor routine to print A to the screen (uses DSP) |
| `$FFFC-$FFFF` | Reset/IRQ vectors (in Woz Monitor ROM) |

### PIA bit 7 convention

The Apple 1 uses **bit 7 as "data valid"** on the 6821 PIA:
- **Write to ECHO**: the character must have bit 7 set → `ORA #$80` before `JSR ECHO`
- **Read from KBD**: bit 7 is always set, strip it with `AND #$7F`
- The `CR` character sent to ECHO is therefore `$8D` (`$0D | $80`)

### Forced uppercase

The Apple 1 keyboard forces uppercase by default (hardware). Only compare against uppercase:

```asm
CMP #'W'          ; OK, always true whether the user types 'w' or 'W'
; CMP #'w'        ; never true
```

### Standard key wait loop

```asm
wait_key:
@wk:    LDA KBDCR
        BPL @wk           ; bit 7 = 0 → no key, keep waiting
        LDA KBD
        AND #$7F          ; strip the PIA bit 7
        RTS
```

### String printing routine

```asm
print_str_ax:
        STA str_lo
        STX str_hi
        LDY #$00
@lp:    LDA (str_lo),Y
        BEQ @dn
        ORA #$80          ; bit 7 for ECHO
        JSR ECHO
        INY
        BNE @lp
@dn:    RTS
```

Call: `LDA #<str_title; LDX #>str_title; JSR print_str_ax`.

---

## 3. 6502 pitfalls that bit me

### Branch range ±127 bytes

Branch instructions (`BEQ`, `BNE`, `BCC`, `BCS`, `BPL`, `BMI`, etc.) can only reach a target at ±127 bytes from the next instruction. Long routines (check_win, execute_move) quickly exceed this range.

**Solution 1**: invert the condition and `JMP`:
```asm
        BCC @skip
        JMP @target_too_far
@skip:
```

**Solution 2**: trampoline in the middle of the routine:
```asm
@blk_tr:
        JMP @blocked       ; visible from both halves
```

### ADC without CLC

`ADC` adds the carry on top of the operand. If the carry is not controlled, you randomly add `0` or `1`. **Rule**: `CLC` before every first `ADC` of a sum. In a multi-precision chain (16-bit +, 16-bit +), let the carry propagate naturally for the following ADCs.

```asm
        CLC               ; indispensable
        LDA lo1
        ADC lo2
        STA result_lo
        LDA hi1
        ADC hi2           ; no CLC here: propagate the carry
        STA result_hi
```

### TAX clobbers X — golden rule

When a helper returns a value in A via a lookup table, the temptation is to write:

```asm
helper:
        TAX
        LDA tbl,X
        RTS
```

**This silently breaks** the caller that held an index in X. The following `STA ARR,X` will write anywhere. In Sokoban, this bug caused the player to "disappear" when returning to its starting square.

**Rule**: always use `TAY` for the lookup in a helper, never `TAX`:

```asm
helper:
        TAY
        LDA tbl,Y
        RTS
```

Y is rarely used by the caller for array indices (that's typically X), so we clobber it without risk.

### Accessing tables > 256 bytes

`LDA $4000,Y` with Y=0..255 covers one page. Beyond that, two approaches:

**Page-aligned table** at `$XX00`: use `(zp),Y` with a pointer whose high byte is incremented by index:
```asm
        LDA #>ARRAY        ; = $40
        CLC
        ADC index_hi
        STA ptr_hi
        LDA #<ARRAY        ; = $00 if page-aligned
        STA ptr_lo
        LDY index_lo
        LDA (ptr_lo),Y
```

**Parallel lo/hi tables**: handy for ~20 16-bit entries (e.g. HGR scanlines, row*128 lookup):
```asm
        LDX row
        LDA offset_lo,X
        STA ptr_lo
        LDA offset_hi,X
        STA ptr_hi
```

### Relative `.include`

In ca65, `.include "file.inc"` resolves relative to the source file. So put the `.inc` in the same folder.

---

## 4. Text mode — 40×24 terminal

### Philosophy

The Apple 1 text screen is **append-only**: no direct cursor addressing, characters appear at the current position and `$0D` moves to the next line (scrolling if necessary).

Consequence: **no partial refresh**. To "redraw", you reprint the whole scene and let the scroll do its work — previous frames naturally exit from the top.

### Minimal frame structure

```asm
render_screen:
        ; Blank line on entry (separates from the previous frame)
        LDA #$8D
        JSR ECHO

        ; Game content (e.g. 12 grid rows)
        ; ... print loop ...

        ; Footer with available keys
        LDA #<str_footer
        LDX #>str_footer
        JSR print_str_ax
        RTS
```

**What not to do**:
- Reprint the `* SOKOBAN *` title every move (history pollution, the user sees useless text scrolling by)
- Clear with 24 `CR` every frame (wastes time and causes flicker)

### Text alignment with separators

Watch cell-width consistency between separator lines and data lines:

```
       +---+---+---+    ← 4 chars per segment (1 '+' + 3 '-')
       | X | Y | Z |    ← must also be 4 chars per cell ( ' X |' )
       +---+---+---+
```

If you only print `X |` (3 chars), the `|` visibly shift away from the `+`. Classic Connect 4 first-draft bug.

### Implementation example

`dev/projects/games_sokoban/Sokoban.asm` — 20×12 ASCII grid, full redraw every move, ~4 KB binary with 47 levels.

---

## 5. HGR mode — GEN2 Color Graphics Card

### Characteristics

- **Resolution**: 280×192 pixels
- **Framebuffer**: `$2000-$3FFF` (8 KB)
- **Scanline layout**: non-linear Apple II (3 groups of 64 lines, interleaved by 8)
- **Format**: 7 pixels per byte, **bit 7 = NTSC group selector** (not a pixel!)

### NTSC colours by artefact

Bit 7 of each byte determines the "group":
- Group 0 (bit 7 = 0): violet/green
- Group 2 (bit 7 = 1): blue/orange

The pixel ends up as:
- **White** if the lit pixel has a lit neighbour (left or right — `resolveColor`: `prevOn || nextOn`)
- **Coloured** if the pixel is isolated:
  - screenX even, group 0 → violet
  - screenX odd, group 0 → green
  - Even, group 2 → blue
  - Odd, group 2 → orange

### Getting clean white

**Rule**: each lit pixel must have a lit neighbour next to it, in the same byte or across a boundary. A 1-pixel-thick vertical line will always render as violet/green/blue/orange depending on position. **A 2-pixel-thick line will render white**.

### Lookup table hgr_tables.inc

The file `dev/lib/hgr/hgr_tables.inc` (included via `-I ../../lib/hgr` in the project Makefiles) provides (896 bytes + 30 of code):
- `hgr_lo[192]`, `hgr_hi[192]`: address of each scanline (handles the Apple II interleave) — via `hgr_scanline.inc`
- `hgr_col[256]`, `hgr_mask[256]`: for a pixel at screenX x, gives the byte column and the bitmask — via `hgr_plot_tables.inc`
- `plot_pixel`: ~45-cycle routine that plots a pixel at (cur_x, cur_y) — via `hgr_plot.asm`
- `clear_hgr`: zeros `$2000-$3FFF` — via `hgr_clear.asm`

It is now an *umbrella bundle* that `.include`s these four modules; to take only part of it, include the modules directly. `umul8` (8×8 → 16-bit multiplication) **is no longer in this bundle**: it lives in `dev/lib/m6502/multiply.asm`, to include separately if needed.

**Pitfall**: even if you don't use `plot_pixel`, its ZP variables (`cur_x`, `cur_y`, `ptr_lo`, `ptr_hi`) must be declared, otherwise the assembler fails.

### Byte-aligned tiles

For simplicity, use tiles whose width is a multiple of 7: **7, 14, 21, 28 pixels**. Each row of the tile writes in 1, 2, 3 or 4 whole bytes.

Example 14×16: 2 bytes × 16 scanlines = 32 bytes per tile.

### Non-aligned tiles — sub-byte rendering

If the width is not a multiple of 7 (e.g. maze with 4-pixel walls), you need **lookup tables repeating a pattern of 7**. For a block at grid gx:

| gx%7 | col_byte offset | col_mask1 | col_mask2 |
|------|-----------------|-----------|-----------|
| 0 | +0 | $0F | $00 |
| 1 | +0 | $70 | $01 |
| 2 | +1 | $1E | $00 |
| 3 | +1 | $60 | $03 |
| 4 | +2 | $3C | $00 |
| 5 | +2 | $40 | $07 |
| 6 | +3 | $78 | $00 |

`fill_block` does a read-modify-write: `byte |= mask1`, and if `mask2 ≠ 0`, `byte+1 |= mask2`. See `dev/projects/hgr_maze/HGR_Maze.asm`.

### Scanline stride +$0400 trick

Within a group of 8 consecutive scanlines (Apple II HGR), the next is at `+$0400`. Usable to write an 8-scanline tile without a lookup:

```asm
        LDA #$04
        STA stride_counter
@loop:
        ; write the scanline
        LDA ptr_hi
        CLC
        ADC #$04          ; +$0400
        STA ptr_hi
        DEC stride_counter
        BNE @loop
```

**Caution**: this breaks when crossing a group boundary (scanline 8, 16, 24…). For tiles larger than 8 scanlines, use `hgr_lo`/`hgr_hi` on every line.

### Initialisation and clearing

```asm
.include "hgr_tables.inc"  ; at the end of the file

; at the start of the program
JSR clear_hgr              ; zero the framebuffer
```

### Implementation example

- `dev/projects/hgr_maze/HGR_Maze.asm` — sub-byte rendering maze (4-pixel walls)
- `dev/projects/hgr_sokoban/HGR_Sokoban.asm` — full 72-level game, 14×16 tiles, delta rendering
- `dev/projects/hgr_mandelbrot/HGR_Mandelbrot.asm` — computation + pixel plotting
- `dev/projects/hgr_house/HGR_House.asm` — shape drawing

---

## 6. TMS9918 mode — P-LAB Graphic Card

### Characteristics

- **Resolution**: 256×192 pixels (Graphics I: 32×24 characters 8×8)
- **VRAM**: 16 KB **separate** from main RAM (communication via I/O only)
- **I/O**: `$CC00` (data) and `$CC01` (control)

### Graphics I mode (the sweet spot for tile games)

VRAM tables:
| Table | VRAM address | Size | Contents |
|-------|-------------|--------|---------|
| Pattern table | `$0000-$07FF` | 2048 B | 256 8×8 glyphs (8 bytes each) |
| Name table | `$1800-$1AFF` | 768 B | 32×24 character codes |
| Colour table | `$2000-$201F` | 32 B | **One entry per group of 8 characters** |
| Sprite attr | `$1B00-$1B7F` | 128 B | 32 sprites × 4 bytes |
| Sprite pattern | `$3800-$3FFF` | 2048 B | 256 sprite patterns |

### Initialisation sequence

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

        ; 2. Upload patterns (see below)
        ; 3. Upload the colour table
        ; 4. Clear the name table
        ; 5. Disable sprites (IMPORTANT)

vdp_regs:
        .byte $00, $C0, $06, $80, $00, $36, $07, $01
        ; R0=mode, R1=16K+display on+GfxI, R2=name@$1800, R3=colour@$2000,
        ; R4=pattern@$0000, R5=sprite attr@$1B00, R6=sprite pattern@$3800, R7=backdrop black
```

### VRAM address latch on $CC01 (two writes)

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

### Disable sprites — mandatory

Graphics I always enables sprites. By default, the sprite attribute table contains random values → garbage sprites appear. Writing `$D0` to the first Y byte (address `$1B00`) stops the sprite chain:

```asm
        LDA #$00
        STA $CC01
        LDA #$5B              ; $1B | $40
        STA $CC01
        LDA #$D0
        STA $CC00
```

### Colour-group trick for free colours

The colour table has **one fg/bg colour per group of 8 characters**. Trick: place each tile type at the first character of its group:

| Tile | Char code | Group | Colour |
|-------|-----------|--------|---------|
| Tile 0 | 0 | 0 (chars 0-7) | Colour A |
| Tile 1 | 8 | 1 (chars 8-15) | Colour B |
| Tile 2 | 16 | 2 (chars 16-23) | Colour C |
| Tile 3 | 24 | 3 (chars 24-31) | Colour D |
| etc. | | | |

Each tile then has its own colour for free. The intermediate chars (1-7, 9-15, ...) stay unused.

TMS Sokoban uses this technique for 7 coloured tiles (grey wall, red target, yellow crate, green crate-on-target, blue player, medium-green player-on-target).

### Big tiles — 4×4-cell pieces

For larger sprites, use multiple characters per piece:

- **2×2 chars** (16×16 px): 4 glyphs per piece, fits in 1 colour group
- **3×3 chars** (24×24 px): 9 glyphs, spans 2 groups — force both groups to the same colour
- **4×4 chars** (32×32 px): 16 glyphs = **exactly 2 groups** → 1 group triplet per piece type

TMS Connect 4 uses 4×4 (32×32 px per token): 28×24-cell board = 224×192 pixels, fills the screen almost entirely. Three group triplets (empty, red, yellow) × 16 glyphs = 48 chars, 7 colour entries.

### Colour byte format

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

### Delta rendering on TMS

Trivial compared to HGR: compute the name-table address `$1800 + row*32 + col`, latch it on `$CC01`, write a single character code to `$CC00`. No clearing needed, no neighbour redraw.

### Screen independence

The Apple 1 text screen and the TMS9918 window are **two independent displays**. Convention: use the text for the title, the keyboard prompt (QWERTY/AZERTY), the victory messages; use TMS for the game itself.

### Frame synchronisation — POLLING recommended (IRQ also works)

**P-LAB golden rule**: the P-LAB Apple-1 card **wires** the TMS9918 `/INT` pin to the 6502 `/IRQ` (trace verified on real hardware by Parmigiani). The Nippur72 software doesn't use it: the convention is to synchronise frames via **polling** of the status register — it's simpler, portable, and independent of the I flag. IRQ-on-VBlank works nevertheless (R1 bit 5 + `CLI` + handler at vector `$FFFE` reading `$CC01` atomically), but remains the option to avoid unless specifically needed.

This is the pattern that the Nippur72 libs target and that all POM1 games (Galaga, Sokoban, Snake, Life, Rogue, Asteroids, Connect4, etc.) use.

#### Standard idiom

```asm
        ; Wait for next VBlank — silicon-correct polling pattern
        BIT $CC01             ; drain stale F flag (clears bits 5/6/7)
@v_wait:
        BIT $CC01
        BPL @v_wait           ; bit 7 = 0 → not yet VBlank
        ; here: we're in VBlank, ~4 554 cycles of VRAM bandwidth
        ; "gate 2c" available before the next frame's active display
```

Or with the shared `WAIT_VBLANK` macro in `dev/lib/tms9918/tms9918.inc`:

```asm
.include "tms9918.inc"

        WAIT_VBLANK           ; expands to the pattern above, 7 bytes
        ; ... upload SAT, sprites, animations …
```

#### Why no IRQ

Case that **deadlocks on P-LAB silicon and POM1 by default**:

```asm
        LDA #$E0              ; R1 = display ON + IRQ enable + 16K
        STA $CC01
        LDA #$81              ; reg 1
        STA $CC01
        CLI                   ; allow /IRQ
@loop:  JMP @loop             ; wait for frame IRQ at $FFFE
```

The 6502 will wait forever because the chip's `/INT` pin isn't connected to `/IRQ`. POM1 emulates this behaviour faithfully (`TMS9918::irqAsserted()` returns `false` until `setIrqStrapped(true)` is called — and nobody calls it in the default chain).

#### Polling side effect on bits 5/6

Reading `$CC01` clears bits 5 (collision), 6 (5S overflow) **and** 7 (F flag) together. If you use these flags, read them **before** the `WAIT_VBLANK`, or snapshot them into a variable:

```asm
        LDA $CC01             ; full snapshot
        STA last_status
        AND #$80              ; isolate F
        BEQ @nope             ; no VBlank this time
        LDA last_status
        AND #$20              ; bit 5 collision
        ; …
```

For games that only read F (the majority), the 5/6 clobber has no visible consequence.

#### Special case: community FPGA strap

Some users have **modified their Apple-1 replica / P-LAB replica-1** to bridge `int_n_o` (VDP) → `irq_n` (6502) with an inverter. This mod **is not stock P-LAB**. POM1 exposes `TMS9918::setIrqStrapped(true)` to emulate it. As long as you write code intended to run on **stock P-LAB**, ignore this branch: poll, that's it. (Details in [`SILICONBUGS.md`](SILICONBUGS.md) Bug N°2.)

#### Strict silicon — VRAM writes too fast

When **Silicon Strict** is active (default outside the Multiplexing Fantasy presets), POM1 may **drop** bytes if `$CC00`/`$CC01` accesses arrive faster than the slot-table model — same behaviour as too-fast access on real silicon. See [`SILICONBUGS.md`](SILICONBUGS.md) §2 (Bug N°1), `tms9918_pad12` helpers, `tools/silicon_strict_patch.py` patcher. Toggle: Hardware menu or [`doc/CLI.md`](../doc/CLI.md) (`--silicon-strict` / `--no-silicon-strict`).

### Implementation examples

- `dev/projects/tms9918_sokoban/TMS_Sokoban.asm` — 47 levels, 8×8 tiles with 7 colours
- `dev/projects/tms9918_connect4/TMS_Connect4.asm` — 32×32 tokens on full-screen blue board
- `dev/projects/tms9918_galaga/TMS_Galaga.asm:2593-2600` — polling pattern commented in place

---

## 7. Shared patterns for games

### State grid at `$4000`

On the **Fantasy** presets (flat 64 KB RAM) or when the `$4000-$7FFF` window is not occupied by the CodeTank / Juke-Box ROM, `$4000+` can hold a grid or sizeable buffers. On the **dual-bank 8+8 KB** presets, the same area may be ROM or out of scope depending on the card — check the preset in [`README.md`](../README.md) or test on the target. For a **20×12** grid (240 bytes), a page in low user RAM or **`GRID_BASE = $4000`** (when available) is enough:

```asm
GRID_BASE = $4000

; Fast access:
        LDY cell_index              ; 0..239
        LDA GRID_BASE,Y             ; indexed absolute
```

To compute a cell's index `(row, col)`, use a lookup instead of multiplying:

```asm
row_x20:
        .byte 0, 20, 40, 60, 80, 100, 120, 140, 160, 180, 200, 220

; idx = row*20 + col:
        LDX row
        LDA row_x20,X
        CLC
        ADC col
        TAX                         ; or TAY for a lookup next
```

### Portable level format

4-byte header then ASCII:

```asm
level1:
        .byte 5, 3, 4, 7            ; w, h, row_offset, col_offset
        .byte "#####"
        .byte "#.$@#"
        .byte "#####"
```

The offset lets you centre small levels inside a larger grid (20×12 for example).

Sokoban convention: `#` wall, `.` target, `$` crate, `@` player, `*` crate on target, `+` player on target, ` ` floor.

For non-rectangular levels (e.g. Microban), pad short lines with spaces before compiling.

### QWERTY/AZERTY keyboard prompt

The `1` and `2` keys are at the same physical position on both layouts, ideal as a prompt. Store the variable keys in zero-page:

```asm
.zeropage
key_up_code:   .res 1   ; 'W' (QWERTY) or 'Z' (AZERTY)
key_left_code: .res 1   ; 'A' or 'Q'

.code
        LDA #<str_layout
        LDX #>str_layout
        JSR print_str_ax
@wait:
        JSR wait_key
        CMP #'1'
        BEQ @qwerty
        CMP #'2'
        BEQ @azerty
        JMP @wait
@qwerty:
        LDA #'W'
        STA key_up_code
        LDA #'A'
        STA key_left_code
        JMP @start
@azerty:
        LDA #'Z'
        STA key_up_code
        LDA #'Q'
        STA key_left_code
@start:
        ; ...

; In move_loop:
        CMP key_up_code
        BEQ key_up
        CMP #'S'                    ; S and D share the physical position
        BEQ key_down
        CMP key_left_code
        BEQ key_left
        CMP #'D'
        BEQ key_right
```

### Logic / rendering separation

The game logic (collision, rules, victory condition, etc.) is **100% shareable** across the three modes. Only these change:

- `init_display`: `clear_hgr` (HGR) vs `init_vdp` (TMS) vs nothing (text)
- `render_all`: loop that draws the full grid
- `draw_cell`: draws a single cell at a given grid position
- The visual data: HGR bitmap, TMS patterns, ASCII characters

**Template**:
```
main → init_display → game_loop → {
    init_level             ; common logic
    render_all             ; mode-specific
    move_loop → {
        wait_key
        execute_move       ; common logic; modifies the grid
        if moved: draw_cell(s)  ; mode-specific; redraws 1-4 tiles
        check_win          ; common logic
    }
}
```

### Delta rendering

After a move, the number of modified cells is bounded (2-4 for Sokoban: old and new player position, + possibly the pushed crate). **Only redrawing these cells** brings rendering time from ~80 ms (full frame) down to < 1 ms. Smooth gameplay guaranteed.

### Victory condition: watch out for indirect cases

Sokoban: a target is "empty" if it still contains `TILE_TARGET` (2) **or** `TILE_PLAYER_TARGET` (6, player standing on it without a crate). Checking only value 2 yields a false positive when the player walks onto the last target.

---

## 8. Zero page — reminder

The Apple 1 zero page runs from `$0000` to `$00FF`, but cc65 configs typically reserve 32 to 35 bytes for the user (`$0000-$001F` or `$0000-$0022`). The Woz monitor and BASIC use the rest.

Typical use for a game:
```asm
.zeropage
temp:           .res 1
temp2:          .res 1
ptr_lo:         .res 1  ; generic pointer 1
ptr_hi:         .res 1
src_lo:         .res 1  ; source pointer (patterns, strings)
src_hi:         .res 1
current_player: .res 1
move_count:     .res 1
winner:         .res 1
row_cnt:        .res 1
col_cnt:        .res 1
draw_row:       .res 1
draw_col:       .res 1
; ... game-specific variables
```

---

## 9. Reference implementations

The three **Sokoban** ports share the same grid, the same move logic (`execute_move`, `leave_tile`, `enter_player`, `check_win`), the same level format. Only the rendering differs:

| File | Mode | Size | Levels |
|---------|------|--------|---------|
| `dev/projects/games_sokoban/Sokoban.asm` | Text | 4054 B | 47 |
| `dev/projects/hgr_sokoban/HGR_Sokoban.asm` | HGR GEN2 | 7399 B | 72 |
| `dev/projects/tms9918_sokoban/TMS_Sokoban.asm` | TMS9918 | 4354 B | 47 |

The three **Connect 4** ports likewise:

| File | Mode | Size |
|---------|------|--------|
| `dev/projects/games_connect4/Connect4.asm` | Text | 1021 B |
| `dev/projects/hgr_connect4/HGR_Connect4.asm` | HGR GEN2 | 2003 B |
| `dev/projects/tms9918_connect4/TMS_Connect4.asm` | TMS9918 | 1230 B |

Other GEN2 programs useful as templates:
- `dev/projects/hgr_maze/HGR_Maze.asm`: maze sub-byte rendering (4-px walls)
- `dev/projects/hgr_mandelbrot/HGR_Mandelbrot.asm`: computation + pixel plotting
- `dev/projects/hgr_house/HGR_House.asm`: shape drawing

Reusable libraries (`dev/lib/`):
- `dev/lib/apple1/apple1.inc` — Wozmon + PIA equates
- `dev/lib/m6502/math.asm` — fixed-point trig, LFSR RNG, decimal printing
- `dev/lib/tms9918/{tms9918.inc,tms9918m2.asm}` — VDP equates + Mode 2 driver
- `dev/lib/hgr/{hgr_tables.inc,smiley.inc}` — HGR tables
- `dev/lib/games/sokoban/sokoban_*.inc` — shared Sokoban level data
- `dev/lib/games/chess/{chess_engine.asm,chess_text_io.asm,chess_*.inc}` — shared chess engine (text/HGR/TMS9918)

---

## 10. External resources

- **[`SILICONBUGS.md`](SILICONBUGS.md)** / **[`APPLE1DEV.md`](APPLE1DEV.md)** — TMS9918 pitfalls and POM1 deployment (prefer these files over scattered summaries).
- **Microban I (David W. Skinner, 2000)** — 155 progressive Sokoban levels, small and pedagogical.
  Raw sources: `https://github.com/martin-t/sokoban-solver/tree/master/levels/microban1/N.txt`
- **Sokoban Wiki (level format)**: http://sokobano.de/wiki/index.php?title=Level_format
- **cc65 documentation**: https://cc65.github.io/doc/
- **TMS9918 datasheet / reference**: for register and timing details
- **Apple II HGR memory layout**: Apple II documentation (GEN2 is Apple II-compatible)
- **P-LAB cards**: PDF documentation in `doc/` (Graphic Card ENG, SID, microSD, etc.)

---

## 11. Checklist before starting a new game

1. Pick the target mode(s) — text, HGR, TMS, or all three
2. Size the game grid (fits in 256 bytes? page-aligned at `$4000`?)
3. Pick the linker config according to the code budget
4. Share the pure logic (no I/O) across modes
5. Implement `draw_cell` before `render_all` — to test visually
6. Add delta rendering after validating the full rendering
7. For HGR: draw byte-aligned tiles (width multiple of 7) if possible
8. For TMS: use the colour-group trick from the start if multiple colours are needed
9. Handle the keyboard layout (prompt `1`/`2`, store the keys in ZP)
10. Test edge cases: full grid, borders, exotic victory condition

---

*Document drawn from agent memories and from the Sokoban and Connect 4 game code (3 ports each).*
