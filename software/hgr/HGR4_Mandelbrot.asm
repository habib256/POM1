; =============================================
; HGR MANDELBROT SET (NTSC color, optimized)
; GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; 4.12 fixed-point, byte-column rendering
; =============================================
; Optimizations:
;   1. Y-axis symmetry: compute 96 rows, mirror bottom half (~2× speedup)
;   2. Identity 2*zr*zi = (zr+zi)² - zr² - zi² (replaces general multiply)
;   3. ssquare16: squaring skips sign handling (result always positive)
;
; ~40 sec at 1 MHz, ~1 sec at Max speed.
; Window: cr = -2.0..+0.7, ci = -1.2..+1.2
; =============================================

; --- Apple 1 I/O ---
ECHO    = $FFEF
KBDCR   = $D011
KBD     = $D010

; --- Fixed-point 4.12 constants ---
CR_MIN_LO  = $00       ; -2.0 × 4096 = $E000
CR_MIN_HI  = $E0
CR_STEP_LO = $14       ; 2.7 × 4096 / 40 = 276 = $0114
CR_STEP_HI = $01
CI_MIN_LO  = $CD       ; -1.2 × 4096 = $ECCD
CI_MIN_HI  = $EC
CI_STEP    = 51         ; 2.4 × 4096 / 192 ≈ 51
ESCAPE_HI  = $40        ; 4.0 in 4.12 = $4000
MAX_ITER   = 16
HALF_H     = 96         ; render top half only, mirror bottom

; --- Zero page variables ---
.zeropage
            .res 2      ; $00-$01
cur_x:      .res 1      ; $02
cur_y:      .res 1      ; $03
ptr_lo:     .res 1      ; $04
ptr_hi:     .res 1      ; $05
cr_lo:      .res 1      ; $06
cr_hi:      .res 1      ; $07
ci_lo:      .res 1      ; $08
ci_hi:      .res 1      ; $09
zr_lo:      .res 1      ; $0A
zr_hi:      .res 1      ; $0B
zi_lo:      .res 1      ; $0C
zi_hi:      .res 1      ; $0D
zr2_lo:     .res 1      ; $0E
zr2_hi:     .res 1      ; $0F
zi2_lo:     .res 1      ; $10
zi2_hi:     .res 1      ; $11
iter:       .res 1      ; $12
arg1_lo:    .res 1      ; $13
arg1_hi:    .res 1      ; $14
arg2_lo:    .res 1      ; $15
arg2_hi:    .res 1      ; $16
mul_res0:   .res 1      ; $17
mul_res1:   .res 1      ; $18
mul_res2:   .res 1      ; $19
mul_res3:   .res 1      ; $1A
mul_tmp:    .res 1      ; $1B
col_byte:   .res 1      ; $1C
mirror_y:   .res 1      ; $1D

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

; =============================================
; RENDER (symmetric: top 96 rows mirrored)
; =============================================
render:
        JSR clear_hgr

        LDA #<str_draw
        LDX #>str_draw
        JSR print_str_ax

        LDA #CI_MIN_LO
        STA ci_lo
        LDA #CI_MIN_HI
        STA ci_hi

        LDA #$00
        STA cur_y

@yloop:
        ; Compute mirror row: 191 - cur_y
        LDA #191
        SEC
        SBC cur_y
        STA mirror_y

        ; cr = CR_MIN
        LDA #CR_MIN_LO
        STA cr_lo
        LDA #CR_MIN_HI
        STA cr_hi

        LDA #$00
        STA cur_x

@xloop:
        JSR mandel_iter     ; A = iteration count

        ; --- Color lookup (parity-aware) ---
        TAX
        LDA cur_x
        AND #$01
        BNE @odd
        LDA color_even,X
        JMP @got
@odd:   LDA color_odd,X
@got:   STA col_byte

        ; --- Write to scanline cur_y ---
        LDX cur_y
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi
        LDA col_byte
        LDY cur_x
        STA (ptr_lo),Y

        ; --- Write to mirror scanline (191 - cur_y) ---
        LDX mirror_y
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi
        LDA col_byte
        LDY cur_x
        STA (ptr_lo),Y

        ; --- Advance cr ---
        LDA cr_lo
        CLC
        ADC #CR_STEP_LO
        STA cr_lo
        LDA cr_hi
        ADC #CR_STEP_HI
        STA cr_hi

        INC cur_x
        LDA cur_x
        CMP #40
        BNE @xloop

        ; --- Advance ci ---
        LDA ci_lo
        CLC
        ADC #CI_STEP
        STA ci_lo
        BCC @nci
        INC ci_hi
@nci:
        INC cur_y
        LDA cur_y
        CMP #HALF_H         ; only top half (96 rows)
        BNE @yloop

        LDA #<str_done
        LDX #>str_done
        JSR print_str_ax
@wk:    LDA KBDCR
        BPL @wk
        LDA KBD
        JMP render

; =============================================
; MANDEL_ITER: z = z² + c (optimized)
; Uses identity: 2*zr*zi = (zr+zi)² - zr² - zi²
; All multiplies are squarings (ssquare16).
; =============================================
mandel_iter:
        LDA #$00
        STA zr_lo
        STA zr_hi
        STA zi_lo
        STA zi_hi
        STA iter

