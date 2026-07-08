; =============================================
; CodeTank DEMOS lower-bank menu — picks between TMS_Life, TMS_Mandel,
; TMS_Plasma, TMS_Vague and TMS_Nyan. Lives in Codetank_DEMOS.rom lower
; bank ($4000-$7FFF when CodeTank board jumper = Lower).
;
; Layout in the lower bank (Codetank_DEMOS.rom):
;   $4000-$41FF  Menu (this file)               (  512 B)
;   $4200-$49FF  TMS_Life   (linked at $4200)   (2 048 B slot)
;   $4A00-$51FF  TMS_Mandel (linked at $4A00)   (2 048 B slot)
;   $5200-$59FF  TMS_Plasma (linked at $5200)   (2 048 B slot)
;   $5A00-$5FFF  TMS_Vague  (linked at $5A00)   (1 536 B slot)
;   $6000-$7FFF  TMS_Nyan   (linked at $6000)   (8 192 B slot)
;
; Upper bank ships demo_sprite_animals (cc65 C) at $4000 — flip jumper to
; "Upper" and type 4000R from Wozmon to launch it.
;
; Wozmon entry: 4000R after plugging the CodeTank card.
; =============================================

; --- Apple 1 I/O ---
.include "apple1.inc"

; menu_select scratch — the menu cfg has no ZEROPAGE segment (512 B
; ROM stub), so alias tmp/tmp2 onto the canonical zp.inc slots $00/$01
; as plain equates. The demos re-initialise their own ZP on entry.
tmp   := $0000
tmp2  := $0001

; --- Demo entry points (must match the linker configs):
;     apple1_life_codetank_demos_bank.cfg    start=$4200
;     apple1_mandel_codetank_demos_bank.cfg  start=$4A00
;     apple1_plasma_codetank_demos_bank.cfg  start=$5200
;     apple1_vague_codetank_demos_bank.cfg   start=$5A00
;     apple1_nyan_codetank_demos_bank.cfg    start=$6000
LIFE_ENTRY   = $4200
MANDEL_ENTRY = $4A00
PLASMA_ENTRY = $5200
VAGUE_ENTRY  = $5A00
NYAN_ENTRY   = $6000

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
        LDA #'1'                ; menu_select blocks until '1'..'5',
        LDX #'5'                ; echoes the digit, returns it in A
        JSR menu_select
        CMP #'1'
        BEQ @go_life
        CMP #'2'
        BEQ @go_mandel
        CMP #'3'
        BEQ @go_plasma
        CMP #'4'
        BEQ @go_vague
        JMP NYAN_ENTRY          ; only '5' left

@go_life:
        JMP LIFE_ENTRY

@go_mandel:
        JMP MANDEL_ENTRY

@go_plasma:
        JMP PLASMA_ENTRY

@go_vague:
        JMP VAGUE_ENTRY

; --- Shared library routines (dev/lib) ---
.include "kbd.asm"              ; wait_key (lib/apple1)
.include "menu.asm"             ; menu_select (lib/text40)

; --- Prompt string. NUL-terminated; print loop ORs in bit 7 for the
;     Apple-1 display. $0D = CR (Apple-1 wraps + line-feeds on its own).
prompt:
        .byte $0D
        .byte "P-LAB CODETANK DEMOS", $0D
        .byte $0D
        .byte "1 = LIFE   (CONWAY GAME OF LIFE)", $0D
        .byte "2 = MANDEL (MANDELBROT SET)", $0D
        .byte "3 = PLASMA (CYCLING PLASMA EFFECT)", $0D
        .byte "4 = VAGUE  (BOAT ON A WAVE)", $0D
        .byte "5 = NYAN   (MODE III ANIMATION)", $0D
        .byte "(SPRITE ANIMALS ON UPPER JUMPER)", $0D
        .byte $0D
        .byte "PICK 1 TO 5 ? "
        .byte 0
