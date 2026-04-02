; =============================================
; MAZE 2 - GENERATOR FOR APPLE 1
; VERHILLE Arnaud - 2026
; Recursive Backtracker (DFS) Algorithm
; 19x11 cells -> 40x24 display
; With title screen and S/E markers
; =============================================
; Assemble with cc65:
;   ca65 -o build/Maze2.o software/demos/Maze2.asm
;   ld65 -C software/apple1.cfg -o build/Maze2.bin build/Maze2.o
;
; Load in POM1 via File > Load Memory (Maze2.txt)
; or load binary at $0300, then type 300R in Woz Monitor.
; =============================================

; --- Constants ---
ECHO    = $FFEF         ; Woz Monitor character output routine
KBDCR   = $D011         ; Keyboard control register (bit 7 = key ready)
KBD     = $D010         ; Keyboard data register

NCOLS   = 19            ; Number of cell columns
NROWS   = 11            ; Number of cell rows
NCELLS  = 209           ; NCOLS * NROWS
DISP_W  = 39            ; Display width - 1 (for hashcr to add 40th)
END_CY  = 10            ; End marker cell row (last row = NROWS-1)
END_DX  = 37            ; End marker display col (2*(NCOLS-1)+1)

; Grid cell bitfield:
;   bit 0 = NORTH passage (horizontal wall above removed)
;   bit 1 = EAST passage  (vertical wall to right removed)
;   bit 7 = visited flag
NORTH_BIT = $01
EAST_BIT  = $02
VISITED   = $80

; RAM areas (outside code, not saved in binary)
GRID    = $2000         ; 209 bytes: cell data
DFS_STK = $2100         ; 209 bytes: backtracking stack (cell indices)

; --- Zero page variables ---
.zeropage
prng_lo:    .res 1      ; $00 - PRNG state low byte
prng_hi:    .res 1      ; $01 - PRNG state high byte
            .res 1      ; $02 - unused
cell_row:   .res 1      ; $03 - rendering: current display row
temp:       .res 1      ; $04 - temp storage
temp2:      .res 1      ; $05 - temp / empty line count
str_lo:     .res 1      ; $06 - string pointer low
str_hi:     .res 1      ; $07 - string pointer high
cur_row:    .res 1      ; $08 - DFS: current cell row
cur_col:    .res 1      ; $09 - DFS: current cell column
stkp:       .res 1      ; $0A - DFS: stack pointer (0-208)
num_dirs:   .res 1      ; $0B - DFS: number of valid directions
cell_idx:   .res 1      ; $0C - DFS: current cell index (row*19+col)
row_base:   .res 1      ; $0D - rendering: row_offset[cell_row]
dir_buf:    .res 4      ; $0E-$11 - DFS: direction candidates

; --- Code at $0300 ---
.code

; ======================================================
; MAIN
; ======================================================
main:
        JSR show_title          ; display title, wait key, seed PRNG
        JSR clearscr            ; clear title screen

; ======================================================
; RESTART: generate and display a new maze
; ======================================================
restart:
        JSR generate_maze       ; DFS generation into GRID

        ; --- Render the maze ---
        LDA #$00
        STA cell_row

render_rowloop:
        ; Compute row_base for current row
        LDX cell_row
        LDA row_offset,X
        STA row_base

        LDA cell_row
        BNE wallrow             ; CY != 0 -> wall row

; --- TOP BORDER: 39x '#' + HASHCR ---
        LDX #DISP_W
@bdr:   LDA #($23 | $80)
        JSR ECHO
        DEX
        BNE @bdr
        JSR hashcr
        JMP cellrow

; ======================================================
; WALLROW: horizontal walls between cell rows
; ======================================================
wallrow:
        LDX #$00

