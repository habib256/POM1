# lib/tms9918 — P-LAB TMS9918 Graphic Card driver

*[← dev/lib index](../README.md)*

*Tutorials: [step-by-step TMS9918 assembly guide](../../../sketchs/doc/Programming_TMS9918.md) · [TMS9918 sprite init](../../../sketchs/doc/TMS9918-SPRITE_INIT.md) · [TMS9918 sprite best practices](../../../sketchs/doc/TMS9918-SPRITE_BEST_PRACTICES.md).*

Equates + drivers for the P-LAB Apple-1 TMS9918 card. Two modes shipped,
mutually exclusive (you pick one per project — links the matching `.o`).

## Files

- **`tms9918.inc`** — `VDP_DATA = $CC00`, `VDP_CTRL = $CC01` + the
  silicon-strict `WRT_DATA_REG` / `WRT_DATA_VAL` macros (see below).
- **`tms9918m1.asm`** — Mode 1 (Graphics I, 32×24 cells of 8×8 px) driver.
  Mutualises the init + upload + name-table writes that 4+ games
  (TMS_Sokoban, TMS_Connect4, TMS_Snake, TMS_Galaga) currently re-derive.
- **`tms9918m2.asm`** — Mode 2 (bitmap, 256×192) driver. Used by TMS_Logo.

### Fonts (shared with the GEN2 HGR card — Axe 2 of the lib factoring)

The Beautiful Boot 8×8 font now comes from ONE master, `dev/lib/gen2/bbfont_cp437.inc`,
emitted per format by `tools/build_shared_font.py` (`--check` to verify no drift):

- **`bbfont_tms.inc`** — *generated.* Full ASCII (0x20-0x7F, 96 glyphs) TMS9918
  pattern table, **bit 7 = leftmost pixel** (the bit-reverse of the HGR
  `gen2_bbfont.inc`). Inline fragment with a `tms_bbfont:` label — `.include` it
  at the pattern-table location and index `(ch-0x20)*8`.
- **`font_hud8x8.inc`** — *generated.* The 37-glyph HUD subset (char codes
  56..92) Snake/Sokoban draw for score/title text, now the BB font (was a
  separate hand-tuned font). Same on-ROM order/codes, so the games are
  unchanged; rebuild them to refresh the shipped artifacts.
- **`font_quale.asm`** — Quale's display font (independent, not from the master).
- **`c64font_tms.inc`** — *generated* by `tools/build_c64font_tms.py` (`--check`
  wired into `make -C dev/lib check`) from `tms9918c/c64font.c` (the C runtime's
  C64-style font master): the same 96 glyphs as a ca65 inline fragment, for asm
  programs that want the C-track look. Consumer: `sketchs/tms9918/applesoft_tms9918`
  (`tmsgfx.inc`).

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
1 = XOR). See `sketchs/tms9918/tool_logo/TMS_Logo_16k.asm` for the
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

## VBlank synchronisation macro (`WAIT_VBLANK`)

**The P-LAB board wires /INT → /IRQ** (trace verified on real hardware by
Parmigiani), but the Nippur72 software does not use it. Recommendation:
**synchronise frames by polling** rather than by IRQ — it is simpler and
independent of the I flag. The frame IRQ is available (R1 bit 5 + `CLI` +
a handler at vector $FFFE), but every POM1 game (Galaga, Sokoban, Snake,
Life, Rogue, …) follows the polling pattern.

```asm
.include "tms9918.inc"

        ; … main game loop …
@frame:
        WAIT_VBLANK            ; spin on bit 7 of $CC01 until F=1
        JSR render_sprites     ; ~4,554c of VRAM bandwidth ("gate 2c")
        JSR update_logic       ; …then logic while the beam scans back down
        JMP @frame
```

`WAIT_VBLANK` expands to 7 bytes:

```asm
        BIT VDP_CTRL           ; drain stale F (clears bits 5/6/7)
@vbl_wait:
        BIT VDP_CTRL
        BPL @vbl_wait
```

Side effect: reading `$CC01` also clears bits 5 (collision) and 6
(5S overflow). If your code depends on these flags, read them **before**
calling `WAIT_VBLANK` (or snapshot the status register into a variable).
For games polling only F, clobbering 5/6 is harmless.

The frame IRQ is strapped by default (`irqStrapped=true`); use
`TMS9918::setIrqStrapped(false)` to model a hypothetical unstrapped card.
For code targeting stock P-LAB, polling is the simplest path — no
configuration needed. The /INT → /IRQ wiring quirk (P-LAB straps it, the
Nippur72 software ignores it) is why polling — not the frame IRQ — is the
recommended sync model.

