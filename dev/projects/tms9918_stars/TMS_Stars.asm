; ============================================================================
; TMS_Stars.asm  --  3-plane parallax starfield (MSX1-scene classic)
; ----------------------------------------------------------------------------
; P-LAB TMS9918 Graphic Card / POM1 / Apple 1, Mode 1 (Graphics I), 8x8
; sprites. The screen stays solid black via name-table cells using a fully
; transparent colour group 0 -> the R7 backdrop ($01 = black) shows through.
;
; 32 hardware sprites are split into three parallax planes:
;   Plane 0 (slots  0..10, fast)   : pattern 0 = bright 2x2 dot, white,  X -= 3
;   Plane 1 (slots 11..21, medium) : pattern 1 = single pixel,   cyan,   X -= 2
;   Plane 2 (slots 22..31, slow)   : pattern 2 = single pixel,   grey,   X -= 1
;
; Byte-wraparound on the X subtraction gives the scrolling-off-the-left,
; reappearing-on-the-right effect for free -- $02 - 3 = $FF, $01 - 3 = $FE,
; etc. The pattern/colour are precomputed per slot in two 32-byte LUTs so
; the SAT loop body is the minimum 4 LDA+STA+pad12 quadruplets, no branches.
;
; Per-frame budget (~3 k cycles on 1.022 MHz 6502):
;   - X advance for 3 planes : ~330c (outside VBlank, RAM-only)
;   - SAT stream (33 entries * 4 bytes via auto-increment) : ~2840c (in VBlank)
;
; Keys: ESC -> exit to Wozmon.
; ============================================================================

        .import init_vdp_g1, disable_sprites, clear_name_table
        .import tms9918_pad12

.include "apple1.inc"
.include "tms9918.inc"

KEY_ESC      = $9B
TOTAL_STARS  = 32
FAST_END     = 11               ; slots 0..10 are fast
MEDIUM_END   = 22               ; slots 11..21 are medium, 22..31 slow

; ----- ZP ------------------------------------------------------------------
.segment "ZEROPAGE"
        .res 2                  ; $00-$01 reserved
tmp:    .res 1
.exportzp tmp

; ----- BSS -----------------------------------------------------------------
.segment "BSS"
x_buf:  .res TOTAL_STARS        ; runtime X positions (per frame)

; ----- CODE ----------------------------------------------------------------
.segment "CODE"

start:
        SEI
        CLD
        LDX #$FF
        TXS

        JSR init_vdp_g1
        JSR disable_sprites
        JSR clear_name_table

        ; --- R7 = $01 (backdrop = black) ---
        LDA #$01
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$87                ; cmd = $80 | reg-7
        STA VDP_CTRL
        JSR tms9918_pad12

        ; --- Colour table $2000 -> all $00 (transparent FG & BG). Every
        ;     name-table cell renders as backdrop = black. Pattern table
        ;     content does not matter (transparent overrides bits).
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$60                ; $20 | $40 -> write at $2000
        STA VDP_CTRL
        JSR tms9918_pad12
        LDX #32
        LDA #$00
@ct:    STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @ct

        ; --- Sprite pattern table $3800: 3 star patterns (24 bytes total)
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$78                ; $38 | $40 -> write at $3800
        STA VDP_CTRL
        JSR tms9918_pad12
        LDY #0
@sp:    LDA star_patterns,Y
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        CPY #24
        BNE @sp

        ; --- Copy initial X positions to x_buf ---
        LDX #0
@cx:    LDA x_init,X
        STA x_buf,X
        INX
        CPX #TOTAL_STARS
        BNE @cx

; ============================================================================
main_loop:
        ; ---- Phase 1 (active display, RAM only) : advance X per plane ----
        LDX #0
@plf:   LDA x_buf,X
        SEC
        SBC #3                  ; fast plane: -3 per frame
        STA x_buf,X
        INX
        CPX #FAST_END
        BNE @plf

@plm:   LDA x_buf,X
        SEC
        SBC #2                  ; medium plane: -2
        STA x_buf,X
        INX
        CPX #MEDIUM_END
        BNE @plm

