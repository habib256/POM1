# lib/tms9918 — P-LAB TMS9918 Graphic Card driver

Equates + drivers for the P-LAB Apple-1 TMS9918 card. Two modes shipped,
mutually exclusive (you pick one per project — links the matching `.o`).

## Files

- **`tms9918.inc`** — `VDP_DATA = $CC00`, `VDP_CTRL = $CC01` + the
  silicon-strict `WRT_DATA_REG` / `WRT_DATA_VAL` macros (see below).
- **`tms9918m1.asm`** — Mode 1 (Graphics I, 32×24 cells of 8×8 px) driver.
  Mutualises the init + upload + name-table writes that 4+ games
  (TMS_Sokoban, TMS_Connect4, TMS_Snake, TMS_Galaga) currently re-derive.
- **`tms9918m2.asm`** — Mode 2 (bitmap, 256×192) driver. Used by TMS_Logo.

## Mode 1 (`tms9918m1.asm`) — public symbols

| Symbol            | Description                                              |
|-------------------|----------------------------------------------------------|
| `init_vdp_g1`     | 8 registers + tail-call disable_sprites                  |
| `disable_sprites` | Y=`$D0` to sprite #0 → chip stops scanning sprites       |
| `clear_name_table`| zero the 768-byte name table at `$1800`                  |
| `vdp_set_write`   | prep VRAM auto-increment write at `vdp_lo:hi`            |
| `vdp_set_read`    | prep VRAM read at `vdp_lo:hi`                            |
| `vdp_upload_a`    | A = count, copy from `(vdp_src_lo:hi)` to `VDP_DATA`     |
| `name_at_rc`      | `(vdp_row, vdp_col)` → `vdp_lo:hi` (no write yet)        |
| `print_at_rc`     | A = char, write at `(vdp_row, vdp_col)` — full sequence  |

### Owned ZP slots (6 bytes)

`vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col`

Distinct from Mode 2's `pix_*` / `ln_*` slots so a project that
hypothetically links both `.o` files would not collide. In practice m1
and m2 are mutually exclusive (different display modes); a project
picks one.

### Caller imports

`tmp` (1 ZP byte) — used inside `name_at_rc` and `vdp_upload_a`. Comes
free if you `.include "lib/apple1/zp.inc"` once.

### Mode 1 memory map (fixed by the register table)

| VRAM range | Purpose | Notes |
|---|---|---|
| `$0000-$07FF` | Pattern table | 256 chars × 8 bytes |
| `$1800-$1AFF` | Name table | 32 × 24 = 768 bytes |
| `$1B00-$1B7F` | Sprite attribute | 32 entries × 4 bytes |
| `$2000-$201F` | Colour table | **One byte per group of 8 chars** — design tile families starting at char 0/8/16/24/… |
| `$3800-$3FFF` | Sprite pattern | unused if disable_sprites |

## Mode 2 (`tms9918m2.asm`) — public symbols

| Symbol            | Description                                              |
|-------------------|----------------------------------------------------------|
| `init_vdp_g2`     | 8 registers + linear name table + colour table           |
| `clear_bitmap`    | zero the 6144 B pattern table at `$0000`                 |
| `disable_sprites` | Y=`$D0` to sprite #0 → chip stops scanning sprites       |
| `vdp_set_write`   | prep VRAM auto-increment write at `pix_addr_lo:hi`       |
| `vdp_set_read`    | prep VRAM read at `pix_addr_lo:hi`                       |
| `calc_pix_addr`   | `(pix_x, pix_y)` → `pix_addr_lo:hi` (no mask)            |
| `plot_set`        | plot at `(pix_x, pix_y)`, OR or XOR per `plot_mode`      |
| `line_xy`         | Bresenham `(ln_x0,y0)→(ln_x1,y1)`, 16-bit signed err     |

### Owned ZP slots (16 bytes)

`pix_x, pix_y, pix_addr_lo, pix_addr_hi, pix_mask, pix_byte, ln_x0, ln_y0,
ln_x1, ln_y1, ln_dx, ln_dy, ln_sx, ln_sy, ln_err, ln_err_hi`

### Caller imports

