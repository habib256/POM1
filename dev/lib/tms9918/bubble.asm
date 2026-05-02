; ============================================================================
; bubble.asm  --  comic-book speech-bubble frame for TMS9918 Mode-2
; ----------------------------------------------------------------------------
; Draws a fixed-position speech bubble on the bitmap: rectangle
; (8,80)-(247,112) plus a small triangular tail from (124,80)/(132,80) up
; to (128,72). Six line_xy calls. Assumes the narrator sprite sits centred
; around (128, 64) -- about 1/3 down the 192-line screen so the bubble
; has room for up to 3 wrapped text lines below it. Total ~85 bytes.
;
; Forces plot_mode = 0 (OR) so the frame paints additively. The caller
; should pre-load pen_color with the desired outline shade (white = $0F
; works for every background colour used by the LOGO scene library).
;
; Lifted from TMS_Logo_16k.asm. Gated by CODETANK_BUILD so DRAM builds
; that don't ship LABEL/SAY don't pay 85 bytes for an unused routine.
; ============================================================================

.ifdef CODETANK_BUILD

.export   draw_bubble
.import   line_xy
.import   plot_mode
.importzp ln_x0, ln_y0, ln_x1, ln_y1

.segment "CODE"

draw_bubble:
        LDA #0
        STA plot_mode
        ; --- top edge (8,80) -> (247,80)
        LDA #8
        STA ln_x0
        LDA #80
        STA ln_y0
        STA ln_y1
        LDA #247
        STA ln_x1
        JSR line_xy
        ; --- bottom edge (8,112) -> (247,112)
        LDA #8
        STA ln_x0
        LDA #112
        STA ln_y0
        STA ln_y1
        LDA #247
        STA ln_x1
        JSR line_xy
        ; --- left edge (8,80) -> (8,112)
        LDA #8
        STA ln_x0
        STA ln_x1
        LDA #80
        STA ln_y0
        LDA #112
        STA ln_y1
        JSR line_xy
        ; --- right edge (247,80) -> (247,112)
        LDA #247
        STA ln_x0
        STA ln_x1
        LDA #80
        STA ln_y0
        LDA #112
        STA ln_y1
        JSR line_xy
        ; --- tail left (124,80) -> (128,72)
        LDA #124
        STA ln_x0
        LDA #80
        STA ln_y0
        LDA #128
        STA ln_x1
        LDA #72
        STA ln_y1
        JSR line_xy
        ; --- tail right (128,72) -> (132,80)
        LDA #128
        STA ln_x0
        LDA #72
        STA ln_y0
        LDA #132
        STA ln_x1
        LDA #80
        STA ln_y1
        JMP line_xy

.endif  ; CODETANK_BUILD
