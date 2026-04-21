; =============================================
; HGR ORBITAL POOL - gravitational billiards
; GEN2 Color Graphics Card - POM1 / Apple 1
; 32x24 cell grid (7x8 px cells), 3 levels, 1-3 wells
;
; Keyboard:
;   A / Z    -> angle - / + (16 directions, wraps)
;   S / X    -> power - / + (1..7)
;   SPACE    -> fire
;   R        -> reset shot / retry (result state: same level)
;   N        -> next level (result state: advances on WIN, retries on LOSE)
;   ESC      -> exit to Woz Monitor
;
; Gameplay:
;   Launch the ball from the left cell, land it in the target hole on
;   the right. Gravity wells bend the trajectory. Hitting a wall OR a
;   well ends the shot as a LOSE. Reaching the target cell is a WIN.
;
; =============================================
; Assemble:
;   ca65 -o build/HGR_OrbitalPool.o software/hgr/HGR_OrbitalPool.asm
;   ld65 -C software/hgr/apple1_gen2.cfg \
;        -o build/HGR_OrbitalPool.bin build/HGR_OrbitalPool.o
;
; Or just:
;   python3 software/hgr/emit_HGR_OrbitalPool_txt.py
;
; Run in POM1: plug GEN2 (auto-enabled when loading from software/hgr/),
; File > Load Memory HGR_OrbitalPool.txt, then 280R in the Woz Monitor.
;
; Memory footprint:
;   $0280-~$1000  code + LUTs + level data (output file)
;   $1800-$1813   game state (non-critical, NOT in output file)
;   $2000-$3FFF   HGR framebuffer (GEN2 reads this)
;
; Physics: 16-bit 8.8 fixed-point position/velocity. Per well per step,
; gravity delta is (dx * pull_lut[d2]) >> 4 with d2 = dx*dx + dy*dy.
; pull_lut[d2] ~= 1024 / d2^1.5, saturated to 255.
; =============================================

; ----- Apple 1 I/O -----
KBDCR   = $D011
KBD     = $D010

; ----- Keys (Apple 1: upper-case ASCII with bit 7 set) -----
KEY_ESC   = $9B
KEY_A     = $C1
KEY_Z     = $DA
KEY_S     = $D3
KEY_X     = $D8
KEY_R     = $D2
KEY_N     = $CE
KEY_SPACE = $A0

; ----- Geometry -----
GRID_W    = 32             ; interior cell columns
GRID_H    = 24             ; interior cell rows
MARGIN    = 4              ; HGR byte columns before first grid column
                           ; (4 + 32 = 36 bytes used out of 40; 2 empty on each side)

; ----- States -----
ST_AIM    = 0
ST_FLIGHT = 1
ST_WIN    = 2
ST_LOSE   = 3

; ----- Levels -----
NUM_LEVELS = 3
MAX_WELLS  = 3
LEVEL_STRIDE = 11          ; bytes per level record (see level_0)

; ----- Runtime RAM (absolute; NOT in output file) -----
state         := $1800
lvl_idx       := $1801
aim_angle     := $1802
aim_power     := $1803
frame_cnt_lo  := $1804
frame_cnt_hi  := $1805
num_wells     := $1806
wells_x       := $1807     ; 3 bytes
wells_y       := $180A     ; 3 bytes
target_x      := $180D
target_y      := $180E
ball_start_x  := $180F
ball_start_y  := $1810
prev_px       := $1811
prev_py       := $1812
exit_flag     := $1813

; ----- Zero page -----
.zeropage
          .res 2            ; $00-$01 reserved
