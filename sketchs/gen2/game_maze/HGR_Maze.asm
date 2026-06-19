; =============================================
; HGR MAZE - GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; Recursive Backtracker (DFS) Algorithm
; 34x23 cells -> 280x192 HGR screen
; Sub-byte rendering with lookup tables
; =============================================
; Assemble with cc65:
;   Build: make
;
; The GEN2 linker config reserves $2000-$3FFF for the HGR
; framebuffer; BSS (grid, DFS stacks) is page-aligned in the
; low bank at $0300-$0EFF so the program runs on the
; Parmigiani 8 KB dual-bank Apple-1 (preset 11) where the
; gap $1000-$DFFF has no RAM.
;
; In POM1: plug GEN2 card, File > Load Memory (HGR_Maze.txt)
; then type E000R in Woz Monitor.
;
; Each grid unit maps to a 4x4 pixel block.
; 4 pixels don't align to 7-pixel HGR byte boundaries,
; so walls are rendered via sub-byte read-modify-write
; using lookup tables (col_byte, col_mask1, col_mask2).
;
; With 4 adjacent pixels per wall segment, NTSC artifact
; color resolves to solid white (prevOn || nextOn).
;
; The 69x47 display grid covers 276x188 pixels.
; Start/end openings in the top/bottom border.
; =============================================

; --- Apple 1 I/O ---
.include "apple1.inc"

; --- Maze constants ---
NCOLS   = 34            ; Cell columns
NROWS   = 23            ; Cell rows
GRID_W  = 69            ; 2*NCOLS+1 display grid columns
GRID_H  = 47            ; 2*NROWS+1 display grid rows

; --- Grid cell bitfield ---
NORTH_BIT = $01
EAST_BIT  = $02
VISITED   = $80

; --- Runtime RAM (page-aligned for efficient indirect access) ---
; Placed in the dual-bank low window ($0300-$0EFF) so the
; program runs on the 8 KB Parmigiani Apple-1 (preset 11).
; Each block reserves 4 pages (1 KB) but only uses 782 bytes.
GRID       = $0300      ; 782 bytes: cell data ($0300-$060D)
DFS_STK_LO = $0700      ; 782 bytes: stack low bytes ($0700-$0A0D)
DFS_STK_HI = $0B00      ; 782 bytes: stack high bytes ($0B00-$0E0D)

; --- Zero page variables ---
.zeropage
prng_lo:     .res 1     ; $00
prng_hi:     .res 1     ; $01
             .res 1     ; $02
temp:        .res 1     ; $03
temp2:       .res 1     ; $04
temp3:       .res 1     ; $05
ptr_lo:      .res 1     ; $06 - HGR pointer
ptr_hi:      .res 1     ; $07
cur_row:     .res 1     ; $08
cur_col:     .res 1     ; $09
stkp_lo:     .res 1     ; $0A
stkp_hi:     .res 1     ; $0B
num_dirs:    .res 1     ; $0C
cell_idx_lo: .res 1     ; $0D
cell_idx_hi: .res 1     ; $0E
dir_buf:     .res 4     ; $0F-$12
render_gx:   .res 1     ; $13
render_gy:   .res 1     ; $14
gptr_lo:     .res 1     ; $15 - GRID pointer (low byte always $00)
gptr_hi:     .res 1     ; $16
temp_lo:     .res 1     ; $17
temp_hi:     .res 1     ; $18

; --- Code at $E000 ---
.code

; =============================================
; MAIN
; =============================================
main:
        JSR gen2_hgr_init
        ; Initialize grid pointer low byte (always $00 for page-aligned access)
        LDA #$00
        STA gptr_lo

        ; Seed PRNG
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

        LDA #<str_new
        LDX #>str_new
        JSR print_str_ax

; =============================================
; WAITKEY: press key -> new maze
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
.include "hgr_clear.asm"

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
; IS_WALL: returns A!=0 if wall, A=0 if passage
; =============================================
is_wall:
        ; --- Top border (gy=0) with opening at gx=1 ---
        LDA render_gy
        BNE @not_top
        LDA render_gx
        CMP #$01
        BNE @top_wall
        JMP @pass
@top_wall:
        JMP @wall
@not_top:
        ; --- Bottom border (gy=46) with opening at gx=67 ---
        LDA render_gy
        CMP #(GRID_H-1)
        BNE @not_bot
        LDA render_gx
        CMP #67
        BEQ @pass
        JMP @wall
