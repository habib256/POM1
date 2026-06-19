; =============================================
; CodeTank TEST upper-bank menu — picks between TMS_Clone and TMS_Split.
; Lives in Codetank_TEST.rom upper bank ($4000-$7FFF when CodeTank board
; jumper = Upper).
;
; Layout in the upper bank (Codetank_TEST.rom):
;   $4000-$40FF  Menu (this file)              (256 B)
;   $4100-$44FF  TMS_Clone (linked at $4100)   (1 024 B slot, ~804 B)
;   $4500-$48FF  TMS_Split (linked at $4500)   (1 024 B slot, ~588 B)
;   $4900-$7FFF  reserved (free)
;
; Lower bank ships TMS_SilBench at $4000 — flip jumper to "Lower" and
; type 4000R from Wozmon to launch the silicon-validation suite.
;
; Wozmon entry: 4000R after plugging the CodeTank card.
; =============================================

; --- Apple 1 I/O ---
.include "apple1.inc"

; --- Test entry points (must match the linker configs):
;     apple1_clone_codetank_bank.cfg  start=$4100
;     apple1_split_codetank_bank.cfg  start=$4500
CLONE_ENTRY = $4100
SPLIT_ENTRY = $4500

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
        BEQ @go_clone
        CMP #('2' | $80)
        BEQ @go_split
        JMP @wait_key

@go_clone:
        JMP CLONE_ENTRY

@go_split:
        JMP SPLIT_ENTRY

; --- Prompt string. NUL-terminated; print loop ORs in bit 7 for the
;     Apple-1 display. $0D = CR (Apple-1 wraps + line-feeds on its own).
prompt:
        .byte $0D
        .byte "P-LAB CODETANK TEST", $0D
        .byte $0D
        .byte "1 = CLONE  (BUG N.8 SPRITE CLONE)", $0D
        .byte "2 = SPLIT  (5TH-SPRITE PALETTE SPLIT)", $0D
        .byte "(SILBENCH ON LOWER JUMPER)", $0D
        .byte $0D
        .byte "PICK 1 OR 2 ? "
        .byte 0
