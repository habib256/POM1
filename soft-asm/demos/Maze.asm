; =============================================
; MAZE GENERATOR FOR APPLE 1
; Binary Tree Algorithm - 215 bytes
; 19x10 cells → 40x22 display
; =============================================

; Zero page: $00-$01=PRNG, $03=CY, $04=temp
;            $10-$22=choices[19]
; I/O:       ECHO=$FFEF, KBDCR=$D011, KBD=$D010

; === MAIN: Seed PRNG ===
0300  A9 7B     LDA #$7B          ; seed low
0302  85 00     STA $00
0304  A9 E3     LDA #$E3          ; seed high
0306  85 01     STA $01

; === RESTART: Init cell row ===
0308  A9 00     LDA #$00          ; CY = 0
030A  85 03     STA $03

; === ROWLOOP: Generate choices for row ===
030C  A2 00     LDX #$00          ; cell_col = 0

; --- GENCHOICE: decide NORTH or EAST ---
030E  E0 12     CPX #$12          ; CX==18 (rightmost)?
0310  F0 11     BEQ $0323         ; → RIGHTCOL (force NORTH)
0312  A5 03     LDA $03           ; load CY
0314  F0 08     BEQ $031E         ; CY==0 → TOPROW (force EAST)
0316  20 C0 03  JSR $03C0         ; call RANDOM
0319  29 01     AND #$01          ; 0=NORTH, 1=EAST
031B  4C 25 03  JMP $0325         ; → STORECHOICE

; --- TOPROW: CY==0, force EAST ---
031E  A9 01     LDA #$01          ; choice = EAST
0320  4C 25 03  JMP $0325         ; → STORECHOICE

; --- RIGHTCOL: CX==18, force NORTH ---
0323  A9 00     LDA #$00          ; choice = NORTH
                                   ; fall through ↓

; --- STORECHOICE ---
0325  95 10     STA $10,X         ; choices[CX] = A
0327  E8        INX
0328  E0 13     CPX #$13          ; CX == 19?
032A  D0 E2     BNE $030E         ; → GENCHOICE loop

; === Draw wall row (or top border) ===
032C  A5 03     LDA $03           ; CY
032E  D0 10     BNE $0340         ; CY!=0 → WALLROW

; --- TOP BORDER: 39x '#' + HASHCR ---
0330  A2 27     LDX #$27          ; count = 39
0332  A9 A3     LDA #$A3          ; '#' | $80
0334  20 EF FF  JSR $FFEF         ; ECHO
0337  CA        DEX
0338  D0 F8     BNE $0332         ; loop
033A  20 CC 03  JSR $03CC         ; '#' + CR (= 40th '#' + newline)
033D  4C 66 03  JMP $0366         ; skip to CELLROW

; === WALLROW: horizontal walls ===
0340  A2 00     LDX #$00          ; display col = 0

; --- WALLROWLOOP ---
0342  8A        TXA               ; A = display col
0343  29 01     AND #$01          ; even or odd?
0345  F0 12     BEQ $0359         ; even → always '#'
; Odd position: check if cell opens NORTH
0347  86 04     STX $04           ; save display col
0349  8A        TXA
034A  4A        LSR A             ; cell_col = X >> 1
034B  AA        TAX
034C  B5 10     LDA $10,X         ; choices[cell_col]
034E  A6 04     LDX $04           ; restore display col
0350  C9 00     CMP #$00          ; NORTH?
0352  D0 05     BNE $0359         ; no → '#'
; Wall removed (NORTH passage)
0354  A9 A0     LDA #$A0          ; ' ' | $80
0356  4C 5B 03  JMP $035B         ; → WALLOUT

; --- WALLHASH ---
0359  A9 A3     LDA #$A3          ; '#' | $80

; --- WALLOUT ---
035B  20 EF FF  JSR $FFEF         ; ECHO
035E  E8        INX
035F  E0 27     CPX #$27          ; col == 39?
0361  D0 DF     BNE $0342         ; → WALLROWLOOP
0363  20 CC 03  JSR $03CC         ; '#' + CR

; === CELLROW: cells and vertical walls ===
0366  A2 00     LDX #$00          ; display col = 0

; --- CELLROWLOOP ---
0368  E0 00     CPX #$00          ; col == 0?
036A  F0 18     BEQ $0384         ; → left border '#'
036C  8A        TXA
036D  29 01     AND #$01
036F  D0 17     BNE $0388         ; odd → cell = ' '
; Even, X>0: vertical wall
0371  86 04     STX $04           ; save display col
0373  8A        TXA
0374  4A        LSR A             ; X/2
0375  AA        TAX
0376  CA        DEX               ; X/2 - 1 = cell to the left
0377  B5 10     LDA $10,X         ; choices[left_cell]
0379  A6 04     LDX $04           ; restore display col
037B  C9 01     CMP #$01          ; EAST?
037D  F0 09     BEQ $0388         ; yes → wall removed = ' '
; Wall present
037F  A9 A3     LDA #$A3          ; '#'
0381  4C 8A 03  JMP $038A         ; → CELLOUT

; --- CELLHASH: left border ---
0384  A9 A3     LDA #$A3          ; '#'
0386  D0 02     BNE $038A         ; → CELLOUT

; --- CELLSPACE ---
0388  A9 A0     LDA #$A0          ; ' '

; --- CELLOUT ---
038A  20 EF FF  JSR $FFEF         ; ECHO
038D  E8        INX
038E  E0 27     CPX #$27          ; col == 39?
0390  D0 D6     BNE $0368         ; → CELLROWLOOP
0392  20 CC 03  JSR $03CC         ; '#' + CR

; === NEXT ROW ===
0395  E6 03     INC $03           ; CY++
0397  A5 03     LDA $03
0399  C9 0A     CMP #$0A          ; CY == 10?
039B  F0 03     BEQ $03A0         ; → BOTTOMBORDER
039D  4C 0C 03  JMP $030C         ; → ROWLOOP

; === BOTTOM BORDER: 39x '#' + HASHCR ===
03A0  A2 27     LDX #$27          ; count = 39
03A2  A9 A3     LDA #$A3          ; '#'
03A4  20 EF FF  JSR $FFEF         ; ECHO
03A7  CA        DEX
03A8  D0 F8     BNE $03A2         ; loop
03AA  20 CC 03  JSR $03CC         ; '#' + CR

; === WAITKEY: press key → new maze ===
03AD  AD 11 D0  LDA $D011         ; KBDCR
03B0  10 FB     BPL $03AD         ; loop until key ready
03B2  AD 10 D0  LDA $D010         ; read key (clear strobe)
03B5  85 00     STA $00           ; key → new PRNG seed low
03B7  49 FF     EOR #$FF
03B9  85 01     STA $01           ; complement → seed high
03BB  4C 08 03  JMP $0308         ; → RESTART

; === RANDOM: 16-bit Galois LFSR ===
03C0  A5 00     LDA $00
03C2  0A        ASL A
03C3  26 01     ROL $01
03C5  90 02     BCC $03C9         ; no feedback
03C7  49 2D     EOR #$2D          ; tap polynomial
03C9  85 00     STA $00
03CB  60        RTS

; === HASHCR: print '#' then CR ===
; (adds right border + newline to each row)
03CC  A9 A3     LDA #$A3          ; '#'
03CE  20 EF FF  JSR $FFEF         ; ECHO
03D1  A9 8D     LDA #$8D          ; CR
03D3  20 EF FF  JSR $FFEF         ; ECHO
03D6  60        RTS
