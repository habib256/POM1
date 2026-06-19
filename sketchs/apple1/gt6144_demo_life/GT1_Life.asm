; =============================================
; GT-6144 LIFE — Conway's Game of Life
; SWTPC GT-6144 Graphic Terminal (1976, $D00A)
; VERHILLE Arnaud - 2026
; =============================================
; 40x60 cell grid, centered on the 64x96 GT-6144 matrix, byte-per-cell
; storage with a ghost border (so neighbour counting skips the bounds
; check) and a B3/S23 rule LUT. Display writes only the cells that
; change from one generation to the next, using a 2-buffer flip so the
; previous frame IS the comparison baseline — the classic trick the
; HGR/TMS versions use, ported to the GT-6144's write-only framebuffer.
;
; Why this version is fast (prior bit-packed version was NOT):
;   - 1 byte per cell: neighbour count is a CLC + 7 chained ADC
;     (p0/p1/p2),Y — the max possible sum is 8 so no intermediate carry
;     handling is needed. ~12 cycles per neighbour instead of ~100 in
;     the bit-packed version that had to bounds-check + shift + mask.
;   - Ghost border (rows 0/61, cols 0/41 stay 0 forever) → no
;     per-neighbour bounds check.
;   - row_ofs[r] = r * ROW_SIZ precomputed → no runtime multiply.
;   - rule_lut[count*2 + alive] → one LDA, no branches per cell.
;   - Pointer swap (src/dst) each gen → no inter-frame copy.
;
; Display (DISPLAY_DELTA):
;   - Walk src & dst in parallel: if the byte differs, emit one
;     latch-X + commit-Y pair per changed cell using dst's new value.
;     Late-stage Life rarely changes more than a few dozen cells per
;     generation, so GT-6144 traffic stays well under 200 $D00A pokes
;     per frame (vs. 4800 if we naively repainted everything).
;
; Memory footprint (fits the real 8 KB dual-bank Apple-1+GT-6144 preset):
;   $0000-$001F     zero page (ca65-allocated)
;   $0300-~$050C    code + tables (this .bin file)
;   $0580-$0FAB     grid_a (42 * 62 = 2604 bytes, ghost-bordered)
;   $E000-$EA2B     grid_b (same layout, upper 4 KB bank)
;
; Cell layout:
;   cell (r, c), r in 1..60, c in 1..40
;   byte = grid[r*42 + c]   (0 = dead, 1 = alive)
;   ghost border at r = 0/61 and c = 0/41 stays 0 → dead edges.
;
; Display mapping:
;   The 40x60 grid is centered on the 64x96 GT-6144 matrix:
;     gt_col = (c - 1) + 12   (so cell col 1..40 → x 12..51)
;     gt_row = (r - 1) + 18   (so cell row 1..60 → y 18..77)
;
; Keyboard:
;   Any key → return to the Woz Monitor.
;
; Assemble:
;   Build: make
;
; Preset: ./POM1 --preset 5 --cpu-max --load 0300:build/GT1_Life.bin --run 0300
; =============================================

; ----- Apple 1 I/O -----
.include "apple1.inc"
.include "gt6144.inc"
; Woz Monitor GETLINE entry that prints '\' + CR (real prompt). $FF1F
; would skip the '\' — see GT1_Hello.asm for the trap rationale.

; ----- Geometry -----
ROW_SIZ  = 42              ; 40 interior cols + 2 ghost
N_ROWS   = 60              ; interior rows
N_COLS   = 40              ; interior cols
COL_OFS  = 12              ; GT-6144 x of cell col 1 (centred: 12+40+12 = 64)
ROW_OFS  = 18              ; GT-6144 y of cell row 1 (centred: 18+60+18 = 96)

; ----- Grid buffers -----
; POM1 models the real 8 KB layout as $0000-$0FFF + $E000-$EFFF, not
; contiguous $0000-$1FFF RAM. Keep one Life buffer in each bank.
grid_a  := $0580
grid_b  := $E000

; ----- Zero page -----
.zeropage
            .res 2          ; $00-$01 reserved
src_lo:     .res 1
src_hi:     .res 1
dst_lo:     .res 1
dst_hi:     .res 1
p0_lo:      .res 1          ; row r-1 (source read)
p0_hi:      .res 1
p1_lo:      .res 1          ; row r
p1_hi:      .res 1
p2_lo:      .res 1          ; row r+1
p2_hi:      .res 1
dstp_lo:    .res 1          ; row r (dst write)
dstp_hi:    .res 1
sp_lo:      .res 1          ; render_delta: src row r
sp_hi:      .res 1
dp_lo:      .res 1          ; render_delta: dst row r
dp_hi:      .res 1
row_i:      .res 1
col_i:      .res 1
n_cnt:      .res 1          ; neighbour count
n_alive:    .res 1          ; centre cell value (0/1)
tmp:        .res 1          ; scratch

