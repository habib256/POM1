; =============================================
; GT-6144 HELLO — clear + centered two-line text
; SWTPC GT-6144 Graphic Terminal (1976, $D00A)
; VERHILLE Arnaud - 2026
; =============================================
; Clears the 64x96 monochrome framebuffer (Intel 2102 bistable SRAM
; power-on noise) then draws two centered lines with a 3x5-pixel font:
;
;     APPLE-1
;     GT-6144
;
; Prints a one-line confirmation on the Apple-1 text screen and
; hands control back to the Woz Monitor automatically (no keypress
; required). The GT-6144 framebuffer survives the transition because
; Wozmon only touches $D010/$D011 (keyboard) and $D012 (display) —
; never $D00A.
;
; Assemble:
;   ca65 -o build/GT1_Hello.o software/GT-6144/GT1_Hello.asm
;   ld65 -C software/GT-6144/gt6144.cfg -o build/GT1_Hello.bin build/GT1_Hello.o
;
; Load via Wozmon (once built):
;   300: <paste hex>
;   300R
;
; Requires the GT-6144 to be plugged:
;   ./POM1 --preset 5               (SWTPC preset)
;   ./POM1 --enable gt6144          (any other preset)
;
; Protocol recap (see GT6144.h):
;   byte < 64       : latch X = byte,     pixel OFF
;   64 <= byte <128 : latch X = byte-64,  pixel ON
;   128 <= byte<224 : commit Y = byte-128 using latched X + state
;   224 <= byte     : control opcode (not used here)
; =============================================

