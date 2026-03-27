; =============================================
; MAZE GENERATOR FOR APPLE 1
; Binary Tree Algorithm - 213 bytes
; 19x10 cells → 40x22 display
; =============================================
; Assemble with cc65:
;   ca65 -o build/Maze.o soft-asm/demos/Maze.asm
;   ld65 -C soft-asm/apple1.cfg -o build/Maze.bin build/Maze.o
;
; Load in POM1 via File > Load Memory (Maze.txt)
; or load binary at $0300, then type 300R in Woz Monitor.
; =============================================

; --- Constants ---
ECHO    = $FFEF         ; Woz Monitor character output routine
KBDCR   = $D011         ; Keyboard control register (bit 7 = key ready)
KBD     = $D010         ; Keyboard data register

; --- Zero page variables ---
.zeropage
prng_lo:    .res 1      ; $00 - PRNG state low byte
prng_hi:    .res 1      ; $01 - PRNG state high byte
            .res 1      ; $02 - unused
cell_row:   .res 1      ; $03 - current cell row (CY)
temp:       .res 1      ; $04 - temp storage for display column
            .res 11     ; $05-$0F unused
choices:    .res 19     ; $10-$22 - choices[19] for each cell column

; --- Code at $0300 ---
.code

; === MAIN: Seed PRNG ===
main:
        LDA #$7B
        STA prng_lo             ; seed low
        LDA #$E3
        STA prng_hi             ; seed high

; === RESTART: Init cell row ===
restart:
        LDA #$00
        STA cell_row            ; CY = 0

; === ROWLOOP: Generate choices for row ===
rowloop:
        LDX #$00                ; cell_col = 0

; --- GENCHOICE: decide NORTH or EAST ---
genchoice:
        CPX #18                 ; CX==18 (rightmost)?
        BEQ rightcol            ; → force NORTH
        LDA cell_row
        BEQ toprow              ; CY==0 → force EAST
        JSR random              ; call PRNG
        AND #$01                ; 0=NORTH, 1=EAST
        JMP storechoice

; --- TOPROW: CY==0, force EAST ---
toprow:
        LDA #$01                ; choice = EAST
        JMP storechoice

; --- RIGHTCOL: CX==18, force NORTH ---
rightcol:
        LDA #$00                ; choice = NORTH
                                ; fall through ↓

; --- STORECHOICE ---
storechoice:
        STA choices,X           ; choices[CX] = A
        INX
        CPX #19                 ; CX == 19?
        BNE genchoice           ; → loop

; === Draw wall row (or top border) ===
        LDA cell_row
        BNE wallrow             ; CY!=0 → WALLROW

; --- TOP BORDER: 39x '#' + HASHCR ---
        LDX #39                 ; count = 39
@loop:  LDA #($23 | $80)       ; '#' | $80
        JSR ECHO
        DEX
        BNE @loop
        JSR hashcr              ; 40th '#' + newline
        JMP cellrow             ; skip to CELLROW

; === WALLROW: horizontal walls ===
wallrow:
        LDX #$00                ; display col = 0

wallrowloop:
        TXA                     ; A = display col
        AND #$01                ; even or odd?
        BEQ wallhash            ; even → always '#'
        ; Odd position: check if cell opens NORTH
        STX temp                ; save display col
        TXA
        LSR A                   ; cell_col = X >> 1
        TAX
        LDA choices,X           ; choices[cell_col]
        LDX temp                ; restore display col
        CMP #$00                ; NORTH?
        BNE wallhash            ; no → '#'
        ; Wall removed (NORTH passage)
        LDA #($20 | $80)       ; ' ' | $80
        JMP wallout

wallhash:
        LDA #($23 | $80)       ; '#' | $80

wallout:
        JSR ECHO
        INX
        CPX #39                 ; col == 39?
        BNE wallrowloop
        JSR hashcr              ; '#' + CR

; === CELLROW: cells and vertical walls ===
cellrow:
        LDX #$00                ; display col = 0

cellrowloop:
        CPX #$00                ; col == 0?
        BEQ cellhash            ; → left border '#'
        TXA
        AND #$01
        BNE cellspace           ; odd → cell = ' '
        ; Even, X>0: vertical wall
        STX temp                ; save display col
        TXA
        LSR A                   ; X/2
        TAX
        DEX                     ; X/2 - 1 = cell to the left
        LDA choices,X           ; choices[left_cell]
        LDX temp                ; restore display col
        CMP #$01                ; EAST?
        BEQ cellspace           ; yes → wall removed = ' '
        ; Wall present
        LDA #($23 | $80)       ; '#'
        JMP cellout

; --- left border ---
cellhash:
        LDA #($23 | $80)       ; '#'
        BNE cellout             ; → CELLOUT (always taken)

cellspace:
        LDA #($20 | $80)       ; ' '

cellout:
        JSR ECHO
        INX
        CPX #39                 ; col == 39?
        BNE cellrowloop
        JSR hashcr              ; '#' + CR

; === NEXT ROW ===
        INC cell_row            ; CY++
        LDA cell_row
        CMP #10                 ; CY == 10?
        BEQ bottomborder
        JMP rowloop

; === BOTTOM BORDER: 39x '#' + HASHCR ===
bottomborder:
        LDX #39                 ; count = 39
@loop:  LDA #($23 | $80)       ; '#'
        JSR ECHO
        DEX
        BNE @loop
        JSR hashcr              ; '#' + CR

; === WAITKEY: press key → new maze ===
waitkey:
        LDA KBDCR              ; keyboard control register
        BPL waitkey             ; loop until key ready
        LDA KBD                 ; read key (clear strobe)
        STA prng_lo             ; key → new PRNG seed low
        EOR #$FF
        STA prng_hi             ; complement → seed high
        JMP restart             ; → new maze

; === RANDOM: 16-bit Galois LFSR ===
random:
        LDA prng_lo
        ASL A
        ROL prng_hi
        BCC @nofeedback
        EOR #$2D                ; tap polynomial
@nofeedback:
        STA prng_lo
        RTS

; === HASHCR: print '#' then CR ===
; (adds right border + newline to each row)
hashcr:
        LDA #($23 | $80)       ; '#'
        JSR ECHO
        LDA #$8D               ; CR
        JSR ECHO
        RTS