hgr_lo:   .res 1            ; $02
hgr_hi:   .res 1
tile_lo:  .res 1            ; $04 pointer to 8-byte tile bitmap
tile_hi:  .res 1
px_hi:    .res 1            ; $06 ball position 8.8 (hi = cell column)
px_lo:    .res 1
py_hi:    .res 1            ; $08
py_lo:    .res 1
vx_hi:    .res 1            ; $0A ball velocity 8.8 signed
vx_lo:    .res 1
vy_hi:    .res 1            ; $0C
vy_lo:    .res 1
mul_a:    .res 1            ; $0E multiply scratch
mul_b:    .res 1
mul_hi:   .res 1
mul_lo:   .res 1
tile_col: .res 1            ; $12 HGR byte column for draw_tile
sl_idx:   .res 1            ; $13 scanline index 0..191
tmp:      .res 1            ; $14
tmp2:     .res 1
adx:      .res 1            ; $16 gravity workspace
ady:      .res 1
sign_dx:  .res 1            ; $18 0 = positive, $FF = negative
sign_dy:  .res 1
pull:     .res 1            ; $1A
d_sq_lo:  .res 1
d_sq_hi:  .res 1
well_i:   .res 1            ; $1D
          .res 2            ; $1E-$1F free

.code

; =============================================
; MAIN: boot + state-machine driver
; =============================================
main:
        LDA #0
        STA lvl_idx
        STA aim_angle
        STA exit_flag
        LDA #4
        STA aim_power
        JSR load_level
        JSR reset_shot
        LDA KBD                 ; swallow stale key from POM1 boot

main_loop:
        LDA exit_flag
        BEQ @cont
        RTS                     ; back to Woz Monitor
@cont:
        LDA state
        CMP #ST_AIM
        BNE @not_aim
        JSR handle_aim
        JMP main_loop
@not_aim:
        CMP #ST_FLIGHT
        BNE @result
        JSR handle_flight
        JMP main_loop
@result:
        JSR handle_result
        JMP main_loop

; =============================================
; reset_shot: ball at start, velocity 0, state = AIM
; =============================================
reset_shot:
        LDA ball_start_x
        STA px_hi
        STA prev_px
        LDA ball_start_y
        STA py_hi
        STA prev_py
        LDA #0
        STA px_lo
        STA py_lo
        STA vx_hi
        STA vx_lo
        STA vy_hi
        STA vy_lo
        STA frame_cnt_lo
        STA frame_cnt_hi
        LDA #ST_AIM
        STA state
        RTS

; =============================================
; handle_aim: render scene (with aim tip), wait key,
; dispatch.
; =============================================
handle_aim:
        JSR render_scene
        JSR wait_key
        CMP #KEY_ESC
        BNE @n1
        INC exit_flag
        RTS
@n1:    CMP #KEY_A
        BNE @n2
        LDA aim_angle
        SEC
        SBC #1
        AND #$0F
        STA aim_angle
        RTS
@n2:    CMP #KEY_Z
        BNE @n3
        LDA aim_angle
        CLC
        ADC #1
        AND #$0F
        STA aim_angle
        RTS
@n3:    CMP #KEY_S
        BNE @n4
        LDA aim_power
        CMP #1
        BEQ @done
        DEC aim_power
@done:  RTS
@n4:    CMP #KEY_X
        BNE @n5
        LDA aim_power
        CMP #7
        BEQ @done
        INC aim_power
        RTS
@n5:    CMP #KEY_R
        BNE @n6
        JSR reset_shot
        RTS
@n6:    CMP #KEY_SPACE
        BNE @done
        JSR fire
        RTS

; =============================================
; fire: compute initial velocity, state = FLIGHT,
; redraw scene without aim tip.
; =============================================
fire:
        JSR compute_initial_velocity
        LDA #ST_FLIGHT
        STA state
        LDA px_hi
        STA prev_px
        LDA py_hi
        STA prev_py
        JSR render_scene
        RTS

