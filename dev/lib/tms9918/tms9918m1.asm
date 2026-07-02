; ============================================================================
; tms9918m1.asm -- TMS9918 Mode 1 (Graphics I, 32x24 cells of 8x8 px) driver
;                  for Apple-1 + P-LAB Graphic Card. Mutualises the init +
;                  upload + name-table writes that 4+ games (TMS_Sokoban,
;                  TMS_Connect4, TMS_Snake, TMS_Galaga) currently re-derive.
;
; Public symbols:
;   init_vdp_g1     -- 8 registers + full 16 KB VRAM wipe + disable_sprites
;                      (self-sufficient: no power-on garbage survives init)
;   wipe_all_vram   -- zero all 16 KB $0000-$3FFF (display MUST be blanked)
;   disable_sprites -- write Y=$D0 to sprite #0 attribute (chip stops scan)
;   clear_name_table -- fill 768 bytes at $1800 with character 0
;   vdp_set_write   -- prep VRAM auto-increment write at vdp_lo:hi
;   vdp_set_read    -- prep VRAM read at vdp_lo:hi
;   vdp_upload_a    -- copy A bytes from (vdp_src_lo:hi) to VDP_DATA
;                      (caller must vdp_set_write first)
;   name_at_rc      -- load vdp_lo:hi with name-table addr for (vdp_row,
;                      vdp_col) WITHOUT setting up the write (lets caller
;                      stage multiple writes via vdp_set_write later)
;   print_at_rc     -- write char A at (vdp_row, vdp_col) — full sequence
;
; Owns ZP slots: vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col.
;                (6 bytes total — distinct from tms9918m2.asm's pix_* slots
;                 so a project that ever links both .o would not collide.
;                 In practice m1 and m2 are mutually exclusive: a project
;                 picks ONE TMS9918 mode and links the matching driver.)
;
; Imports (caller must define):
;   tmp                 -- 1 ZP scratch byte (used inside name_at_rc).
;
; Memory map (Mode 1, fixed by the register table below):
;   $0000-$07FF  Pattern table (256 chars * 8 bytes)
;   $1800-$1AFF  Name table (32 cols * 24 rows = 768 bytes)
;   $1B00-$1B7F  Sprite attribute table (32 entries * 4 bytes)
;   $2000-$201F  Colour table (32 entries — ONE colour byte per group of
;                8 chars; design tiles around char-id 0/8/16/24/...)
;   $3800-$3FFF  Sprite pattern table
; ============================================================================

        .import tms9918_pad18  ; silicon-strict pad18-v4 (helper from tms9918_pad.asm)
.include "apple1.inc"
.include "tms9918.inc"  ; provides WRT_DATA_REG / WRT_DATA_VAL macros (pad18)

; --- exported ZP slots -----------------------------------------------------
.exportzp vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col

; --- imports ---------------------------------------------------------------
.importzp tmp

; --- exported routines -----------------------------------------------------
.export init_vdp_g1, disable_sprites, clear_name_table, wipe_all_vram
.export vdp_set_write, vdp_set_read, vdp_upload_a
.export name_at_rc, print_at_rc

; --- ZP layout -------------------------------------------------------------
.segment "ZEROPAGE"
vdp_lo:         .res 1
vdp_hi:         .res 1
vdp_src_lo:     .res 1
vdp_src_hi:     .res 1
vdp_row:        .res 1
vdp_col:        .res 1

; ============================================================================
.segment "CODE"
; ============================================================================

; ----------------------------------------------------------------------------
; init_vdp_g1: write 8 Mode-1 registers, wipe all 16 KB of VRAM, disable
;   sprites, then re-arm R1. Self-sufficient — a caller that uploads nothing
;   still gets a clean, garbage-free chip (the wipe + SAT terminator run while
;   the display is blanked, so the burst rides the dense ScreenOff slots).
;   pad18 calls bridge the intra-pair (value→cmd) and inter-iter
;   (cmd→next-value via BNE @rg loop-back) writes. Even if the caller
;   leaves R1 with display ON (Rogue boot after a game-switch from
;   LOGO/Galaga), iter 1 (X=1) commits R1 with bit 6 cleared and the rest
;   of init runs in the relaxed blanked-display window.
;
;   Always disable sprites on init — without this, random bistable VRAM
;   noise on power-on appears as floating sprites (CLAUDE.md gotcha). We
;   call disable_sprites as a normal subroutine (was a fall-through before)
;   so the SAT terminator write happens while the display is still blanked.
; ----------------------------------------------------------------------------
init_vdp_g1:
        ; Caller-gap cushion: covers the worst-case scenario where the
        ; caller's last VDP write was very recent (e.g. Rogue boot
        ; immediately after the menu's last STA VDP_CTRL).
        JSR     tms9918_pad18   ; cross-caller cushion (18c)
        LDX     #0
@rg:    LDA     vdp1_regs,X
        CPX     #1
        BNE     @rg_store
        AND     #$BF            ; force display OFF for the loop pass
