; ============================================================================
; shadowcast.asm -- Recursive shadowcasting field-of-view (Björn Bergström,
;                   RogueBasin 2002), display-agnostic 6502 implementation.
; ============================================================================
;
; Partitions the plane around the player into 8 octants. Each octant is
; processed by `cast_octant`, which scans rows of increasing depth and
; columns from outer (slope ≈ 1) to inner (slope = 0). Walls encountered
; mid-row narrow the visible cone for deeper rows via JSR-recursion. The
; result is mathematically symmetric — no slope artifacts, no coverage
; holes, every cell visible from the player iff the player is visible
; from it (within the radius).
;
; The library reads/writes nothing display-specific — it walks an abstract
; 1-byte-per-cell map and fires two caller-supplied callbacks per cell:
; one to mark visibility, one to test opacity. Use it for any grid-based
; fog-of-war: roguelikes (TMS9918, HGR, GT-6144), stealth games, board
; games with line-of-sight.
;
; ----------------------------------------------------------------------------
; Caller responsibilities -- declare these BEFORE `.include "shadowcast.asm"`
; ----------------------------------------------------------------------------
;
; Constants (scalar `.equ`-style):
;
;   SHADOW_COLS         = grid width  (1..127)  — playfield X dimension
;   SHADOW_ROWS         = grid height (1..127)  — playfield Y dimension
;
; Zero-page bytes (declare in your `.zeropage` block):
;
;   player_col          = player's grid X (0..SHADOW_COLS-1)
;   player_row          = player's grid Y (0..SHADOW_ROWS-1)
;   fov_r               = active radius (1..15)
;
; Subroutines (define in your CODE segment):
;
;   mark_visible_at_cur  = side-effect: mark cell (cur_x, cur_y) lit in your
;                          vis buffer. Caller may assume cur_x < SHADOW_COLS
;                          and cur_y < SHADOW_ROWS (the lib OOR-tests before
;                          calling). Clobbers A, Y; preserves X.
;
;   is_opaque_at_cur     = predicate: A = $01 (Z clear) iff cell at
;                          (cur_x, cur_y) blocks sight, else A = $00 (Z set).
;                          Walls AND opaque doors should both return $01.
;                          Clobbers A, Y; preserves X.
;
; Multiply primitive: caller must `.include "multiply.asm"` before this
; library so `umul4` (4×4 → 8-bit) is linkable. shadowcast uses umul4 for
; every slope cross-multiplication; with SHADOW_COLS, SHADOW_ROWS and
; fov_r all bounded by 15, the slope numerators / denominators stay in
; 0..15 and the products fit in one byte (15*15 = 225).
;
; ----------------------------------------------------------------------------
; Lib-owned ZP (allocated via `.ifndef` guard so callers can pre-alias
; if they need to share these slots with their own scratch — most don't).
; ----------------------------------------------------------------------------

; Two .ifndef guards: cur_x/cur_y often double as throwaway scratch in
; the rest of the project (the rogue's dagger animation reuses them),
; so the caller may pre-declare them in its own .zeropage. The cast_*
; / oct_* slots are private to the shadowcaster and almost nobody
; pre-declares them.
.ifndef cur_x
.zeropage
cur_x:           .res 1          ; cell currently being marked / opacity-tested
cur_y:           .res 1
.endif
.ifndef cast_depth
.zeropage
oct_xx:          .res 1          ; canonical (col, depth) → grid offset xform
oct_xy:          .res 1          ;   each in {-1, 0, +1} as signed byte
oct_yx:          .res 1
oct_yy:          .res 1
oct_idx:         .res 1          ; outer 0..7 octant counter
cast_depth:      .res 1          ; current row depth (1..fov_r)
cast_col:        .res 1          ; current col cursor inside row (depth..0)
cast_blocked:    .res 1          ; non-zero while inside a wall chain
cast_start_n:    .res 1          ; live cone — high-slope edge as a
cast_start_d:    .res 1          ;   reduced fraction (num / den)
cast_end_n:      .res 1          ; live cone — low-slope edge
cast_end_d:      .res 1
cast_save_n:     .res 1          ; "new_start" — narrowed start that
cast_save_d:     .res 1          ;   takes effect at next wall→floor
cast_lslope_n:   .res 1          ; current cell's left (high) slope
cast_lslope_d:   .res 1          ;   = (2c+1) / (2d-1)
cast_rslope_n:   .res 1          ; current cell's right (low) slope
cast_rslope_d:   .res 1          ;   = max(0, 2c-1) / (2d+1)
cast_xprod:      .res 1          ; cross-product LHS scratch for slope cmp
.endif

; ----------------------------------------------------------------------------
; Public entry: shadowcast_octants
; ----------------------------------------------------------------------------
; Casts the FOV cone from (player_col, player_row) over all 8 octants.
; The caller is responsible for:
;   - wiping its vis buffer beforehand (the lib doesn't know where the
;     buffer lives or how big it is)
;   - lighting the player's own cell (cast_octant starts at depth=1, so
;     the player's depth=0 cell is never marked by the lib)
;   - any post-pass cleanup (e.g. stripping pit reveals out of FOV)
;
; Reads:  player_col, player_row, fov_r
; Calls:  mark_visible_at_cur, is_opaque_at_cur (per cell, in-bounds only)
; Stack:  recursion ≤ fov_r levels, ~11 B per frame, ~80 B peak at fov_r=7
; ----------------------------------------------------------------------------

.segment "CODE"

shadowcast_octants:
        LDA     #0
        STA     oct_idx
@oct_lp:
        LDX     oct_idx
        LDA     octant_xx,X
        STA     oct_xx
        LDA     octant_xy,X
        STA     oct_xy
        LDA     octant_yx,X
        STA     oct_yx
        LDA     octant_yy,X
        STA     oct_yy

        ; Initial cone: start = 1/1, end = 0/1, depth = 1.
        LDA     #1
        STA     cast_start_n
        STA     cast_start_d
        STA     cast_end_d
        STA     cast_depth
        LDA     #0
        STA     cast_end_n
        JSR     cast_octant

        INC     oct_idx
        LDA     oct_idx
        CMP     #8
        BCC     @oct_lp
        RTS


; ----------------------------------------------------------------------------
; cast_octant -- internal recursive shadowcast for one octant.
; Reads cast_depth / cast_start_n,d / cast_end_n,d from ZP; iterates rows
; of increasing depth and inside each row scans cols from outer (col =
; depth, slope ≈ 1) to inner (col = 0, slope = 0). For each in-cone cell:
;   - mark visible
;   - if the cell is opaque (wall / door):
;       * floor → wall transition: recurse one row deeper with the cone
;         narrowed to (start, leftSlope) — the wall's high edge, which
;         is the steepest slope the wall occludes at deeper rows.
;       * cache rightSlope as cast_save_n/d ("new_start"). This is what
;         the row's `start` becomes if we cross back to floor later in
;         the same row.
;   - if the cell is floor and we just exited a wall chain, rewind
;     start to cast_save_n/d.
; If the row ends with cast_blocked still set (an unbroken wall chain
; across the cone tail), the routine returns — every deeper row is
; entirely shadowed.
; ----------------------------------------------------------------------------

cast_octant:
        ; --- Empty cone? if start_n/start_d <= end_n/end_d -> return.
        ; Cross-multiplication: start_n * end_d <= end_n * start_d.
        LDA     cast_start_n
        LDX     cast_end_d
        JSR     umul4                   ; A = start_n * end_d
        STA     cast_xprod
        LDA     cast_end_n
        LDX     cast_start_d
        JSR     umul4                   ; A = end_n * start_d
        CMP     cast_xprod              ; carry set iff end*start_d >= start*end_d
        BCC     @row_lp                 ; start > end -> real cone
        JMP     @rt                     ; start <= end -> empty cone
@row_lp:
        LDA     cast_depth
        CMP     fov_r
        BEQ     @row_ok
        BCC     @row_ok                 ; depth < fov_r -> process row
        JMP     @rt                     ; depth > fov_r -> done with octant
@row_ok:
        ; --- Per-row state ---
        LDA     #0
        STA     cast_blocked
        LDA     cast_start_n
        STA     cast_save_n             ; "new_start" — initial value =
        LDA     cast_start_d            ;   current start; updated as we
        STA     cast_save_d             ;   discover wall chains.

        LDA     cast_depth
        STA     cast_col                ; outer-most cell in row

@col_lp:
        ; --- Compute leftSlope = (2c + 1) / (2d - 1) ---
        ; --- Compute rightSlope = max(0, 2c - 1) / (2d + 1) ---
        LDA     cast_col
        ASL                             ; A = 2c
        PHA                             ; save 2c
        CLC
        ADC     #1                      ; 2c + 1
        STA     cast_lslope_n
        LDA     cast_depth
        ASL                             ; A = 2d
        PHA                             ; save 2d
        SEC
        SBC     #1                      ; 2d - 1
        STA     cast_lslope_d
        ; rightSlope numerator: 0 if col=0, else 2c-1.
        LDA     cast_col
        BEQ     @rs_zero
        PLA                             ; A = 2d
        STA     cast_rslope_d
        PLA                             ; A = 2c
        SEC
        SBC     #1                      ; 2c - 1
        STA     cast_rslope_n
        JMP     @rs_d
@rs_zero:
        PLA                             ; A = 2d
        STA     cast_rslope_d
        PLA                             ; discard 2c (col was 0)
        LDA     #0
        STA     cast_rslope_n
@rs_d:
        INC     cast_rslope_d           ; 2d + 1

        ; --- Cone test (a): if cast_start < rightSlope, skip cell.
        ; start_n/start_d < rslope_n/rslope_d
        ;   <=>  start_n * rslope_d  <  rslope_n * start_d
        LDA     cast_start_n
        LDX     cast_rslope_d
        JSR     umul4
        STA     cast_xprod              ; start_n * rslope_d
        LDA     cast_rslope_n
        LDX     cast_start_d
        JSR     umul4                   ; rslope_n * start_d
        CMP     cast_xprod              ; >= cast_xprod => rslope >= start
        BEQ     @in_cone_a              ; equal: still in cone (boundary)
        BCC     @in_cone_a              ; rslope < start -> in cone
        JMP     @next_col               ; rslope > start -> too steep, skip
@in_cone_a:
        ; --- Cone test (b): if leftSlope < cast_end, break (past cone).
        ; lslope_n/lslope_d < end_n/end_d
        ;   <=>  lslope_n * end_d  <  end_n * lslope_d
        LDA     cast_lslope_n
        LDX     cast_end_d
        JSR     umul4
        STA     cast_xprod              ; lslope_n * end_d
        LDA     cast_end_n
        LDX     cast_lslope_d
        JSR     umul4                   ; end_n * lslope_d
        CMP     cast_xprod
        BEQ     @in_cone_b              ; equal: keep
        BCC     @in_cone_b              ; end*lslope_d < lslope_n*end_d
                                        ;   -> end < lslope -> in cone
        JMP     @break_row              ; lslope < end -> past cone
@in_cone_b:
        ; --- Resolve cell to grid (cur_x, cur_y) and OOR-test. ---
        JSR     apply_octant
        LDA     cur_x
        CMP     #SHADOW_COLS
        BCS     @oor_cell
        LDA     cur_y
        CMP     #SHADOW_ROWS
        BCS     @oor_cell

        ; In-bounds: mark visible + check opacity.
        JSR     mark_visible_at_cur
        JSR     is_opaque_at_cur
        BNE     @cell_blocks            ; opaque → wall handling

        ; --- Floor cell ---
        LDA     cast_blocked
        BEQ     @next_col               ; not blocked -> just continue
        ; Wall→floor transition: rewind start to cast_save_n/d.
        LDA     #0
        STA     cast_blocked
        LDA     cast_save_n
        STA     cast_start_n
        LDA     cast_save_d
        STA     cast_start_d
        JMP     @next_col

@cell_blocks:
        ; --- Wall cell ---
        LDA     cast_blocked
        BNE     @wall_extend            ; already in wall chain — just
                                        ; track new_start, no recursion.

        ; Floor → wall transition. Recurse one row deeper with cone
        ; (start, l_slope), then cache new_start = r_slope.
        LDA     #1
        STA     cast_blocked
        LDA     cast_depth
        CMP     fov_r
        BCS     @save_new_start         ; depth >= fov_r -> deepest row,
                                        ; nothing past the wall to scan.

        ; --- Save 9-byte frame, set up child params, recurse, restore.
        LDA     cast_depth
        PHA
        LDA     cast_col
        PHA
        LDA     cast_blocked
        PHA
        LDA     cast_save_n
        PHA
        LDA     cast_save_d
        PHA
        LDA     cast_start_n
        PHA
        LDA     cast_start_d
        PHA
        LDA     cast_end_n
        PHA
        LDA     cast_end_d
        PHA

        INC     cast_depth              ; child row = depth + 1
        LDA     cast_lslope_n
        STA     cast_end_n              ; cone narrowed to (start, lslope)
        LDA     cast_lslope_d
        STA     cast_end_d

        JSR     cast_octant

        PLA
        STA     cast_end_d
        PLA
        STA     cast_end_n
        PLA
        STA     cast_start_d
        PLA
        STA     cast_start_n
        PLA
        STA     cast_save_d
        PLA
        STA     cast_save_n
        PLA
        STA     cast_blocked
        PLA
        STA     cast_col
        PLA
        STA     cast_depth

@save_new_start:
@wall_extend:
        ; Cache rightSlope as new_start (latest wall in chain wins).
        LDA     cast_rslope_n
        STA     cast_save_n
        LDA     cast_rslope_d
        STA     cast_save_d
        JMP     @next_col

@oor_cell:
        ; Treat off-grid as wall; runs the wall-state machine but skips
        ; mark_visible / opacity (the cell is unreachable anyway).
        LDA     cast_blocked
        BNE     @oor_extend
        LDA     #1
        STA     cast_blocked
@oor_extend:
        LDA     cast_rslope_n
        STA     cast_save_n
        LDA     cast_rslope_d
        STA     cast_save_d

@next_col:
        LDA     cast_col
        BEQ     @col_done               ; col already 0 -> row scan done
        DEC     cast_col
        JMP     @col_lp

@break_row:
@col_done:
        ; If the row ended while still inside a wall chain, every
        ; deeper row would be entirely shadowed by it -> return.
        LDA     cast_blocked
        BNE     @rt

        INC     cast_depth
        JMP     @row_lp
@rt:
        RTS


; ----------------------------------------------------------------------------
; apply_octant -- internal: cur_x = player_col + col*oct_xx + depth*oct_xy
;                           cur_y = player_row + col*oct_yx + depth*oct_yy
; All four oct_* coefficients are signed: $00, $01, or $FF (= -1).
; Result lands unsigned in cur_x/cur_y (signed-wrap leaves $80..$FF, which
; the caller's CMP #SHADOW_COLS / BCS catches as OOR).
; ----------------------------------------------------------------------------
apply_octant:
        LDA     oct_xx
        LDY     cast_col
        JSR     signed_mul_unit
        STA     cast_xprod              ; col * oct_xx (reuse xprod scratch)
        LDA     oct_xy
        LDY     cast_depth
        JSR     signed_mul_unit
        CLC
        ADC     cast_xprod              ; + depth * oct_xy
        CLC
        ADC     player_col
        STA     cur_x

        LDA     oct_yx
        LDY     cast_col
        JSR     signed_mul_unit
        STA     cast_xprod
        LDA     oct_yy
        LDY     cast_depth
        JSR     signed_mul_unit
        CLC
        ADC     cast_xprod
        CLC
        ADC     player_row
        STA     cur_y
        RTS


; ----------------------------------------------------------------------------
; signed_mul_unit -- internal: A = signed_unit * magnitude.
;   In:  A = coefficient ($00, $01, or $FF)
;        Y = unsigned magnitude (≤ 15)
;   Out: A = signed product as two's-complement byte
;        ($00, +Y, or -Y respectively)
; ----------------------------------------------------------------------------
signed_mul_unit:
        CMP     #0
        BEQ     @zero
        BMI     @neg
        TYA
        RTS
@neg:
        TYA
        EOR     #$FF
        CLC
        ADC     #1
        RTS
@zero:
        LDA     #0
        RTS


; ----------------------------------------------------------------------------
; Octant transforms. Each row of the table maps canonical (col, depth) to
; a grid (dx, dy) offset from (player_col, player_row):
;     dx = col * oct_xx + depth * oct_xy
;     dy = col * oct_yx + depth * oct_yy
; Each row has exactly one nonzero in {xx, xy} and one in {yx, yy}; the
; nonzeros are ±1, $FF being two's-complement -1. The 8 rows together
; partition the plane around the player without overlap.
; ----------------------------------------------------------------------------
octant_xx:      .byte   0,    1,   $FF,   0,    0,   $FF,   1,    0
octant_xy:      .byte   1,    0,    0,   $FF, $FF,   0,     0,    1
octant_yx:      .byte $FF,    0,    0,   $FF,   1,    0,    0,    1
octant_yy:      .byte   0,   $FF, $FF,    0,    0,    1,    1,    0
