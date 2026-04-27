; =============================================
; CodeTank UPPER-bank dispatcher menu.
;
; Replaces the Tetris-only auto-loader at $4000 with a 1-key prompt:
;
;   1 = TETRIS  (copy 7 308 B payload from $4080 to $0280, JMP $0280)
;   2 = LOGO    (JMP $5E00, runs in place from ROM)
;
; Layout in the upper 16 kB bank ($4000-$7FFF when CodeTank board jumper
; = Upper):
;
;   $4000-$407F  This menu (≤128 B, fits in the slot ahead of Tetris)
;   $4080-$5DAB  Tetris raw payload (7 308 B, unchanged from before)
;   $5DAC-$5DFF  $FF padding
;   $5E00-$76FF  LOGO V1.8 ROM
;   $7700-$7FFF  $FF padding
;
; Wozmon entry: 4000R after plugging the CodeTank card with the Upper
; jumper. Pick 1 or 2 at the prompt.
; =============================================

; --- Apple 1 I/O + monitor ROM ---
KBD    = $D010
KBDCR  = $D011
ECHO   = $FFEF

; --- Targets ---
LOGO_ENTRY = $5E00          ; LOGO ROM start, runs in place
TETRIS_DST = $0280          ; Tetris in RAM after copy

; --- Tetris payload location in ROM (CPU $4080 when jumper = Upper) ---
SRC_LO   = $80
SRC_HI   = $40
DST_LO   = <TETRIS_DST
DST_HI   = >TETRIS_DST
NPAGES   = 29               ; (7308 + 255) >> 8 = 29 pages

.code

start:
        ; --- print prompt ---
        LDX #0
@print: LDA prompt,X
        BEQ @wait
        ORA #$80                ; Apple-1 display wants bit 7 set
        JSR ECHO
        INX
        BNE @print

@wait:
        LDA KBDCR
        BPL @wait               ; bit 7 = 1 when key ready
        LDA KBD
        AND #$7F
        CMP #'1'
        BEQ @run_tetris
        CMP #'2'
        BEQ @run_logo
        JMP @wait               ; ignore anything else

@run_logo:
        JMP LOGO_ENTRY          ; LOGO runs in place from ROM at $5E00

@run_tetris:
        ; Copy NPAGES pages of payload from $4080 down to $0280.
        ; Same algorithm as the original tetris_loader.asm.
        LDA #SRC_LO
        STA $00
        LDA #SRC_HI
        STA $01
        LDA #DST_LO
        STA $02
        LDA #DST_HI
        STA $03
        LDX #NPAGES
        LDY #0
@page:  LDA ($00),Y
        STA ($02),Y
        INY
        BNE @page               ; 256 bytes per inner pass
        INC $01
        INC $03
        DEX
        BNE @page
        JMP TETRIS_DST          ; hand off to Tetris

prompt:
        .byte $0D, "1=TETRIS  2=LOGO", $0D, 0
