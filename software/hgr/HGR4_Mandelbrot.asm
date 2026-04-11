; =============================================
; HGR MANDELBROT SET (NTSC color)
; GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; 4.12 fixed-point, byte-column rendering
; =============================================
; Assemble:
;   ca65 -o build/HGR4_Mandelbrot.o software/hgr/HGR4_Mandelbrot.asm
;   ld65 -C software/hgr/apple1_gen2.cfg -o build/HGR4_Mandelbrot.bin build/HGR4_Mandelbrot.o
;
; Renders at byte-column resolution (40 × 192 = 7680 points).
; Each Mandelbrot point fills 7 pixels (one HGR byte) with a
; NTSC artifact color based on the escape iteration count.
;
; ~4 min at 1 MHz, ~4 sec at Max speed.
; Window: cr = -2.0..+0.7, ci = -1.2..+1.2
; =============================================

; --- Apple 1 I/O ---
ECHO    = $FFEF
KBDCR   = $D011
KBD     = $D010

; --- Fixed-point 4.12 constants ---
; cr ranges over 40 byte columns (280 pixels), mapped to -2.0..+0.7
; ci ranges over 192 scanlines, mapped to -1.2..+1.2
CR_MIN_LO = $00         ; -2.0 × 4096 = -8192 = $E000
CR_MIN_HI = $E0
; Step per byte column: 2.7 × 4096 / 40 = 276 = $0114
CR_STEP_LO = $14
CR_STEP_HI = $01
CI_MIN_LO  = $CD        ; -1.2 × 4096 = -4915 = $ECCD
CI_MIN_HI  = $EC
; Step per scanline: 2.4 × 4096 / 192 ≈ 51 = $0033
CI_STEP    = 51

ESCAPE_HI = $40         ; 4.0 in 4.12 = $4000
MAX_ITER   = 16

; --- Zero page variables ---
.zeropage
; HGR
            .res 2      ; $00-$01
cur_x:      .res 1      ; $02 (byte column 0-39, used for scanline write)
cur_y:      .res 1      ; $03
ptr_lo:     .res 1      ; $04
ptr_hi:     .res 1      ; $05

; Mandelbrot
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

; Multiply workspace
arg1_lo:    .res 1      ; $13
arg1_hi:    .res 1      ; $14
arg2_lo:    .res 1      ; $15
arg2_hi:    .res 1      ; $16
mul_res0:   .res 1      ; $17
mul_res1:   .res 1      ; $18
mul_res2:   .res 1      ; $19
mul_res3:   .res 1      ; $1A
mul_tmp:    .res 1      ; $1B
col_byte:   .res 1      ; $1C  HGR byte pattern for current pixel

; --- Code at $0280 ---
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
; RENDER
; =============================================
render:
        JSR clear_hgr

        LDA #<str_draw
        LDX #>str_draw
        JSR print_str_ax

        ; ci = CI_MIN
        LDA #CI_MIN_LO
        STA ci_lo
        LDA #CI_MIN_HI
        STA ci_hi

        LDA #$00
        STA cur_y

@yloop:
        ; cr = CR_MIN
        LDA #CR_MIN_LO
        STA cr_lo
        LDA #CR_MIN_HI
        STA cr_hi

        LDA #$00
        STA cur_x           ; byte column 0..39

@xloop:
        ; --- Compute Mandelbrot iteration for (cr, ci) ---
        JSR mandel_iter     ; returns iteration count in A

        ; --- Map iteration to NTSC color byte ---
        TAX
        LDA color_table,X
        STA col_byte

        ; --- Write byte to HGR framebuffer at (cur_x, cur_y) ---
        LDX cur_y
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi
        LDA col_byte
        LDY cur_x
        STA (ptr_lo),Y

        ; --- Advance cr += CR_STEP (16-bit) ---
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

        ; --- Advance ci += CI_STEP (8-bit step, sign-extend) ---
        LDA ci_lo
        CLC
        ADC #CI_STEP
        STA ci_lo
        BCC @nci
        INC ci_hi
@nci:
        INC cur_y
        LDA cur_y
        CMP #192
        BNE @yloop

        ; Done
        LDA #<str_done
        LDX #>str_done
        JSR print_str_ax

@wk:    LDA KBDCR
        BPL @wk
        LDA KBD
        JMP render

; =============================================
; MANDEL_ITER: iterate z = z² + c
; Input: cr_lo/hi, ci_lo/hi (4.12 fixed-point)
; Output: A = iteration count (0..MAX_ITER)
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
        STA arg2_lo
        LDA zr_hi
        STA arg1_hi
        STA arg2_hi
        JSR smul16
        LDA mul_res1
        STA zr2_lo
        LDA mul_res2
        STA zr2_hi

        ; --- zi² ---
        LDA zi_lo
        STA arg1_lo
        STA arg2_lo
        LDA zi_hi
        STA arg1_hi
        STA arg2_hi
        JSR smul16
        LDA mul_res1
        STA zi2_lo
        LDA mul_res2
        STA zi2_hi

        ; --- Check escape: zr² + zi² > 4.0 ---
        LDA zr2_lo
        CLC
        ADC zi2_lo
        PHA                 ; save sum low
        LDA zr2_hi
        ADC zi2_hi
        BMI @escaped        ; overflow → escaped
        CMP #ESCAPE_HI
        BCS @escaped
        PLA                 ; discard sum low

        ; --- 2 * zr * zi ---
        LDA zr_lo
        STA arg1_lo
        LDA zr_hi
        STA arg1_hi
        LDA zi_lo
        STA arg2_lo
        LDA zi_hi
        STA arg2_hi
        JSR smul16
        ; ×2: shift left
        ASL mul_res1
        ROL mul_res2

        ; --- zi_new = 2*zr*zi + ci ---
        LDA mul_res1
        CLC
        ADC ci_lo
        STA zi_lo
        LDA mul_res2
        ADC ci_hi
        STA zi_hi

        ; --- zr_new = (zr² - zi²) + cr ---
        ; Step 1: 16-bit subtraction zr² - zi²
        LDA zr2_lo
        SEC
        SBC zi2_lo
        STA zr_lo
        LDA zr2_hi
        SBC zi2_hi
        STA zr_hi
        ; Step 2: 16-bit addition + cr
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
        PLA                 ; clean stack
        LDA iter
        RTS