@rg_store:
        STA     VDP_CTRL
        TXA
        ORA     #$80
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (back-to-back VDP store)
        STA     VDP_CTRL
        INX
        CPX     #8
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (back-to-back VDP store)
        BNE     @rg
        JSR     wipe_all_vram   ; zero all 16 KB while display is still OFF
        JSR     disable_sprites ; SAT terminator with display still OFF
        ; --- Final: re-arm R1 with the table value (display ON typically).
        ;     Display stays OFF until the cmd byte commits — threshold = 2c
        ;     through both STAs, no inline pad needed.
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA zp/abs bridge)
        LDA     vdp1_regs+1
        STA     VDP_CTRL
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA #imm bridge)
        LDA     #$81            ; cmd = $80 | reg-1
        STA     VDP_CTRL
        RTS

; ----------------------------------------------------------------------------
; wipe_all_vram: zero ALL 16 KB of VRAM ($0000-$3FFF). Power-on VRAM is
;   undefined (CLAUDE.md gotcha), so a forgetful caller that skips the
;   per-table uploads must still boot a clean chip — this closes that footgun.
;
;   *** Must be called with the display blanked (R1 bit 6 = 0). *** init_vdp_g1
;   does exactly that (the @rg loop forces R1 OFF), so the chip serves the dense
;   "ScreenOff" CPU access slots: drain ~2c, far below the inner STA/INY/BNE gap
;   (>=9c). The burst therefore needs NO per-byte silicon-strict pad — ~3x
;   faster than a padded clear (see doc/TMS9918_TRANSFER_WINDOWS.md). Calling it
;   with the display ON would drop bytes under Gfx12 slot density.
;   Clobbers A, X, Y.
; ----------------------------------------------------------------------------
wipe_all_vram:
        JSR     tms9918_pad18   ; cushion before the addr-setup control writes
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad18
        LDA     #$40            ; $00 | $40 = auto-increment write from $0000
        STA     VDP_CTRL
        JSR     tms9918_pad18   ; cmd byte -> first STA VDP_DATA cushion
        LDA     #$00
        LDX     #64             ; 64 * 256 = 16384 bytes
@wp:    LDY     #0
@wb:    STA     VDP_DATA        ; no pad: ScreenOff drain ~2c << 9c inner gap
        INY
        BNE     @wb
        DEX
        BNE     @wp
        RTS

; ----------------------------------------------------------------------------
; disable_sprites: defensive SAT init at VRAM $1B00 (Mode I SAT base, R5=$36).
;   Single $D0 at SAT[0].Y was observed insufficient under POM1 silicon-strict
;   (LOGO demo2 + Life CodeTank ghost sprites from noise SAT entries past
;   slot 0, May 2026). Adopt the Rogue gold-standard (sketchs/doc/TMS9918-SPRITE_INIT.md
;   §4.2): write $D0 to SAT[0].Y then 127× $D1 (off-screen Y, NOT terminator)
;   via auto-increment to cover all 32 SAT entries. Even if SAT[0] is ever
;   overwritten with a real sprite later, SAT[1].Y = $D1 aborts visible
;   rendering of slot 1+. Mirrors tms9918m2.asm::disable_sprites.
; ----------------------------------------------------------------------------
disable_sprites:
        JSR     tms9918_pad18   ; MANUAL caller-gap cushion (18c)
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad18
        LDA     #$5B            ; $1B | $40 = write at $1B00
        STA     VDP_CTRL
        JSR     tms9918_pad18
        LDA     #$D0            ; SAT[0].Y = chain terminator
        STA     VDP_DATA
        JSR     tms9918_pad18
        LDX     #127            ; SAT[1..127] = off-screen Y via auto-inc
        LDA     #$D1
@sat:   STA     VDP_DATA
        JSR     tms9918_pad18
        DEX
        BNE     @sat
        RTS

; ----------------------------------------------------------------------------
; clear_name_table: fill all 768 bytes of the name table at $1800-$1AFF
;   with character 0. Use chars 0-7 for "blank/background" tiles whose
;   colour byte at $2000 makes them invisible.
; ----------------------------------------------------------------------------
clear_name_table:
        JSR     tms9918_pad18   ; MANUAL caller-gap cushion (18c)
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA #imm bridge)
        LDA     #$58            ; $18 | $40 = write at $1800
        STA     VDP_CTRL
        JSR     tms9918_pad18   ; addr-cmd → first STA VDP_DATA cushion
        LDX     #$03            ; 3 * 256 = 768 bytes
        LDA     #$00
@np:    LDY     #$00
@nb:    STA     VDP_DATA
        JSR     tms9918_pad18   ; silicon-strict 18c (loop-back inner @nb,
                                ; raw inner gap = STA+INY+BNE = 9c)
        INY
        BNE     @nb
        DEX
        BNE     @np
        RTS
        ; NOTE: does NOT touch R1 — caller responsibility. Wrapping in an
        ; internal display_off/display_on bracket regresses callers (notably
        ; Rogue) that use a non-default R1 like $C2 (sprite 16x16): every
        ; clear_name_table call would silently reset R1 to $C0 (8x8) and
        ; render sprites at 1/4 size. If silicon-strict drops matter for a
        ; specific burst, the caller should JSR vdp_display_off (lib helper
        ; from tms9918_pad.asm) before and re-arm their own R1 after.