; =============================================
; handle_flight: one frame of flight simulation.
; Stamp trail at prev cell, step physics, collide,
; draw ball at new cell, delay.
; =============================================
handle_flight:
        ; Stamp trail at previous ball cell
        LDA prev_px
        STA tile_col
        LDA prev_py
        STA sl_idx              ; temp: we'll convert to scanline below
        ; Bounds check (prev may have been a valid cell already, but be safe)
        LDA prev_px
        CMP #GRID_W
        BCS @phys
        LDA prev_py
        CMP #GRID_H
        BCS @phys
        LDA #<tile_trail
        STA tile_lo
        LDA #>tile_trail
        STA tile_hi
        LDA prev_py
        STA tmp2                ; draw_tile uses tile_col + tmp2 (row) below
        JSR draw_tile_at

@phys:
        JSR apply_gravity
        JSR apply_velocity
        LDA px_hi
        STA prev_px
        LDA py_hi
        STA prev_py

        JSR check_collision
        LDA state
        CMP #ST_FLIGHT
        BNE @post

        ; Draw ball at new cell
        LDA px_hi
        STA tile_col
        LDA py_hi
        STA tmp2
        LDA #<tile_ball
        STA tile_lo
        LDA #>tile_ball
        STA tile_hi
        JSR draw_tile_at

        JSR delay

        ; Allow ESC abort during flight
        LDA KBDCR
        BPL @post
        LDA KBD
        CMP #KEY_ESC
        BNE @post
        INC exit_flag
@post:
        RTS

; =============================================
; handle_result: wait key, dispatch.
;   ESC -> exit
;   N   -> next level (WIN) or retry (LOSE)
;   any -> retry same level
; =============================================
handle_result:
        JSR wait_key
        CMP #KEY_ESC
        BNE @n1
        INC exit_flag
        RTS
@n1:    CMP #KEY_N
        BNE @retry
        LDA state
        CMP #ST_WIN
        BNE @retry
        INC lvl_idx
        LDA lvl_idx
        CMP #NUM_LEVELS
        BCC @load
        LDA #0
        STA lvl_idx
@load:
        JSR load_level
@retry:
        JSR reset_shot
        RTS

; =============================================
; render_scene: full clear, wells, target, ball,
; and aim tip (only when state == ST_AIM).
; =============================================
render_scene:
        JSR clear_hgr

        ; Draw wells
        LDX #0
@wloop:
        CPX num_wells
        BEQ @done_wells
        STX well_i
        LDA wells_x,X
        STA tile_col
        LDA wells_y,X
        STA tmp2
        LDA #<tile_well
        STA tile_lo
        LDA #>tile_well
        STA tile_hi
        JSR draw_tile_at
        LDX well_i
        INX
        JMP @wloop
@done_wells:

        ; Draw target
        LDA target_x
        STA tile_col
        LDA target_y
        STA tmp2
        LDA #<tile_target
        STA tile_lo
        LDA #>tile_target
        STA tile_hi
        JSR draw_tile_at

        ; Draw ball at current position
        LDA px_hi
        CMP #GRID_W
        BCS @no_ball
        LDA py_hi
        CMP #GRID_H
        BCS @no_ball
        LDA px_hi
        STA tile_col
        LDA py_hi
        STA tmp2
        LDA #<tile_ball
        STA tile_lo
        LDA #>tile_ball
        STA tile_hi
        JSR draw_tile_at
@no_ball:

        ; Aim tip only in ST_AIM
        LDA state
        CMP #ST_AIM
        BNE @done
        JSR draw_aim_tip
@done:
        RTS

; =============================================
; draw_aim_tip: compute aim tick at ball + dir*power/32,
; draw tile_aim_tip there if inside grid.
; =============================================
draw_aim_tip:
        LDX aim_angle
        LDA dx_table,X
        STA mul_a
        LDA aim_power
        STA mul_b
        JSR smult_8x8           ; mul_hi:mul_lo = dx*power (signed 16)
        LDX #5
        JSR asr_16_x            ; /32
        LDA mul_lo
        CLC
        ADC ball_start_x
        STA tile_col

        LDX aim_angle
        LDA dy_table,X
        STA mul_a
        LDA aim_power
        STA mul_b
        JSR smult_8x8
        LDX #5
        JSR asr_16_x
        LDA mul_lo
        CLC
        ADC ball_start_y
        STA tmp2

        LDA tile_col
        CMP #GRID_W
        BCS @off
        LDA tmp2
        CMP #GRID_H
        BCS @off
        LDA #<tile_aim
        STA tile_lo
        LDA #>tile_aim
        STA tile_hi
        JSR draw_tile_at
