; =============================================
; HGR SIERPINSKI TRIANGLE (full screen, centered)
; GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; Deterministic bitwise fractal
; =============================================
; Assemble:
;   Build: make
;
; Centered symmetric Sierpinski triangle pointing UP:
;   tx = |x - 128|, ty = 191 - y
;   Plot if tx <= ty AND (tx AND ty) == 0
;
; Apex at top center, base fills bottom of screen.
; ~5000 pixels, renders in ~2 seconds at 1 MHz.
; =============================================

; --- Apple 1 I/O ---
.include "apple1.inc"

; --- Constants ---
CENTER_X = 128          ; horizontal center
SCREEN_H = 192          ; screen height

; --- Zero page variables ---
.zeropage
tx:         .res 1      ; $00  |x - center|
ty:         .res 1      ; $01  191 - y
cur_x:      .res 1      ; $02  (required by hgr_tables.inc)
cur_y:      .res 1      ; $03  (required by hgr_tables.inc)
ptr_lo:     .res 1      ; $04  (required by hgr_tables.inc)
ptr_hi:     .res 1      ; $05  (required by hgr_tables.inc)

; --- Code at $E000 ---
.code

; =============================================
; MAIN
; =============================================
main:
        JSR gen2_hgr_init
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax

; =============================================
; DRAW: centered symmetric Sierpinski triangle
; =============================================
draw:
        JSR clear_hgr

        LDA #<str_draw
        LDX #>str_draw
        JSR print_str_ax

        LDA #$00
        STA cur_y

@yloop:
        ; ty = 191 - y (base at bottom, apex at top)
        LDA #(SCREEN_H - 1)
        SEC
        SBC cur_y
        STA ty

        LDA #$00
        STA cur_x

@xloop:
        ; tx = |x - 128|
        LDA cur_x
        SEC
        SBC #CENTER_X
        BCS @pos            ; x >= 128: result is positive
        EOR #$FF
        ADC #1              ; carry=0 from failed SBC → negate
@pos:   STA tx

        ; Skip if tx > ty (outside triangle)
        LDA ty
        CMP tx
        BCC @skip           ; ty < tx → outside

        ; Fractal test: (tx AND ty) == 0?
        LDA ty
        AND tx
        BNE @skip           ; not on the fractal

        JSR plot_pixel      ; plot at (cur_x, cur_y) via tables

@skip:  INC cur_x
        BNE @xloop          ; x = 0..255

        INC cur_y
        LDA cur_y
        CMP #SCREEN_H
        BNE @yloop

        ; Done
        LDA #<str_done
        LDX #>str_done
        JSR print_str_ax

@wk:    LDA KBDCR
        BPL @wk
        LDA KBD
        JMP draw

; =============================================
; print_str_ax — promoted to dev/lib/apple1/print.asm (Tier 2 mutualization).
; =============================================
.include "print.asm"

; =============================================
; STRINGS
; =============================================
str_title:
        .byte $0D, " * HGR SIERPINSKI *", $0D
        .byte " GEN2 COLOR GRAPHICS CARD", $0D
        .byte " CENTERED BITWISE FRACTAL", $0D
        .byte $0D, " DRAWING...", $0D, 0

str_draw:
        .byte " DRAWING...", $0D, 0

str_done:
        .byte " DONE. KEY=REDRAW", $0D, 0

; =============================================
; HGR TABLES & ROUTINES
; =============================================
.include "hgr_tables.inc"
.include "gen2_init.asm"
