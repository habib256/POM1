; ============================================================================
; tms9918_text.asm -- TMS9918 Text Mode F1 (40x24 monochrome) driver
;                     for Apple-1 + P-LAB Graphic Card.
;
; Forks tms9918m1.asm. The TMS9918's Text Mode F1 has the *exact same*
; geometry as the Apple-1 PIA display (40 columns x 24 rows), so it's the
; natural drop-in replacement for the slow $D012-driven character output
; once the charmap is uploaded to the VDP pattern table.
;
; Public symbols:
;   init_vdp_text   -- 8 registers + tail-call disable_sprites_text
;   upload_charmap  -- copy 1 KB Apple-1 charmap (bit-reversed) to pattern
;                      table at $0000. Display is blanked during the burst.
;   clear_screen_text -- fill 960 bytes at $0800 with $20 (space)
;   vdp_set_write   -- prep VRAM auto-increment write at vdp_lo:hi
;   vdp_set_read    -- prep VRAM read at vdp_lo:hi
;   name_at_rc_text -- load vdp_lo:hi with name-table addr for
;                      (vdp_row, vdp_col) -- 40-col arithmetic
;   print_at_rc_text-- write char A at (vdp_row, vdp_col) -- full sequence
;
; Owns ZP slots: vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col.
;                Same layout as tms9918m1.asm so a project never links both.
;
; Imports (caller must define):
;   tmp                 -- 1 ZP scratch byte (used inside name_at_rc_text
;                          and the bit-reverse loop of upload_charmap).
;
; Memory map (Text Mode F1, fixed by the register table below):
;   $0000-$03FF  Pattern table (1 KB = 128 chars * 8 bytes)
;   $0800-$0BBF  Name table   (40 cols * 24 rows = 960 bytes)
;   (no sprites, no colour table -- text mode uses R7 for fg/bg)
;
; Silicon-strict timing: every STA VDP_DATA in this module is followed by
; a paired STA/INX/CPX/BNE that already eats >= 8 cycles, OR by an explicit
; NOP pad. The display is blanked (R1 = $80) during upload_charmap so even
; the densest burst sees an open access window.
; ============================================================================

        .import tms9918_pad40  ; silicon-strict pad40 (helper from tms9918_pad.asm)
.include "apple1.inc"
.include "tms9918.inc"

; --- exported ZP slots -----------------------------------------------------
.exportzp vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col

; --- imports ---------------------------------------------------------------
.importzp tmp

; --- exported routines -----------------------------------------------------
.export init_vdp_text, upload_charmap, clear_screen_text
.export vdp_set_write, vdp_set_read
.export name_at_rc_text, print_at_rc_text

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
; init_vdp_text: write 8 Text-Mode-F1 registers.
;   Display starts ON (R1 = $D0). Caller may blank by overwriting R1 with
;   $80 before bulk uploads -- upload_charmap does that automatically.
; ----------------------------------------------------------------------------
init_vdp_text:
        LDX     #0
@rg:    LDA     vdpt_regs,X
        STA     VDP_CTRL
        TXA
        ORA     #$80
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA     VDP_CTRL
        INX
        CPX     #8
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BNE     @rg
        RTS

; ----------------------------------------------------------------------------
; upload_charmap: copy the 1024-byte Apple-1 charmap to pattern table $0000,
;   bit-reversing each row (Apple-1 = LSB toward left, TMS9918 = MSB toward
;   left). Display is blanked during the burst so the silicon-strict access
;   window is wide open. Pattern lifted from text_bitmap.asm:88-94.
;
;   Clobbers: A, X, Y, tmp, vdp_lo, vdp_hi, vdp_src_lo/hi.
; ----------------------------------------------------------------------------
upload_charmap:
        ; 1) Blank display (R1 = $80, M1=1, screen off).
        LDA     #$80
        STA     VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA     #$81            ; reg 1
        STA     VDP_CTRL

        ; 2) Set VRAM write pointer to $0000 (pattern table base).
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA     #$00
        STA     VDP_CTRL
        NOP
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA     #$40            ; $00 | $40 = write at $0000
        STA     VDP_CTRL

        ; 3) Stream 1024 bytes = 4 pages * 256 bytes from charmap_table.
        ;    Bit-reverse each byte: Apple-1 charmap.rom encodes bit 0 as
        ;    leftmost pixel, but TMS9918 reads bit 7 as leftmost. Pattern
        ;    cf. text_bitmap.asm:88-94 (LSR src -> C, ROL dst x 8). X is
        ;    the page counter so we use vdp_lo as the inner bit counter
        ;    (irrelevant during streaming -- VDP auto-increments the
        ;    address it latched in step 2).
        LDA     #<charmap_table
        STA     vdp_src_lo
        LDA     #>charmap_table
        STA     vdp_src_hi
        LDX     #4              ; 4 pages
@page:  LDY     #0
@byte:  LDA     (vdp_src_lo),Y
        STA     tmp             ; tmp = source byte
        LDA     #8
        STA     vdp_lo          ; bit counter
        LDA     #0              ; A = destination accumulator
@bitrev:
        LSR     tmp
        ROL
        DEC     vdp_lo
        BNE     @bitrev
        STA     VDP_DATA        ; >> 8c since previous latch
        INY
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BNE     @byte
        INC     vdp_src_hi      ; advance to next page
        DEX
        BNE     @page

        ; 4) Re-enable display (R1 = $D0).
        LDA     #$D0
        STA     VDP_CTRL
        NOP
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA     #$81
        STA     VDP_CTRL
        RTS

