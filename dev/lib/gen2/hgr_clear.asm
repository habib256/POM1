; =============================================
; hgr_clear.asm - clear the GEN2 HGR framebuffer
; =============================================
; Standalone module (split out of hgr_tables.inc). Needs ZP: ptr_lo, ptr_hi.
; clear_hgr - zero out $2000-$3FFF (8 KB). Trashes A, X, Y, ptr_lo, ptr_hi.
; =============================================
clear_hgr:
        LDA #$00
        TAY
        STA ptr_lo
        LDX #$20
        STX ptr_hi
@clr:   STA (ptr_lo),Y
        INY
        BNE @clr
        INC ptr_hi
        LDX ptr_hi
        CPX #$40
        BNE @clr
        RTS