wallrowloop:
        TXA
        AND #$01
        BEQ wallhash            ; even -> always '#'
        ; Odd: check NORTH passage
        STX temp
        TXA
        LSR A                   ; cell_col = display_col >> 1
        CLC
        ADC row_base            ; grid offset
        TAY
        LDA GRID,Y              ; grid[row][col]
        LDX temp
        AND #NORTH_BIT
        BEQ wallhash            ; no NORTH -> '#'
        LDA #($20 | $80)       ; space (passage)
        JMP wallout

wallhash:
        LDA #($23 | $80)       ; '#'

wallout:
        JSR ECHO
        INX
        CPX #DISP_W
        BNE wallrowloop
        JSR hashcr

; ======================================================
; CELLROW: cells and vertical walls
; ======================================================
cellrow:
        LDX #$00

cellrowloop:
        CPX #$00
        BEQ cellhash            ; col 0 -> left border '#'
        TXA
        AND #$01
        BNE cellspace           ; odd -> cell content
        ; Even, X>0: vertical wall
        STX temp
        TXA
        LSR A                   ; X/2
        SEC
        SBC #$01                ; X/2 - 1 = left cell column
        CLC
        ADC row_base            ; grid offset
        TAY
        LDA GRID,Y              ; grid[row][left_cell]
        LDX temp
        AND #EAST_BIT           ; EAST passage?
        BNE cellspace_char      ; yes -> wall removed
        LDA #($23 | $80)       ; '#' (wall present)
        JMP cellout

cellhash:
        LDA #($23 | $80)       ; '#'
        BNE cellout

cellspace:
        ; Check for START marker: CY=0, display col=1
        LDA cell_row
        BNE @not_start
        CPX #1
        BNE @not_start
        LDA #($53 | $80)       ; 'S'
        JMP cellout
@not_start:
        ; Check for END marker: CY=END_CY, display col=END_DX
        LDA cell_row
        CMP #END_CY
        BNE cellspace_char
        CPX #END_DX
        BNE cellspace_char
        LDA #($45 | $80)       ; 'E'
        JMP cellout

cellspace_char:
        LDA #($20 | $80)       ; ' '

cellout:
        JSR ECHO
        INX
        CPX #DISP_W
        BNE cellrowloop
        JSR hashcr

; === NEXT ROW ===
        INC cell_row
        LDA cell_row
        CMP #NROWS
        BEQ bottomborder
        JMP render_rowloop

; ======================================================
; BOTTOM BORDER + STATUS LINE
; ======================================================
bottomborder:
        LDX #DISP_W
@loop:  LDA #($23 | $80)
        JSR ECHO
        DEX
        BNE @loop
        JSR hashcr

        ; Status line (24th line, no trailing CR)
        LDA #<str_status
        LDX #>str_status
        JSR print_str_ax

; ======================================================
; WAITKEY: press key -> new maze
; ======================================================
waitkey:
        INC prng_lo
        BNE @no_hi
        INC prng_hi
@no_hi: LDA KBDCR
        BPL waitkey
        LDA KBD
        EOR prng_lo
        STA prng_lo
        JSR clearscr
        JMP restart

; ======================================================
; GENERATE_MAZE: Recursive Backtracker (DFS)
; ======================================================
generate_maze:
        ; --- Clear grid ---
        LDX #$00
        TXA
@clear: STA GRID,X
        INX
        CPX #NCELLS
        BNE @clear

        ; --- Start DFS at cell (0,0) ---
        LDA #$00
        STA cur_row
        STA cur_col
        STA stkp
        STA cell_idx
        ; Mark (0,0) as visited
        LDA #VISITED
        STA GRID

; --- DFS main loop ---
dfs_loop:
        ; --- Find unvisited neighbors ---
        LDY #$00               ; Y = direction count

        ; Check NORTH: cur_row > 0
        LDA cur_row
        BEQ @skip_n
        LDA cell_idx
        SEC
        SBC #NCOLS
        TAX
        LDA GRID,X
        BMI @skip_n            ; visited
        LDA #$00               ; DIR_NORTH
        STA dir_buf,Y
        INY
