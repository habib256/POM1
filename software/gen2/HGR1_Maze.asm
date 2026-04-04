; =============================================
; HGR MAZE - GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; Recursive Backtracker (DFS) Algorithm
; 19x11 cells → 280x192 HGR screen
; =============================================
; Assemble with cc65:
;   ca65 -o build/HGR_Maze.o software/gen2/HGR_Maze.asm
;   ld65 -C software/gen2/apple1_gen2.cfg -o build/HGR_Maze.bin build/HGR_Maze.o
;
; The GEN2 linker config reserves $2000-$3FFF for the HGR
; framebuffer and places BSS (grid, stack) at $4000+.
;
; In POM1: plug GEN2 card, File > Load Memory (HGR_Maze.txt)
; then type 280R in Woz Monitor.
;
; Each grid unit maps to one HGR byte column (7 pixels wide)
; and 8 scanlines tall. The 39x23 grid covers 273x184 pixels.
; Walls are white ($7F). Start/end openings in the border
; are marked with green (NTSC artifact) and orange.
; =============================================

; --- Apple 1 I/O ---
ECHO    = $FFEF         ; Woz Monitor character output
KBDCR   = $D011         ; Keyboard control register
KBD     = $D010         ; Keyboard data register

; --- Maze constants ---
NCOLS   = 19            ; Cell columns
NROWS   = 11            ; Cell rows
NCELLS  = 209           ; NCOLS * NROWS
GRID_W  = 39            ; 2*NCOLS+1 display grid columns
GRID_H  = 23            ; 2*NROWS+1 display grid rows

; --- Fill byte patterns ---
WALL_BYTE  = $7F        ; All 7 pixels on, group 0 → white
START_BYTE = $55        ; Bits 0,2,4,6 on, group 0 → green at odd byte col
END_BYTE   = $D5        ; Bits 0,2,4,6 on, group 1 → orange at odd byte col

; --- Grid cell bitfield ---
NORTH_BIT = $01
EAST_BIT  = $02
VISITED   = $80

; --- Zero page variables ---
.zeropage
prng_lo:     .res 1     ; $00
prng_hi:     .res 1     ; $01
             .res 1     ; $02
temp:        .res 1     ; $03
temp2:       .res 1     ; $04
             .res 1     ; $05
ptr_lo:      .res 1     ; $06
ptr_hi:      .res 1     ; $07
cur_row:     .res 1     ; $08
cur_col:     .res 1     ; $09
stkp:        .res 1     ; $0A
num_dirs:    .res 1     ; $0B
cell_idx:    .res 1     ; $0C
             .res 1     ; $0D
dir_buf:     .res 4     ; $0E-$11
render_gx:   .res 1     ; $12
render_gy:   .res 1     ; $13

; --- Code at $0280 ---
.code

; =============================================
; MAIN
; =============================================
main:
        LDA #$01
        STA prng_lo
        LDA #$C7
        STA prng_hi

        ; Title on Apple 1 screen
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax

        ; Wait for key (seeds PRNG with timing)
@wait:  INC prng_lo
        BNE @no_hi
        INC prng_hi
@no_hi: LDA KBDCR
        BPL @wait
        LDA KBD
        EOR prng_lo
        STA prng_lo

; =============================================
; RESTART: generate and display a new maze
; =============================================
restart:
        JSR clear_hgr
        JSR generate_maze
        JSR render_maze
        JSR draw_markers

        LDA #<str_new
        LDX #>str_new
        JSR print_str_ax

; =============================================
; WAITKEY: press key → new maze
; =============================================
waitkey:
        INC prng_lo
        BNE @wk_no
        INC prng_hi
@wk_no: LDA KBDCR
        BPL waitkey
        LDA KBD
        EOR prng_lo
        STA prng_lo
        JMP restart

; =============================================
; CLEAR HGR: zero out $2000-$3FFF
; =============================================
clear_hgr:
        LDA #$00
        TAY
        STA ptr_lo
        LDX #$20
        STX ptr_hi
@clr:   STA (ptr_lo),Y
        INY
        BNE @clr
        INC ptr_hi
        LDX ptr_hi
        CPX #$40
        BNE @clr
        RTS

; =============================================
; RENDER MAZE: iterate grid, fill wall blocks
; =============================================
render_maze:
        LDA #$00
        STA render_gy
@rrow:  LDA #$00
        STA render_gx
@rcol:  JSR is_wall
        BEQ @rskip
        LDA #WALL_BYTE
        JSR fill_block
