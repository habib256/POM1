; =============================================
; CodeTank lower-bank menu - picks between TMS_A1GALAGA,
; TMS_SOKOBAN, TMS_SNAKE and TMS_LIFE. All four share the
; lower 16 kB bank ($4000-$7FFF when CodeTank board jumper
; = Lower).
;
; Layout in the lower bank (packed tight):
;   $4000-$40FF  Menu (this file)
;   $4100-$5DFF  TMS_A1GALAGA   (linked at $4100)
;   $5E00-$70FF  TMS_SOKOBAN    (linked at $5E00)
;   $7100-$79FF  TMS_SNAKE      (linked at $7100)
;   $7A00-$7FFF  TMS_LIFE       (linked at $7A00)
;
; The upper 16 kB bank ships TMS_LOGO V2.0 (turtle graphics
; interpreter) at $4000 — flip the CodeTank board jumper to
; "Upper" and type 4000R from Wozmon to launch the REPL.
;
; Wozmon entry: 4000R after plugging the CodeTank card.
; =============================================

; --- Apple 1 I/O ---
KBD    = $D010
KBDCR  = $D011
ECHO   = $FFEF

; --- Game entry points (must match the linker configs below).
GALAGA_ENTRY  = $4100
SOKOBAN_ENTRY = $5E10            ; +16 B headroom for Galaga's hide_slot_4 helper
SNAKE_ENTRY   = $7100
LIFE_ENTRY    = $7A00

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
        CMP #('4' | $80)
        BEQ @go_life
        JMP @wait_key

@go_galaga:
        JMP GALAGA_ENTRY

@go_sokoban:
        JMP SOKOBAN_ENTRY

@go_snake:
        JMP SNAKE_ENTRY

@go_life:
        JMP LIFE_ENTRY

; --- Prompt string. NUL-terminated; print loop ORs in bit 7 for the
;     Apple-1 display. $0D = CR (Apple-1 wraps + line-feeds on its own).
prompt:
        .byte $0D
        .byte "P-LAB CODETANK", $0D
        .byte $0D
        .byte "1 = GALAGA", $0D
        .byte "2 = SOKOBAN", $0D
        .byte "3 = SNAKE", $0D
        .byte "4 = LIFE", $0D
        .byte "(LOGO V2 ON UPPER JUMPER)", $0D
        .byte $0D
        .byte "PICK 1, 2, 3 OR 4 ? "
        .byte 0