@skip_n:
        ; Check EAST: cur_col < NCOLS-1
        LDA cur_col
        CMP #(NCOLS-1)
        BCS @skip_e
        LDX cell_idx
        INX
        LDA GRID,X
        BMI @skip_e
        LDA #$01               ; DIR_EAST
        STA dir_buf,Y
        INY
@skip_e:
        ; Check SOUTH: cur_row < NROWS-1
        LDA cur_row
        CMP #(NROWS-1)
        BCS @skip_s
        LDA cell_idx
        CLC
        ADC #NCOLS
        TAX
        LDA GRID,X
        BMI @skip_s
        LDA #$02               ; DIR_SOUTH
        STA dir_buf,Y
        INY
@skip_s:
        ; Check WEST: cur_col > 0
        LDA cur_col
        BEQ @skip_w
        LDX cell_idx
        DEX
        LDA GRID,X
        BMI @skip_w
        LDA #$03               ; DIR_WEST
        STA dir_buf,Y
        INY
@skip_w:
        STY num_dirs

        ; Any unvisited neighbors?
        CPY #$00
        BEQ @backtrack

        ; --- Pick random direction ---
        JSR random
        AND #$03               ; mask 0-3
@mod:   CMP num_dirs
        BCC @mod_ok
        SBC num_dirs           ; carry set from CMP
        JMP @mod
@mod_ok:
        TAX
        LDA dir_buf,X          ; chosen direction in A

        ; --- Push current cell onto DFS stack ---
        PHA                    ; save direction
        LDX stkp
        LDA cell_idx
        STA DFS_STK,X
        INC stkp
        PLA                    ; restore direction

        ; --- Carve wall and move ---
        CMP #$01
        BEQ @go_east
        CMP #$02
        BEQ @go_south
        CMP #$03
        BEQ @go_west
        ; Fall through: DIR_NORTH (0)

@go_north:
        ; Set NORTH bit on current cell
        LDX cell_idx
        LDA GRID,X
        ORA #NORTH_BIT
        STA GRID,X
        DEC cur_row
        JMP @mark_new

@go_east:
        ; Set EAST bit on current cell
        LDX cell_idx
        LDA GRID,X
        ORA #EAST_BIT
        STA GRID,X
        INC cur_col
        JMP @mark_new

@go_south:
        ; Set NORTH bit on the SOUTH neighbor
        LDA cell_idx
        CLC
        ADC #NCOLS
        TAX
        LDA GRID,X
        ORA #NORTH_BIT
        STA GRID,X
        INC cur_row
        JMP @mark_new

@go_west:
        ; Set EAST bit on the WEST neighbor
        LDX cell_idx
        DEX
        LDA GRID,X
        ORA #EAST_BIT
        STA GRID,X
        DEC cur_col

@mark_new:
        ; Compute new cell_idx and mark visited
        LDX cur_row
        LDA row_offset,X
        CLC
        ADC cur_col
        STA cell_idx
        TAX
        LDA GRID,X
        ORA #VISITED
        STA GRID,X
        JMP dfs_loop

; --- Backtrack ---
@backtrack:
        LDA stkp
        BEQ @gen_done          ; stack empty = all cells visited
        DEC stkp
        LDX stkp
        LDA DFS_STK,X          ; pop cell_index

        ; Divide by 19 to recover (row, col)
        LDX #$00
@div:   CMP #NCOLS
        BCC @div_done
        SBC #NCOLS              ; carry set from CMP
        INX
        BCS @div                ; always taken
@div_done:
        STA cur_col
        STX cur_row

        ; Recompute cell_idx
        LDX cur_row
        LDA row_offset,X
        CLC
        ADC cur_col
        STA cell_idx
        JMP dfs_loop

@gen_done:
        RTS

; ======================================================
; SUBROUTINES
; ======================================================

