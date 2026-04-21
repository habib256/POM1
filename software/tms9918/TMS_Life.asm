; =============================================
; TMS CONWAY'S GAME OF LIFE — multi-pattern edition
; P-LAB TMS9918 Graphic Card - POM1 / Apple 1
; 32x24 cells (one 8x8 char per cell), double-buffered
; B3/S23 rules, dead borders
;
; Keyboard:
;   ANY KEY   -> cycle to next pattern (wraps)
;   ESC ($1B) -> exit to Woz Monitor
;
; Patterns shipped (wrap after last):
;   0 Pulsar              (period-3 oscillator, 13x13)
;   1 Pentadecathlon      (period-15 oscillator)
;   2 Die Hard            (7 cells, vanishes after 130 gens)
;   3 Acorn               (7-cell methuselah, chaotic growth)
;   4 R-pentomino         (5-cell methuselah, grandfather of chaos)
;   5 Spaceship Parade    (two LWSS + a glider crossing the grid)
; =============================================
; Assemble:
;   ca65 -o build/TMS_Life.o software/tms9918/TMS_Life.asm
;   ld65 -C software/hgr/apple1_gen2.cfg \
;        -o build/TMS_Life.bin build/TMS_Life.o
;
; Or just:
;   python3 software/tms9918/emit_TMS_Life_txt.py
;
; Run in POM1: plug the TMS9918 card (auto-enabled when loading from
; software/tms9918/), File > Load Memory TMS_Life.txt, then 280R in
; the Woz Monitor. Tap any key to change pattern, ESC to exit.
;
; Memory footprint:
;   $0280-~$0700  code + pattern tables (output file)
;   $4000-$4373   grid_a       (884 B, zeroed at boot)
;   $4400-$4773   grid_b       (884 B, zeroed at boot)
;   VRAM on card  pattern/name/color tables (not main bus)
;
; Cell layout:       cell (r, c), r in 1..24, c in 1..32
;                    byte = grid[r*34 + c]   (0 = dead, 1 = alive)
;                    ghost border at r = 0 / 25 and c = 0 / 33
;                    stays 0 forever -> no toroidal wrap
; Display mapping:   cell value IS the TMS9918 char code:
;                      0 -> char 0 (all $00, dead)
;                      1 -> char 1 (all $FF, alive)
;                    Name table at $1800, row-major, 32 chars per row.
; =============================================

; ----- Apple 1 I/O -----
KBDCR   = $D011
KBD     = $D010

; ----- TMS9918 MMIO -----
VDP_DATA = $CC00
VDP_CTRL = $CC01

; ----- Geometry -----
ROW_SIZ   = 34              ; 32 interior cells + 2 ghost columns
N_ROWS    = 24              ; interior rows
N_COLS    = 32              ; interior cols

; ----- Keys -----
KEY_ESC   = $9B             ; Apple 1 ESC (bit 7 set)

; ----- Pattern catalog -----
NUM_PATTERNS = 6

; ----- Runtime RAM (absolute, NOT in the output file) -----
grid_a  := $4000
grid_b  := $4400

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
row_i:      .res 1
col_i:      .res 1
n_cnt:      .res 1          ; neighbor count
n_alive:    .res 1          ; center cell value
tmp:        .res 1          ; scratch
pat_idx:    .res 1          ; current pattern index (0..NUM_PATTERNS-1)
pat_lo:     .res 1          ; cursor into active pattern table
pat_hi:     .res 1

.code

; =============================================
; MAIN: boot, then infinite render/step loop
; =============================================
main:
        LDA #0
        STA pat_idx
        JSR init_vdp
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
        LDA KBDCR           ; bit 7 = key ready
        BPL @no_key
        LDA KBD             ; consume key
        CMP #KEY_ESC
        BEQ @exit
        JSR next_pattern    ; any other key -> cycle
        JMP gen_loop
@exit:
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
; next_pattern: advance pat_idx (with wrap), clear
; both grids, seed grid_a from the new pattern, then
; reset src/dst so the next render shows grid_a.
; =============================================
next_pattern:
        INC pat_idx
        LDA pat_idx
        CMP #NUM_PATTERNS
        BNE @ok
        LDA #0
        STA pat_idx
@ok:
        JSR clear_grids
        JSR init_pattern
        LDA #<grid_a
        STA src_lo
        LDA #>grid_a
        STA src_hi
        LDA #<grid_b
        STA dst_lo
        LDA #>grid_b
        STA dst_hi
        RTS

; =============================================
; init_vdp: set up Graphics I mode, upload the two
; pattern glyphs (dead=char 0, alive=char 1), install
; colour group 0 = green-on-black, and clear the name
; table to char 0 (all dead).
; =============================================
init_vdp:
        LDX #0
