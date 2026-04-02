; =============================================
; MAZE GENERATOR FOR APPLE 1
; Sidewinder Algorithm
; 19x11 cells -> 40x24 display
; With title screen and S/E markers
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

NCOLS   = 19            ; Number of cell columns
NROWS   = 11            ; Number of cell rows
DISP_W  = 39            ; Display width - 1 (for hashcr to add 40th)
END_CY  = 10            ; End marker cell row (last row = NROWS-1)
END_DX  = 37            ; End marker display col (2*18+1)

; choices[] bitfield:
;   bit 0 = NORTH passage (horizontal wall above removed)
;   bit 1 = EAST passage  (vertical wall to right removed)
NORTH_BIT = $01
EAST_BIT  = $02

; --- Zero page variables ---
.zeropage
prng_lo:    .res 1      ; $00 - PRNG state low byte
prng_hi:    .res 1      ; $01 - PRNG state high byte
run_start:  .res 1      ; $02 - Sidewinder: start column of current run
cell_row:   .res 1      ; $03 - current cell row (CY)
temp:       .res 1      ; $04 - temp storage
temp2:      .res 1      ; $05 - temp / empty line count
str_lo:     .res 1      ; $06 - string pointer low
str_hi:     .res 1      ; $07 - string pointer high
run_len:    .res 1      ; $08 - Sidewinder: length of current run
            .res 7      ; $09-$0F unused
choices:    .res 19     ; $10-$22 - choices[19] bitfield per cell column

; --- Code at $0300 ---
.code

; ======================================================
; MAIN: Show title screen then start maze generation
; ======================================================
main:
        JSR show_title          ; display title, wait key, seed PRNG
        JSR clearscr            ; clear title screen
                                ; fall through to restart

; ======================================================
; RESTART: Init cell row and generate a new maze
; ======================================================
restart:
        LDA #$00
        STA cell_row            ; CY = 0

; ======================================================
; ROWLOOP: Generate choices using Sidewinder algorithm
; ======================================================
rowloop:
        ; Clear choices array
        LDX #$00
        LDA #$00
@clear: STA choices,X
        INX
        CPX #NCOLS
        BNE @clear

        LDA #$00
        STA run_start           ; run starts at column 0
        LDX #$00                ; CX = 0

; --- Sidewinder: process each cell ---
genchoice:
        LDA cell_row
        BNE @not_top

        ; === Top row: always EAST except last column ===
        CPX #(NCOLS-1)
        BEQ @next               ; last col: stays 0 (dead end)
        LDA choices,X
        ORA #EAST_BIT
        STA choices,X
        JMP @next

@not_top:
        ; === Other rows: close run or extend east ===
        CPX #(NCOLS-1)
        BEQ @close              ; must close at right edge

        JSR random
        AND #$01
        BEQ @close              ; 50% chance to close run

        ; Extend run east
        LDA choices,X
        ORA #EAST_BIT
        STA choices,X
        JMP @next

@close:
        ; Close the run: pick random cell for NORTH passage
        ; run_len = CX - run_start + 1
        STX temp                ; save CX
        TXA
        SEC
        SBC run_start           ; A = CX - run_start
        CLC
        ADC #$01                ; A = CX - run_start + 1 = run_len
        STA run_len

        ; Pick random offset within run
        JSR random              ; A = random byte
        AND #$1F                ; mask to 0-31
@mod:   CMP run_len             ; A >= run_len?
        BCC @mod_done
        SBC run_len             ; A -= run_len (carry is set from CMP)
        JMP @mod
@mod_done:
        ; A = random offset in [0, run_len-1]
        CLC
        ADC run_start           ; A = north_cell column index
        TAX
        LDA choices,X
        ORA #NORTH_BIT          ; set NORTH passage
        STA choices,X

        LDX temp                ; restore CX
        ; Next run starts after this cell
        TXA
        CLC
        ADC #$01
        STA run_start

@next:  INX
        CPX #NCOLS              ; CX == 19?
        BNE genchoice           ; -> loop

; === Draw wall row (or top border for CY==0) ===
        LDA cell_row
        BNE wallrow             ; CY != 0 -> WALLROW

; --- TOP BORDER: 39x '#' + HASHCR ---
        LDX #DISP_W             ; count = 39
