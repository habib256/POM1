; ============================================================================
; tms9918m1.asm -- TMS9918 Mode 1 (Graphics I, 32x24 cells of 8x8 px) driver
;                  for Apple-1 + P-LAB Graphic Card. Mutualises the init +
;                  upload + name-table writes that 4+ games (TMS_Sokoban,
;                  TMS_Connect4, TMS_Snake, TMS_Galaga) currently re-derive.
;
; Public symbols:
;   init_vdp_g1     -- 8 registers + tail-call disable_sprites
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

.include "apple1.inc"
.include "tms9918.inc"

; --- exported ZP slots -----------------------------------------------------
.exportzp vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col

; --- imports ---------------------------------------------------------------
.importzp tmp

; --- exported routines -----------------------------------------------------
.export init_vdp_g1, disable_sprites, clear_name_table
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
; init_vdp_g1: write 8 Mode-1 registers, then tail-call disable_sprites.
;   Always disable sprites on init — without this, random bistable VRAM
;   noise on power-on appears as floating sprites (CLAUDE.md gotcha).
; ----------------------------------------------------------------------------
init_vdp_g1:
        LDX     #0
@rg:    LDA     vdp1_regs,X
        STA     VDP_CTRL
        TXA
        ORA     #$80
        STA     VDP_CTRL
        INX
        CPX     #8
        BNE     @rg
        ; fall through to disable_sprites

; ----------------------------------------------------------------------------
; disable_sprites: write $D0 to sprite #0 Y attribute at VRAM $1B00. The
;   TMS9918 stops processing the sprite chain at the first $D0 it sees.
; ----------------------------------------------------------------------------
disable_sprites:
        LDA     #$00
        STA     VDP_CTRL
        NOP                     ; +2c silicon-strict gap (LDA #imm bridge)
        LDA     #$5B            ; $1B | $40 = write at $1B00
        STA     VDP_CTRL
        NOP                     ; +2c silicon-strict gap (LDA #imm bridge)
        LDA     #$D0
        STA     VDP_DATA
        RTS

; ----------------------------------------------------------------------------
; clear_name_table: fill all 768 bytes of the name table at $1800-$1AFF
;   with character 0. Use chars 0-7 for "blank/background" tiles whose
;   colour byte at $2000 makes them invisible.
; ----------------------------------------------------------------------------
clear_name_table:
        LDA     #$00
        STA     VDP_CTRL
        NOP                     ; +2c silicon-strict gap (LDA #imm bridge)
        LDA     #$58            ; $18 | $40 = write at $1800
        STA     VDP_CTRL
        LDX     #$03            ; 3 * 256 = 768 bytes
        LDA     #$00
@np:    LDY     #$00
@nb:    STA     VDP_DATA
        INY
        BNE     @nb
        DEX
        BNE     @np
        RTS

; ----------------------------------------------------------------------------
; vdp_set_write: prep VDP for auto-incrementing writes at vdp_lo:hi.
;   Caller writes data via STA VDP_DATA after this returns.
; ----------------------------------------------------------------------------
vdp_set_write:
        LDA     vdp_lo
        STA     VDP_CTRL
        LDA     vdp_hi
        ORA     #$40            ; bit 6 = write
        STA     VDP_CTRL
        RTS

; ----------------------------------------------------------------------------
; vdp_set_read: prep VDP for reads at vdp_lo:hi.
;   Caller reads via LDA VDP_DATA after this returns.
; ----------------------------------------------------------------------------
vdp_set_read:
        LDA     vdp_lo
        STA     VDP_CTRL
        NOP                     ; +2c silicon-strict gap (LDA zp/abs bridge)
        LDA     vdp_hi          ; bit 6 = 0 → read
        STA     VDP_CTRL
        RTS

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