@off:
        RTS

; =============================================
; draw_tile_at: draw 8-row tile at cell (tile_col, tmp2).
; tile_col is cell column (0..31) -> HGR byte col+MARGIN.
; tmp2 is cell row (0..23) -> HGR scanline = row*8.
; tile_lo/hi points to 8-byte bitmap.
; =============================================
draw_tile_at:
        LDA tile_col
        CLC
        ADC #MARGIN
        STA tile_col            ; reuse: now holds HGR byte column

        LDA tmp2
        ASL
        ASL
        ASL                     ; row * 8
        STA sl_idx

        LDY #0                  ; tile byte index
@sl_loop:
        STY tmp
        LDX sl_idx
        LDA hgr_lo_tbl,X
        STA hgr_lo
        LDA hgr_hi_tbl,X
        STA hgr_hi
        LDY tmp
        LDA (tile_lo),Y
        LDY tile_col
        STA (hgr_lo),Y
        LDY tmp
        INY
        INC sl_idx
        CPY #8
        BNE @sl_loop
        RTS

; =============================================
; clear_hgr: zero $2000-$3FFF (8 KB)
; =============================================
clear_hgr:
        LDA #$00
        STA hgr_lo
        LDA #$20
        STA hgr_hi
        LDY #0
        LDA #0
@lp:
        STA (hgr_lo),Y
        INY
        BNE @lp
        INC hgr_hi
        LDX hgr_hi
        CPX #$40
        BNE @lp
        RTS

; =============================================
; apply_velocity: (px,py) += (vx,vy) as 16-bit signed
; =============================================
apply_velocity:
        CLC
        LDA px_lo
        ADC vx_lo
        STA px_lo
        LDA px_hi
        ADC vx_hi
        STA px_hi
        CLC
        LDA py_lo
        ADC vy_lo
        STA py_lo
        LDA py_hi
        ADC vy_hi
        STA py_hi
        RTS

; =============================================
; check_collision: updates state if WIN/LOSE/timeout.
; px_hi/py_hi are the ball's integer cell (8.8 high byte).
; =============================================
check_collision:
        ; Wall (out of grid). px_hi signed-negative OR >= GRID_W.
        LDA px_hi
        BMI @lose
        CMP #GRID_W
        BCS @lose
        LDA py_hi
        BMI @lose
        CMP #GRID_H
        BCS @lose

        ; Target?
        LDA px_hi
        CMP target_x
        BNE @check_wells
        LDA py_hi
        CMP target_y
        BEQ @win

@check_wells:
        LDX #0
@wl:
        CPX num_wells
        BEQ @timeout
        LDA wells_x,X
        CMP px_hi
        BNE @wn
        LDA wells_y,X
        CMP py_hi
        BEQ @lose
@wn:
        INX
        JMP @wl

@timeout:
        INC frame_cnt_lo
        BNE @ok
        INC frame_cnt_hi
@ok:
        LDA frame_cnt_hi
        CMP #2                  ; ~512 frames @ 15 FPS = 34 s
        BCC @done
        ; fall through to lose
@lose:
        LDA #ST_LOSE
        STA state
        RTS
@win:
        LDA #ST_WIN
        STA state
@done:
        RTS