.code

; =============================================
; MAIN
; =============================================
main:
        jsr clear_gt            ; wipe the Intel 2102 bistable noise
        jsr clear_grids         ; both grids zero (grid_a matches blank screen)
        jsr seed_into_b         ; R-pentomino into grid_b; first render paints it

        ; src = grid_a (on-screen, blank), dst = grid_b (seeded)
        lda #<grid_a
        sta src_lo
        lda #>grid_a
        sta src_hi
        lda #<grid_b
        sta dst_lo
        lda #>grid_b
        sta dst_hi

gen_loop:
        jsr render_delta        ; emit (src XOR dst) using dst values
        lda KBDCR
        bpl @no_key
        lda KBD                 ; consume key
        jmp WOZMON              ; clean return, GT-6144 image stays on screen
@no_key:
        ; Swap src <-> dst so next compute_next reads the new state
        lda src_lo
        ldx dst_lo
        stx src_lo
        sta dst_lo
        lda src_hi
        ldx dst_hi
        stx src_hi
        sta dst_hi
        jsr compute_next        ; src (now the just-rendered state) → dst
        jmp gen_loop

; =============================================
; clear_gt — paint every pixel OFF (64x96 OFF-latch + Y-commit pairs)
; =============================================
clear_gt:
        ldx #0
@xl:
        stx GT_PORT             ; X<64 → latch X, mode=OFF
        ldy #128
@yl:
        sty GT_PORT             ; commit pixel (X, Y-128) OFF
        iny
        cpy #224
        bne @yl
        inx
        cpx #64
        bne @xl
        rts

; =============================================
; clear_grids — zero both grid_a and grid_b (2604 bytes each).
; Uses a fall-through tail-call so the inner helper is reached twice.
; =============================================
clear_grids:
        lda #<grid_a
        sta p0_lo
        lda #>grid_a
        sta p0_hi
        jsr clear_2604
        lda #<grid_b
        sta p0_lo
        lda #>grid_b
        sta p0_hi
        ; fall through
clear_2604:
        lda #0
        ldy #0
        ldx #10             ; 10 full pages = 2560 bytes
@full:
        sta (p0_lo),y
        iny
        bne @full
        inc p0_hi
        dex
        bne @full
        ldy #44             ; 44 more (10*256 + 44 = 2604)
@tail:
        dey
        sta (p0_lo),y
        bne @tail
        rts

; =============================================
; seed_into_b — write the R-pentomino into grid_b (NOT grid_a).
; Placement: cells (r, c) = (30, 20), (30, 21), (31, 19), (31, 20),
; (32, 20). Shape:
;       . # #
;       # # .
;       . # .
; Centering: rows 30..32 in 1..60, cols 19..21 in 1..40.
; =============================================
seed_into_b:
        ; row 30 → r*42 = 1260 = $04EC, +c
        ; grid_b + $04EC = $E000 + $04EC = $E4EC
        lda #1                  ; (30,20)
        sta grid_b + $04EC + 20
        sta grid_b + $04EC + 21 ; (30,21)
        ; row 31 → 31*42 = 1302 = $0516; grid_b + $0516 = $E516
        sta grid_b + $0516 + 19 ; (31,19)
        sta grid_b + $0516 + 20 ; (31,20)
        ; row 32 → 32*42 = 1344 = $0540; grid_b + $0540 = $E540
        sta grid_b + $0540 + 20 ; (32,20)
        rts

; =============================================
; compute_next — one generation, src → dst, B3/S23 rules.
; Rows 1..60, cols 1..40. Ghost border never written so it stays 0.
;
; Neighbour count: 8 cells, each 0/1; max sum = 8; no inter-ADC CLC
; needed. Result indexed into rule_lut as count*2 + alive.
; =============================================
compute_next:
        lda #1
        sta row_i
@row_loop:
        ; p0 = src + row_ofs[row_i - 1]
        ldx row_i
        dex
        lda src_lo
        clc
        adc row_ofs_lo,x
        sta p0_lo
        lda src_hi
        adc row_ofs_hi,x
        sta p0_hi

        ; p1 = src + row_ofs[row_i]
        inx
        lda src_lo
        clc
        adc row_ofs_lo,x
        sta p1_lo
        lda src_hi
        adc row_ofs_hi,x
        sta p1_hi

        ; p2 = src + row_ofs[row_i + 1]
        inx
        lda src_lo
        clc
        adc row_ofs_lo,x
        sta p2_lo
        lda src_hi
        adc row_ofs_hi,x
        sta p2_hi

        ; dstp = dst + row_ofs[row_i]
        ldx row_i
        lda dst_lo
        clc
        adc row_ofs_lo,x
        sta dstp_lo
        lda dst_hi
        adc row_ofs_hi,x
        sta dstp_hi

        lda #1
        sta col_i
