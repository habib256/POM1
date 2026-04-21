; =============================================
; HGR CONWAY'S GAME OF LIFE
; GEN2 Color Graphics Card - POM1 / Apple 1
; 40x40 cells, 7x4 pixels each, double-buffered
; B3/S23 rules, dead borders (gliders die at edge)
; Default pattern: Gosper Glider Gun (period 30)
; =============================================
; Assemble:
;   ca65 -o build/HGR10_Life.o software/hgr/HGR10_Life.asm
;   ld65 -C software/hgr/apple1_gen2.cfg \
;        -o build/HGR10_Life.bin build/HGR10_Life.o
;
; Or just:
;   python3 software/hgr/emit_HGR10_Life_txt.py
;
; Run in POM1: plug GEN2 card (auto-enabled when loading from
; software/hgr/), File > Load Memory HGR10_Life.txt, then 280R
; in the Woz Monitor. Any key exits.
;
; Memory footprint (all within 8 KB from $0280):
;   $0280-~$0700  code + static tables (output file)
;   $1200-$18E3   grid_a       (1764 B, zeroed at boot)
;   $1900-$1FE3   grid_b       (1764 B, zeroed at boot)
;   $2000-$3FFF   HGR framebuffer (GEN2 reads this)
;
; Cell layout:       cell (r, c), r in 1..40, c in 1..40
;                    byte = grid[r*42 + c]   (0 = dead, 1 = alive)
;                    ghost border at r = 0 / 41 and c = 0 / 41
;                    stays 0 forever -> no toroidal wrap
; Display mapping:   scanline y = 16 + (r-1)*4 .. 16 + (r-1)*4 + 3
;                    HGR byte col = c - 1 (7 pixels wide)
; =============================================

; ----- Apple 1 I/O -----
KBDCR   = $D011
KBD     = $D010

; ----- Geometry -----
ROW_SIZ   = 42              ; 40 interior cells + 2 ghost
Y_OFFSET  = 16              ; centers 160-pixel area in 192-line HGR

; ----- Runtime RAM (absolute, NOT in the output file) -----
grid_a  := $1200
grid_b  := $1900

; ----- Zero page -----
.zeropage
            .res 2          ; $00-$01 reserved
src_lo:     .res 1
src_hi:     .res 1
dst_lo:     .res 1
dst_hi:     .res 1
p0_lo:      .res 1          ; src row r-1
p0_hi:      .res 1
p1_lo:      .res 1          ; src row r
p1_hi:      .res 1
p2_lo:      .res 1          ; src row r+1
p2_hi:      .res 1
dstp_lo:    .res 1          ; dst row r
dstp_hi:    .res 1
hgr_lo_p:   .res 1          ; HGR scanline base - 1
hgr_hi_p:   .res 1
row_i:      .res 1
col_i:      .res 1
y_cur:      .res 1
line_cnt:   .res 1
n_cnt:      .res 1          ; neighbor count
n_alive:    .res 1          ; center cell value
tmp:        .res 1          ; scratch

.code

; =============================================
; MAIN: boot, then infinite render/step loop
; =============================================
main:
        JSR clear_hgr
        JSR clear_grids
        JSR init_pattern
        LDA KBD             ; swallow any stale key from POM1 boot

        LDA #<grid_a
        STA src_lo
        LDA #>grid_a
        STA src_hi
        LDA #<grid_b
        STA dst_lo
        LDA #>grid_b
        STA dst_hi

gen_loop:
        JSR render
        LDA KBDCR           ; bit 7 = key ready -> exit
        BPL @no_key
        LDA KBD             ; consume before handing back to Woz
        RTS
@no_key:
        JSR compute_next
        ; swap src <-> dst (single byte each, via A and X)
        LDA src_lo
        LDX dst_lo
        STX src_lo
        STA dst_lo
        LDA src_hi
        LDX dst_hi
        STX src_hi
        STA dst_hi
        JMP gen_loop

; =============================================
; clear_hgr: zero $2000-$3FFF (8 KB framebuffer)
; Trashes A, X, Y; uses hgr_lo_p / hgr_hi_p as pointer.
; =============================================
clear_hgr:
        LDA #$00
        STA hgr_lo_p
        LDA #$20
        STA hgr_hi_p
        LDY #0
        LDA #0              ; stays 0 for the whole run
@lp:
        STA (hgr_lo_p),Y
        INY
        BNE @lp
        INC hgr_hi_p
        LDX hgr_hi_p
        CPX #$40
        BNE @lp
        RTS

