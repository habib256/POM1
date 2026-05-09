; =============================================
; HGR HOUSE & TREE SCENE
; GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; Draws a house with tree using fills and lines
; NTSC artifact colors
; =============================================
; Assemble:
;   ca65 -o build/HGR_House.o software/hgr/HGR_House.asm
;   ld65 -C software/hgr/apple1_gen2.cfg -o build/HGR_House.bin build/HGR_House.o
;
; Scene: house with triangular roof, door, windows,
; chimney, pine tree, ground, sky, sun.
; All drawn at byte-column resolution (40 cols × 192 rows).
; =============================================

ECHO    = $FFEF
KBDCR   = $D011
KBD     = $D010

; --- NTSC color byte pairs (even/odd columns) ---
; To get consistent colors, swap patterns for even/odd byte columns.
COL_BLUE_E  = $D5       ; blue on even col
COL_BLUE_O  = $AA       ; blue on odd col
COL_GREEN_E = $2A       ; green on even col
COL_GREEN_O = $55       ; green on odd col
COL_ORANGE_E = $AA      ; orange on even col
COL_ORANGE_O = $D5      ; orange on odd col
COL_VIOLET_E = $55      ; violet on even col
COL_VIOLET_O = $2A      ; violet on odd col
COL_WHITE   = $7F       ; white (group 0, all pixels)
COL_BLACK   = $00

; --- Zero page ---
.zeropage
            .res 2      ; $00-$01
cur_x:      .res 1      ; $02
cur_y:      .res 1      ; $03
ptr_lo:     .res 1      ; $04
ptr_hi:     .res 1      ; $05
; Fill params
fill_x1:    .res 1      ; $06 left byte column
fill_x2:    .res 1      ; $07 right byte column
fill_y1:    .res 1      ; $08 top scanline
fill_y2:    .res 1      ; $09 bottom scanline
fill_even:  .res 1      ; $0A pattern for even cols
fill_odd:   .res 1      ; $0B pattern for odd cols
; Triangle params
tri_peak:   .res 1      ; $0C peak column
tri_hw:     .res 1      ; $0D current half-width
tri_accum:  .res 1      ; $0E Bresenham accumulator
tri_step:   .res 1      ; $0F step (target half-width)
tri_span:   .res 1      ; $10 row span (y2 - y1)
; General
mul_tmp:    .res 1      ; $11
mul_res0:   .res 1      ; $12

.code

; =============================================
; MAIN
; =============================================
main:
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax
@wait:  LDA KBDCR
        BPL @wait
        LDA KBD

draw:
        JSR clear_hgr

        ; === SKY (blue, rows 0-159) ===
        LDA #0
        STA fill_x1
        LDA #39
        STA fill_x2
        LDA #0
        STA fill_y1
        LDA #159
        STA fill_y2
        LDA #COL_BLUE_E
        STA fill_even
        LDA #COL_BLUE_O
        STA fill_odd
        JSR fill_rect

        ; === GROUND (green, rows 160-191) ===
        LDA #160
        STA fill_y1
        LDA #191
        STA fill_y2
        LDA #COL_GREEN_E
        STA fill_even
        LDA #COL_GREEN_O
        STA fill_odd
        JSR fill_rect

        ; === SUN (white oval: 3 rects for round shape) ===
        LDA #COL_WHITE
        STA fill_even
        STA fill_odd
        ; top arc: cols 4-5, rows 8-11
        LDA #4
        STA fill_x1
        LDA #5
        STA fill_x2
        LDA #8
        STA fill_y1
        LDA #11
        STA fill_y2
        JSR fill_rect
        ; middle: cols 3-6, rows 12-20
        LDA #3
        STA fill_x1
        LDA #6
        STA fill_x2
        LDA #12
        STA fill_y1
        LDA #20
        STA fill_y2
        JSR fill_rect
        ; bottom arc: cols 4-5, rows 21-24
        LDA #4
        STA fill_x1
        LDA #5
        STA fill_x2
        LDA #21
        STA fill_y1
        LDA #24
        STA fill_y2
        JSR fill_rect

        ; === CHIMNEY (orange, cols 19-20, rows 40-79) ===
        LDA #19
        STA fill_x1
        LDA #20
        STA fill_x2
        LDA #40
        STA fill_y1
        LDA #79
        STA fill_y2
        LDA #COL_ORANGE_E
        STA fill_even
        LDA #COL_ORANGE_O
        STA fill_odd
        JSR fill_rect

        ; === ROOF (orange triangle, peak col 14, rows 55-79) ===
        LDA #14
        STA tri_peak
        LDA #9          ; half-width at base (cols 5-23)
        STA tri_step
        LDA #55
        STA fill_y1
        LDA #79
        STA fill_y2
        LDA #COL_ORANGE_E
        STA fill_even
        LDA #COL_ORANGE_O
        STA fill_odd
        JSR fill_triangle

        ; === HOUSE BODY (white, cols 6-22, rows 80-159) ===
        LDA #6
        STA fill_x1
        LDA #22
        STA fill_x2
        LDA #80
        STA fill_y1
        LDA #159
        STA fill_y2
        LDA #COL_WHITE
        STA fill_even
        STA fill_odd
        JSR fill_rect

        ; === DOOR (orange, cols 13-15, rows 125-159) ===
        LDA #13
        STA fill_x1
        LDA #15
        STA fill_x2
        LDA #125
        STA fill_y1
        LDA #159
        STA fill_y2
        LDA #COL_ORANGE_E
        STA fill_even
        LDA #COL_ORANGE_O
        STA fill_odd
        JSR fill_rect

        ; === WINDOW LEFT (blue, cols 8-10, rows 92-112) ===
        LDA #8
        STA fill_x1
        LDA #10
        STA fill_x2
        LDA #92
        STA fill_y1
        LDA #112
        STA fill_y2
        LDA #COL_BLUE_E
        STA fill_even
        LDA #COL_BLUE_O
        STA fill_odd
        JSR fill_rect

        ; === WINDOW RIGHT (blue, cols 18-20, rows 92-112) ===
        LDA #18
        STA fill_x1
        LDA #20
        STA fill_x2
        JSR fill_rect

        ; === TREE: 3-layer pine (green triangles) ===
        LDA #COL_GREEN_E
        STA fill_even
        LDA #COL_GREEN_O
        STA fill_odd

        ; Top layer (narrow, peak col 32, rows 40-85)
        LDA #32
        STA tri_peak
        LDA #3          ; half-width at base
        STA tri_step
        LDA #40
        STA fill_y1
        LDA #85
        STA fill_y2
        JSR fill_triangle

        ; Middle layer (medium, peak col 32, rows 70-120)
        LDA #32
        STA tri_peak
        LDA #4
        STA tri_step
        LDA #70
        STA fill_y1
        LDA #120
        STA fill_y2
        JSR fill_triangle

        ; Bottom layer (wide, peak col 32, rows 100-150)
        LDA #32
        STA tri_peak
        LDA #5
        STA tri_step
        LDA #100
        STA fill_y1
        LDA #150
        STA fill_y2
        JSR fill_triangle

        ; === TREE TRUNK (sparse, cols 31-33, rows 150-159) ===
        LDA #31
        STA fill_x1
        LDA #33
        STA fill_x2
        LDA #150
        STA fill_y1
        LDA #159
        STA fill_y2
        LDA #$08
        STA fill_even
        STA fill_odd
        JSR fill_rect

        ; Done
        LDA #<str_done
        LDX #>str_done
        JSR print_str_ax