@regloop:
        LDA vdp_regs,X
        STA VDP_CTRL
        TXA
        ORA #$80
        STA VDP_CTRL
        INX
        CPX #8
        BNE @regloop

        ; Pattern table at $0000: 16 bytes for chars 0-1.
        LDA #$00
        STA VDP_CTRL
        LDA #$40            ; $00 | $40 = write to $0000
        STA VDP_CTRL
        LDX #0
@ptn:
        LDA patterns_chars,X
        STA VDP_DATA
        INX
        CPX #16
        BNE @ptn

        ; Colour table at $2000 - write group 0 only (chars 0-7 green on black).
        LDA #$00
        STA VDP_CTRL
        LDA #$60            ; $20 | $40 = write to $2000
        STA VDP_CTRL
        LDA #$21            ; fg=2 medium green, bg=1 black
        STA VDP_DATA

        ; Clear name table at $1800 (768 bytes = char 0 everywhere).
        LDA #$00
        STA VDP_CTRL
        LDA #$58            ; $18 | $40 = write to $1800
        STA VDP_CTRL
        LDX #3              ; 3 full pages = 768 bytes
        LDA #0
@clr_pg:
        LDY #0
@clr_b:
        STA VDP_DATA
        INY
        BNE @clr_b
        DEX
        BNE @clr_pg
        RTS

; =============================================
; clear_grids: zero 884 bytes at grid_a AND grid_b.
; Grids are page-aligned so the page loop + tail works.
; =============================================
clear_grids:
        LDA #<grid_a
        STA p0_lo
        LDA #>grid_a
        STA p0_hi
        JSR clear_884
        LDA #<grid_b
        STA p0_lo
        LDA #>grid_b
        STA p0_hi
        ; fall through
clear_884:
        LDA #0
        LDY #0
        LDX #3              ; 3 full pages = 768 bytes
@full:
        STA (p0_lo),Y
        INY
        BNE @full
        INC p0_hi
        DEX
        BNE @full
        LDY #116            ; 116 more (3*256 + 116 = 884)
@tail:
        DEY
        STA (p0_lo),Y
        BNE @tail
        RTS

; =============================================
; init_pattern: walk the pattern table indexed by
; pat_idx, set grid_a[r][c]=1 for each (r, c) pair.
; Terminator = $FF. Rows 1..24, cols 1..32 (interior).
; =============================================
init_pattern:
        LDX pat_idx
        LDA patterns_lo,X
        STA pat_lo
        LDA patterns_hi,X
        STA pat_hi
        LDY #0
@lp:
        LDA (pat_lo),Y
        CMP #$FF
        BEQ @done
        STA row_i
        INY
        LDA (pat_lo),Y
        STA col_i
        INY
        STY tmp             ; save table cursor

        ; p0 = grid_a + row_ofs[row_i]
        LDX row_i
        LDA #<grid_a
        CLC
        ADC row_ofs_lo,X
        STA p0_lo
        LDA #>grid_a
        ADC row_ofs_hi,X
        STA p0_hi
        LDY col_i
        LDA #1
        STA (p0_lo),Y

        LDY tmp             ; restore table cursor
        JMP @lp
@done:
        RTS

; =============================================
; render: stream current src grid to the TMS9918
; name table at $1800. Cell values (0/1) are used
; directly as char codes. One VDP address set-up,
; then 24 rows * 32 bytes = 768 sequential writes.
; =============================================
render:
        ; Set VDP write address = $1800 (name table base)
        LDA #$00
        STA VDP_CTRL
        LDA #$58            ; $18 | $40
        STA VDP_CTRL

        LDA #1
        STA row_i
@row_loop:
        ; p1 = src + row_ofs[row_i]
        LDX row_i
        LDA src_lo
        CLC
        ADC row_ofs_lo,X
        STA p1_lo
        LDA src_hi
        ADC row_ofs_hi,X
        STA p1_hi

        LDY #1
@col_loop:
        LDA (p1_lo),Y
        STA VDP_DATA        ; cell value IS the char code
        INY
        CPY #33
        BNE @col_loop

        INC row_i
        LDA row_i
        CMP #25
        BNE @row_loop
        RTS

; =============================================
; compute_next: src -> dst, one generation of B3/S23.
; Rows 1..24, cols 1..32. Borders (r or c = 0, 25, 33)
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
        CMP #33
        BNE @col_loop

        INC row_i
        LDA row_i
        CMP #25
        BEQ @done
        JMP @row_loop       ; long branch (body > 128 bytes)
@done:
        RTS

; =============================================
; DATA
; =============================================

