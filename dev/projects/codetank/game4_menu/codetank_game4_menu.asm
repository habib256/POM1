; =============================================
; CodeTank GAME4 lower-bank menu — picks between TMS_Split, TMS_Vague
; and TMS9918_Hello. Lives in Codetank_GAME4.rom lower bank ($4000-$7FFF
; when CodeTank board jumper = Lower).
;
; Layout in the lower bank (Codetank_GAME4.rom):
;   $4000-$40FF  Menu (this file)                 (256 B)
;   $4100-$50FF  TMS_Split  (linked at $4100)     (4 096 B slot)
;   $5100-$60FF  TMS_Vague  (linked at $5100)     (4 096 B slot)
;   $6100-$68FF  TMS9918_Hello (linked at $6100)  (2 048 B slot)
;   $6900-$7FFF  reserved (free)
;
; Upper bank ships demo_sprite_animals (cc65 C, tms9918c runtime) at
; $4000 — flip jumper to "Upper" and type 4000R from Wozmon for the
; four SCROLL-O-SPRITES Fauna silhouettes.
;
; Wozmon entry: 4000R after plugging the CodeTank card.
; =============================================

; --- Apple 1 I/O ---
.include "apple1.inc"

; --- Demo entry points (must match the linker configs):
;     apple1_split_codetank_game4_bank.cfg   start=$4100
;     apple1_vague_codetank_game4_bank.cfg   start=$5100
;     apple1_hello_codetank_game4_bank.cfg   start=$6100
SPLIT_ENTRY = $4100
VAGUE_ENTRY = $5100
HELLO_ENTRY = $6100

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
        BEQ @go_split
        CMP #('2' | $80)
        BEQ @go_vague
        CMP #('3' | $80)
        BEQ @go_hello
        JMP @wait_key

@go_split:
        JMP SPLIT_ENTRY

@go_vague:
        JMP VAGUE_ENTRY

@go_hello:
        JMP HELLO_ENTRY

; --- Prompt string. NUL-terminated; print loop ORs in bit 7 for the
;     Apple-1 display. $0D = CR (Apple-1 wraps + line-feeds on its own).
prompt:
        .byte $0D
        .byte "P-LAB CODETANK GAME4", $0D
        .byte $0D
        .byte "1 = SPLIT (MID-FRAME PALETTE SWAP)", $0D
        .byte "2 = VAGUE (BOAT ON A WAVE)", $0D
        .byte "3 = HELLO (TMS9918 HELLO WORLD)", $0D
        .byte "(SPRITE ANIMALS ON UPPER JUMPER)", $0D
        .byte $0D
        .byte "PICK 1, 2 OR 3 ? "
        .byte 0