.include "gt6144.inc"
.include "apple1.inc"
; Woz Monitor GETLINE entry. $FF1A prints '\' (the prompt) + CR and falls
; through to the keyboard-wait loop. $FF1F is tempting (it's the label at
; LDA #$8D / JSR ECHO) but it SKIPS the '\' print — the user then sees no
; prompt and thinks the machine rebooted. Always enter at $FF1A.

; ---- Zero-page scratch ----
STR_LO    = $20
STR_HI    = $21
STR_IDX   = $22
COL_X     = $23
COL_Y     = $24
FONT_LO   = $25
FONT_HI   = $26
ROW_BYTE  = $27
MUL_TMP   = $28

    .org $0300

; =============================================
; Entry point
; =============================================
START:
    jsr CLR_SCREEN

    ; Line 1: "APPLE-1" — 7 glyphs x 4 px = 28 px wide
    lda #<STR_APPLE
    sta STR_LO
    lda #>STR_APPLE
    sta STR_HI
    lda #18
    sta COL_X
    lda #38
    sta COL_Y
    jsr DRAW_STR

    ; Line 2: "GT-6144" right below
    lda #<STR_GT
    sta STR_LO
    lda #>STR_GT
    sta STR_HI
    lda #18
    sta COL_X
    lda #48
    sta COL_Y
    jsr DRAW_STR

    ; Print a confirmation message on the Apple-1 text screen, then hand
    ; control back to Woz Monitor immediately (no keypress required).
    ; The Apple-1 display auto-scrolls so the graphic output on the
    ; GT-6144 is undisturbed (Wozmon never writes $D00A).
    ;
    ; Note: plain RTS is NOT usable here. Wozmon's R command runs the
    ; program via JMP (XAML), so the stack has no return address. An RTS
    ; would pop garbage and let the CPU run wild, eventually scribbling
    ; random bytes into $D00A (control opcodes 240/245 flip the video
    ; mode; stray OFF-latch + Y-commit pairs erase the text) — the
    ; "screen blinks then disappears" symptom. We must JMP WOZMON.
    ;
    ; The Apple-1 PIA's DSP register has bit 7 as a "busy" flag: it is
    ; driven by the terminal-card 60 Hz /RDA and stays HIGH while the
    ; previous character is still being painted. We spin on BIT DSP
    ; until BPL falls through (bit 7 = 0 = ready) before each write.
    ; Characters are sent with bit 7 set — Apple-1 convention so the
    ; signetics terminal driver treats them as valid ASCII.
    ldy #0
PRINT_LOOP:
    lda MSG,y
    beq PRINT_DONE           ; $00 terminator (tested BEFORE ORA so the
                             ;  bit-7 OR below can't hide the null byte)
    ora #$80                 ; Apple-1 convention: chars written to DSP
                             ;  carry bit 7 set so the signetics terminal
                             ;  driver treats them as valid ASCII
PRINT_WAIT:
    bit DSP
    bmi PRINT_WAIT           ; bit 7 = 1 => display busy, spin
    sta DSP
    iny
    jmp PRINT_LOOP
PRINT_DONE:
    jmp WOZMON               ; return control; Wozmon prints \ + CR

; =============================================
; CLR_SCREEN — OFF at every (x, y) in 64 x 96.
;   Latch each X column once, then commit all 96 Y values.
;   Stop before 224 (control-opcode range).
; =============================================
CLR_SCREEN:
    ldx #0
@xloop:
    stx GT_PORT          ; X < 64 => latch X, mode=OFF
    ldy #128             ; Y=0 encoded as 128
@yloop:
    sty GT_PORT          ; commit pixel (X, Y-128) = OFF
    iny
    cpy #224
    bne @yloop
    inx
    cpx #64
    bne @xloop
    rts

; =============================================
; DRAW_STR — plot the $FF-terminated glyph-index string at (STR_LO/HI).
;   Glyph width 3 + 1-px gap = 4 px per character.
; =============================================
DRAW_STR:
    lda #0
    sta STR_IDX
@next:
    ldy STR_IDX
    lda (STR_LO),y
    cmp #$FF
    beq @done
    jsr DRAW_CH          ; A = glyph index
    inc STR_IDX
    lda COL_X
    clc
    adc #4               ; advance pen
    sta COL_X
    jmp @next
@done:
    rts

; =============================================
; DRAW_CH — draw a 3x5 glyph at (COL_X, COL_Y).
;   A = glyph index. FONTS is 5 bytes per glyph.
;   Leftmost column = bit 2, rightmost = bit 0.
; =============================================
DRAW_CH:
    sta MUL_TMP
    asl a                ; A*2
    asl a                ; A*4
    clc
    adc MUL_TMP          ; A*4 + A = A*5
    clc
    adc #<FONTS
    sta FONT_LO
    lda #0
    adc #>FONTS
    sta FONT_HI

    ldy #0               ; row 0..4
@rloop:
    lda (FONT_LO),y
    sta ROW_BYTE
    ldx #0               ; col 0..2
@cloop:
    lda COL_BIT,x
    and ROW_BYTE
    beq @skip
    ; Plot ON pixel at (COL_X + x, COL_Y + y)
    txa
    clc
    adc COL_X
    ora #64              ; bit 6 => latch X, mode=ON
    sta GT_PORT
    tya
    clc
    adc COL_Y
    ora #128             ; bit 7 => commit Y
    sta GT_PORT
@skip:
    inx
    cpx #3
    bne @cloop
    iny
    cpy #5
    bne @rloop
    rts

; =============================================
; Data
; =============================================
COL_BIT: .byte $04, $02, $01     ; col 0 = bit 2, col 1 = bit 1, col 2 = bit 0

; 3x5 font — only the 10 glyphs used by this demo.
; Each row = 3 bits, leftmost column in bit 2.
FONTS:
    .byte $02, $05, $07, $05, $05   ; 0 : A
    .byte $06, $05, $06, $04, $04   ; 1 : P
    .byte $04, $04, $04, $04, $06   ; 2 : L
    .byte $07, $04, $06, $04, $07   ; 3 : E
    .byte $00, $00, $07, $00, $00   ; 4 : -
    .byte $02, $06, $02, $02, $07   ; 5 : 1
    .byte $03, $04, $05, $05, $03   ; 6 : G
    .byte $07, $02, $02, $02, $02   ; 7 : T
    .byte $03, $04, $06, $05, $06   ; 8 : 6
    .byte $05, $05, $07, $01, $01   ; 9 : 4

; Glyph-index strings ($FF terminator).
;              A  P  P  L  E  -  1
STR_APPLE: .byte 0, 1, 1, 2, 3, 4, 5, $FF
;              G  T  -  6  1  4  4
STR_GT:    .byte 6, 7, 4, 8, 5, 9, 9, $FF

; Apple-1 text-screen message. Plain ASCII — the print loop ORs $80 on
; each char before writing DSP (Apple-1 convention). $0D = CR, $00 =
; end-of-string terminator (BEQ fires before the ORA so the null byte
; cleanly ends the loop).
MSG:
    .byte $0D
    .byte "GT-6144 DEMO: APPLE-1 + GT-6144 DRAWN", $0D
    .byte "ON THE SWTPC 64X96 GRAPHIC SCREEN.", $0D
    .byte $00