@col_loop:
        ; 8-neighbour sum (no intermediate carry: cells are 0/1, max 8).
        ldy col_i
        dey                     ; Y = c-1
        clc
        lda (p0_lo),y           ; p0[c-1]
        iny
        adc (p0_lo),y           ; + p0[c]
        iny
        adc (p0_lo),y           ; + p0[c+1]
        dey
        dey
        adc (p1_lo),y           ; + p1[c-1]
        iny
        iny
        adc (p1_lo),y           ; + p1[c+1]   (skip center)
        dey
        dey
        adc (p2_lo),y           ; + p2[c-1]
        iny
        adc (p2_lo),y           ; + p2[c]
        iny
        adc (p2_lo),y           ; + p2[c+1]
        sta n_cnt

        ldy col_i
        lda (p1_lo),y           ; centre cell
        sta n_alive

        ; next = rule_lut[count*2 + alive]
        lda n_cnt
        asl
        ora n_alive
        tay
        lda rule_lut,y

        ldy col_i
        sta (dstp_lo),y

        inc col_i
        lda col_i
        cmp #(N_COLS + 1)       ; = 41
        bne @col_loop

        inc row_i
        lda row_i
        cmp #(N_ROWS + 1)       ; = 61
        beq @done
        jmp @row_loop           ; long branch (body > 128 bytes)
@done:
        rts

; =============================================
; render_delta — walk src and dst in lockstep. For every cell where
; they differ, emit one latch-X + commit-Y pair on the GT-6144 using
; dst's new value. Typical per-frame traffic: tens to low hundreds of
; $D00A writes (vs. 4800 for a full repaint).
;
; Hot path per cell is ~15 cycles when src == dst (LDA-CMP-BEQ chain),
; ~40 cycles when they differ (decide ON/OFF, emit 2 bytes to GT_PORT).
; =============================================
render_delta:
        lda #1
        sta row_i
@row_loop:
        ; sp = src + row_ofs[row_i]
        ldx row_i
        lda src_lo
        clc
        adc row_ofs_lo,x
        sta sp_lo
        lda src_hi
        adc row_ofs_hi,x
        sta sp_hi

        ; dp = dst + row_ofs[row_i]
        lda dst_lo
        clc
        adc row_ofs_lo,x
        sta dp_lo
        lda dst_hi
        adc row_ofs_hi,x
        sta dp_hi

        ; gt_row = row_i - 1 + ROW_OFS (pre-OR'd with $80 since that's
        ; what the GT-6144 commit-Y phase wants)
        lda row_i
        clc
        adc #(ROW_OFS - 1) | $80
        sta tmp                 ; tmp = commit-Y byte for this row

        ldy #1
@col_loop:
        lda (dp_lo),y           ; new
        cmp (sp_lo),y           ; vs old
        beq @skip               ; unchanged → no $D00A writes
        ; Changed. A still holds the new value (CMP doesn't modify A).
        ; Decide ON vs OFF based on new == 0 (dead) vs non-zero (alive).
        cmp #0
        beq @emit_off
        ; Alive: latch X with bit 6 set → mode=ON, X = Y - 1 + COL_OFS
        tya
        clc
        adc #(COL_OFS - 1) | $40
        sta GT_PORT
        jmp @commit
@emit_off:
        ; Dead: latch X with bit 6 clear → mode=OFF
        tya
        clc
        adc #(COL_OFS - 1)
        sta GT_PORT
@commit:
        lda tmp                 ; commit-Y byte (row | $80)
        sta GT_PORT
@skip:
        iny
        cpy #(N_COLS + 1)       ; = 41
        bne @col_loop

        inc row_i
        lda row_i
        cmp #(N_ROWS + 1)       ; = 61
        beq @done
        jmp @row_loop           ; long branch
@done:
        rts

; =============================================
; DATA
; =============================================

; B3/S23 rule LUT. index = count*2 + alive ; count 0..8, alive 0/1.
rule_lut:
        .byte 0, 0              ; count=0: under-pop
        .byte 0, 0              ; count=1: under-pop
        .byte 0, 1              ; count=2:            alive SURVIVES
        .byte 1, 1              ; count=3: BIRTH      SURVIVES
        .byte 0, 0              ; count=4: over-pop
        .byte 0, 0              ; count=5
        .byte 0, 0              ; count=6
        .byte 0, 0              ; count=7
        .byte 0, 0              ; count=8

; row_ofs[r] = r * 42  (0 <= r <= 61)
row_ofs_lo:
        .repeat 62, I
            .byte <(I * ROW_SIZ)
        .endrepeat
row_ofs_hi:
        .repeat 62, I
            .byte >(I * ROW_SIZ)
        .endrepeat