; === RANDOM: 16-bit Galois LFSR ===
random:
        LDA prng_lo
        ASL A
        ROL prng_hi
        BCC @nofeedback
        EOR #$2D
@nofeedback:
        STA prng_lo
        RTS

; === HASHCR: print '#' then CR ===
hashcr:
        LDA #($23 | $80)
        JSR ECHO
        LDA #$8D
        JSR ECHO
        RTS

; === CLEARSCR: scroll 24 blank lines ===
clearscr:
        LDX #24
@loop:  LDA #$8D
        JSR ECHO
        DEX
        BNE @loop
        RTS

; === PRINT_STR_AX: print null-terminated ASCII string ===
print_str_ax:
        STA str_lo
        STX str_hi
        LDY #$00
@loop:  LDA (str_lo),Y
        BEQ @done
        ORA #$80
        JSR ECHO
        INY
        BNE @loop
@done:  RTS

; ======================================================
; TITLE SCREEN (fills 24 lines)
; ======================================================
show_title:
        JSR title_border        ; line 1
        LDA #4
        JSR empty_lines         ; lines 2-5
        LDA #<str_title
        LDX #>str_title
        JSR title_text          ; line 6
        JSR title_empty         ; line 7
        LDA #<str_algo
        LDX #>str_algo
        JSR title_text          ; line 8
        LDA #<str_apple
        LDX #>str_apple
        JSR title_text          ; line 9
        LDA #2
        JSR empty_lines         ; lines 10-11
        LDA #<str_author
        LDX #>str_author
        JSR title_text          ; line 12
        LDA #9
        JSR empty_lines         ; lines 13-21
        LDA #<str_press
        LDX #>str_press
        JSR title_text          ; line 22
        JSR title_empty         ; line 23
        ; Line 24: bottom border WITHOUT CR
        LDX #40
@bdr:   LDA #($23 | $80)
        JSR ECHO
        DEX
        BNE @bdr
        ; Wait for key (timing-based PRNG seed)
@wait:  INC prng_lo
        BNE @no_hi
        INC prng_hi
@no_hi: LDA KBDCR
        BPL @wait
        LDA KBD
        EOR prng_lo
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
@loop:  LDA #($23 | $80)
        JSR ECHO
        DEX
        BNE @loop
        LDA #$8D
        JSR ECHO
        RTS

; === TITLE_EMPTY: '#' + 38 spaces + '#' + CR ===
title_empty:
        LDA #($23 | $80)
        JSR ECHO
        LDX #38
@loop:  LDA #($20 | $80)
        JSR ECHO
        DEX
        BNE @loop
        LDA #($23 | $80)
        JSR ECHO
        LDA #$8D
        JSR ECHO
        RTS

; === TITLE_TEXT: '#' + text padded to 38 + '#' + CR ===
title_text:
        STA str_lo
        STX str_hi
        LDA #($23 | $80)
        JSR ECHO
        LDY #$00
        LDX #$00
@print: LDA (str_lo),Y
        BEQ @pad
        ORA #$80
        JSR ECHO
        INY
        INX
        BNE @print
@pad:   CPX #38
        BCS @right
        LDA #($20 | $80)
        JSR ECHO
        INX
        JMP @pad
@right: LDA #($23 | $80)
        JSR ECHO
        LDA #$8D
        JSR ECHO
        RTS

; ======================================================
; DATA
; ======================================================

; Row offset lookup table: row_offset[r] = r * 19
row_offset:
        .byte 0, 19, 38, 57, 76, 95, 114, 133, 152, 171, 190

; String data (normal ASCII, null-terminated)
str_title:
        .byte "           * M A Z E  II *", 0
str_algo:
        .byte "     Recursive Backtracker Maze", 0
str_apple:
        .byte "            for Apple 1", 0
str_author:
        .byte "         By VERHILLE Arnaud", 0
str_press:
        .byte "      Press any key to start...", 0
str_status:
        .byte " S=START E=END   ANY KEY=NEW MAZE", 0
