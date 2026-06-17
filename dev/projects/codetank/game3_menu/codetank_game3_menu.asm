; =============================================
; CodeTank GAME3 upper-bank menu — picks between TMS_Life, TMS_Mandel,
; and TMS_Plasma. Lives in Codetank_GAME3.rom upper bank ($4000-$7FFF
; when CodeTank board jumper = Upper).
;
; Layout in the upper bank (Codetank_GAME3.rom):
;   $4000-$40FF  Menu (this file)               (256 B)
;   $4100-$48FF  TMS_Life  (linked at $4100)    (2 048 B slot, ~1 558 B)
;   $4900-$50FF  TMS_Mandel (linked at $4900)   (2 048 B slot, ~1 571 B)
;   $5100-$58FF  TMS_Plasma (linked at $5100)   (2 048 B slot, ~1 613 B)
;   $5900-$7FFF  reserved (free)
;
; Lower bank ships TMS_Tetris/CodeTank at $4000 — flip jumper to "Lower"
; and type 4000R from Wozmon to launch Tetris.
;
; Wozmon entry: 4000R after plugging the CodeTank card.
; =============================================

; --- Apple 1 I/O ---
.include "apple1.inc"

; --- Game entry points (must match the linker configs):
;     apple1_life_codetank_game3_bank.cfg   start=$4100
;     apple1_mandel_codetank_bank.cfg       start=$4900
;     apple1_plasma_codetank_bank.cfg       start=$5100
LIFE_ENTRY   = $4100
MANDEL_ENTRY = $4900
PLASMA_ENTRY = $5100

.code

start:
        LDX #0
@print: LDA prompt,X
        BEQ @wait_key
        ORA #$80                ; Apple-1 display wants bit 7 set
        JSR ECHO
        INX
        BNE @print

@wait_key:
        LDA KBDCR
        BPL @wait_key           ; KBDCR bit 7 = 1 when a key is ready
        LDA KBD                 ; bit 7 always set on Apple-1 keyboard
        CMP #('1' | $80)
        BEQ @go_life
        CMP #('2' | $80)
        BEQ @go_mandel
        CMP #('3' | $80)
        BEQ @go_plasma
        JMP @wait_key

@go_life:
        JMP LIFE_ENTRY

@go_mandel:
        JMP MANDEL_ENTRY

@go_plasma:
        JMP PLASMA_ENTRY

; --- Prompt string. NUL-terminated; print loop ORs in bit 7 for the
;     Apple-1 display. $0D = CR (Apple-1 wraps + line-feeds on its own).
prompt:
        .byte $0D
        .byte "P-LAB CODETANK GAME3", $0D
        .byte $0D
        .byte "1 = LIFE   (CONWAY GAME OF LIFE)", $0D
        .byte "2 = MANDEL (MANDELBROT SET)", $0D
        .byte "3 = PLASMA (CYCLING PLASMA EFFECT)", $0D
        .byte "(TETRIS ON LOWER JUMPER)", $0D
        .byte $0D
        .byte "PICK 1, 2 OR 3 ? "
        .byte 0