; =============================================
; clear_grids: zero 1764 bytes at grid_a AND grid_b.
; Uses a fall-through tail-call so the inner helper
; is reached twice (once via JSR, once by fall-through).
; =============================================
clear_grids:
        LDA #<grid_a
        STA p0_lo
        LDA #>grid_a
        STA p0_hi
        JSR clear_1764
        LDA #<grid_b
        STA p0_lo
        LDA #>grid_b
        STA p0_hi
        ; fall through: clear_1764 will RTS back to our caller
clear_1764:
        LDA #0
        LDY #0
        LDX #6              ; 6 full pages = 1536 bytes
@full:
        STA (p0_lo),Y
        INY
        BNE @full
        INC p0_hi
        DEX
        BNE @full
        LDY #228            ; 228 more (6*256 + 228 = 1764)
@tail:
        DEY
        STA (p0_lo),Y
        BNE @tail
        RTS

; =============================================
; init_pattern: walk init_data, set grid_a[r][c]=1
; for each (r, c) pair. Terminator = $FF.
; Rows/cols are 1..40 (interior only).
; =============================================
init_pattern:
        LDX #0
@lp:
        LDA init_data,X
        CMP #$FF
        BEQ @done
        STA row_i
        INX
        LDA init_data,X
        STA col_i
        INX
        STX tmp             ; save table cursor

        ; p0 = grid_a + row_ofs[row_i]
        LDY row_i
        LDA #<grid_a
        CLC
        ADC row_ofs_lo,Y
        STA p0_lo
        LDA #>grid_a
        ADC row_ofs_hi,Y
        STA p0_hi
        LDY col_i
        LDA #1
        STA (p0_lo),Y

        LDX tmp
        JMP @lp
@done:
        RTS

; =============================================
; render: blit current src grid to $2000-$3FFF.
; Each cell -> 7 pixels wide ($7F if alive, $00 if dead),
; replicated across 4 scanlines.
; y_cur is maintained incrementally: init once to Y_OFFSET,
; then @scan_loop increments it 4 times per cell-row, which
; lands exactly on the next cell-row's base y.
; =============================================
render:
        LDA #Y_OFFSET
        STA y_cur
        LDA #0
        STA row_i
@row_loop:
        ; p1 = src + row_ofs[row_i + 1]  (interior row)
        LDX row_i
        INX
        LDA src_lo
        CLC
        ADC row_ofs_lo,X
        STA p1_lo
        LDA src_hi
        ADC row_ofs_hi,X
        STA p1_hi

        LDA #4
        STA line_cnt
