; =============================================
; CodeTank lower-bank menu — picks between TMS_A1GALAGA, TMS_SOKOBAN
; and TMS_SNAKE. All three share the lower 16 kB bank ($4000-$7FFF
; when CodeTank board jumper = Lower).
;
; Layout in the lower bank (Codetank_GAME1.rom):
;   $4000-$40FF  Menu (this file)                   (256 B)
;   $4100-$61FF  TMS_A1GALAGA   (linked at $4100)   (8 448 B slot)
;   $6200-$75FF  TMS_SOKOBAN    (linked at $6200)   (5 120 B slot)
;   $7600-$7FFF  TMS_SNAKE      (linked at $7600)   (2 560 B slot)
;
; Re-shuffled May 2026 v2: cross-JSR strict-mode pads pushed Galaga past
; 8 192 B; absorbed by shifting Sokoban entry +256 B (was $6100, now $6200).
; Snake entry kept at $7600 — its slot stayed at 2 560 B.
;
; Life moved to its own cartridge (Codetank_GAME3.rom). The upper 16 kB
; bank ships TMS_LOGO V2.6 (turtle interpreter) at $4000 — flip the
; CodeTank board jumper to "Upper" and type 4000R from Wozmon to launch
; the REPL.
;
; Wozmon entry: 4000R after plugging the CodeTank card.
; =============================================

; --- Apple 1 I/O ---
.include "apple1.inc"

; --- Game entry points (must match the linker configs):
;     apple1_galaga_codetank_bank.cfg   start=$4100
;     apple1_sokoban_codetank_bank.cfg  start=$6200
;     apple1_snake_codetank_bank.cfg    start=$7600
GALAGA_ENTRY  = $4100
SOKOBAN_ENTRY = $6200
SNAKE_ENTRY   = $7600

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