@mloop:
        ; --- zr² ---
        LDA zr_lo
        STA arg1_lo
        LDA zr_hi
        STA arg1_hi
        JSR ssquare16
        LDA mul_res1
        STA zr2_lo
        LDA mul_res2
        STA zr2_hi

        ; --- zi² ---
        LDA zi_lo
        STA arg1_lo
        LDA zi_hi
        STA arg1_hi
        JSR ssquare16
        LDA mul_res1
        STA zi2_lo
        LDA mul_res2
        STA zi2_hi

        ; --- Escape test: zr² + zi² > 4.0 ---
        LDA zr2_lo
        CLC
        ADC zi2_lo
        PHA
        LDA zr2_hi
        ADC zi2_hi
        BMI @escaped
        CMP #ESCAPE_HI
        BCS @escaped
        PLA

        ; --- (zr + zi)² ---
        LDA zr_lo
        CLC
        ADC zi_lo
        STA arg1_lo
        LDA zr_hi
        ADC zi_hi
        STA arg1_hi
        JSR ssquare16
        ; mul_res1:mul_res2 = (zr+zi)²

        ; --- 2*zr*zi = (zr+zi)² - zr² - zi² ---
        LDA mul_res1
        SEC
        SBC zr2_lo
        STA mul_res1
        LDA mul_res2
        SBC zr2_hi
        STA mul_res2

        LDA mul_res1
        SEC
        SBC zi2_lo
        STA mul_res1
        LDA mul_res2
        SBC zi2_hi
        STA mul_res2

        ; --- zi_new = 2*zr*zi + ci ---
        LDA mul_res1
        CLC
        ADC ci_lo
        STA zi_lo
        LDA mul_res2
        ADC ci_hi
        STA zi_hi

        ; --- zr_new = (zr² - zi²) + cr ---
        LDA zr2_lo
        SEC
        SBC zi2_lo
        STA zr_lo
        LDA zr2_hi
        SBC zi2_hi
        STA zr_hi
        LDA zr_lo
        CLC
        ADC cr_lo
        STA zr_lo
        LDA zr_hi
        ADC cr_hi
        STA zr_hi

        INC iter
        LDA iter
        CMP #MAX_ITER
        BEQ @maxed
        JMP @mloop
@maxed: LDA #MAX_ITER
        RTS

@escaped:
        PLA
        LDA iter
        RTS

; =============================================
; SSQUARE16: compute arg1² (unsigned result)
; Faster than smul16: always positive, no sign fixup.
; Input: arg1_lo/hi
; Output: mul_res1/res2 = 4.12 result
; =============================================
ssquare16:
        ; Make arg1 positive (result is always positive for a²)
        LDA arg1_hi
        BPL @pos
        LDA arg1_lo
        EOR #$FF
        CLC
        ADC #1
        STA arg1_lo
        LDA arg1_hi
        EOR #$FF
        ADC #0
        STA arg1_hi
@pos:
        ; arg2 = arg1 (squaring)
        LDA arg1_lo
        STA arg2_lo
        LDA arg1_hi
        STA arg2_hi

        ; Clear result
        LDA #$00
        STA mul_res0
        STA mul_res1
        STA mul_res2
        STA mul_res3

        ; P1: lo × lo
        LDA arg1_lo
        LDX arg2_lo
        JSR umul8
        STA mul_res0
        STX mul_res1

        ; P2: hi × lo (done once, doubled for P2+P3 since arg1=arg2)
        LDA arg1_hi
        LDX arg1_lo
        JSR umul8
        ; Add twice (P2 + P3 are identical for squaring)
        ASL A
        STA mul_tmp         ; save doubled lo
        TXA
        ROL A               ; doubled hi (with carry from ASL)
        TAX                 ; X = doubled hi
        LDA mul_tmp
        CLC
        ADC mul_res1
        STA mul_res1
        TXA
        ADC mul_res2
        STA mul_res2
        BCC @n23
        INC mul_res3
@n23:
        ; P4: hi × hi
        LDA arg1_hi
        LDX arg1_hi
        JSR umul8
        CLC
        ADC mul_res2
        STA mul_res2
        TXA
        ADC mul_res3
        STA mul_res3

        ; Convert 8.24 → 4.12: shift right 4
        LDX #4
@fp:    LSR mul_res3
        ROR mul_res2
        ROR mul_res1
        DEX
        BNE @fp
        RTS

; =============================================
; COLOR TABLES (parity-aware for consistent NTSC colors)
; =============================================
color_even:
        .byte  $7F,  $7F,  $7F,  $55,  $55,  $55,  $2A,  $2A
        .byte  $2A,  $14,  $14,  $08,  $08,  $04,  $02,  $01,  $00
color_odd:
        .byte  $7F,  $7F,  $7F,  $2A,  $2A,  $2A,  $55,  $55
        .byte  $55,  $0A,  $0A,  $08,  $08,  $04,  $02,  $01,  $00

; =============================================
; Print null-terminated string
; =============================================
print_str_ax:
        STA ptr_lo
        STX ptr_hi
        LDY #$00
@lp:    LDA (ptr_lo),Y
        BEQ @dn
        ORA #$80
        JSR ECHO
        INY
        BNE @lp
@dn:    RTS

; =============================================
; STRINGS
; =============================================
str_title:
        .byte $0D, " * HGR MANDELBROT *", $0D
        .byte " GEN2 COLOR GRAPHICS CARD", $0D
        .byte " OPTIMIZED: SYMMETRY + SQUARE ID", $0D
        .byte " USE MAX SPEED FOR FAST RENDER", $0D
        .byte $0D, " PRESS ANY KEY...", $0D, 0
str_draw:
        .byte " RENDERING...", $0D, 0
str_done:
        .byte " DONE. KEY=REDRAW", $0D, 0

; =============================================
; HGR TABLES
; =============================================
.include "hgr_tables.inc"