@scan_loop:
        ; hgr_ptr = scanline_base[y_cur] - 1
        ; so (hgr_ptr),Y for Y=1..40 writes HGR columns 0..39
        ; (lets us share Y with the grid's 1..40 indexing)
        LDY y_cur
        LDA hgr_lo_tbl,Y
        SEC
        SBC #1
        STA hgr_lo_p
        LDA hgr_hi_tbl,Y
        SBC #0
        STA hgr_hi_p

        LDY #1
@col_loop:
        LDA (p1_lo),Y       ; grid cell (0 or 1)
        TAX
        LDA pixel_byte,X    ; $00 or $7F
        STA (hgr_lo_p),Y
        INY
        CPY #41
        BNE @col_loop

        INC y_cur
        DEC line_cnt
        BNE @scan_loop

        INC row_i
        LDA row_i
        CMP #40
        BNE @row_loop
        RTS

; =============================================
; compute_next: src -> dst, one generation of B3/S23.
; Rows 1..40, cols 1..40. Borders (r or c = 0, 41)
; never written - they stay 0 from clear_grids.
;
; Neighbor count = sum of 8 cells around (r, c).
; Cells are 0/1 and max sum is 8, so a single CLC at
; the top of the inner loop and seven chained ADCs
; never overflow - no intermediate CLC needed.
;
; Final rule: index = count*2 + alive -> rule_lut[0..17]
; =============================================
compute_next:
        LDA #1
        STA row_i
@row_loop:
        ; p0 = src + row_ofs[row_i - 1]
        LDX row_i
        DEX
        LDA src_lo
        CLC
        ADC row_ofs_lo,X
        STA p0_lo
        LDA src_hi
        ADC row_ofs_hi,X
        STA p0_hi

        ; p1 = src + row_ofs[row_i]
        INX
        LDA src_lo
        CLC
        ADC row_ofs_lo,X
        STA p1_lo
        LDA src_hi
        ADC row_ofs_hi,X
        STA p1_hi

        ; p2 = src + row_ofs[row_i + 1]
        INX
        LDA src_lo
        CLC
        ADC row_ofs_lo,X
        STA p2_lo
        LDA src_hi
        ADC row_ofs_hi,X
        STA p2_hi

        ; dstp = dst + row_ofs[row_i]
        LDX row_i
        LDA dst_lo
        CLC
        ADC row_ofs_lo,X
        STA dstp_lo
        LDA dst_hi
        ADC row_ofs_hi,X
        STA dstp_hi

        LDA #1
        STA col_i
@col_loop:
        ; Sum 8 neighbors (cells are 0/1, max sum = 8, no carry).
        LDY col_i
        DEY                 ; Y = c-1
        CLC
        LDA (p0_lo),Y       ; p0[c-1]
        INY
        ADC (p0_lo),Y       ; + p0[c]
        INY
        ADC (p0_lo),Y       ; + p0[c+1]
        DEY
        DEY
        ADC (p1_lo),Y       ; + p1[c-1]
        INY
        INY
        ADC (p1_lo),Y       ; + p1[c+1]   (skip center)
        DEY
        DEY
        ADC (p2_lo),Y       ; + p2[c-1]
        INY
        ADC (p2_lo),Y       ; + p2[c]
        INY
        ADC (p2_lo),Y       ; + p2[c+1]
        STA n_cnt

        LDY col_i
        LDA (p1_lo),Y       ; center cell
        STA n_alive

        ; next = rule_lut[count*2 + alive]
        LDA n_cnt
        ASL
        ORA n_alive
        TAY
        LDA rule_lut,Y

        LDY col_i
        STA (dstp_lo),Y

        INC col_i
        LDA col_i
        CMP #41
        BNE @col_loop

        INC row_i
        LDA row_i
        CMP #41
        BEQ @done
        JMP @row_loop       ; long branch (body > 128 bytes)
@done:
        RTS

; =============================================
; DATA
; =============================================

; Map grid cell value (0/1) to HGR byte ($00 / $7F)
pixel_byte:
        .byte $00, $7F

; B3/S23 via 18-entry LUT, index = count*2 + alive.
; count in 0..8, alive in {0, 1}.
rule_lut:
        .byte 0, 0          ; count=0: under-pop
        .byte 0, 0          ; count=1: under-pop
        .byte 0, 1          ; count=2:           alive SURVIVES
        .byte 1, 1          ; count=3: BIRTH     SURVIVES
        .byte 0, 0          ; count=4: over-pop
        .byte 0, 0          ; count=5
        .byte 0, 0          ; count=6
        .byte 0, 0          ; count=7
        .byte 0, 0          ; count=8

; Initial cell list as (row, col) pairs; $FF ends.
; Gosper Glider Gun (36 cells, period 30) at origin (2, 1).
; With dead borders, each emitted glider dies at the edge
; but the gun itself stays stable and keeps producing.
init_data:
        .byte  2, 25
        .byte  3, 23,   3, 25
        .byte  4, 13,   4, 14,   4, 21,   4, 22,   4, 35,   4, 36
        .byte  5, 12,   5, 16,   5, 21,   5, 22,   5, 35,   5, 36
        .byte  6,  1,   6,  2,   6, 11,   6, 17,   6, 21,   6, 22
        .byte  7,  1,   7,  2,   7, 11,   7, 15,   7, 17,   7, 18
        .byte  7, 23,   7, 25
        .byte  8, 11,   8, 17,   8, 25
        .byte  9, 12,   9, 16
        .byte 10, 13,  10, 14
        ; R-pentomino at (30, 20) for chaotic contrast
        .byte 30, 21,  30, 22
        .byte 31, 20,  31, 21
        .byte 32, 21
        .byte $FF

; row_ofs[r] = r * 42  (0 <= r <= 41)
row_ofs_lo:
        .repeat 42, I
            .byte <(I * 42)
        .endrepeat
row_ofs_hi:
        .repeat 42, I
            .byte >(I * 42)
        .endrepeat

; HGR scanline base address (Apple II interleaved layout):
;   addr[y] = $2000 + (y mod 8)*$0400
;                   + ((y/8) mod 8)*$80
;                   + (y/64)*$28
hgr_lo_tbl:
        .repeat 192, YS
            .byte <($2000 + ((YS .MOD 8) * $0400) + (((YS / 8) .MOD 8) * $80) + ((YS / 64) * $28))
        .endrepeat
hgr_hi_tbl:
        .repeat 192, YS
            .byte >($2000 + ((YS .MOD 8) * $0400) + (((YS / 8) .MOD 8) * $80) + ((YS / 64) * $28))
        .endrepeat