@not_bot:
        ; --- Left/right border ---
        LDA render_gx
        BEQ @wall
        CMP #(GRID_W-1)
        BEQ @wall

        ; --- Check gy parity ---
        LDA render_gy
        AND #$01
        BEQ @even_gy

        ; --- gy odd: cell row ---
        LDA render_gx
        AND #$01
        BNE @pass           ; gx odd -> cell interior -> passage

        ; gx even, gy odd -> vertical wall
        ; Check EAST of cell (gx/2-1, gy>>1)
        LDA render_gx
        LSR A
        SEC
        SBC #$01
        STA temp            ; col = gx/2 - 1
        LDA render_gy
        LSR A               ; row = gy >> 1
        TAX
        CLC
        LDA row_offset_lo,X
        ADC temp
        STA temp_lo
        LDA row_offset_hi,X
        ADC #$00
        STA temp_hi
        ; Read GRID[temp_lo/hi]
        CLC
        LDA #>GRID
        ADC temp_hi
        STA gptr_hi
        LDY temp_lo
        LDA (gptr_lo),Y
        AND #EAST_BIT
        BNE @pass
        BEQ @wall

@even_gy:
        ; --- gy even: wall row ---
        LDA render_gx
        AND #$01
        BEQ @wall           ; gx even -> intersection -> wall

        ; gx odd, gy even -> horizontal wall
        ; Check NORTH of cell (gx>>1, gy/2)
        LDA render_gx
        LSR A               ; col = gx >> 1
        STA temp
        LDA render_gy
        LSR A               ; row = gy / 2
        TAX
        CLC
        LDA row_offset_lo,X
        ADC temp
        STA temp_lo
        LDA row_offset_hi,X
        ADC #$00
        STA temp_hi
        CLC
        LDA #>GRID
        ADC temp_hi
        STA gptr_hi
        LDY temp_lo
        LDA (gptr_lo),Y
        AND #NORTH_BIT
        BNE @pass

@wall:  LDA #$01
        RTS
@pass:  LDA #$00
        RTS

; =============================================
; FILL BLOCK: fill 4x4 pixel block at (gx, gy)
; Sub-byte read-modify-write via lookup tables.
; 4 scanlines within a block are $0400 apart.
; =============================================
fill_block:
        LDX render_gx
        LDA col_mask1,X
        STA temp2           ; mask for first byte
        LDA col_mask2,X
        STA temp3           ; mask for second byte (0 = single byte)
        LDA col_byte,X
        TAY                 ; Y = byte column offset

        LDX render_gy
        LDA hgr_base_lo,X
        STA ptr_lo
        LDA hgr_base_hi,X
        STA ptr_hi

        LDX #$04            ; 4 scanlines
@fl:    LDA (ptr_lo),Y
        ORA temp2
        STA (ptr_lo),Y

        LDA temp3
        BEQ @no_s
        INY
        LDA (ptr_lo),Y
        ORA temp3
        STA (ptr_lo),Y
        DEY
@no_s:  LDA ptr_hi
        CLC
        ADC #$04
        STA ptr_hi

        DEX
        BNE @fl
        RTS

; =============================================
; GENERATE MAZE: Recursive Backtracker (DFS)
; 16-bit cell indices for 34x23 = 782 cells
; =============================================
generate_maze:
        ; --- Clear GRID: zero 4 pages (covers 782 bytes) ---
        LDA #$00
        TAY
        STA gptr_lo
        LDX #$40
        STX gptr_hi
        LDX #$04
@clr:   STA (gptr_lo),Y
        INY
        BNE @clr
        INC gptr_hi
        DEX
        BNE @clr

        ; --- Start DFS at cell (0,0) ---
        LDA #$00
        STA cur_row
        STA cur_col
        STA stkp_lo
        STA stkp_hi
        STA cell_idx_lo
        STA cell_idx_hi

        ; Mark (0,0) as visited
        LDA #>GRID
        STA gptr_hi
        LDY #$00
        LDA #VISITED
        STA (gptr_lo),Y

; --- DFS main loop ---
dfs_loop:
        LDA #$00
        STA num_dirs

        ; ---- Check NORTH: cur_row > 0 ----
        LDA cur_row
        BEQ @skip_n
        ; neighbor = cell_idx - NCOLS
        SEC
        LDA cell_idx_lo
        SBC #NCOLS
        STA temp_lo
        LDA cell_idx_hi
        SBC #$00
        STA temp_hi
        CLC
        LDA #>GRID
        ADC temp_hi
        STA gptr_hi
        LDY temp_lo
        LDA (gptr_lo),Y
        BMI @skip_n         ; visited
        LDX num_dirs
        LDA #$00            ; DIR_NORTH
        STA dir_buf,X
        INC num_dirs