; =============================================
; apply_gravity: for each well, add gravity delta to (vx,vy).
;   dx = wells_x[i] - px_hi (signed)
;   dy = wells_y[i] - py_hi
;   d2 = dx*dx + dy*dy (16-bit), clamp to 255 -> 0 pull otherwise
;   pull = pull_lut[d2]
;   vx += sign(dx) * (|dx| * pull) >> 4
;   vy += sign(dy) * (|dy| * pull) >> 4
; =============================================
apply_gravity:
        LDA #0
        STA well_i
@wloop:
        LDA well_i
        CMP num_wells
        BNE @body
        RTS
@body:
        LDX well_i

        ; dx = wells_x[X] - px_hi  (signed)
        LDA wells_x,X
        SEC
        SBC px_hi
        LDY #0                  ; sign = 0 (positive)
        BPL @adx_ok
        EOR #$FF
        CLC
        ADC #1
        LDY #$FF                ; sign = negative
@adx_ok:
        STA adx
        STY sign_dx

        ; dy = wells_y[X] - py_hi  (signed)
        LDA wells_y,X
        SEC
        SBC py_hi
        LDY #0
        BPL @ady_ok
        EOR #$FF
        CLC
        ADC #1
        LDY #$FF
@ady_ok:
        STA ady
        STY sign_dy

        ; d2 = adx*adx + ady*ady  (16-bit)
        LDA adx
        STA mul_a
        STA mul_b
        JSR umult_8x8
        LDA mul_lo
        STA d_sq_lo
        LDA mul_hi
        STA d_sq_hi

        LDA ady
        STA mul_a
        STA mul_b
        JSR umult_8x8
        CLC
        LDA mul_lo
        ADC d_sq_lo
        STA d_sq_lo
        LDA mul_hi
        ADC d_sq_hi
        STA d_sq_hi

        ; If d2 hi > 0, well is too far - skip
        LDA d_sq_hi
        BEQ @close
        JMP @next

@close:
        LDX d_sq_lo
        LDA pull_lut,X
        STA pull
        BNE @apply
        JMP @next               ; pull == 0, skip
@apply:
        ; vx delta = sign_dx * (adx * pull) >> 4
        LDA adx
        STA mul_a
        LDA pull
        STA mul_b
        JSR umult_8x8           ; mul_hi:mul_lo = adx*pull
        LDX #4
        JSR lsr_16_x            ; unsigned shift right 4

        ; Apply sign
        LDA sign_dx
        BEQ @vxadd
        SEC
        LDA #0
        SBC mul_lo
        STA mul_lo
        LDA #0
        SBC mul_hi
        STA mul_hi
@vxadd:
        CLC
        LDA vx_lo
        ADC mul_lo
        STA vx_lo
        LDA vx_hi
        ADC mul_hi
        STA vx_hi

        ; vy delta = sign_dy * (ady * pull) >> 4
        LDA ady
        STA mul_a
        LDA pull
        STA mul_b
        JSR umult_8x8
        LDX #4
        JSR lsr_16_x

        LDA sign_dy
        BEQ @vyadd
        SEC
        LDA #0
        SBC mul_lo
        STA mul_lo
        LDA #0
        SBC mul_hi
        STA mul_hi
@vyadd:
        CLC
        LDA vy_lo
        ADC mul_lo
        STA vy_lo
        LDA vy_hi
        ADC mul_hi
        STA vy_hi

@next:
        INC well_i
        JMP @wloop

; =============================================
; compute_initial_velocity: vx = dx_table[angle]*power/4,
; vy = dy_table[angle]*power/4  (signed 8.8 values)
; =============================================
compute_initial_velocity:
        LDX aim_angle
        LDA dx_table,X
        STA mul_a
        LDA aim_power
        STA mul_b
        JSR smult_8x8
        LDX #2
        JSR asr_16_x
        LDA mul_lo
        STA vx_lo
        LDA mul_hi
        STA vx_hi

        LDX aim_angle
        LDA dy_table,X
        STA mul_a
        LDA aim_power
        STA mul_b
        JSR smult_8x8
        LDX #2
        JSR asr_16_x
        LDA mul_lo
        STA vy_lo
        LDA mul_hi
        STA vy_hi
        RTS