; ----------------------------------------------------------------------------
; vdp_set_write: prep VDP for auto-incrementing writes at vdp_lo:hi.
;   Caller writes data via STA VDP_DATA after this returns.
; ----------------------------------------------------------------------------
vdp_set_write:
        JSR     tms9918_pad18   ; MANUAL caller-gap cushion (18c)
        LDA     vdp_lo
        STA     VDP_CTRL
        JSR     tms9918_pad18   ; silicon-strict 18c (LDA zp + ORA bridge)
        LDA     vdp_hi
        ORA     #$40            ; bit 6 = write
        STA     VDP_CTRL
        JSR     tms9918_pad18   ; cushion: caller's first STA VDP_DATA lands
        RTS                     ; ≥ 30c after the cmd byte (18c+6c+6c=30c gap)

; ----------------------------------------------------------------------------
; vdp_set_read: prep VDP for reads at vdp_lo:hi.
;   Caller reads via LDA VDP_DATA after this returns.
; ----------------------------------------------------------------------------
vdp_set_read:
        JSR     tms9918_pad18   ; MANUAL caller-gap cushion (18c)
        LDA     vdp_lo
        STA     VDP_CTRL
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA zp/abs bridge)
        LDA     vdp_hi          ; bit 6 = 0 → read
        STA     VDP_CTRL
        JSR     tms9918_pad18   ; cushion: caller's first LDA VDP_DATA lands
        RTS                     ; ≥ 30c after the cmd byte

; ----------------------------------------------------------------------------
; vdp_upload_a: copy A bytes from (vdp_src_lo:hi) to VDP_DATA.
;   Caller responsibility: vdp_set_write at the destination first.
;   Inputs:   A = byte count (1..256; 0 → 256 by wrap)
;             vdp_src_lo:hi = source pointer
;   Clobbers: A, Y. X preserved.
; ----------------------------------------------------------------------------
vdp_upload_a:
        STA     tmp             ; tmp = count (0 → 256)
        LDY     #0
@lp:    LDA     (vdp_src_lo),Y
        STA     VDP_DATA
        INY
        CPY     tmp
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (back-to-back VDP store)
        BNE     @lp
        RTS

; ----------------------------------------------------------------------------
; name_at_rc: compute name-table VRAM address for (vdp_row, vdp_col) and
;             store in vdp_lo:hi (with bit 6 set for write).
;   addr = $1800 + 32 * row + col
;   Inputs:   vdp_row (0..23), vdp_col (0..31)
;   Output:   vdp_lo:hi loaded — caller can JSR vdp_set_write next, OR
;             call print_at_rc which does that for you.
;   Clobbers: A. X, Y preserved.
; ----------------------------------------------------------------------------
name_at_rc:
        ; Compute (32 * row) into vdp_lo:hi.
        ;   low byte  = (row & $07) << 5
        ;   high byte = row >> 3
        LDA     vdp_row
        AND     #$07
        ASL
        ASL
        ASL
        ASL
        ASL                     ; A = (row & 7) << 5
        STA     vdp_lo
        LDA     vdp_row
        LSR
        LSR
        LSR                     ; A = row >> 3 (max value: 23 >> 3 = 2)
        STA     vdp_hi
        ; Add col into low byte, carry into high.
        LDA     vdp_lo
        CLC
        ADC     vdp_col
        STA     vdp_lo
        LDA     vdp_hi
        ADC     #$18            ; carry from low + name-table base $1800
        ORA     #$40            ; write bit
        STA     vdp_hi
        RTS

; ----------------------------------------------------------------------------
; print_at_rc: write char A at (vdp_row, vdp_col) — full sequence.
;   Inputs:   A = char, vdp_row, vdp_col
;   Clobbers: A. X, Y preserved.
; ----------------------------------------------------------------------------
print_at_rc:
        PHA                     ; preserve char
        JSR     name_at_rc
        JSR     vdp_set_write
        PLA
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (back-to-back VDP store)
        STA     VDP_DATA
        RTS

; ============================================================================
; Mode 1 register table (Graphics I, standard layout used by every Mode-1
; project in this repo). Reg 7 sets backdrop colour 1 (black) — change
; in your project init by writing a different value to VDP_CTRL after
; calling init_vdp_g1, or fork the table to a project-local copy.
;
;   Reg 0: $00  External video off, M3=0 (Mode 1 base)
;   Reg 1: $C0  16K mode, screen on, IRQ off, sprite 8x8, no magnify
;   Reg 2: $06  Name table at $1800 ($06 << 10)
;   Reg 3: $80  Colour table at $2000 ($80 << 6)
;   Reg 4: $00  Pattern table at $0000
;   Reg 5: $36  Sprite attribute at $1B00 ($36 << 7)
;   Reg 6: $07  Sprite pattern at $3800 ($07 << 11)
;   Reg 7: $01  Backdrop = colour 1 (black)
; ============================================================================
vdp1_regs:
        .byte   $00, $C0, $06, $80, $00, $36, $07, $01