@skip_n:

        ; ---- Check EAST: cur_col < NCOLS-1 ----
        LDA cur_col
        CMP #(NCOLS-1)
        BCS @skip_e
        ; neighbor = cell_idx + 1
        CLC
        LDA cell_idx_lo
        ADC #$01
        STA temp_lo
        LDA cell_idx_hi
        ADC #$00
        STA temp_hi
        CLC
        LDA #>GRID
        ADC temp_hi
        STA gptr_hi
        LDY temp_lo
        LDA (gptr_lo),Y
        BMI @skip_e
        LDX num_dirs
        LDA #$01            ; DIR_EAST
        STA dir_buf,X
        INC num_dirs
@skip_e:

        ; ---- Check SOUTH: cur_row < NROWS-1 ----
        LDA cur_row
        CMP #(NROWS-1)
        BCS @skip_s
        ; neighbor = cell_idx + NCOLS
        CLC
        LDA cell_idx_lo
        ADC #NCOLS
        STA temp_lo
        LDA cell_idx_hi
        ADC #$00
        STA temp_hi
        CLC
        LDA #>GRID
        ADC temp_hi
        STA gptr_hi
        LDY temp_lo
        LDA (gptr_lo),Y
        BMI @skip_s
        LDX num_dirs
        LDA #$02            ; DIR_SOUTH
        STA dir_buf,X
        INC num_dirs
@skip_s:

        ; ---- Check WEST: cur_col > 0 ----
        LDA cur_col
        BEQ @skip_w
        ; neighbor = cell_idx - 1
        LDA cell_idx_lo
        SEC
        SBC #$01
        STA temp_lo
        LDA cell_idx_hi
        SBC #$00
        STA temp_hi
        CLC
        LDA #>GRID
        ADC temp_hi
        STA gptr_hi
        LDY temp_lo
        LDA (gptr_lo),Y
        BMI @skip_w
        LDX num_dirs
        LDA #$03            ; DIR_WEST
        STA dir_buf,X
        INC num_dirs
@skip_w:

        ; Any unvisited neighbors?
        LDA num_dirs
        BNE @has_dirs
        JMP @backtrack
@has_dirs:

        ; ---- Pick random direction ----
        JSR random
        AND #$03
@mod:   CMP num_dirs
        BCC @mod_ok
        SBC num_dirs        ; carry set from CMP
        JMP @mod
@mod_ok:
        TAX
        LDA dir_buf,X       ; chosen direction in A

        ; ---- Push current cell onto DFS stack ----
        PHA                 ; save direction
        ; DFS_STK_LO[stkp] = cell_idx_lo
        CLC
        LDA #>DFS_STK_LO
        ADC stkp_hi
        STA gptr_hi
        LDY stkp_lo
        LDA cell_idx_lo
        STA (gptr_lo),Y
        ; DFS_STK_HI[stkp] = cell_idx_hi
        CLC
        LDA #>DFS_STK_HI
        ADC stkp_hi
        STA gptr_hi
        ; Y still = stkp_lo
        LDA cell_idx_hi
        STA (gptr_lo),Y
        ; stkp++
        INC stkp_lo
        BNE @no_sinc
        INC stkp_hi
@no_sinc:
        PLA                 ; restore direction

        ; ---- Carve wall and move ----
        CMP #$01
        BEQ @go_east
        CMP #$02
        BEQ @go_south
        CMP #$03
        BEQ @go_west

        ; --- NORTH: set NORTH bit on current cell, move up ---
        CLC
        LDA #>GRID
        ADC cell_idx_hi
        STA gptr_hi
        LDY cell_idx_lo
        LDA (gptr_lo),Y
        ORA #NORTH_BIT
        STA (gptr_lo),Y
        DEC cur_row
        JMP @mark_new

@go_east:
        ; Set EAST bit on current cell, move right
        CLC
        LDA #>GRID
        ADC cell_idx_hi
        STA gptr_hi
        LDY cell_idx_lo
        LDA (gptr_lo),Y
        ORA #EAST_BIT
        STA (gptr_lo),Y
        INC cur_col
        JMP @mark_new

@go_south:
        ; Set NORTH bit on SOUTH neighbor, move down
        CLC
        LDA cell_idx_lo
        ADC #NCOLS
        STA temp_lo
        LDA cell_idx_hi
        ADC #$00
        STA temp_hi
        CLC
        LDA #>GRID
        ADC temp_hi
        STA gptr_hi
        LDY temp_lo
        LDA (gptr_lo),Y
        ORA #NORTH_BIT
        STA (gptr_lo),Y
        INC cur_row
        JMP @mark_new