## Mid-frame raster trap — 5th-sprite-overflow primitive (`tms9918_5strigger.asm`)

V-blank polling gives **one** sync point per frame (the last line / end of
active display). To schedule an event **mid-frame** — palette split,
name-table swap, extra pattern uploads during the bottom half — you can
hijack the status register's bit 6 (5S = "5th sprite overflow") by placing
5 invisible sprites on the line where you want to trap the beam.

This is exactly Daniel Vik's technique in the MSX demo *Waves*, adapted to
pure polling — the TMS9918 has no scanline interrupt (only the /INT frame),
so the mid-frame must be polled regardless of how /INT is wired (the IRQ
strap buys nothing here).

### When to use the mid-frame raster trap

V-blank polling hands you one event per frame, at end-of-display. Reach
for the 5S trap when you need a *second*, programmable trip point partway
down the screen:

- **Palette / colour split** — rewrite the colour table (Mode 1) so the
  top and bottom bands of the screen use different colours from one frame.
- **Name-table swap** — flip R2 (name-table base) at the trap line for an
  instant top/bottom status-bar-over-playfield split with no extra VRAM.
- **Mid-frame pattern upload** — push fresh patterns to VRAM during the
  active-display half-frame instead of waiting for the next V-blank window,
  doubling the per-frame VRAM bandwidth budget for a costly update.

Skip it for anything that only needs once-per-frame timing (regular
animation, input, logic) — plain `WAIT_VBLANK` is cheaper and never burns
CPU spinning toward the trap line.

### Public symbols

| Symbol            | Description                                              |
|-------------------|----------------------------------------------------------|
| `arm_5s_trigger`  | A = scan line (1..192) → writes 5 invisible sprites on that line. SAT[0..4] consumed, SAT[5].Y = $D0 terminator. |
| `wait_5s_trigger` | spin `BIT $CC01 / BVC` until 5S = 1. Cost ~6c/iteration. Preserves A. |
| `WAIT_5S`         | inline macro equivalent to `wait_5s_trigger` (4 bytes, no `JSR`). In `tms9918.inc`. |

### Canonical usage

```asm
        .import arm_5s_trigger, wait_5s_trigger
        .import disable_sprites          ; tms9918m1.asm — to disarm
.include "tms9918.inc"

@frame:
        WAIT_VBLANK                       ; clears flags 5/6/7
        LDA #96                           ; mid-screen scan line
        JSR arm_5s_trigger                ; 5 invisible sprites at Y=95
        JSR upload_top_palette            ; colours for the top half
        WAIT_5S                           ; (or JSR wait_5s_trigger)
        JSR upload_bottom_palette         ; swap mid-frame
        JSR disable_sprites               ; disarm before the next frame
        JMP @frame
```

### Caveats — read once

1. **Any read of `$CC01` clears bits 5/6/7 together**. Do NOT interleave a
   `WAIT_VBLANK` between `arm_5s_trigger` and `WAIT_5S` — the V-blank
   poll would consume bit 6 along the way.
2. The 5S flag **latches on the first line** where the 5th sprite is
   found. Later lines that still have 5+ sprites do not re-raise it. To
   trap several lines in the same frame, disarm + re-arm between the two
   waits.
3. The chip counts on the Y coordinate only. The X position and the
   pattern do not enter the counter — `arm_5s_trigger` uses early-clock
   + colour 0 (transparent) to make the sprites invisible regardless of
   the pattern table contents.
4. If the program already uses sprites for gameplay, `arm_5s_trigger`
   overwrites SAT[0..5]. Either save/restore around the call, or reserve
   the first 5 SAT slots as "trigger sprites" held at Y=$D0 by default,
   bumped to the real Y only when arming.
5. Disarm after use: `disable_sprites` (lib mode 1 or 2) writes Y=$D0 to
   SAT[0]; the chip stops all SAT scanning from that entry on. Without
   disarming, 5S will re-trigger every frame.

### Cost

- `arm_5s_trigger`: ~25 VDP stores with pad18 between each ≈ 650 cycles.
  Done 1× per frame, negligible.
- `WAIT_5S`: ~6c × (lines remaining until the trigger). Worst case
  ~12,000c when triggering shortly after a V-blank (line 8). That is a
  dead loss of CPU bandwidth during the wait — use splits **low** in the
  frame when possible.

