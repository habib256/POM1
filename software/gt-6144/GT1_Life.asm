; =============================================
; GT-6144 LIFE — Conway's Game of Life
; SWTPC GT-6144 Graphic Terminal (1976, $D00A)
; VERHILLE Arnaud - 2026
; =============================================
; 64x96 bit-packed cell grid. Seeded with an R-pentomino near the
; centre — evolves for about 1100 generations and stabilises into a
; mix of blinkers, blocks, gliders, and a beehive or two.
;
; Memory:
;   GRID_A = $1000 (768 bytes, what's currently on the GT-6144 screen)
;   GRID_B = $1400 (768 bytes, next generation being computed)
;   Row N lives at GRID_X + N * 8 (8 bytes per row, 8 cells per byte,
;   bit 0 = leftmost cell of the byte).
;
; Loop (the key optimisation — only CHANGED pixels are emitted):
;   DISPLAY_DELTA : for every byte where GRID_A != GRID_B, walk the
;                   diff bits and emit a single latch-X + commit-Y
;                   pair per changed cell (using GRID_B's new state).
;                   Copies GRID_B → GRID_A in the same sweep. Untouched
;                   bytes skip the entire bit loop.
;   STEP          : compute GRID_B = next_gen(GRID_A) using the usual
;                   B3/S23 rules. Clears GRID_B up front so we only
;                   SET bits for live cells.
;   Key poll      : any key → return to the Woz Monitor.
;
; Why the delta matters: a naive full repaint is 6144 cells × 2 writes
; = 12288 $D00A pokes per frame. With deltas the typical late-evolution
; Life board only changes a few dozen cells per gen, so we emit well
; under 200 pokes. At 1 MHz the animation becomes flicker-free and the
; perceived speed jumps roughly 10×.
;
; Initial paint trick: CLEAR_GT leaves the screen all-OFF. GRID_A is
; zeroed to match. SEED writes the R-pentomino into GRID_B (NOT A), so
; the very first DISPLAY_DELTA naturally paints the seed as "changes".
; No separate full-redraw path is needed.
;
; Performance envelope (STEP dominates):
;   STEP ≈ 7.4 M cycles/gen (~7 s at 1 MHz, ~150 ms at --cpu-max).
;   DISPLAY_DELTA ≈ 3 k cycles + ~60 cycles per changed cell.
;   Run with --cpu-max for a fluid animation.
;
; Assemble:
;   ca65 -o build/GT1_Life.o software/gt-6144/GT1_Life.asm
;   ld65 -C software/gt-6144/gt6144.cfg -o build/GT1_Life.bin build/GT1_Life.o
;
; Load + run (Wozmon):
;   300: <paste hex>
;   300R
;
; Preset: ./POM1 --preset 2 --cpu-max
; =============================================

GT_PORT   = $D00A
KBD       = $D010
KBDCR     = $D011
; Woz Monitor GETLINE entry that prints '\' + CR (real prompt). $FF1F
; would skip the '\' and look like a reboot — see GT1_Hello.asm.
WOZMON    = $FF1A

GRID_A    = $1000      ; current-on-screen generation
GRID_B    = $1400      ; next generation being computed

; Zero-page scratch
OROW      = $10        ; STEP outer row 0..95
OCOL      = $11        ; STEP outer col 0..63  — reused as OROW_BASE in DELTA
ROW       = $12        ; GET_CELL arg (signed; $FF = out-of-bounds)
COL       = $13        ; GET_CELL arg
NCNT      = $14        ; STEP neighbour count — reused as DIFF in DELTA
CENTER    = $15        ; STEP center-cell state — reused as DRAW_ROW in DELTA
NB_IDX    = $16        ; STEP neighbour index — reused as TMP2 in DRAW_BITS
RP_LO     = $17        ; grid byte pointer, low
RP_HI     = $18        ; grid byte pointer, high
TMP       = $19        ; scratch (row*8 low byte during address math)
NEW_B     = $1A        ; DELTA: new byte value (for ON/OFF decision per bit)
X_BASE    = $1B        ; DELTA: leftmost col of the current byte (0..56)
Y_SAVE    = $1C        ; DRAW_BITS: save Y across the bit loop

    .org $0300

; =============================================
; MAIN
; =============================================
START:
    jsr CLEAR_GT        ; wipe the Intel 2102 bistable power-on noise
    jsr ZERO_GRIDS      ; both A and B zero (A matches the blank screen)
    jsr SEED_B          ; drop the R-pentomino into GRID_B

MAIN:
    jsr DISPLAY_DELTA   ; emit pixels for (A ^ B) bits, then A := B
    jsr STEP            ; GRID_B = next_gen(GRID_A)
    lda KBDCR
    bpl MAIN            ; bit 7 clear => no key, keep evolving
    lda KBD             ; consume strobe
    jmp WOZMON

; =============================================
; CLEAR_GT — paint every pixel OFF (64x96 OFF-latch + Y-commit pairs).
; Called once at boot to mask the Intel 2102 SRAM power-on noise.
; =============================================
CLEAR_GT:
    ldx #0
@xl:
    stx GT_PORT         ; X < 64 => latch X, mode=OFF
    ldy #128
@yl:
    sty GT_PORT         ; commit pixel (X, Y-128) OFF
    iny
    cpy #224
    bne @yl
    inx
    cpx #64
    bne @xl
    rts

; =============================================
; ZERO_GRIDS — wipe both 768-byte buffers (rounded to 3 pages each).
; =============================================
ZERO_GRIDS:
    lda #0
    ldy #0
@l:
    sta GRID_A,y
    sta GRID_A+$100,y
    sta GRID_A+$200,y
    sta GRID_B,y
    sta GRID_B+$100,y
    sta GRID_B+$200,y
    iny
    bne @l
    rts

; =============================================
; SEED_B — R-pentomino placed with its bounding box around
; (row=46..48, col=31..33). Shape:
;       . # #       (46,32) (46,33)
;       # # .       (47,31) (47,32)
;       . # .       (48,32)
; The seed is written into GRID_B so the first DISPLAY_DELTA paints
; it as a set of changes-from-blank.
; =============================================
SEED_B:
    lda #$03            ; (46,32) bit0 + (46,33) bit1
    sta GRID_B+46*8+4
    lda #$80            ; (47,31) bit7
    sta GRID_B+47*8+3
    lda #$01            ; (47,32) bit0
    sta GRID_B+47*8+4
    lda #$01            ; (48,32) bit0
    sta GRID_B+48*8+4
    rts

; =============================================
; DISPLAY_DELTA — stream the delta between GRID_A and GRID_B to the
; GT-6144, and fold the new state into GRID_A so next frame's compare
; is free.
;
; Walk structure: 3 page-sized loops (Y 0..255), one per 32-row band.
; For each byte:
;   new = GRID_B[idx]
;   diff = GRID_A[idx] XOR new
;   if diff != 0: DRAW_BITS (emit pixels for the 1-bits)
;   GRID_A[idx] = new
; The `eor` + `beq` fast-path skips the bit loop for unchanged bytes —
; which is ~98% of them on a settled Life board.
; =============================================
DISPLAY_DELTA:
    lda #0
    sta OCOL            ; OROW_BASE = 0 (page 0 covers rows 0..31)
    ldy #0
@p0:
    lda GRID_B,y
    sta NEW_B
    eor GRID_A,y        ; A = diff
    beq @p0_skip
    sta NCNT            ; stash diff (reuse NCNT as DIFF)
    jsr DRAW_BITS
@p0_skip:
    lda NEW_B
    sta GRID_A,y
    iny
    bne @p0

    lda #32
    sta OCOL            ; page 1 covers rows 32..63
    ldy #0
@p1:
    lda GRID_B+$100,y
    sta NEW_B
    eor GRID_A+$100,y
    beq @p1_skip
    sta NCNT
    jsr DRAW_BITS
@p1_skip:
    lda NEW_B
    sta GRID_A+$100,y
    iny
    bne @p1

    lda #64
    sta OCOL            ; page 2 covers rows 64..95
    ldy #0
@p2:
    lda GRID_B+$200,y
    sta NEW_B
    eor GRID_A+$200,y
    beq @p2_skip
    sta NCNT
    jsr DRAW_BITS
@p2_skip:
    lda NEW_B
    sta GRID_A+$200,y
    iny
    bne @p2
    rts

; =============================================
; DRAW_BITS — walk the 8 bits of NCNT (DIFF) and emit one latch-X +
; commit-Y pair per set bit. Called from DISPLAY_DELTA.
;   Y         = in-page offset (row_in_page*8 + byte_col)
;   OCOL      = OROW_BASE (0 / 32 / 64 for pages 0/1/2)
;   NCNT      = diff byte (which bits changed)
;   NEW_B     = new byte (ON/OFF decision for each changed bit)
; Preserves Y for the caller's page walk.
; =============================================
DRAW_BITS:
    sty Y_SAVE
    ; DRAW_ROW = (Y >> 3) + OROW_BASE
    tya
    lsr a
    lsr a
    lsr a
    clc
    adc OCOL            ; + OROW_BASE
    sta CENTER          ; (reused as DRAW_ROW in this routine)
    ; X_BASE = (Y & 7) << 3
    tya
    and #$07
    asl a
    asl a
    asl a
    sta X_BASE
    ldx #0              ; bit index 0..7
@bit:
    lda BIT_MASK,x
    and NCNT
    beq @skip
    ; This cell changed — decide ON vs OFF from the new byte.
    lda BIT_MASK,x
    and NEW_B
    beq @off
    ; ON: latch X (col) with bit 6 set → mode=ON
    lda X_BASE
    stx NB_IDX          ; (reused as TMP2 — save X across the add)
    clc
    adc NB_IDX
    ora #$40
    jmp @emit
@off:
    ; OFF: latch X (col) with bit 6 clear → mode=OFF
    lda X_BASE
    stx NB_IDX
    clc
    adc NB_IDX
@emit:
    sta GT_PORT
    lda CENTER          ; DRAW_ROW
    ora #$80            ; commit Y (128..223 range)
    sta GT_PORT
@skip:
    inx
    cpx #8
    bne @bit
    ldy Y_SAVE
    rts

; =============================================
; STEP — compute GRID_B = next_gen(GRID_A) under Conway's B3/S23.
; For every (OROW, OCOL), count alive neighbours via GET_CELL. Edges
; are treated as permanently dead (GET_CELL's bounds check). GRID_B
; is cleared first so SET_LIVE only has to OR in bits.
; =============================================
STEP:
    ; Clear GRID_B (3 pages)
    lda #0
    ldy #0
@czl:
    sta GRID_B,y
    sta GRID_B+$100,y
    sta GRID_B+$200,y
    iny
    bne @czl

    lda #0
    sta OROW
@row_loop:
    lda #0
    sta OCOL
@col_loop:
    lda #0
    sta NCNT
    ldx #0              ; neighbour-delta index 0..7
@nb_loop:
    lda OROW
    clc
    adc DROW,x          ; DROW entry is signed byte ($FF = -1)
    sta ROW
    lda OCOL
    clc
    adc DCOL,x
    sta COL
    stx NB_IDX
    jsr GET_CELL
    ldx NB_IDX
    clc
    adc NCNT
    sta NCNT
    inx
    cpx #8
    bne @nb_loop

    ; Read the centre cell's current state
    lda OROW
    sta ROW
    lda OCOL
    sta COL
    jsr GET_CELL
    sta CENTER

    ; Life rules (B3/S23)
    lda CENTER
    beq @dead_rule
    ; alive: count in [2, 3] survives
    lda NCNT
    cmp #2
    beq @set_live
    cmp #3
    beq @set_live
    jmp @next
@dead_rule:
    ; dead: count == 3 → born
    lda NCNT
    cmp #3
    bne @next
@set_live:
    ; GRID_B[OROW][OCOL] |= bit
    lda #0
    sta RP_HI
    lda OROW
    asl a
    rol RP_HI
    asl a
    rol RP_HI
    asl a
    rol RP_HI
    sta TMP             ; row*8 low byte
    lda OCOL
    lsr a
    lsr a
    lsr a               ; byte_col = OCOL >> 3
    clc
    adc TMP
    sta RP_LO
    lda RP_HI
    adc #0
    sta RP_HI
    clc
    lda RP_LO
    adc #<GRID_B
    sta RP_LO
    lda RP_HI
    adc #>GRID_B
    sta RP_HI
    lda OCOL
    and #$07
    tax
    lda BIT_MASK,x
    ldy #0
    ora (RP_LO),y
    sta (RP_LO),y
@next:
    inc OCOL
    lda OCOL
    cmp #64
    beq @next_row
    jmp @col_loop
@next_row:
    inc OROW
    lda OROW
    cmp #96
    beq @done
    jmp @row_loop
@done:
    rts

; =============================================
; GET_CELL — returns A = 1 if cell (ROW, COL) in GRID_A is alive, else
; A = 0. ROW or COL = $FF (signed -1) falls into the bounds check.
; =============================================
GET_CELL:
    lda ROW
    cmp #96
    bcs @dead           ; ROW >= 96 (or $FF) => dead
    lda COL
    cmp #64
    bcs @dead           ; COL >= 64 (or $FF) => dead
    ; addr = GRID_A + ROW*8 + (COL>>3)
    lda #0
    sta RP_HI
    lda ROW
    asl a
    rol RP_HI
    asl a
    rol RP_HI
    asl a
    rol RP_HI
    sta TMP
    lda COL
    lsr a
    lsr a
    lsr a
    clc
    adc TMP
    sta RP_LO
    lda RP_HI
    adc #0
    sta RP_HI
    clc
    lda RP_LO
    adc #<GRID_A
    sta RP_LO
    lda RP_HI
    adc #>GRID_A
    sta RP_HI
    lda COL
    and #$07
    tax
    lda BIT_MASK,x
    ldy #0
    and (RP_LO),y
    beq @dead
    lda #1
    rts
@dead:
    lda #0
    rts

; =============================================
; Tables
; =============================================
; Bit 0 (leftmost cell in byte) at index 0, bit 7 (rightmost) at 7.
BIT_MASK: .byte $01, $02, $04, $08, $10, $20, $40, $80

; Neighbour deltas (dr, dc). $FF = signed -1; 6502 ADC treats these
; as unsigned bytes, and out-of-range values fall into GET_CELL's
; bounds check (ROW/COL == $FF reads as >= 96/64 → dead).
DROW: .byte $FF, $FF, $FF, $00, $00, $01, $01, $01
DCOL: .byte $FF, $00, $01, $FF, $01, $FF, $00, $01