; =============================================
; SMUL16: signed 16×16 → 32-bit multiply
; Input: arg1 × arg2 (4.12 signed)
; Output: mul_res0..3. Use res1/res2 as 4.12 result.
; =============================================
smul16:
        LDA arg1_hi
        EOR arg2_hi
        PHP                 ; save result sign

        ; abs(arg1)
        LDA arg1_hi
        BPL @a1p
        LDA arg1_lo
        EOR #$FF
        CLC
        ADC #1
        STA arg1_lo
        LDA arg1_hi
        EOR #$FF
        ADC #0
        STA arg1_hi
@a1p:
        ; abs(arg2)
        LDA arg2_hi
        BPL @a2p
        LDA arg2_lo
        EOR #$FF
        CLC
        ADC #1
        STA arg2_lo
        LDA arg2_hi
        EOR #$FF
        ADC #0
        STA arg2_hi
@a2p:
        ; Clear result
        LDA #$00
        STA mul_res0
        STA mul_res1
        STA mul_res2
        STA mul_res3

        ; P1: arg1_lo × arg2_lo → res0:res1
        LDA arg1_lo
        LDX arg2_lo
        JSR umul8
        STA mul_res0
        STX mul_res1

        ; P2: arg1_hi × arg2_lo → +res1:res2
        LDA arg1_hi
        LDX arg2_lo
        JSR umul8
        CLC
        ADC mul_res1
        STA mul_res1
        TXA
        ADC mul_res2
        STA mul_res2
        BCC @n2
        INC mul_res3
@n2:
        ; P3: arg1_lo × arg2_hi → +res1:res2
        LDA arg1_lo
        LDX arg2_hi
        JSR umul8
        CLC
        ADC mul_res1
        STA mul_res1
        TXA
        ADC mul_res2
        STA mul_res2
        BCC @n3
        INC mul_res3
@n3:
        ; P4: arg1_hi × arg2_hi → +res2:res3
        LDA arg1_hi
        LDX arg2_hi
        JSR umul8
        CLC
        ADC mul_res2
        STA mul_res2
        TXA
        ADC mul_res3
        STA mul_res3

        ; Apply sign
        PLP
        BPL @sdone
        LDA mul_res0
        EOR #$FF
        CLC
        ADC #1
        STA mul_res0
        LDA mul_res1
        EOR #$FF
        ADC #0
        STA mul_res1
        LDA mul_res2
        EOR #$FF
        ADC #0
        STA mul_res2
        LDA mul_res3
        EOR #$FF
        ADC #0
        STA mul_res3
@sdone:
        ; Convert 8.24 product → 4.12: shift res3:res2:res1 right by 4 bits
        LDX #4
@fp:    LSR mul_res3
        ROR mul_res2
        ROR mul_res1
        DEX
        BNE @fp
        RTS

; =============================================
; UMUL8: unsigned 8×8 → 16-bit
; Input: A × X → A=low, X=high
; =============================================
umul8:
        STA mul_tmp
        STX mul_res0
        LDA #$00
        LDX #8
@u:     LSR mul_res0
        BCC @na
        CLC
        ADC mul_tmp
@na:    ROR A
        ROR mul_res0
        DEX
        BNE @u
        TAX
        LDA mul_res0
        RTS

; =============================================
; COLOR TABLE: iteration → HGR byte pattern
; 17 entries (0..15 = escaped, 16 = in set → black)
; Alternating NTSC artifact colors create a vivid palette.
; =============================================
color_table:
        ;       iter:  0     1     2     3     4     5     6     7
        .byte  $00,  $2A,  $55,  $7F,  $AA,  $D5,  $FF,  $2A
        ;       iter:  8     9    10    11    12    13    14    15    16(set)
        .byte  $55,  $7F,  $AA,  $D5,  $FF,  $2A,  $55,  $7F,  $00

; =============================================
; Print null-terminated string (A=lo, X=hi)
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
        .byte " 4.12 FIXED-POINT FRACTAL", $0D
        .byte " USE MAX SPEED FOR FAST RENDER", $0D
        .byte $0D, " PRESS ANY KEY...", $0D, 0

str_draw:
        .byte " RENDERING...", $0D, 0

str_done:
        .byte " DONE. KEY=REDRAW", $0D, 0

; =============================================
; HGR TABLES (only hgr_lo/hgr_hi used, not pixel tables)
; =============================================
.include "hgr_tables.inc"
