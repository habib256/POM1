; HELLO WORLD - Apple-1 text via WozMon ECHO ($FFEF).
; DevBench target: Apple-1 dual 4K/8K (asm), entry $0280.

ECHO = $FFEF

.segment "CODE"

start:
    ldx #0
loop:
    lda msg,x
    beq done
    ora #$80
    jsr ECHO
    inx
    bne loop
done:
    jmp done

msg:
    .byte $0D, "HELLO WORLD", $0D, $00

