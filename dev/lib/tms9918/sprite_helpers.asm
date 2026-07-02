; ============================================================================
; sprite_helpers.asm  --  TMS9918 sprite plumbing shared across projects
; ----------------------------------------------------------------------------
; Two small routines lifted from TMS_Logo_16k.asm:
;
;   apply_sprite_size     Derive spr_xoff / spr_yoff / spr_r1 from spr_size.
;                         spr_size must be 8 (8x8 sprites) or 32 (16x16).
;                         Anything else falls back to 8x8 (defensive;
;                         the CMP #32 path only matches exactly 32).
;                           16x16: xoff=8, yoff=9, R1=$C2  (16K|DISP|spr-16)
;                           8x8:   xoff=4, yoff=5, R1=$C0  (16K|DISP|spr-8)
;                         Call after writing spr_size, then upload R1 to
;                         the VDP separately (the lib doesn't poke VDP_CTRL).
;                         Clobbers A. X, Y preserved.
;
;   heading_to_octant     Map a 16-bit heading (th_lo:th_hi, 0..359) to an
;                         octant 0..7: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW,
;                         6=W, 7=NW. Each octant spans 45 deg centred on
;                         its compass point, so heading=22 already commits
;                         to NE.
;                         Returns A = octant. Preserves Y. Clobbers X,
;                         tmp, tmp2.
;
; Caller-provided ZP (must be .exportzp'd; see TMS_Logo_16k.asm for the
; canonical wiring):
;   spr_size, spr_xoff, spr_yoff, spr_r1   (1 byte each)
;   th_lo, th_hi                           (1 byte each, heading 0..359)
;   tmp, tmp2                              (scratch, clobbered)
; ============================================================================

.export   apply_sprite_size, heading_to_octant
.importzp spr_size, spr_xoff, spr_yoff, spr_r1
.importzp th_lo, th_hi
.importzp tmp, tmp2

.segment "CODE"

; ----------------------------------------------------------------------------
apply_sprite_size:
        LDA spr_size
        CMP #32
        BEQ @big
        ; --- 8x8 ---
        LDA #4
        STA spr_xoff
        LDA #5
        STA spr_yoff
        LDA #$C0
        STA spr_r1
        RTS
@big:   ; --- 16x16 ---
        LDA #8
        STA spr_xoff
        LDA #9
        STA spr_yoff
        LDA #$C2
        STA spr_r1
        RTS

; ----------------------------------------------------------------------------
heading_to_octant:
        LDA th_lo
        CLC
        ADC #22
        STA tmp
        LDA th_hi
        ADC #0
        STA tmp2
        LDX #0
@hloop: LDA tmp2
        BNE @hsub             ; high byte non-zero, definitely >= 45
        LDA tmp
        CMP #45
        BCC @hdone
@hsub:  LDA tmp
        SEC
        SBC #45
        STA tmp
        LDA tmp2
        SBC #0
        STA tmp2
        INX
        JMP @hloop
@hdone: TXA
        AND #7                ; mod 8 (heading 360+22 -> X=8 -> 0=N)
        RTS
