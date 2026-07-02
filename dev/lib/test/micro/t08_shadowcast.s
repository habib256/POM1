; ============================================================================
; t08_shadowcast.s — micro-test: games/rogue shadowcast.asm FOV + index math
; ============================================================================
; GUARDS: the index-computation class (Maze3D cell_index_xy tmp-clobber):
;   apply_octant turns canonical (col, depth) into grid (cur_x, cur_y) via
;   signed ±1 coefficient products and the callbacks re-derive a row-major
;   byte index from it — exactly the arithmetic that silently breaks when a
;   scratch slot is clobbered. On an 8x8 map with the player at (3,3), one
;   wall at (4,3), fov_r = 3, the pinned cells assert:
;     - all 4 orthogonal neighbours visible (every octant pair fired),
;     - the wall cell itself visible,
;     - (5,3) and (6,3) — dead behind the wall — NOT visible (occlusion
;       narrowing works, i.e. the recursion carried the right slopes),
;     - diagonal (2,2)/(4,4) visible, and (7,3) beyond fov_r not visible.
;   The vis buffer bytes land in the mailbox at MB+1+ (y*8+x); the FULL
;   64-byte map is pinned (octant redundancy makes cherry-picked cells
;   forgiving — a broken oct_xx term still passed a spot-check version of
;   this test). The load-bearing cells were verified from first principles:
;
;         0 #######.        player (3,3) unmarked (caller's job),
;         1 ######..        wall (4,3) itself visible,
;         2 #####...        (5,3)/(6,3) dead behind the wall: dark,
;         3 ###.#...        (5,2)/(5,4) inside the widened shadow cone
;         4 #####...            ((2c+1)/(2d-1) slopes): dark,
;         5 ######..        diagonals at Chebyshev distance 3 visible,
;         6 #######.        col 7 / row 7 beyond fov_r = 3: dark.
;         7 ........
;
; POM1-LIB-MICRO-TEST
; LIBS:
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 30000
; EXPECT: 0F00 A5
; EXPECT: 0F01 01 01 01 01 01 01 01 00
; EXPECT: 0F09 01 01 01 01 01 01 00 00
; EXPECT: 0F11 01 01 01 01 01 00 00 00
; EXPECT: 0F19 01 01 01 00 01 00 00 00
; EXPECT: 0F21 01 01 01 01 01 00 00 00
; EXPECT: 0F29 01 01 01 01 01 01 00 00
; EXPECT: 0F31 01 01 01 01 01 01 01 00
; EXPECT: 0F39 00 00 00 00 00 00 00 00
; ============================================================================

.include "apple1.inc"

SHADOW_COLS = 8
SHADOW_ROWS = 8

.segment "ZEROPAGE"
player_col:     .res 1
player_row:     .res 1
fov_r:          .res 1
idx:            .res 1          ; callback scratch

.segment "BSS"
map:            .res 64         ; 1 = opaque
vis:            .res 64         ; callbacks set 1

; .include-style libs, caller-owned callbacks below (like TMS_Rogue does):
.include "multiply.asm"         ; umul4 for the slope cross-products
.include "shadowcast.asm"

MB = $0F00

.segment "ENTRY"
main:
        APPLE1_PREAMBLE

        ; clear map + vis
        LDX     #0
        LDA     #0
@clr:   STA     map,X
        STA     vis,X
        INX
        CPX     #64
        BNE     @clr

        LDA     #1              ; wall at (4,3) = index 3*8+4 = 28
        STA     map+28

        LDA     #3
        STA     player_col
        STA     player_row
        STA     fov_r

        JSR     shadowcast_octants

        ; copy vis[0..63] -> MB+1..MB+64
        LDX     #0
@cp:    LDA     vis,X
        STA     MB+1,X
        INX
        CPX     #64
        BNE     @cp

        LDA     #$A5
        STA     MB
spin:   JMP     spin

; ----------------------------------------------------------------------------
; Caller-provided callbacks (contract: clobber A, Y; preserve X).
; Row-major index = cur_y * 8 + cur_x — the "real index computation" under
; guard here.
; ----------------------------------------------------------------------------
mark_visible_at_cur:
        LDA     cur_y
        ASL
        ASL
        ASL
        CLC
        ADC     cur_x
        TAY
        LDA     #1
        STA     vis,Y
        RTS

is_opaque_at_cur:
        LDA     cur_y
        ASL
        ASL
        ASL
        CLC
        ADC     cur_x
        TAY
        LDA     map,Y           ; A = $01 (Z clear) iff opaque
        RTS