## Silicon-strict timing macros (`WRT_DATA_REG`, `WRT_DATA_VAL`)

When POM1's Hardware menu → **Silicon Strict** is ON (default for every
preset except the Multiplexing Fantasy ones), the TMS9918 models the CPU
access windows with the openMSX per-mode slot tables: during active
Graphics I/II display the worst CPU-slot gap is ≈ 8 cycles, and writes
that arrive faster than the chip can drain them are dropped. Real
TMS9918A silicon is stricter still — measurements on Claudio
Parmigiani's Replica-1 put the drop floor near **~16 cycles** between
stores in active Mode I/II. Two helper macros in `tms9918.inc` add the
right padding between consecutive `STA VDP_DATA`:

```asm
; A already loaded with the byte to push (typical loop body).
WRT_DATA_REG     ; expands to: STA VDP_DATA / JSR tms9918_pad18

; Or load-immediate then push.
WRT_DATA_VAL #$AA  ; expands to: LDA #$AA / STA VDP_DATA / JSR tms9918_pad18
```

Both append an 18-cycle pad (a 4 + 18 = 22c STA-to-STA gap between
back-to-back `STA VDP_DATA`), comfortably above the ~16c real-silicon
floor. Callers must `.import tms9918_pad18` (`tms9918_pad12` survives
only as a legacy alias resolving to pad18). Use them in new code; for an
existing project, retrofitting is mechanical — insert a `JSR
tms9918_pad18` (or the equivalent NOP run) between every back-to-back
VDP store that can fire during active display. Reference implementation:
`sketchs/tms9918/game_galaga/TMS_Galaga.asm` carries ~219 NOPs across its
sprite / HUD / title / help routines.

The macros only matter when the program writes back-to-back during
*active display* (R1 bit 6 = 1). VRAM uploads done with display blanked
(R1 bit 6 = 0) get the relaxed 2-cycle window — `init_vdp_g1` /
`init_vdp_g2` could opt to blank around uploads to skip the macros, but
none currently do.

### Transmission zones: when the pad is (and isn't) needed

Think of VRAM access in two zones (full model + measured tables:
[`doc/TMS9918_TRANSFER_WINDOWS.md`](../../../doc/TMS9918_TRANSFER_WINDOWS.md)):

- **Free zones — display blanked (R1 bit 6 = 0) or the VBlank burst**:
  the chip serves the dense "ScreenOff" access slots (~2c apart), so
  bursts need **no per-byte pad**. This is why `init_vdp_g1` /
  `init_vdp_g2` blank the display around their 16 KB wipes and table
  fills (~3× faster than a padded loop). Bracket your own big uploads
  with `vdp_display_off` / `vdp_display_on` (helpers in
  `tms9918_pad.asm`) to run them in a free zone.
- **Active display (R1 bit 6 = 1)**: the pad18 contract applies to
  every back-to-back VDP data store — use `WRT_DATA_REG` /
  `WRT_DATA_VAL`, or an explicit `JSR tms9918_pad18`.

**Real-silicon caveat — do not shave the pad in active display.** The
drop floor measured on real TMS9918A hardware (Claudio Parmigiani's
Replica-1) is roughly **16 cycles** between stores in active Mode I/II,
regardless of whether sprites are enabled — the silicon does not
discriminate sprites-on/off, and it is much stricter than POM1's
openMSX slot tables (worst active gap ≈ 8c). An unpadded natural inner
loop (`STA VDP_DATA / INY / BNE` ≈ 9-10c STA-to-STA) can therefore pass
in POM1 yet drop bytes on the real card. If a hot loop genuinely cannot
afford the pad, move the burst into a free zone (blank the display, or
confine it to VBlank) instead of dropping the pad.

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

## Source of truth (asm ↔ C)

Shared with the C runtime in [`../tms9918c/`](../tms9918c/):

- **VDP port addresses** — canonical in **`tms9918.inc`** (`VDP_DATA` = `$CC00`,
  `VDP_CTRL` = `$CC01`); `tms9918c/tms9918.c` mirrors them (`VDP_DATA`,
  `VDP_REG`). Edit the `.inc` first. Pinned by `tools/check_lib_equates.py`.
- **Beautiful Boot font** (`bbfont_tms.inc`, `font_hud8x8.inc`) — *generated*
  from the GEN2 master `../gen2/bbfont_cp437.inc` by `tools/build_shared_font.py`
  (bit-reversed for the TMS pattern table). Never hand-edit; pinned by
  `build_shared_font.py --check`.

Both checks run under `make -C dev/lib check`.