@pls:   LDA x_buf,X
        SEC
        SBC #1                  ; slow plane: -1
        STA x_buf,X
        INX
        CPX #TOTAL_STARS
        BNE @pls

        WAIT_VBLANK

        ; ESC?
        LDA KBDCR
        BPL @no_key
        LDA KBD
        CMP #KEY_ESC
        BEQ exit
@no_key:

        ; ---- Phase 2 (vblank) : stream 32 SAT entries + terminator ----
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$5B                ; $1B | $40 -> write at $1B00
        STA VDP_CTRL
        JSR tms9918_pad12

        LDX #0
@sat:   LDA y_table,X
        STA VDP_DATA
        JSR tms9918_pad12
        LDA x_buf,X
        STA VDP_DATA
        JSR tms9918_pad12
        LDA pattern_per_slot,X
        STA VDP_DATA
        JSR tms9918_pad12
        LDA color_per_slot,X
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #TOTAL_STARS
        BNE @sat

        ; Terminator -> chip stops scanning at slot 32
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12

        JMP main_loop

exit:
        LDA #$80                ; display OFF
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$81
        STA VDP_CTRL
        JSR tms9918_pad12
        JSR disable_sprites
        LDA KBD
        JMP $FF00


; ============================================================================
; Initial X positions -- scrambled to avoid visible alignment at frame 0.
; Sequence = (slot * 23 + 7) mod 256.
; ============================================================================
x_init:
        .byte $07, $1E, $35, $4C, $63, $7A, $91, $A8
        .byte $BF, $D6, $ED, $04, $1B, $32, $49, $60
        .byte $77, $8E, $A5, $BC, $D3, $EA, $01, $18
        .byte $2F, $46, $5D, $74, $8B, $A2, $B9, $D0

; ============================================================================
; Fixed Y positions. Previously a linear (slot*17 + 5) mod 184 sequence
; which combined with the linear X distribution produced a visible
; "top-left to bottom-right" diagonal of same-speed stars. Now: bit-reversed
; slot index scaled to 184 lines + tiny non-linear jitter. Within each
; parallax plane consecutive stars differ in Y by >= 45 px, killing the
; diagonal correlation while keeping a deterministic (snapshotable) layout.
; ============================================================================
y_table:
        .byte $00, $60, $33, $93, $1A, $7A, $4D, $A2
        .byte $13, $68, $3B, $9B, $22, $82, $55, $B5
        .byte $0B, $6B, $33, $93, $25, $7A, $4D, $AD
        .byte $14, $74, $47, $9C, $2E, $8E, $56, $B6

; ============================================================================
; Per-slot pattern name (0 = fast, 1 = medium, 2 = slow)
; ============================================================================
pattern_per_slot:
        .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0          ; slots  0..10 fast
        .byte 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1          ; slots 11..21 medium
        .byte 2, 2, 2, 2, 2, 2, 2, 2, 2, 2             ; slots 22..31 slow

; ============================================================================
; Per-slot colour (low nibble = palette index, bit 7 = early-clock = 0).
; ============================================================================
color_per_slot:
        .byte $0F, $0F, $0F, $0F, $0F, $0F, $0F, $0F   ; white fast
        .byte $0F, $0F, $0F
        .byte $07, $07, $07, $07, $07, $07, $07, $07   ; cyan medium
        .byte $07, $07, $07
        .byte $0E, $0E, $0E, $0E, $0E, $0E, $0E, $0E   ; grey slow
        .byte $0E, $0E

; ============================================================================
; Star sprite patterns (8x8, 3 patterns = 24 bytes)
;   pattern 0: bright 2x2 dot at center -> fast/foreground stars look bigger
;   pattern 1: single pixel at center  -> medium
;   pattern 2: single pixel offset by 1 row -> slow (different "shape")
; ============================================================================
star_patterns:
        ; pattern 0 -- 2x2 bright dot
        .byte $00, $00, $00, $18, $18, $00, $00, $00
        ; pattern 1 -- 1x1 dot
        .byte $00, $00, $00, $10, $00, $00, $00, $00
        ; pattern 2 -- 1x1 dim dot (shifted 1 row)
        .byte $00, $00, $00, $00, $08, $00, $00, $00
