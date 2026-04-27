# lib/tms9918 — P-LAB TMS9918 Graphic Card driver

Equates + Mode-2 (bitmap, 256×192) driver for the P-LAB Apple-1 TMS9918 card.

## Files

- **`tms9918.inc`** — `VDP_DATA = $CC00`, `VDP_CTRL = $CC01`. Pure equates.
- **`tms9918m2.asm`** — Mode-2 driver (init, plot, line, sprites off).

## Public routines (tms9918m2.asm)

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

The caller must define `tmp`, `tmp2` (1 ZP byte each) and `plot_mode` (1 BSS
byte: 0 = OR, 1 = XOR). See `dev/projects/tms9918_logo/TMS_Logo.asm`.

## Use

    .include "apple1.inc"
    .include "tms9918.inc"
    .include "tms9918m2.asm"

In your project Makefile:

    LIB := -I ../../lib/apple1 -I ../../lib/tms9918