@rskip: INC render_gx
        LDA render_gx
        CMP #GRID_W
        BNE @rcol
        INC render_gy
        LDA render_gy
        CMP #GRID_H
        BNE @rrow
        RTS

; =============================================
; DRAW MARKERS: colored start/end blocks
; =============================================
draw_markers:
        ; Green entrance at top (1,0)
        LDA #$01
        STA render_gx
        LDA #$00
        STA render_gy
        LDA #START_BYTE
        JSR fill_block

        ; Orange exit at bottom (37,22)
        LDA #37
        STA render_gx
        LDA #(GRID_H-1)
        STA render_gy
        LDA #END_BYTE
        JSR fill_block
        RTS

; =============================================
; IS_WALL: returns A!=0 if wall, A=0 if passage
; =============================================
is_wall:
        ; Top border (gy=0) with opening at gx=1
        LDA render_gy
        BNE @not_top
        LDA render_gx
        CMP #$01
        BEQ @pass
        JMP @wall
@not_top:
        ; Bottom border (gy=22) with opening at gx=37
        LDA render_gy
        CMP #(GRID_H-1)
        BNE @not_bot
        LDA render_gx
        CMP #37
        BEQ @pass
        JMP @wall
@not_bot:
        ; Left/right border
        LDA render_gx
        BEQ @wall
        CMP #(GRID_W-1)
        BEQ @wall

        ; Check gy parity
        LDA render_gy
        AND #$01
        BEQ @even_gy

        ; --- gy odd: cell row ---
        LDA render_gx
        AND #$01
        BNE @pass           ; gx odd → cell interior → passage

        ; gx even, gy odd → vertical wall
        ; Check EAST of cell (gx/2-1, gy>>1)
        LDA render_gx
        LSR A
        SEC
        SBC #$01
        STA temp
        LDA render_gy
        LSR A
        TAX
        LDA row_offset,X
        CLC
        ADC temp
        TAX
        LDA GRID,X
        AND #EAST_BIT
        BNE @pass
        BEQ @wall

@even_gy:
        ; --- gy even: wall row ---
        LDA render_gx
        AND #$01
        BEQ @wall           ; gx even → intersection → wall

        ; gx odd, gy even → horizontal wall
        ; Check NORTH of cell (gx>>1, gy/2)
        LDA render_gx
        LSR A
        STA temp
        LDA render_gy
        LSR A
        TAX
        LDA row_offset,X
        CLC
        ADC temp
        TAX
        LDA GRID,X
        AND #NORTH_BIT
        BNE @pass

@wall:  LDA #$01
        RTS
@pass:  LDA #$00
        RTS

; =============================================
; FILL BLOCK: fill 7x8 pixel block at (gx, gy)
; A = byte value to write (e.g. $7F for white)
; Uses HGR non-linear scanline layout:
;   8 scanlines within a block are $0400 apart.
; =============================================
fill_block:
        STA temp2
        LDX render_gy
        LDA hgr_base_lo,X
        STA ptr_lo
        LDA hgr_base_hi,X
        STA ptr_hi
        LDY render_gx
        LDX #$08
@fl:    LDA temp2
        STA (ptr_lo),Y
        LDA ptr_hi
        CLC
        ADC #$04
        STA ptr_hi
        DEX
        BNE @fl
        RTS

; =============================================
; GENERATE MAZE: Recursive Backtracker (DFS)
; Same algorithm as Maze2_Backtracker
; =============================================
generate_maze:
        LDX #$00
        TXA
@clr:   STA GRID,X
        INX
        CPX #NCELLS
        BNE @clr

        LDA #$00
        STA cur_row
        STA cur_col
        STA stkp
        STA cell_idx
        LDA #VISITED
        STA GRID

; --- DFS main loop ---
dfs_loop:
        LDY #$00

        ; Check NORTH
        LDA cur_row
        BEQ @sk_n
        LDA cell_idx
        SEC
        SBC #NCOLS
        TAX
        LDA GRID,X
        BMI @sk_n
        LDA #$00
        STA dir_buf,Y
        INY
@sk_n:
        ; Check EAST
        LDA cur_col
        CMP #(NCOLS-1)
        BCS @sk_e
        LDX cell_idx
        INX
        LDA GRID,X
        BMI @sk_e
        LDA #$01
        STA dir_buf,Y
        INY