`tmp`, `tmp2` (1 ZP byte each) and `plot_mode` (1 BSS byte: 0 = OR,
1 = XOR). See `dev/projects/tms9918_logo/TMS_Logo.asm` for the
caller-side declaration template.

## Use

Mode 1 (typical game):

```asm
.include "apple1.inc"
.include "zp.inc"
.include "tms9918.inc"

.import init_vdp_g1, clear_name_table, vdp_upload_a
.import vdp_set_write, print_at_rc
.importzp vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col

main:
        JSR init_vdp_g1
        JSR clear_name_table

        ; Upload a custom pattern at char 8 → VRAM $0040
        LDA #$00 / STA vdp_lo
        LDA #$40 / STA vdp_hi
        JSR vdp_set_write
        LDA #<my_pattern / STA vdp_src_lo
        LDA #>my_pattern / STA vdp_src_hi
        LDA #128            ; 16 chars × 8 bytes
        JSR vdp_upload_a

        ; Print char 8 at row 12, col 14
        LDA #12 / STA vdp_row
        LDA #14 / STA vdp_col
        LDA #8
        JSR print_at_rc
```

Mode 2: `.include "apple1.inc"` + `.include "tms9918.inc"` + `.include
"tms9918m2.asm"`, callers also export `tmp/tmp2/plot_mode` (see
`tms9918_logo/TMS_Logo.asm:78-79` for the canonical declaration).

In your project Makefile (Mode 1 example, multi-object link):

    LIB := -I ../../lib/apple1 -I ../../lib/tms9918
    OBJS := MyGame.o tms9918m1.o
    tms9918m1.o: ../../lib/tms9918/tms9918m1.asm
        ca65 $(LIB) $@:= -o $@ $<
    $(OUT)/MyGame.bin: $(OBJS)
        ld65 -C my_game.cfg $^ -o $@

## Silicon-strict timing macros (`WRT_DATA_REG`, `WRT_DATA_VAL`)

When POM1's Hardware menu → **Silicon Strict** is ON (default for every
preset except the Multiplexing Fantasy ones), the TMS9918 enforces real-
silicon access windows: VRAM writes happening less than ~8 cycles apart
in Mode I + sprites are dropped. Two helper macros in `tms9918.inc` add
the right NOP padding between consecutive `STA VDP_DATA`:

```asm
; A already loaded with the byte to push (typical loop body).
WRT_DATA_REG     ; expands to: STA VDP_DATA / NOP / NOP

; Or load-immediate then push.
WRT_DATA_VAL #$AA  ; expands to: LDA #$AA / STA VDP_DATA / NOP
```

Both leave ≥ 8 cycles between the previous `STA VDP_DATA` and the next,
which matches the worst-case window in Graphic I + sprites. Use them in
new code; for an existing project, the patching playbook
([`dev/SILICONBUGS.md`](../../SILICONBUGS.md) §17 Annexe E) covers
mechanical NOP insertion across all back-to-back VDP stores. Reference
implementation: `dev/projects/tms9918_galaga/TMS_Galaga.asm` carries
~219 NOPs across its sprite / HUD / title / help routines.

The macros only matter when the program writes back-to-back during
*active display* (R1 bit 6 = 1). VRAM uploads done with display blanked
(R1 bit 6 = 0) get the relaxed 2-cycle window — `init_vdp_g1` /
`init_vdp_g2` could opt to blank around uploads to skip the macros, but
none currently do.

## Migration path for existing Mode-1 games

`TMS_Sokoban`, `TMS_Connect4`, `TMS_Snake`, `TMS_Galaga` each carry
their own copy of `init_vdp` (~70 lines), `vdp_set_write` (~6 lines),
`upload_pattern` (~12 lines). One-by-one migration:

1. Add `.include "lib/apple1/zp.inc"` to fold `tmp` into the project.
2. Replace local `init_vdp` body with `.import init_vdp_g1` + `JSR
   init_vdp_g1`.
3. Replace local `vdp_set_write` / `upload_pattern` calls with the
   library equivalents.
4. Switch the Makefile to multi-object link including `tms9918m1.o`.
5. Rebuild, byte-compare against the previous `.bin` to confirm
   semantic equivalence (the .bin will likely shrink since the lib
   factors away duplicated code).

Each migration drops ~80 lines of boilerplate from a project.