@go_west:
        ; Set EAST bit on WEST neighbor, move left
        LDA cell_idx_lo
        SEC
        SBC #$01
        STA temp_lo
        LDA cell_idx_hi
        SBC #$00
        STA temp_hi
        CLC
        LDA #>GRID
        ADC temp_hi
        STA gptr_hi
        LDY temp_lo
        LDA (gptr_lo),Y
        ORA #EAST_BIT
        STA (gptr_lo),Y
        DEC cur_col

@mark_new:
        ; Compute new cell_idx = row_offset[cur_row] + cur_col
        LDX cur_row
        CLC
        LDA row_offset_lo,X
        ADC cur_col
        STA cell_idx_lo
        LDA row_offset_hi,X
        ADC #$00
        STA cell_idx_hi

        ; Mark visited: GRID[cell_idx] |= VISITED
        CLC
        LDA #>GRID
        ADC cell_idx_hi
        STA gptr_hi
        LDY cell_idx_lo
        LDA (gptr_lo),Y
        ORA #VISITED
        STA (gptr_lo),Y
        JMP dfs_loop

; --- Backtrack ---
@backtrack:
        ; Stack empty?
        LDA stkp_lo
        ORA stkp_hi
        BEQ @gen_done

        ; stkp--
        LDA stkp_lo
        BNE @no_sdec
        DEC stkp_hi
@no_sdec:
        DEC stkp_lo

        ; Pop cell_idx from stack
        CLC
        LDA #>DFS_STK_LO
        ADC stkp_hi
        STA gptr_hi
        LDY stkp_lo
        LDA (gptr_lo),Y
        STA temp_lo

        CLC
        LDA #>DFS_STK_HI
        ADC stkp_hi
        STA gptr_hi
        ; Y still = stkp_lo
        LDA (gptr_lo),Y
        STA temp_hi

        ; Divide temp (16-bit) by NCOLS (34) to recover (row, col)
        LDX #$00            ; row counter
@div:   LDA temp_hi
        BNE @subtract       ; hi > 0 -> definitely >= 34
        LDA temp_lo
        CMP #NCOLS
        BCC @div_done
@subtract:
        SEC
        LDA temp_lo
        SBC #NCOLS
        STA temp_lo
        LDA temp_hi
        SBC #$00
        STA temp_hi
        INX
        JMP @div
@div_done:
        LDA temp_lo
        STA cur_col
        STX cur_row

        ; Recompute cell_idx
        LDX cur_row
        CLC
        LDA row_offset_lo,X
        ADC cur_col
        STA cell_idx_lo
        LDA row_offset_hi,X
        ADC #$00
        STA cell_idx_hi
        JMP dfs_loop

@gen_done:
        RTS

; =============================================
; SUBROUTINES
; =============================================

; random — promoted to dev/lib/m6502/prng8.asm (Tier 2.2 mutualization).
.include "prng8.asm"

; print_str_ax — promoted to dev/lib/apple1/print.asm (Tier 2 mutualization).
.include "print.asm"
.include "gen2_init.asm"

; =============================================
; DATA TABLES
; =============================================

; --- Row offset: row_offset[r] = r * 34 (16-bit, r=0..22) ---
row_offset_lo:
        .byte $00,$22,$44,$66,$88,$AA,$CC,$EE ; r 0-7
        .byte $10,$32,$54,$76,$98,$BA,$DC,$FE ; r 8-15
        .byte $20,$42,$64,$86,$A8,$CA,$EC     ; r 16-22

row_offset_hi:
        .byte $00,$00,$00,$00,$00,$00,$00,$00 ; r 0-7
        .byte $01,$01,$01,$01,$01,$01,$01,$01 ; r 8-15
        .byte $02,$02,$02,$02,$02,$02,$02     ; r 16-22

; --- HGR base address for each grid row (gy=0..46) ---
; base = scanlineAddress(gy * 4)
; 4 scanlines within a block are spaced $0400 apart.
hgr_base_lo:
        .byte $00,$00,$80,$80,$00,$00,$80,$80 ; gy 0-7
        .byte $00,$00,$80,$80,$00,$00,$80,$80 ; gy 8-15
        .byte $28,$28,$A8,$A8,$28,$28,$A8,$A8 ; gy 16-23
        .byte $28,$28,$A8,$A8,$28,$28,$A8,$A8 ; gy 24-31
        .byte $50,$50,$D0,$D0,$50,$50,$D0,$D0 ; gy 32-39
        .byte $50,$50,$D0,$D0,$50,$50,$D0     ; gy 40-46