@bdr:   LDA #($23 | $80)       ; '#' | $80
        JSR ECHO
        DEX
        BNE @bdr
        JSR hashcr              ; 40th '#' + newline
        JMP cellrow             ; skip to CELLROW

; ======================================================
; WALLROW: horizontal walls between cell rows
; ======================================================
wallrow:
        LDX #$00                ; display col = 0

wallrowloop:
        TXA                     ; A = display col
        AND #$01                ; even or odd?
        BEQ wallhash            ; even -> always '#'
        ; Odd position: check if cell has NORTH passage
        STX temp                ; save display col
        TXA
        LSR A                   ; cell_col = X >> 1
        TAX
        LDA choices,X           ; choices[cell_col]
        LDX temp                ; restore display col
        AND #NORTH_BIT          ; NORTH bit set?
        BEQ wallhash            ; no -> '#'
        ; Wall removed (NORTH passage)
        LDA #($20 | $80)       ; ' ' | $80
        JMP wallout

wallhash:
        LDA #($23 | $80)       ; '#' | $80

wallout:
        JSR ECHO
        INX
        CPX #DISP_W             ; col == 39?
        BNE wallrowloop
        JSR hashcr              ; '#' + CR

; ======================================================
; CELLROW: cells and vertical walls
; ======================================================
cellrow:
        LDX #$00                ; display col = 0

cellrowloop:
        CPX #$00                ; col == 0?
        BEQ cellhash            ; -> left border '#'
        TXA
        AND #$01
        BNE cellspace           ; odd -> cell content
        ; Even, X>0: vertical wall
        STX temp                ; save display col
        TXA
        LSR A                   ; X/2
        TAX
        DEX                     ; X/2 - 1 = cell to the left
        LDA choices,X           ; choices[left_cell]
        LDX temp                ; restore display col
        AND #EAST_BIT           ; EAST bit set?
        BNE cellspace_char      ; yes -> wall removed = ' '
        ; Wall present
        LDA #($23 | $80)       ; '#'
        JMP cellout

; --- left border ---
cellhash:
        LDA #($23 | $80)       ; '#'
        BNE cellout             ; -> CELLOUT (always taken)

; --- cell interior (odd display col) ---
cellspace:
        ; Check for START marker: CY=0, display col=1
        LDA cell_row
        BNE @not_start
        CPX #1
        BNE @not_start
        LDA #($53 | $80)       ; 'S' | $80
        JMP cellout
@not_start:
        ; Check for END marker: CY=END_CY, display col=END_DX
        LDA cell_row
        CMP #END_CY
        BNE cellspace_char
        CPX #END_DX
        BNE cellspace_char
        LDA #($45 | $80)       ; 'E' | $80
        JMP cellout

cellspace_char:
        LDA #($20 | $80)       ; ' '

cellout:
        JSR ECHO
        INX
        CPX #DISP_W             ; col == 39?
        BNE cellrowloop
        JSR hashcr              ; '#' + CR

; === NEXT ROW ===
        INC cell_row            ; CY++
        LDA cell_row
        CMP #NROWS              ; CY == 11?
        BEQ bottomborder
        JMP rowloop

; ======================================================
; BOTTOM BORDER + STATUS LINE
; ======================================================
bottomborder:
        LDX #DISP_W             ; count = 39
@loop:  LDA #($23 | $80)       ; '#'
        JSR ECHO
        DEX
        BNE @loop
        JSR hashcr              ; '#' + CR

        ; Status line (24th line, no trailing CR)
        LDA #<str_status
        LDX #>str_status
        JSR print_str_ax

; ======================================================
; WAITKEY: press key -> new maze
; ======================================================
waitkey:
        INC prng_lo             ; spin PRNG while waiting (timing = randomness)
        BNE @no_hi
        INC prng_hi
@no_hi: LDA KBDCR              ; keyboard control register
        BPL waitkey             ; loop until key ready
        LDA KBD                 ; read key (clear strobe)
        EOR prng_lo             ; mix key into PRNG state
        STA prng_lo
        JSR clearscr
        JMP restart             ; -> new maze

; ======================================================
; SUBROUTINES
; ======================================================

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
hashcr:
        LDA #($23 | $80)       ; '#'
        JSR ECHO
        LDA #$8D               ; CR
        JSR ECHO
        RTS

; === CLEARSCR: scroll 24 blank lines ===
clearscr:
        LDX #24
