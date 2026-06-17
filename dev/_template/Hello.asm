; Hello.asm — the smallest useful Apple-1 program. Prints a string through the
; WOZ Monitor ECHO routine, then returns to the Monitor.
;
; COPY THIS FOLDER to start your own asm project. Build: `make` (or by hand,
; see README.md). Load the .bin/.txt in POM1 and run with 280R.
;
; Two rules you must know (see dev/Programming_Apple1_ASM.md §2):
;   - bit 7 = data-valid: ORA #$80 before printing a char; CR is $0D here and
;     ECHO adds the high bit. The keyboard is UPPERCASE only.
;   - return to the Monitor with JMP WOZMON, never RTS (R doesn't push a return).

.include "apple1.inc"          ; ECHO, WOZMON, KBD, DSP… (needs -I ../../lib/apple1)

.segment "CODE"
start:
        ldx #0
loop:
        lda msg,x
        beq done
        ora #$80               ; set bit 7, then print A
        jsr ECHO
        inx
        bne loop
done:
        jmp WOZMON             ; back to the '\' prompt

msg:
        .byte $0D, "HELLO WORLD", $0D, $00
