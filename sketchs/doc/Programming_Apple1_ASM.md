# Apple 1 6502 Assembly Programming

**Practical guide — text mode, HGR (GEN2 Color Graphics Card), TMS9918 (P-LAB Graphic Card)**  
VERHILLE Arnaud — 2026

This document summarises everything you need to program the Apple 1 emulated by POM1, in the three available video modes. It is drawn from the concrete experience of porting **Sokoban** (47-72 levels per mode) and **Connect 4** (all three modes).

**Related documents**

| File | Contents |
|---------|---------|
| [`APPLE1DEV.md`](APPLE1DEV.md) | Agent playbook: presets, deployment, CLI examples, short pitfalls |
| [`Programming_TMS9918.md`](Programming_TMS9918.md) | Real TMS9918 vs POM1, strict VRAM timing, sprites |
| [`CLI.md`](../../doc/CLI.md) | Exhaustive list of flags (`--preset`, `--silicon-strict`, …) |
| [`TODO6502.md`](../../dev/TODO6502.md) | Project backlog (`sketchs/` + `dev/projects/`) |

**Contents (titles §1–§10)** — toolchain · common hardware · 6502 pitfalls · text mode · graphics cards (stub) · game patterns · zero page · reference implementations · external resources · checklist. Card-specific guides: [`Programming_GEN2.md`](Programming_GEN2.md) (HGR) and [`Programming_TMS9918.md`](Programming_TMS9918.md).

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
> a `HELLO WORLD` skeleton per target. Copy `sketchs/apple1/_template/` for a minimal
> asm or text-mode-C starting point (and `sketchs/gen2/_template_gen2c/`,
> `sketchs/tms9918/_template_tms9918c/` for the cc65 graphics-card flavours). In C →
> [`Programming_Apple1_C.md`](Programming_Apple1_C.md).

### Standard commands

Sources live under `sketchs/<profile>/<name>/` (each has its own `Makefile`). Manual workflow if needed:

```bash
# Assemble
ca65 -o build/MyGame.o sketchs/<profile>/<name>/MyGame.asm

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

1. Pick a preset with the desired card — **Machine Presets** table in [`README.md`](../../README.md) (e.g. TMS9918 + CodeTank → **preset 9**, GEN2 HGR → **preset 11**).
2. (Or auto-enable: place the deliverable under `software/Graphic HGR/`, `software/Graphic TMS9918/`, etc. — see [`APPLE1DEV.md`](APPLE1DEV.md) §8.)
3. **File > Load Memory** → select the `.txt`
4. In the Woz Monitor, type `280R` (or the linker's start address)

### Available linker configs

| Config | CODE range | Size | Typical use |
|--------|-----------|--------|---------------|
| `dev/cc65/apple1_4k.cfg` | `$0280-$127F` | 4 096 B | Text or TMS9918 games (VRAM off the bus) — default config |
| `dev/cc65/apple1_gen2.cfg` | `$E000-$EFFF` | 4 096 B | HGR games: code in the high bank (launch with `E000R`), the framebuffer `$2000-$3FFF` stays reserved (direct writes). Programs > 4 KB: dedicated split-bank cfg (cf. `projects/gen2/game_sokoban/apple1_sok_hgr.cfg`) |
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

`sketchs/apple1/game_sokoban/Sokoban.asm` — 20×12 ASCII grid, full redraw every move, ~4 KB binary with 47 levels.

---

## 5. Graphics cards — separate guides

**HGR colour (GEN2 card)** is covered in [`Programming_GEN2.md`](Programming_GEN2.md).

**TMS9918 (P-LAB Graphic Card)** is covered in [`Programming_TMS9918.md`](Programming_TMS9918.md). Before optimising VRAM loops, read its Part 5 (timing and synchronisation) — Bugs N°1, N°2, N°9 in particular.

---

## 6. Shared patterns for games

### State grid at `$4000`

On the **Fantasy** presets (flat 64 KB RAM) or when the `$4000-$7FFF` window is not occupied by the CodeTank / Juke-Box ROM, `$4000+` can hold a grid or sizeable buffers. On the **dual-bank 8+8 KB** presets, the same area may be ROM or out of scope depending on the card — check the preset in [`README.md`](../../README.md) or test on the target. For a **20×12** grid (240 bytes), a page in low user RAM or **`GRID_BASE = $4000`** (when available) is enough:

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

### Movement keys: fixed IJKL

Use `I`/`J`/`K`/`L` (up/left/down/right) for movement. These keys sit at the
same physical position on QWERTY and AZERTY keyboards, so no layout prompt is
needed — every game in this tree dropped its old `1=QWERTY / 2=AZERTY`
selector in juillet 2026. Compare against literal constants:

```asm
.code
        JSR wait_key
        CMP #'I'
        BEQ @up
        CMP #'K'
        BEQ @down
        CMP #'J'
        BEQ @left
        CMP #'L'
        BEQ @right
        ; ...