; ----------------------------------------------------------------------------
; clear_screen_text: fill the 960-byte name table at $0800 with $20 (space).
;   Clobbers: A, X, Y.
; ----------------------------------------------------------------------------
clear_screen_text:
        LDA     #$00
        STA     VDP_CTRL
        NOP
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA     #$48            ; $08 | $40 = write at $0800
        STA     VDP_CTRL
        ; 960 bytes = 3 pages of 256 + 192. Loop X = 3 full pages, then
        ; tail of 192. Each STA VDP_DATA + INY/BNE = >= 8c.
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA     #$20            ; space
        LDX     #3
@p:     LDY     #0
@b:     STA     VDP_DATA
        INY
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BNE     @b
        DEX
        BNE     @p
        ; Tail: 192 bytes
        LDY     #192
@t:     STA     VDP_DATA
        DEY
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BNE     @t
        RTS

; ----------------------------------------------------------------------------
; vdp_set_write: prep VDP for auto-incrementing writes at vdp_lo:hi.
; ----------------------------------------------------------------------------
vdp_set_write:
        LDA     vdp_lo
        STA     VDP_CTRL
        NOP                     ; +2c gap
        LDA     vdp_hi
        ORA     #$40            ; bit 6 = write
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA     VDP_CTRL
        RTS

; ----------------------------------------------------------------------------
; vdp_set_read: prep VDP for reads at vdp_lo:hi.
; ----------------------------------------------------------------------------
vdp_set_read:
        LDA     vdp_lo
        STA     VDP_CTRL
        NOP
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA     vdp_hi          ; bit 6 = 0 -> read
        STA     VDP_CTRL
        RTS

; ----------------------------------------------------------------------------
; name_at_rc_text: compute name-table VRAM address for (vdp_row, vdp_col)
;   in 40-column arithmetic, store in vdp_lo:hi (high byte gets bit 6 set
;   to mark write).
;     addr = $0800 + 40 * row + col
;   Inputs:   vdp_row (0..23), vdp_col (0..39)
;   Output:   vdp_lo:hi loaded.
;   Clobbers: A, tmp.
; ----------------------------------------------------------------------------
name_at_rc_text:
        ; Compute 40 * row using 32 * row + 8 * row.
        LDA     vdp_row
        STA     tmp             ; tmp = row
        ; A = row * 32: low byte = (row & 7) << 5, high byte = row >> 3
        AND     #$07
        ASL
        ASL
        ASL
        ASL
        ASL                     ; A = (row & 7) << 5
        STA     vdp_lo
        LDA     tmp
        LSR
        LSR
        LSR
        STA     vdp_hi          ; high(row*32) = row >> 3
        ; Add 8 * row  (a.k.a. row << 3) to (vdp_lo:hi).
        LDA     tmp
        ASL
        ASL
        ASL                     ; row * 8 (max 23 * 8 = 184, fits in A)
        CLC
        ADC     vdp_lo
        STA     vdp_lo
        BCC     :+
        INC     vdp_hi
:
        ; Add col into low byte, carry into high.
        LDA     vdp_lo
        CLC
        ADC     vdp_col
        STA     vdp_lo
        LDA     vdp_hi
        ADC     #$08            ; carry from low + name-table base $0800
        STA     vdp_hi
        RTS

; ----------------------------------------------------------------------------
; print_at_rc_text: write char A at (vdp_row, vdp_col) -- full sequence.
;   Caller is responsible for col<40 / row<24 bounds. The console runtime
;   on top of this driver handles wrap and scroll.
; ----------------------------------------------------------------------------
print_at_rc_text:
        PHA                     ; save char
        JSR     name_at_rc_text
        JSR     vdp_set_write
        PLA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA     VDP_DATA
        RTS

; ============================================================================
; Text Mode F1 register table.
;
;   Reg 0: $00  External video off, M3=0.
;   Reg 1: $D0  M1=1 (text mode), screen on, IRQ off, 16K, no sprites.
;   Reg 2: $02  Name table at $0800 ($02 << 10).
;   Reg 3: $00  (colour table unused in F1)
;   Reg 4: $00  Pattern table at $0000.
;   Reg 5: $00  (sprite attribute table unused in F1)
;   Reg 6: $00  (sprite pattern table unused in F1)
;   Reg 7: $F0  fg = white (15), bg = black (0).
; ============================================================================
vdpt_regs:
        .byte   $00, $D0, $02, $00, $00, $00, $00, $F0

; ============================================================================
; charmap_table: 1024-byte 8x8 monochrome ASCII font, lifted from
;   roms/charmap.rom. Format: 1 byte per row, LSB = leftmost pixel
;   (Apple-1 native order). The upload routine bit-reverses each byte
;   on the fly because TMS9918 reads bit 7 = leftmost.
;
;   Lives inside the driver so any project linking it gets the font for
;   free. Adds 1 KB to the project's CODE segment -- callers that already
;   reserve cassette-image headroom (CODE size $1D80 in 8 KB DRAM) absorb
;   it without flinching.
; ============================================================================
charmap_table:
        .incbin "../../../roms/charmap.rom"
