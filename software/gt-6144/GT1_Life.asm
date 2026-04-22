; =============================================
; GT-6144 LIFE — Conway's Game of Life
; SWTPC GT-6144 Graphic Terminal (1976, $D00A)
; VERHILLE Arnaud - 2026
; =============================================
; 64x96 bit-packed cell grid. Seeded with an R-pentomino near the
; centre — evolves for about 1100 generations and stabilises into a
; mix of blinkers, blocks, a glider, and a beehive or two.
;
; Memory:
;   GRID_A = $1000 (768 bytes, current generation)
;   GRID_B = $1400 (768 bytes, next generation)
;   Row N lives at GRID_X + N * 8 (8 bytes per row, 8 cells per byte,
;   bit 0 = leftmost cell of the byte).
;
; Main loop:
;   DISPLAY (paint GRID_A) → STEP (compute GRID_B) → copy B→A → key?
;   Any key returns to the Woz Monitor immediately (Wozmon never
;   touches $D00A, so the last life picture stays on screen).
;
; Performance envelope:
;   STEP ≈ 7.4 M CPU cycles per generation (~7 s at 1 MHz, ~150 ms at
;   --cpu-max on a modern host). DISPLAY ≈ 180 k cycles (fast enough
;   to be invisible). Run with --cpu-max for a fluid animation.
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

GT_PORT  = $D00A
KBD      = $D010
KBDCR    = $D011
; Woz Monitor GETLINE entry that prints '\' + CR (real prompt).
; $FF1F would skip the '\' and look like a reboot — see GT1_Hello.asm.
WOZMON   = $FF1A

GRID_A   = $1000
GRID_B   = $1400

; Zero-page scratch
OROW     = $10          ; outer-loop row  (0..95)
OCOL     = $11          ; outer-loop col  (0..63)
ROW      = $12          ; GET_CELL arg — row (may be $FF for -1)
COL      = $13          ; GET_CELL arg — col (may be $FF for -1)
NCNT     = $14          ; neighbour count 0..8
CENTER   = $15          ; center cell state 0/1
NB_IDX   = $16          ; neighbour-loop index / generic save slot
RP_LO    = $17          ; grid byte pointer low
RP_HI    = $18          ; grid byte pointer high
TMP      = $19          ; scratch (row*8 low byte during address math)
GBYTE    = $1A          ; DISPLAY: cached byte from GRID_A
BYTE_SAV = $1B          ; DISPLAY: saved Y (byte-col) across bit loop

    .org $0300

; =============================================
; MAIN
; =============================================
START:
    jsr CLEAR_GT        ; wipe the Intel 2102 bistable power-on noise
    jsr ZERO_GRIDS      ; zero both A and B
    jsr SEED            ; drop the R-pentomino

MAIN:
    jsr DISPLAY
    jsr STEP
    jsr COPY_B_TO_A
    ; Any-key → exit.
    lda KBDCR
    bpl MAIN            ; bit 7 clear => no key, keep evolving
    lda KBD             ; consume strobe
    jmp WOZMON

; =============================================
; CLEAR_GT — paint every pixel OFF (64x96 OFF-latch + Y-commit pairs)
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
; ZERO_GRIDS — wipe both 768-byte buffers (rounded to 3 pages each)
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
; SEED — R-pentomino placed with its bounding box around (row=46..48,
; col=31..33). Shape:
;       . # #       (46,32) (46,33)
;       # # .       (47,31) (47,32)
;       . # .       (48,32)
; =============================================
SEED:
    lda #$03            ; (46,32) bit0 + (46,33) bit1  of GRID_A+$174
    sta GRID_A+46*8+4
    lda #$80            ; (47,31) bit7              of GRID_A+$17B
    sta GRID_A+47*8+3
    lda #$01            ; (47,32) bit0              of GRID_A+$17C
    sta GRID_A+47*8+4
    lda #$01            ; (48,32) bit0              of GRID_A+$184
    sta GRID_A+48*8+4
    rts