; If the KBD value keeps bit 7 set, compare #('I' | $80) instead.
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

## 7. Zero page — reminder

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

## 8. Reference implementations

The three **Sokoban** ports share the same grid, the same move logic (`execute_move`, `leave_tile`, `enter_player`, `check_win`), the same level format. Only the rendering differs:

| File | Mode | Size | Levels |
|---------|------|--------|---------|
| `sketchs/apple1/game_sokoban/Sokoban.asm` | Text | 4054 B | 47 |
| `sketchs/gen2/game_sokoban/HGR_Sokoban.asm` | HGR GEN2 | 7399 B | 72 |
| `sketchs/tms9918/game_sokoban/TMS_Sokoban.asm` | TMS9918 | 4354 B | 47 |

**Connect 4** ships as a text-only port today (HGR / TMS9918 variants existed historically but are no longer in-tree — only `sketchs/apple1/game_connect4/Connect4.asm`, ~1 KB, currently ships).

Other GEN2 programs useful as templates:
- `sketchs/gen2/game_maze/HGR_Maze.asm`: maze sub-byte rendering (4-px walls)
- `sketchs/gen2/demo_mandelbrot/HGR_Mandelbrot.asm`: computation + pixel plotting
- `sketchs/gen2/demo_house/HGR_House.asm`: shape drawing

Reusable libraries (`dev/lib/`):
- `dev/lib/apple1/apple1.inc` — Wozmon + PIA equates
- `dev/lib/m6502/math.asm` — fixed-point trig, LFSR RNG, decimal printing
- `dev/lib/tms9918/{tms9918.inc,tms9918m2.asm}` — VDP equates + Mode 2 driver
- `dev/lib/gen2/{hgr_tables.inc,smiley.inc}` — HGR tables
- `dev/lib/games/sokoban/sokoban_*.inc` — shared Sokoban level data
- `dev/lib/games/chess/{chess_engine.asm,chess_text_io.asm,chess_*.inc}` — renderer-agnostic chess engine; ships a text variant only in-tree (the HGR / TMS9918 front-ends are historical, not in-tree)

---

## 9. External resources

- **[`Programming_TMS9918.md`](Programming_TMS9918.md)** / **[`APPLE1DEV.md`](APPLE1DEV.md)** — TMS9918 pitfalls and POM1 deployment (prefer these files over scattered summaries).
- **Microban I (David W. Skinner, 2000)** — 155 progressive Sokoban levels, small and pedagogical.
  Raw sources: `https://github.com/martin-t/sokoban-solver/tree/master/levels/microban1/N.txt`
- **Sokoban Wiki (level format)**: http://sokobano.de/wiki/index.php?title=Level_format
- **cc65 documentation**: https://cc65.github.io/doc/
- **TMS9918 datasheet / reference**: for register and timing details
- **Apple II HGR memory layout**: Apple II documentation (GEN2 is Apple II-compatible)
- **P-LAB cards**: PDF documentation in `doc/` (Graphic Card ENG, SID, microSD, etc.)

---

## 10. Checklist before starting a new game

1. Pick the target mode(s) — text, HGR, TMS, or all three
2. Size the game grid (fits in 256 bytes? page-aligned at `$4000`?)
3. Pick the linker config according to the code budget
4. Share the pure logic (no I/O) across modes
5. Implement `draw_cell` before `render_all` — to test visually
6. Add delta rendering after validating the full rendering
7. For HGR: draw byte-aligned tiles (width multiple of 7) if possible
8. For TMS: use the colour-group trick from the start if multiple colours are needed
9. Use the fixed IJKL movement keys (same physical keys on QWERTY and AZERTY)
10. Test edge cases: full grid, borders, exotic victory condition

---

*Document drawn from agent memories and from the Sokoban and Connect 4 game code (3 ports each).*
