; =============================================
; HGR SIERPINSKI TRIANGLE
; GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; Chaos Game (random midpoint algorithm)
; 280x192 HGR screen, NTSC artifact colors
; =============================================
; Assemble with cc65:
;   ca65 -o build/HGR2_Sierpinski.o software/hgr/HGR2_Sierpinski.asm
;   ld65 -C software/hgr/apple1_gen2.cfg -o build/HGR2_Sierpinski.bin build/HGR2_Sierpinski.o
;
; In POM1: plug GEN2 card, File > Load Memory (HGR2_Sierpinski.txt)
; then type 280R in Woz Monitor.
;
; The chaos game iterates: pick a random vertex,
; move halfway there, plot the pixel. After a few
; thousand iterations, the Sierpinski triangle emerges.
; Press any key to regenerate with a new random seed.
; =============================================

; --- Apple 1 I/O ---
ECHO    = $FFEF
KBDCR   = $D011
KBD     = $D010

; --- Triangle vertices (8-bit coords) ---
V0_X    = 128           ; top center
V0_Y    = 5
V1_X    = 5             ; bottom left
V1_Y    = 186
V2_X    = 251           ; bottom right
V2_Y    = 186

; --- Zero page variables ---
.zeropage
prng_lo:    .res 1      ; $00
prng_hi:    .res 1      ; $01
cur_x:      .res 1      ; $02
cur_y:      .res 1      ; $03
ptr_lo:     .res 1      ; $04
ptr_hi:     .res 1      ; $05
temp:       .res 1      ; $06
iter_lo:    .res 1      ; $07
iter_hi:    .res 1      ; $08

; --- Code at $0280 ---
.code

; =============================================
; MAIN
; =============================================
main:
        LDA #$01
        STA prng_lo
        LDA #$C7
        STA prng_hi

        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax

        ; Wait for key (timing seeds PRNG)
@wait:  INC prng_lo
        BNE @w1
        INC prng_hi
@w1:    LDA KBDCR
        BPL @wait
        LDA KBD
        EOR prng_lo
        STA prng_lo

; =============================================
; RESTART: generate a new triangle
; =============================================
restart:
        JSR clear_hgr

        ; Start at approximate centroid
        LDA #128
        STA cur_x
        LDA #126
        STA cur_y

        LDA #$00
        STA iter_lo
        STA iter_hi

        LDA #<str_run
        LDX #>str_run
        JSR print_str_ax

; =============================================
; CHAOS LOOP: iterate the random midpoint game
; =============================================
chaos_loop:
        ; Pick random vertex (0, 1, or 2)
@retry: JSR random
        AND #$03
        CMP #$03
        BEQ @retry

        CMP #$01
        BEQ @do_v1
        CMP #$02
        BEQ @do_v2

        ; Vertex 0: top center (128, 5)
        LDA cur_x
        CLC
        ADC #V0_X
        ROR A
        STA cur_x
        LDA cur_y
        CLC
        ADC #V0_Y
        ROR A
        STA cur_y
        JMP @plot

@do_v1: ; Vertex 1: bottom left (5, 186)
        LDA cur_x
        CLC
        ADC #V1_X
        ROR A
        STA cur_x
        LDA cur_y
        CLC
        ADC #V1_Y
        ROR A
        STA cur_y
        JMP @plot

@do_v2: ; Vertex 2: bottom right (251, 186)
        LDA cur_x
        CLC
        ADC #V2_X
        ROR A
        STA cur_x
        LDA cur_y
        CLC
        ADC #V2_Y
        ROR A
        STA cur_y

@plot:
        ; Skip first 10 iterations (convergence)
        LDA iter_hi
        BNE @do_plot
        LDA iter_lo
        CMP #10
        BCC @skip
@do_plot:
        JSR plot_pixel
@skip:
        ; Increment counter
        INC iter_lo
        BNE @no_hi
        INC iter_hi
@no_hi:
        ; Check keypress every 256 iterations
        LDA iter_lo
        BNE chaos_loop
        LDA KBDCR
        BPL chaos_loop

        ; Key pressed — new triangle
        LDA KBD
        EOR prng_lo
        STA prng_lo

        LDA #<str_new
        LDX #>str_new
        JSR print_str_ax

@wk:    INC prng_lo
        BNE @wk2
        INC prng_hi
@wk2:   LDA KBDCR
        BPL @wk
        LDA KBD
        EOR prng_lo
        STA prng_lo
        JMP restart

; =============================================
; PLOT PIXEL at (cur_x, cur_y)
; =============================================
plot_pixel:
        JSR calc_scanline

        ; Divide cur_x by 7: quotient = byte column, remainder = bit pos
        LDA cur_x
        LDX #$00
@div7:  CMP #7
        BCC @ddone
        SBC #7
        INX
        BCS @div7
@ddone: TAY                 ; Y = bit position (0-6)
        LDA bit_mask,Y      ; A = pixel bitmask
        STX temp             ; save byte column
        LDY temp             ; Y = byte column for (ptr),Y
        ORA (ptr_lo),Y       ; merge with existing pixels
        STA (ptr_lo),Y
        RTS

; =============================================
; CALC SCANLINE: compute HGR base addr for cur_y
; Non-linear Apple II layout:
;   addr = $2000 + (y%8)*$400 + ((y/8)%8)*$80 + (y/64)*$28
; Result in ptr_lo, ptr_hi
; =============================================
calc_scanline:
        LDA #$00
        STA ptr_lo
        LDA #$20
        STA ptr_hi

        ; (y % 8) * $0400 → add to ptr_hi
        LDA cur_y
        AND #$07
        ASL A
        ASL A
        CLC
        ADC ptr_hi
        STA ptr_hi

        ; ((y / 8) % 8) * $80 → split into lo/hi
        LDA cur_y
        LSR A
        LSR A
        LSR A
        AND #$07
        LSR A               ; A = val/2, carry = val bit 0
        TAX
        LDA #$00
        BCC @no80
        LDA #$80
@no80:  CLC
        ADC ptr_lo
        STA ptr_lo
        TXA
        ADC ptr_hi
        STA ptr_hi

        ; Group offset: y<64 → 0, y<128 → $28, y>=128 → $50
        LDA cur_y
        CMP #128
        BCS @g2
        CMP #64
        BCS @g1
        RTS
@g1:    LDA ptr_lo
        CLC
        ADC #$28
        STA ptr_lo
        BCC @gd
        INC ptr_hi
@gd:    RTS
@g2:    LDA ptr_lo
        CLC
        ADC #$50
        STA ptr_lo
        BCC @gd2
        INC ptr_hi
@gd2:   RTS

; =============================================
; CLEAR HGR: zero $2000-$3FFF
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

; =============================================
; 16-bit Galois LFSR
; =============================================
random:
        LDA prng_lo
        ASL A
        ROL prng_hi
        BCC @nf
        EOR #$2D
@nf:    STA prng_lo
        RTS

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
; DATA
; =============================================
bit_mask:
        .byte $01, $02, $04, $08, $10, $20, $40

str_title:
        .byte $0D, " * HGR SIERPINSKI *", $0D
        .byte " GEN2 COLOR GRAPHICS CARD", $0D
        .byte " CHAOS GAME ALGORITHM", $0D
        .byte $0D, " PRESS ANY KEY...", $0D, 0

str_run:
        .byte " DRAWING (KEY=NEW)", $0D, 0

str_new:
        .byte $0D, " PRESS ANY KEY...", $0D, 0