; =============================================
; umult_8x8: unsigned mul_a * mul_b -> mul_hi:mul_lo
; Destroys mul_b (shifted out).
; =============================================
umult_8x8:
        LDA #0
        STA mul_hi
        STA mul_lo
        LDX #8
@lp:
        ASL mul_lo
        ROL mul_hi
        ASL mul_b
        BCC @sk
        CLC
        LDA mul_lo
        ADC mul_a
        STA mul_lo
        LDA mul_hi
        ADC #0
        STA mul_hi
@sk:
        DEX
        BNE @lp
        RTS

; =============================================
; smult_8x8: signed mul_a * mul_b -> mul_hi:mul_lo (signed 16)
; Destroys mul_a, mul_b. Uses tmp as sign accumulator.
; =============================================
smult_8x8:
        LDA #0
        STA tmp                 ; sign accumulator (parity of negations)
        LDA mul_a
        BPL @a_pos
        EOR #$FF
        CLC
        ADC #1
        STA mul_a
        LDA tmp
        EOR #$01
        STA tmp
@a_pos:
        LDA mul_b
        BPL @b_pos
        EOR #$FF
        CLC
        ADC #1
        STA mul_b
        LDA tmp
        EOR #$01
        STA tmp
@b_pos:
        JSR umult_8x8
        LDA tmp
        BEQ @pos
        ; Negate 16-bit
        SEC
        LDA #0
        SBC mul_lo
        STA mul_lo
        LDA #0
        SBC mul_hi
        STA mul_hi
@pos:
        RTS

; =============================================
; asr_16_x: arithmetic right shift mul_hi:mul_lo by X
; =============================================
asr_16_x:
@lp:
        LDA mul_hi
        CMP #$80                ; C = 1 if mul_hi >= $80 (negative)
        ROR mul_hi
        ROR mul_lo
        DEX
        BNE @lp
        RTS

; =============================================
; lsr_16_x: logical right shift mul_hi:mul_lo by X
; =============================================
lsr_16_x:
@lp:
        LSR mul_hi
        ROR mul_lo
        DEX
        BNE @lp
        RTS

; =============================================
; wait_key: block until a key is ready, return in A.
; =============================================
wait_key:
        LDA KBDCR
        BPL wait_key
        LDA KBD
        RTS

; =============================================
; delay: ~60-70 ms busy wait at 1 MHz
; =============================================
delay:
        LDX #50
@o:
        LDY #0
@i:
        DEY
        BNE @i
        DEX
        BNE @o
        RTS

; =============================================
; load_level: copy level record to live vars.
; Level record layout (LEVEL_STRIDE = 11):
;   [0]  num_wells
;   [1..3] wells_x[3] (padded)
;   [4..6] wells_y[3]
;   [7]  target_x
;   [8]  target_y
;   [9]  ball_start_x
;   [10] ball_start_y
; =============================================
load_level:
        LDX lvl_idx
        LDA levels_lo,X
        STA tile_lo
        LDA levels_hi,X
        STA tile_hi

        LDY #0
        LDA (tile_lo),Y
        STA num_wells

        LDY #1
        LDX #0
@wx:    LDA (tile_lo),Y
        STA wells_x,X
        INY
        INX
        CPX #3
        BNE @wx

        LDX #0
@wy:    LDA (tile_lo),Y
        STA wells_y,X
        INY
        INX
        CPX #3
        BNE @wy

        LDA (tile_lo),Y
        STA target_x
        INY
        LDA (tile_lo),Y
        STA target_y
        INY
        LDA (tile_lo),Y
        STA ball_start_x
        INY
        LDA (tile_lo),Y
        STA ball_start_y
        RTS

; =============================================
; DATA
; =============================================

; ---- Tile bitmaps (7 px wide, 8 px tall; bit 0 = leftmost pixel, bit 7 = color group / 0 here) ----