; =============================================
; DISPLAY — paint GRID_A to the GT-6144 framebuffer.
;   For each row, walk the 8 row-bytes; for each bit, emit a pixel
;   (latch X with ON/OFF mode, then commit Y). 6144 pixels * ~30
;   cycles ≈ 180 k cycles total.
; =============================================
DISPLAY:
    lda #0
    sta OROW
@row_loop:
    ; RP = GRID_A + OROW * 8 (16-bit multiply)
    lda #0
    sta RP_HI
    lda OROW
    asl a
    rol RP_HI
    asl a
    rol RP_HI
    asl a
    rol RP_HI
    clc
    adc #<GRID_A
    sta RP_LO
    lda RP_HI
    adc #>GRID_A
    sta RP_HI

    lda #0
    sta OCOL
    ldy #0
@byte_loop:
    lda (RP_LO),y
    sta GBYTE
    sty BYTE_SAV
    ldx #0              ; bit index 0..7
@bit_loop:
    lda BIT_MASK,x
    and GBYTE
    beq @off
    lda OCOL            ; ON: X col with bit 6 set (64..127)
    ora #$40
    jmp @emit_x
@off:
    lda OCOL            ; OFF: bare X col (0..63)
@emit_x:
    sta GT_PORT
    lda OROW
    ora #$80            ; commit Y (128..223)
    sta GT_PORT

    inc OCOL
    inx
    cpx #8
    bne @bit_loop

    ldy BYTE_SAV
    iny
    cpy #8
    bne @byte_loop

    inc OROW
    lda OROW
    cmp #96
    bne @row_loop
    rts

; =============================================
; STEP — compute GRID_B from GRID_A using Conway's rules (B3/S23).
;   For every (OROW, OCOL), count alive neighbours via GET_CELL.
;   Edge-of-grid is treated as permanently dead (bounds-check inside
;   GET_CELL). GRID_B is cleared first so we only need to SET bits for
;   live-next cells.
; =============================================
STEP:
    ; Clear GRID_B first
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
    sta TMP             ; row*8 low
    lda OCOL
    lsr a
    lsr a
    lsr a               ; byte_col = OCOL >> 3
    clc
    adc TMP             ; row*8 + byte_col (low)
    sta RP_LO
    lda RP_HI
    adc #0              ; propagate carry
    sta RP_HI
    clc
    lda RP_LO
    adc #<GRID_B
    sta RP_LO
    lda RP_HI
    adc #>GRID_B
    sta RP_HI
    ; bit mask = 1 << (OCOL & 7)
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
; GET_CELL — returns A = 1 if cell (ROW, COL) is alive in GRID_A,
; else A = 0. ROW or COL = $FF (signed -1) falls into the bounds
; check and yields 0.
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
    ; bit test
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
; COPY_B_TO_A — copy 768 bytes (rounded to 3 full pages: 3x256=768).
; =============================================
COPY_B_TO_A:
    ldy #0
@l:
    lda GRID_B,y
    sta GRID_A,y
    lda GRID_B+$100,y
    sta GRID_A+$100,y
    lda GRID_B+$200,y
    sta GRID_A+$200,y
    iny
    bne @l
    rts

; =============================================
; Tables
; =============================================
; Bit 0 (leftmost cell in byte) at index 0, bit 7 (rightmost) at 7.
BIT_MASK: .byte $01, $02, $04, $08, $10, $20, $40, $80

; Neighbour deltas (dr, dc). $FF = -1 signed; 6502 ADC treats these
; as unsigned bytes, and out-of-range values fall into the GET_CELL
; bounds check (ROW/COL == $FF reads as >= 96/64 and returns dead).
DROW: .byte $FF, $FF, $FF, $00, $00, $01, $01, $01
DCOL: .byte $FF, $00, $01, $FF, $01, $FF, $00, $01
