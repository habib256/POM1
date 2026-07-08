; =============================================
; CodeTank lower-bank menu — picks between TMS_A1GALAGA, TMS_SOKOBAN
; and TMS_SNAKE. All three share the lower 16 kB bank ($4000-$7FFF
; when CodeTank board jumper = Lower).
;
; Layout in the lower bank (Codetank_ARCADE.rom):
;   $4000-$40FF  Menu (this file)                   (256 B)
;   $4100-$61FF  TMS_A1GALAGA   (linked at $4100)   (8 448 B slot)
;   $6200-$75FF  TMS_SOKOBAN    (linked at $6200)   (5 120 B slot)
;   $7600-$7FFF  TMS_SNAKE      (linked at $7600)   (2 560 B slot)
;
; Re-shuffled May 2026 v2: cross-JSR strict-mode pads pushed Galaga past
; 8 192 B; absorbed by shifting Sokoban entry +256 B (was $6100, now $6200).
; Snake entry kept at $7600 — its slot stayed at 2 560 B.
;
; The upper 16 kB bank ships TMS_Rogue (dungeon crawler) at $4000 — flip
; the CodeTank board jumper to "Upper" and type 4000R from Wozmon to
; launch it. (Life lives on Codetank_DEMOS.rom; LOGO V2.6 on
; Codetank_BASIC_LOGO.rom.)
;
; Wozmon entry: 4000R after plugging the CodeTank card.
; =============================================

; --- Apple 1 I/O ---
.include "apple1.inc"

; menu_select scratch — the menu cfg has no ZEROPAGE segment (256 B
; ROM stub), so alias tmp/tmp2 onto the canonical zp.inc slots $00/$01
; as plain equates. The games re-initialise their own ZP on entry.
tmp   := $0000
tmp2  := $0001

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
        BEQ @pick
        ORA #$80                ; Apple-1 display wants bit 7 set
        JSR ECHO
        INX
        BNE @print

@pick:
        LDA #'1'                ; menu_select blocks until '1'..'3',
        LDX #'3'                ; echoes the digit, returns it in A
        JSR menu_select
        CMP #'1'
        BEQ @go_galaga
        CMP #'2'
        BEQ @go_sokoban
        JMP SNAKE_ENTRY         ; only '3' left

@go_galaga:
        JMP GALAGA_ENTRY

@go_sokoban:
        JMP SOKOBAN_ENTRY

; --- Shared library routines (dev/lib) ---
.include "kbd.asm"              ; wait_key (lib/apple1)
.include "menu.asm"             ; menu_select (lib/text40)

; --- Prompt string. NUL-terminated; print loop ORs in bit 7 for the
;     Apple-1 display. $0D = CR (Apple-1 wraps + line-feeds on its own).
prompt:
        .byte $0D
        .byte "P-LAB CODETANK ARCADE", $0D
        .byte $0D
        .byte "1 = GALAGA", $0D
        .byte "2 = SOKOBAN", $0D
        .byte "3 = SNAKE", $0D
        .byte "(ROGUE ON UPPER JUMPER)", $0D
        .byte $0D
        .byte "PICK 1, 2 OR 3 ? "
        .byte 0