; Ball: filled 3x3 blob, centered
tile_ball:
        .byte $00, $00, $1C, $1C, $1C, $00, $00, $00

; Well: 5x3 cross with 3x3 center
tile_well:
        .byte $00, $08, $1C, $3E, $3E, $1C, $08, $00

; Target: hollow ring
tile_target:
        .byte $00, $1C, $22, $22, $22, $1C, $00, $00

; Trail: single center dot (dim marker)
tile_trail:
        .byte $00, $00, $00, $08, $00, $00, $00, $00

; Aim tip: 3 pixels horizontal at row 2
tile_aim:
        .byte $00, $1C, $00, $00, $00, $00, $00, $00

; ---- Angle tables (16 directions, CCW from East, values scaled to ~64) ----
; angle 0 = East (+X)
; angle 4 = North (-Y, screen Y grows downward so negative)
; angle 8 = West (-X)
; angle 12 = South (+Y)
dx_table:
        .byte  64,  59,  45,  25,   0, <-25, <-45, <-59
        .byte <-64, <-59, <-45, <-25,   0,  25,  45,  59
dy_table:
        .byte   0, <-25, <-45, <-59, <-64, <-59, <-45, <-25
        .byte   0,  25,  45,  59,  64,  59,  45,  25

; ---- Gravity LUT: pull_lut[d2] ~= clamp(round(1024 / d2^1.5), 0, 255) ----
; d2 = 0 reserved (ball inside well). Beyond d2~161, pull rounds to 0.
pull_lut:
        .byte   0, 255, 255, 197, 128,  92,  70,  55,  45,  38,  32,  28,  25,  22,  20,  18
        .byte  16,  15,  13,  12,  11,  11,  10,   9,   9,   8,   8,   7,   7,   7,   6,   6
        .byte   6,   5,   5,   5,   5,   5,   4,   4,   4,   4,   4,   4,   4,   3,   3,   3
        .byte   3,   3,   3,   3,   3,   3,   3,   3,   2,   2,   2,   2,   2,   2,   2,   2
        .byte   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   1,   1
        .byte   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1
        .byte   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1
        .byte   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1
        .byte   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1
        .byte   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1
        .byte   1,   1
        .res 94, 0              ; d2 = 162..255 -> pull = 0

; ---- Levels ----
; Each: num_wells, wells_x[3], wells_y[3], target_x, target_y, ball_start_x, ball_start_y
; Padding bytes in unused well slots are ignored (num_wells gates loop).

levels_lo:
        .byte <level_0, <level_1, <level_2
levels_hi:
        .byte >level_0, >level_1, >level_2

; Level 0 - Deflection: one well centered, direct line is captured.
level_0:
        .byte 1
        .byte 16,  0,  0
        .byte 12,  0,  0
        .byte 28, 12
        .byte  2, 12

; Level 1 - Slingshot: two wells diagonally offset.
level_1:
        .byte 2
        .byte 12, 22,  0
        .byte 18,  6,  0
        .byte 29,  4
        .byte  2, 20

; Level 2 - Zigzag: three wells alternating.
level_2:
        .byte 3
        .byte 10, 18, 24
        .byte  6, 18,  6
        .byte 29, 12
        .byte  2, 12

; ---- HGR scanline base address LUT (Apple II interleaved, 192 rows) ----
;   addr[y] = $2000 + (y mod 8)*$0400 + ((y/8) mod 8)*$80 + (y/64)*$28
hgr_lo_tbl:
        .repeat 192, YS
            .byte <($2000 + ((YS .MOD 8) * $0400) + (((YS / 8) .MOD 8) * $80) + ((YS / 64) * $28))
        .endrepeat
hgr_hi_tbl:
        .repeat 192, YS
            .byte >($2000 + ((YS .MOD 8) * $0400) + (((YS / 8) .MOD 8) * $80) + ((YS / 64) * $28))
        .endrepeat