@wk:    LDA KBDCR
        BPL @wk
        LDA KBD
        JMP draw

; =============================================
; FILL_RECT: fill byte columns x1..x2, rows y1..y2
; Uses fill_even/fill_odd for parity-aware colors.
; =============================================
fill_rect:
        LDA fill_y1
        STA cur_y

@ry:    LDX cur_y
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi

        LDY fill_x1
@rx:    TYA
        AND #$01
        BNE @ro
        LDA fill_even
        JMP @rw
@ro:    LDA fill_odd
@rw:    STA (ptr_lo),Y
        INY
        CPY fill_x2
        BCC @rx
        BEQ @rx

        INC cur_y
        LDA cur_y
        CMP fill_y2
        BCC @ry
        BEQ @ry
        RTS

; =============================================
; FILL_TRIANGLE: draw filled triangle pointing DOWN
; Peak at (tri_peak, fill_y1), base at fill_y2
; Width grows from 0 to tri_step columns on each side.
; Uses Bresenham-style stepping.
; =============================================
fill_triangle:
        ; Compute span = y2 - y1
        LDA fill_y2
        SEC
        SBC fill_y1
        STA tri_span

        LDA #$00
        STA tri_hw          ; half-width starts at 0
        STA tri_accum

        LDA fill_y1
        STA cur_y

@ty:    ; Set x1/x2 from peak ± half_width
        LDA tri_peak
        SEC
        SBC tri_hw
        STA fill_x1
        LDA tri_peak
        CLC
        ADC tri_hw
        STA fill_x2
        ; Clamp to 0..39
        LDA fill_x1
        BMI @clamp_l
        CMP #40
        BCC @no_clamp_l
@clamp_l:
        LDA #0
        STA fill_x1
@no_clamp_l:
        LDA fill_x2
        CMP #40
        BCC @no_clamp_r
        LDA #39
        STA fill_x2
@no_clamp_r:

        ; Fill this row
        LDX cur_y
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi

        LDY fill_x1
@tcol:  TYA
        AND #$01
        BNE @to
        LDA fill_even
        JMP @tw
@to:    LDA fill_odd
@tw:    STA (ptr_lo),Y
        INY
        CPY fill_x2
        BCC @tcol
        BEQ @tcol

        ; Bresenham step: accum += step. If accum >= span, accum -= span, hw++
        LDA tri_accum
        CLC
        ADC tri_step
        STA tri_accum
        CMP tri_span
        BCC @no_inc
        ; accum >= span → increment half_width
        SEC
        SBC tri_span
        STA tri_accum
        INC tri_hw
@no_inc:
        INC cur_y
        LDA cur_y
        CMP fill_y2
        BCC @ty
        BEQ @ty
        RTS

; =============================================
; print_str_ax — promoted to dev/lib/apple1/print.asm (Tier 2 mutualization).
; =============================================
.include "print.asm"

; =============================================
; STRINGS
; =============================================
str_title:
        .byte $0D, " * HGR HOUSE & TREE *", $0D
        .byte " GEN2 COLOR GRAPHICS CARD", $0D
        .byte " NTSC ARTIFACT COLORS", $0D
        .byte $0D, " PRESS ANY KEY...", $0D, 0
str_done:
        .byte " DONE. KEY=REDRAW", $0D, 0

; =============================================
; HGR TABLES
; =============================================
.include "hgr_tables.inc"
.include "multiply.asm"