@sk_e:
        ; Check SOUTH
        LDA cur_row
        CMP #(NROWS-1)
        BCS @sk_s
        LDA cell_idx
        CLC
        ADC #NCOLS
        TAX
        LDA GRID,X
        BMI @sk_s
        LDA #$02
        STA dir_buf,Y
        INY
@sk_s:
        ; Check WEST
        LDA cur_col
        BEQ @sk_w
        LDX cell_idx
        DEX
        LDA GRID,X
        BMI @sk_w
        LDA #$03
        STA dir_buf,Y
        INY
@sk_w:
        STY num_dirs
        CPY #$00
        BEQ @back

        ; Pick random direction
        JSR random
        AND #$03
@mod:   CMP num_dirs
        BCC @mok
        SBC num_dirs
        JMP @mod
@mok:   TAX
        LDA dir_buf,X

        ; Push current cell
        PHA
        LDX stkp
        LDA cell_idx
        STA DFS_STK,X
        INC stkp
        PLA

        ; Carve wall and move
        CMP #$01
        BEQ @east
        CMP #$02
        BEQ @south
        CMP #$03
        BEQ @west

        ; NORTH: set NORTH bit, move up
        LDX cell_idx
        LDA GRID,X
        ORA #NORTH_BIT
        STA GRID,X
        DEC cur_row
        JMP @mark

@east:  LDX cell_idx
        LDA GRID,X
        ORA #EAST_BIT
        STA GRID,X
        INC cur_col
        JMP @mark

@south: LDA cell_idx
        CLC
        ADC #NCOLS
        TAX
        LDA GRID,X
        ORA #NORTH_BIT
        STA GRID,X
        INC cur_row
        JMP @mark

@west:  LDX cell_idx
        DEX
        LDA GRID,X
        ORA #EAST_BIT
        STA GRID,X
        DEC cur_col

@mark:  LDX cur_row
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
@back:  LDA stkp
        BEQ @done
        DEC stkp
        LDX stkp
        LDA DFS_STK,X

        ; Divide by 19 to recover (row, col)
        LDX #$00
@div:   CMP #NCOLS
        BCC @ddone
        SBC #NCOLS
        INX
        BCS @div
@ddone: STA cur_col
        STX cur_row

        LDX cur_row
        LDA row_offset,X
        CLC
        ADC cur_col
        STA cell_idx
        JMP dfs_loop

@done:  RTS

; =============================================
; SUBROUTINES
; =============================================

; 16-bit Galois LFSR
random:
        LDA prng_lo
        ASL A
        ROL prng_hi
        BCC @nf
        EOR #$2D
@nf:    STA prng_lo
        RTS

; Print null-terminated ASCII string (A=lo, X=hi)
print_str_ax:
        STA ptr_lo
        STX ptr_hi
        LDY #$00
@lp:    LDA (ptr_lo),Y
        BEQ @dn
        ORA #$80
        JSR ECHO
        INY
        BNE @lp
@dn:    RTS

; =============================================
; DATA TABLES
; =============================================

; Row offset: row_offset[r] = r * 19
row_offset:
        .byte 0, 19, 38, 57, 76, 95, 114, 133, 152, 171, 190

; HGR base address for each block row (gy = 0..22)
; base = $2000 + (gy/8)*$28 + (gy%8)*$80
; 8 scanlines within a block are spaced $0400 apart.
hgr_base_lo:
        .byte $00, $80, $00, $80, $00, $80, $00, $80   ; gy 0-7
        .byte $28, $A8, $28, $A8, $28, $A8, $28, $A8   ; gy 8-15
        .byte $50, $D0, $50, $D0, $50, $D0, $50        ; gy 16-22

hgr_base_hi:
        .byte $20, $20, $21, $21, $22, $22, $23, $23   ; gy 0-7
        .byte $20, $20, $21, $21, $22, $22, $23, $23   ; gy 8-15
        .byte $20, $20, $21, $21, $22, $22, $23        ; gy 16-22

; Strings (normal ASCII, null-terminated)
str_title:
        .byte $0D, " * HGR MAZE *", $0D
        .byte " GEN2 COLOR GRAPHICS CARD", $0D
        .byte " RECURSIVE BACKTRACKER", $0D
        .byte $0D, " PRESS ANY KEY...", $0D, 0

str_new:
        .byte " ANY KEY = NEW MAZE", $0D, 0

; =============================================
; BSS: runtime data at $4000+ (after HGR buffer)
; =============================================
.bss
GRID:    .res NCELLS     ; 209 bytes: cell data
DFS_STK: .res NCELLS     ; 209 bytes: DFS backtracking stack