; VDP register setup (Graphics I, 16K, screen on, no int).
;   R0=$00 Graphics I, no external VDP input
;   R1=$C0 16K VRAM, screen on, no interrupt, 8x8 sprites, no mag
;   R2=$06 name table at $1800
;   R3=$80 colour table at $2000
;   R4=$00 pattern table at $0000
;   R5=$36 sprite attribute at $1B00
;   R6=$07 sprite pattern at $3800
;   R7=$01 backdrop colour = black
vdp_regs:
        .byte $00, $C0, $06, $80, $00, $36, $07, $01

; Pattern-table glyphs uploaded at VRAM $0000:
;   char 0 = all $00 (dead cell, invisible on black backdrop)
;   char 1 = all $FF (alive cell, solid green block)
patterns_chars:
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF

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

; ---------- Pattern catalog ----------
; Each pattern is a stream of (row, col) byte pairs, rows in 1..24,
; cols in 1..32, terminated by $FF. NUM_PATTERNS must match.

patterns_lo:
        .byte <pattern_pulsar,  <pattern_pentadeca, <pattern_diehard
        .byte <pattern_acorn,   <pattern_rpent,     <pattern_parade
patterns_hi:
        .byte >pattern_pulsar,  >pattern_pentadeca, >pattern_diehard
        .byte >pattern_acorn,   >pattern_rpent,     >pattern_parade

; Pattern 0 — Pulsar. Period-3 oscillator, 13x13 bounding box,
; centered on (12, 16). Top-left at (6, 10), bottom-right at (18, 22).
pattern_pulsar:
        .byte  6, 12,   6, 13,   6, 14,   6, 18,   6, 19,   6, 20
        .byte  8, 10,   8, 15,   8, 17,   8, 22
        .byte  9, 10,   9, 15,   9, 17,   9, 22
        .byte 10, 10,  10, 15,  10, 17,  10, 22
        .byte 11, 12,  11, 13,  11, 14,  11, 18,  11, 19,  11, 20
        .byte 13, 12,  13, 13,  13, 14,  13, 18,  13, 19,  13, 20
        .byte 14, 10,  14, 15,  14, 17,  14, 22
        .byte 15, 10,  15, 15,  15, 17,  15, 22
        .byte 16, 10,  16, 15,  16, 17,  16, 22
        .byte 18, 12,  18, 13,  18, 14,  18, 18,  18, 19,  18, 20
        .byte $FF

; Pattern 1 — Pentadecathlon. Period-15 oscillator, 3x10 bounding.
; Centered at rows 11-13, cols 11-20.
pattern_pentadeca:
        .byte 11, 13,  11, 18
        .byte 12, 11,  12, 12,  12, 14,  12, 15
        .byte 12, 16,  12, 17,  12, 19,  12, 20
        .byte 13, 13,  13, 18
        .byte $FF

; Pattern 2 — Die Hard. 7 cells, vanishes after 130 generations.
pattern_diehard:
        .byte 11, 18
        .byte 12, 12,  12, 13
        .byte 13, 13,  13, 17,  13, 18,  13, 19
        .byte $FF

; Pattern 3 — Acorn. 7-cell methuselah; grows chaotically.
pattern_acorn:
        .byte 11, 14
        .byte 12, 16
        .byte 13, 13,  13, 14,  13, 17,  13, 18,  13, 19
        .byte $FF

; Pattern 4 — R-pentomino. Classic 5-cell methuselah;
; stabilises around generation 1103 on an infinite grid.
pattern_rpent:
        .byte 11, 16,  11, 17
        .byte 12, 15,  12, 16
        .byte 13, 16
        .byte $FF

; Pattern 5 — Spaceship Parade.
;   LWSS going right starting at (4, 2), moves toward c/2 east
;   Glider heading south-east at (10, 5)
;   LWSS going left starting at (16, 26), moves toward c/2 west
pattern_parade:
        ; LWSS right (rows 4-7, cols 2-6)
        .byte  4,  3,   4,  4,   4,  5,   4,  6
        .byte  5,  2,   5,  6
        .byte  6,  6
        .byte  7,  2,   7,  5
        ; Glider SE (rows 10-12, cols 5-7)
        .byte 10,  6
        .byte 11,  7
        .byte 12,  5,  12,  6,  12,  7
        ; LWSS left (rows 16-19, cols 26-30)
        .byte 16, 26,  16, 27,  16, 28,  16, 29
        .byte 17, 26,  17, 30
        .byte 18, 26
        .byte 19, 27,  19, 30
        .byte $FF

; row_ofs[r] = r * 34  (0 <= r <= 25)
row_ofs_lo:
        .repeat 26, I
            .byte <(I * 34)
        .endrepeat
row_ofs_hi:
        .repeat 26, I
            .byte >(I * 34)
        .endrepeat