hgr_base_hi:
        .byte $20,$30,$20,$30,$21,$31,$21,$31 ; gy 0-7
        .byte $22,$32,$22,$32,$23,$33,$23,$33 ; gy 8-15
        .byte $20,$30,$20,$30,$21,$31,$21,$31 ; gy 16-23
        .byte $22,$32,$22,$32,$23,$33,$23,$33 ; gy 24-31
        .byte $20,$30,$20,$30,$21,$31,$21,$31 ; gy 32-39
        .byte $22,$32,$22,$32,$23,$33,$23     ; gy 40-46

; --- Sub-byte rendering lookup tables (gx=0..68) ---
; Pattern repeats every 7 grid columns (4 pixels / 7 bits per byte):
;   gx%7=0: byte+0, mask $0F, no 2nd byte
;   gx%7=1: byte+0, mask $70, 2nd byte mask $01
;   gx%7=2: byte+1, mask $1E, no 2nd byte
;   gx%7=3: byte+1, mask $60, 2nd byte mask $03
;   gx%7=4: byte+2, mask $3C, no 2nd byte
;   gx%7=5: byte+2, mask $40, 2nd byte mask $07
;   gx%7=6: byte+3, mask $78, no 2nd byte

; Byte column for each grid column
col_byte:
        .byte  0, 0, 1, 1, 2, 2, 3           ; gx 0-6
        .byte  4, 4, 5, 5, 6, 6, 7           ; gx 7-13
        .byte  8, 8, 9, 9,10,10,11           ; gx 14-20
        .byte 12,12,13,13,14,14,15           ; gx 21-27
        .byte 16,16,17,17,18,18,19           ; gx 28-34
        .byte 20,20,21,21,22,22,23           ; gx 35-41
        .byte 24,24,25,25,26,26,27           ; gx 42-48
        .byte 28,28,29,29,30,30,31           ; gx 49-55
        .byte 32,32,33,33,34,34,35           ; gx 56-62
        .byte 36,36,37,37,38,38              ; gx 63-68

; Bit mask for first byte
col_mask1:
        .byte $0F,$70,$1E,$60,$3C,$40,$78    ; gx 0-6
        .byte $0F,$70,$1E,$60,$3C,$40,$78    ; gx 7-13
        .byte $0F,$70,$1E,$60,$3C,$40,$78    ; gx 14-20
        .byte $0F,$70,$1E,$60,$3C,$40,$78    ; gx 21-27
        .byte $0F,$70,$1E,$60,$3C,$40,$78    ; gx 28-34
        .byte $0F,$70,$1E,$60,$3C,$40,$78    ; gx 35-41
        .byte $0F,$70,$1E,$60,$3C,$40,$78    ; gx 42-48
        .byte $0F,$70,$1E,$60,$3C,$40,$78    ; gx 49-55
        .byte $0F,$70,$1E,$60,$3C,$40,$78    ; gx 56-62
        .byte $0F,$70,$1E,$60,$3C,$40        ; gx 63-68

; Bit mask for second byte (0 = no second byte)
col_mask2:
        .byte $00,$01,$00,$03,$00,$07,$00    ; gx 0-6
        .byte $00,$01,$00,$03,$00,$07,$00    ; gx 7-13
        .byte $00,$01,$00,$03,$00,$07,$00    ; gx 14-20
        .byte $00,$01,$00,$03,$00,$07,$00    ; gx 21-27
        .byte $00,$01,$00,$03,$00,$07,$00    ; gx 28-34
        .byte $00,$01,$00,$03,$00,$07,$00    ; gx 35-41
        .byte $00,$01,$00,$03,$00,$07,$00    ; gx 42-48
        .byte $00,$01,$00,$03,$00,$07,$00    ; gx 49-55
        .byte $00,$01,$00,$03,$00,$07,$00    ; gx 56-62
        .byte $00,$01,$00,$03,$00,$07        ; gx 63-68

; Strings (normal ASCII, null-terminated)
str_title:
        .byte $0D, " * HGR MAZE *", $0D
        .byte " GEN2 COLOR GRAPHICS CARD", $0D
        .byte " 34X23 CELLS - 4X4 PX", $0D
        .byte $0D, " PRESS ANY KEY...", $0D, 0

str_new:
        .byte " ANY KEY = NEW MAZE", $0D, 0
