; =============================================
; CodeTank lower-bank menu — picks between TMS_A1GALAGA, TMS_SOKOBAN
; and TMS_SNAKE. All three share the lower 16 kB bank ($4000-$7FFF
; when CodeTank board jumper = Lower).
;
; Layout in the lower bank (Codetank_GAME1.rom):
;   $4000-$40FF  Menu (this file)                   (256 B)
;   $4100-$5FFF  TMS_A1GALAGA   (linked at $4100)   (7 936 B slot)
;   $6000-$73FF  TMS_SOKOBAN    (linked at $6000)   (5 120 B slot)
;   $7400-$7DFF  TMS_SNAKE      (linked at $7400)   (2 560 B slot)
;   $7E00-$7FFF  unused / padding                   (512 B)
;
; Life moved to its own cartridge (Codetank_GAME3.rom). The upper 16 kB
; bank ships TMS_LOGO V2.6 (turtle interpreter) at $4000 — flip the
; CodeTank board jumper to "Upper" and type 4000R from Wozmon to launch
; the REPL.
;
; Wozmon entry: 4000R after plugging the CodeTank card.
; =============================================

; --- Apple 1 I/O ---
KBD    = $D010
KBDCR  = $D011
ECHO   = $FFEF

; --- Game entry points (must match the linker configs):
;     apple1_galaga_codetank_bank.cfg   start=$4100
;     apple1_sokoban_codetank_bank.cfg  start=$6000
;     apple1_snake_codetank_bank.cfg    start=$7400
GALAGA_ENTRY  = $4100
SOKOBAN_ENTRY = $6000
SNAKE_ENTRY   = $7400

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
        BEQ @go_galaga
        CMP #('2' | $80)
        BEQ @go_sokoban
        CMP #('3' | $80)
        BEQ @go_snake
        JMP @wait_key

@go_galaga:
        JMP GALAGA_ENTRY

@go_sokoban:
        JMP SOKOBAN_ENTRY

@go_snake:
        JMP SNAKE_ENTRY

; --- Prompt string. NUL-terminated; print loop ORs in bit 7 for the
;     Apple-1 display. $0D = CR (Apple-1 wraps + line-feeds on its own).
prompt:
        .byte $0D
        .byte "P-LAB CODETANK GAME1", $0D
        .byte $0D
        .byte "1 = GALAGA", $0D
        .byte "2 = SOKOBAN", $0D
        .byte "3 = SNAKE", $0D
        .byte "(LOGO V2 ON UPPER JUMPER)", $0D
        .byte $0D
        .byte "PICK 1, 2 OR 3 ? "
        .byte 0