@loop:  LDA #$8D               ; CR
        JSR ECHO
        DEX
        BNE @loop
        RTS

; === PRINT_STR_AX: print null-terminated ASCII string ===
; Input: A = string addr low, X = string addr high
; Characters are OR'd with $80 for Apple 1 display
print_str_ax:
        STA str_lo
        STX str_hi
        LDY #$00
@loop:  LDA (str_lo),Y
        BEQ @done
        ORA #$80               ; set high bit
        JSR ECHO
        INY
        BNE @loop              ; max 256 chars per string
@done:  RTS

; ======================================================
; TITLE SCREEN (fills 24 lines)
; ======================================================
show_title:
        JSR title_border        ; line 1: ####...####
        LDA #4
        JSR empty_lines         ; lines 2-5: empty bordered
        LDA #<str_title
        LDX #>str_title
        JSR title_text          ; line 6: * M A Z E *
        JSR title_empty         ; line 7: empty
        LDA #<str_algo
        LDX #>str_algo
        JSR title_text          ; line 8: Sidewinder...
        LDA #<str_apple
        LDX #>str_apple
        JSR title_text          ; line 9: for Apple 1
        LDA #2
        JSR empty_lines         ; lines 10-11: empty
        LDA #<str_author
        LDX #>str_author
        JSR title_text          ; line 12: By VERHILLE Arnaud
        LDA #9
        JSR empty_lines         ; lines 13-21: empty
        LDA #<str_press
        LDX #>str_press
        JSR title_text          ; line 22: Press any key...
        JSR title_empty         ; line 23: empty
        ; Line 24: bottom border WITHOUT CR (avoid scroll)
        LDX #40
@bdr:   LDA #($23 | $80)       ; '#'
        JSR ECHO
        DEX
        BNE @bdr
        ; Wait for key and seed PRNG (timing-based)
@wait:  INC prng_lo
        BNE @no_hi
        INC prng_hi
@no_hi: LDA KBDCR
        BPL @wait
        LDA KBD
        EOR prng_lo             ; mix key into PRNG state
        STA prng_lo
        RTS

; === EMPTY_LINES: print A empty bordered lines ===
empty_lines:
        STA temp2
@loop:  JSR title_empty
        DEC temp2
        BNE @loop
        RTS

; === TITLE_BORDER: 40x '#' + CR ===
title_border:
        LDX #40
@loop:  LDA #($23 | $80)       ; '#'
        JSR ECHO
        DEX
        BNE @loop
        LDA #$8D               ; CR
        JSR ECHO
        RTS

; === TITLE_EMPTY: '#' + 38 spaces + '#' + CR ===
title_empty:
        LDA #($23 | $80)       ; '#'
        JSR ECHO
        LDX #38
@loop:  LDA #($20 | $80)       ; ' '
        JSR ECHO
        DEX
        BNE @loop
        LDA #($23 | $80)       ; '#'
        JSR ECHO
        LDA #$8D               ; CR
        JSR ECHO
        RTS

; === TITLE_TEXT: '#' + text padded to 38 + '#' + CR ===
; Input: A = string addr low, X = string addr high
title_text:
        STA str_lo
        STX str_hi
        LDA #($23 | $80)       ; left border '#'
        JSR ECHO
        LDY #$00
        LDX #$00               ; char counter
@print: LDA (str_lo),Y
        BEQ @pad
        ORA #$80               ; set high bit for Apple 1
        JSR ECHO
        INY
        INX
        BNE @print             ; branch always (< 256 chars)
@pad:   CPX #38
        BCS @right
        LDA #($20 | $80)       ; space padding
        JSR ECHO
        INX
        JMP @pad
@right: LDA #($23 | $80)       ; right border '#'
        JSR ECHO
        LDA #$8D               ; CR
        JSR ECHO
        RTS

; ======================================================
; STRING DATA (normal ASCII, null-terminated)
; title_text adds borders and padding automatically
; ======================================================
str_title:
        .byte "             * M A Z E *", 0
str_algo:
        .byte "     Sidewinder Maze Algorithm", 0
str_apple:
        .byte "            for Apple 1", 0
str_author:
        .byte "         By VERHILLE Arnaud", 0
str_press:
        .byte "      Press any key to start...", 0
str_status:
        .byte " S=START E=END   ANY KEY=NEW MAZE", 0
