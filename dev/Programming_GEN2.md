# GEN2 HGR Programming (6502 assembly)

Uncle Bernie's **GEN2 HGR Color Graphics Card** brings Apple II-style 280×192
artifact-colour graphics to the Apple 1. The framebuffer lives at
`$2000-$3FFF` (8 KB, non-linear scanline layout), 7 pixels per byte with bit 7
acting as the NTSC group selector. Hardware reference: [`doc/GEN2_RELEASE.md`](../doc/GEN2_RELEASE.md).

**Related docs**

| Doc | Use |
|-----|-----|
| [`Programming_Apple1_ASM.md`](Programming_Apple1_ASM.md) | Base 6502 / cc65 toolchain, text mode, common patterns |
| [`Programming_GEN2C.md`](Programming_GEN2C.md) | C (cc65) version of this guide |
| [`doc/GEN2_RELEASE.md`](../doc/GEN2_RELEASE.md) | Card hardware, soft-switches, beam-race renderer |
| [`APPLE1DEV.md`](APPLE1DEV.md) | Agent playbook (presets, deployment, CLI) |

---

## 1. Characteristics

- **Resolution**: 280×192 pixels
- **Framebuffer**: `$2000-$3FFF` (8 KB)
- **Scanline layout**: non-linear Apple II (3 groups of 64 lines, interleaved by 8)
- **Format**: 7 pixels per byte, **bit 7 = NTSC group selector** (not a pixel!)

## 2. NTSC colours by artefact

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

## 3. Getting clean white

**Rule**: each lit pixel must have a lit neighbour next to it, in the same byte or across a boundary. A 1-pixel-thick vertical line will always render as violet/green/blue/orange depending on position. **A 2-pixel-thick line will render white**.

## 4. Lookup table hgr_tables.inc

The file `dev/lib/hgr/hgr_tables.inc` (included via `-I ../../lib/hgr` in the project Makefiles) provides (896 bytes + 30 of code):
- `hgr_lo[192]`, `hgr_hi[192]`: address of each scanline (handles the Apple II interleave) — via `hgr_scanline.inc`
- `hgr_col[256]`, `hgr_mask[256]`: for a pixel at screenX x, gives the byte column and the bitmask — via `hgr_plot_tables.inc`
- `plot_pixel`: ~45-cycle routine that plots a pixel at (cur_x, cur_y) — via `hgr_plot.asm`
- `clear_hgr`: zeros `$2000-$3FFF` — via `hgr_clear.asm`

It is now an *umbrella bundle* that `.include`s these four modules; to take only part of it, include the modules directly. `umul8` (8×8 → 16-bit multiplication) **is no longer in this bundle**: it lives in `dev/lib/m6502/multiply.asm`, to include separately if needed.

**Pitfall**: even if you don't use `plot_pixel`, its ZP variables (`cur_x`, `cur_y`, `ptr_lo`, `ptr_hi`) must be declared, otherwise the assembler fails.

## 5. Byte-aligned tiles

For simplicity, use tiles whose width is a multiple of 7: **7, 14, 21, 28 pixels**. Each row of the tile writes in 1, 2, 3 or 4 whole bytes.

Example 14×16: 2 bytes × 16 scanlines = 32 bytes per tile.

## 6. Non-aligned tiles — sub-byte rendering

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

## 7. Scanline stride +$0400 trick

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

## 8. Initialisation and clearing

```asm
.include "hgr_tables.inc"  ; at the end of the file

; at the start of the program
JSR clear_hgr              ; zero the framebuffer
```

## 9. Implementation example

- `dev/projects/hgr_maze/HGR_Maze.asm` — sub-byte rendering maze (4-pixel walls)
- `dev/projects/hgr_sokoban/HGR_Sokoban.asm` — full 72-level game, 14×16 tiles, delta rendering
- `dev/projects/hgr_mandelbrot/HGR_Mandelbrot.asm` — computation + pixel plotting
- `dev/projects/hgr_house/HGR_House.asm` — shape drawing
