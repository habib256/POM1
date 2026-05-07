; =============================================
; TMS ORBITAL POOL - gravitational billiards
; P-LAB TMS9918 Graphic Card - POM1 / Apple 1
; 32x24 cell grid (native 8x8 char cells), 3 levels, 1-3 wells
;
; Keyboard (identical to the HGR port):
;   A / Z    -> angle - / + (16 directions, wraps)
;   S / X    -> power - / + (1..7)
;   SPACE    -> fire
;   R        -> reset shot / retry
;   N        -> next level (WIN) or retry (LOSE)
;   ESC      -> exit to Woz Monitor
;
; =============================================
; Assemble:
;   ca65 -o build/TMS_OrbitalPool.o software/tms9918/TMS_OrbitalPool.asm
;   ld65 -C software/hgr/apple1_gen2.cfg \
;        -o build/TMS_OrbitalPool.bin build/TMS_OrbitalPool.o
;
; Or just:
;   python3 software/tms9918/emit_TMS_OrbitalPool_txt.py
;
; Run in POM1: plug TMS9918 (auto-enabled when loading from
; software/tms9918/), File > Load Memory TMS_OrbitalPool.txt,
; then 280R in the Woz Monitor.
;
; Memory footprint:
;   $0280-~$0800  code + LUTs + level data (output file)
;   $4000-$4013   game state (non-critical, NOT in output file)
;   VRAM on card  pattern / name / color tables
;
; Rendering: 6 glyphs uploaded at boot to VRAM $0000 (chars 0..5).
; Each "tile" draw writes ONE byte (char code) at name-table offset
; $1800 + row*32 + col. No framebuffer poking, no scanline LUT.
;
; Physics identical to HGR port: 16-bit 8.8 fixed-point position/
; velocity, per-well delta = sign(dx) * (|dx|*pull_lut[d2]) >> 4.
; =============================================

; ----- Apple 1 I/O -----
        .import tms9918_pad12  ; silicon-strict pad12-v3 (helper from tms9918_pad.asm)
KBDCR   = $D011
KBD     = $D010

; ----- TMS9918 MMIO -----
VDP_DATA = $CC00
VDP_CTRL = $CC01

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
GRID_W    = 32
GRID_H    = 24

; ----- States -----
ST_AIM    = 0
ST_FLIGHT = 1
ST_WIN    = 2
ST_LOSE   = 3

; ----- Levels -----
NUM_LEVELS = 3
MAX_WELLS  = 3

; ----- Char codes (glyphs uploaded at init) -----
CHR_EMPTY  = 0
CHR_BALL   = 1
CHR_WELL   = 2
CHR_TARGET = 3
CHR_TRAIL  = 4
CHR_AIM    = 5

; ----- Runtime RAM (absolute; NOT in output file) -----
state         := $4000
lvl_idx       := $4001
aim_angle     := $4002
aim_power     := $4003
frame_cnt_lo  := $4004
frame_cnt_hi  := $4005
num_wells     := $4006
wells_x       := $4007     ; 3 bytes
wells_y       := $400A     ; 3 bytes
target_x      := $400D
target_y      := $400E
ball_start_x  := $400F
ball_start_y  := $4010
prev_px       := $4011
prev_py       := $4012
exit_flag     := $4013
ball_pixel_x  := $4014     ; shared between sprite + trail
ball_pixel_y  := $4015
trail_slot    := $4016     ; ring buffer index 1..31

; ----- Zero page -----
.zeropage
          .res 2            ; $00-$01 reserved
cell_col: .res 1            ; $02 cell column for draw_cell
cell_row: .res 1
lvl_lo:   .res 1            ; $04 level loader pointer
lvl_hi:   .res 1
px_hi:    .res 1            ; $06
px_lo:    .res 1
py_hi:    .res 1
py_lo:    .res 1
vx_hi:    .res 1            ; $0A
vx_lo:    .res 1
vy_hi:    .res 1
vy_lo:    .res 1
mul_a:    .res 1            ; $0E
mul_b:    .res 1
mul_hi:   .res 1
mul_lo:   .res 1
cell_char: .res 1           ; $12 char code for draw_cell
tmp:      .res 1
tmp2:     .res 1
adx:      .res 1            ; $15
ady:      .res 1
sign_dx:  .res 1
sign_dy:  .res 1
pull:     .res 1
d_sq_lo:  .res 1
d_sq_hi:  .res 1
well_i:   .res 1            ; $1C
          .res 3            ; free

.code

; =============================================
; MAIN
; =============================================
main:
        LDA #0
        STA lvl_idx
        STA aim_angle
        STA exit_flag
        LDA #4
        STA aim_power
        JSR init_vdp
        JSR load_level
        JSR reset_shot
        LDA KBD                 ; swallow stale key

main_loop:
        LDA exit_flag
        BEQ @cont
        RTS
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
; reset_shot
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
        JSR clear_trail_sprites
        JSR compute_ball_pixel
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR move_ball_sprite
        RTS

; =============================================
; handle_aim
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
        AND #$1F
        STA aim_angle
        RTS
@n2:    CMP #KEY_Z
        BNE @n3
        LDA aim_angle
        CLC
        ADC #1
        AND #$1F
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
; fire
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
        ; Drop the first trail dot at the launch position
        JSR compute_ball_pixel
        JSR stamp_trail_sprite
        RTS

; =============================================
; handle_flight
; =============================================
handle_flight:
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

        ; Sub-pixel: compute pixel coord once, move ball, drop trail dot
        JSR compute_ball_pixel
        JSR move_ball_sprite
        JSR stamp_trail_sprite

        JSR delay

        LDA KBDCR
        BPL @post
        LDA KBD
        CMP #KEY_ESC
        BNE @post
        INC exit_flag
@post:
        RTS

; =============================================
; handle_result
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
; render_scene
; =============================================
render_scene:
        JSR clear_name_table

        LDX #0
@wloop:
        CPX num_wells
        BEQ @done_wells
        STX well_i
        LDA wells_x,X
        STA cell_col
        LDA wells_y,X
        STA cell_row
        LDA #CHR_WELL
        STA cell_char
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR draw_cell
        LDX well_i
        INX
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JMP @wloop
@done_wells:

        LDA target_x
        STA cell_col
        LDA target_y
        STA cell_row
        LDA #CHR_TARGET
        STA cell_char
        JSR draw_cell

        ; Ball is rendered via hardware sprite; refresh its position here
        ; so it survives the name-table clear at the top of render_scene.
        JSR compute_ball_pixel
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR move_ball_sprite

        LDA state
        CMP #ST_AIM
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @done
        JSR draw_aim_tip
@done:
        RTS

; =============================================
; draw_aim_tip
; =============================================
draw_aim_tip:
        LDX aim_angle
        LDA dx_table,X
        STA mul_a
        LDA aim_power
        STA mul_b
        JSR smult_8x8
        LDX #5
        JSR asr_16_x
        LDA mul_lo
        CLC
        ADC ball_start_x
        STA cell_col

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
        STA cell_row

        LDA cell_col
        CMP #GRID_W
        BCS @off
        LDA cell_row
        CMP #GRID_H
        BCS @off
        LDA #CHR_AIM
        STA cell_char
        JSR draw_cell
@off:
        RTS

; =============================================
; draw_cell: write cell_char at cell (cell_col, cell_row)
; via VDP name table ($1800 + row*32 + col).
; =============================================
draw_cell:
        ; tmp:tmp2 = row * 32
        LDA cell_row
        STA tmp                 ; low byte seed
        LDA #0
        STA tmp2                ; high byte seed
        LDX #5
@sh:
        ASL tmp
        ROL tmp2
        DEX
        BNE @sh

        ; + col (8-bit, propagates carry into tmp2)
        LDA tmp
        CLC
        ADC cell_col
        STA tmp
        LDA tmp2
        ADC #0
        STA tmp2

        ; + $1800 base (only high byte adds $18)
        LDA tmp2
        CLC
        ADC #$18
        STA tmp2

        ; Write VDP address: low first, then (high | $40) = write
        LDA tmp
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA tmp2
        ORA #$40
        STA VDP_CTRL

        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA cell_char
        STA VDP_DATA
        RTS

; =============================================
; compute_ball_pixel: derive sub-pixel coords from 8.8 position.
;   ball_pixel_x = px_hi*8 + (px_lo >> 5)   (range 0..255)
;   ball_pixel_y = py_hi*8 + (py_lo >> 5)   (range 0..191)
; =============================================
compute_ball_pixel:
        LDA px_hi
        ASL
        ASL
        ASL
        STA tmp
        LDA px_lo
        LSR
        LSR
        LSR
        LSR
        LSR
        ORA tmp
        STA ball_pixel_x

        LDA py_hi
        ASL
        ASL
        ASL
        STA tmp
        LDA py_lo
        LSR
        LSR
        LSR
        LSR
        LSR
        ORA tmp
        STA ball_pixel_y
        RTS

; =============================================
; move_ball_sprite: sprite 0 = ball, read from ball_pixel_x/y.
; Sprite 0: pattern 0, color 15 (white). TMS Y byte = scanline-1.
; =============================================
move_ball_sprite:
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$5B                ; $1B | $40 (VDP write at $1B00)
        STA VDP_CTRL

        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA ball_pixel_y
        SEC
        SBC #1
        STA VDP_DATA            ; Y
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA ball_pixel_x
        STA VDP_DATA            ; X
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #0
        STA VDP_DATA            ; pattern 0 = ball
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$0F
        STA VDP_DATA            ; color 15
        RTS

; =============================================
; stamp_trail_sprite: drop a trail dot sprite at ball pos.
; Ring-buffers sprites 1..31; each frame overwrites the next slot.
; =============================================
stamp_trail_sprite:
        ; VDP addr = $1B00 + trail_slot * 4  (low byte, high byte stays $1B)
        LDA trail_slot
        ASL
        ASL                     ; *4 (trail_slot <= 31, result <= 124)
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$5B                ; $1B | $40
        STA VDP_CTRL

        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA ball_pixel_y
        SEC
        SBC #1
        STA VDP_DATA            ; Y
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA ball_pixel_x
        STA VDP_DATA            ; X
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #1
        STA VDP_DATA            ; pattern 1 = trail dot
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$0F
        STA VDP_DATA            ; color 15

        INC trail_slot
        LDA trail_slot
        CMP #32
        BCC @done
        LDA #1
        STA trail_slot
@done:
        RTS

; =============================================
; clear_trail_sprites: hide all 31 trail sprites (Y=$D1, off-screen)
; and reset trail_slot = 1.
; =============================================
clear_trail_sprites:
        LDA #$04                ; $1B04 = sprite 1 start
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$5B
        STA VDP_CTRL
        LDX #31
@lp:
        LDA #$D1                ; Y off-screen (not $D0 which terminates list)
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$00
        STA VDP_DATA            ; X
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$01
        STA VDP_DATA            ; pattern 1
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$0F
        STA VDP_DATA            ; color
        DEX
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @lp
        LDA #1
        STA trail_slot
        RTS

; =============================================
; init_vdp: set up Graphics I, upload 6 glyphs at VRAM $0000,
; set colour group 0 = green on black, upload ball sprite pattern
; at VRAM $3800, terminate sprite list at sprite 1.
; =============================================
init_vdp:
        LDX #0
@regs:
        LDA vdp_regs,X
        STA VDP_CTRL
        TXA
        ORA #$80
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_CTRL
        INX
        CPX #8
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @regs

        ; Pattern table at $0000 (6 glyphs × 8 bytes = 48 bytes)
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$40                ; $00 | $40 = write to $0000
        STA VDP_CTRL
        LDX #0
@ptn:
        LDA glyph_data,X
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        INX
        CPX #48
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @ptn

        ; Colour table at VRAM $2000, group 0 only (chars 0-7 green on black)
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$60                ; $20 | $40
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$21                ; fg=2 green, bg=1 black
        STA VDP_DATA

        ; Sprite pattern table at VRAM $3800 - upload 16 bytes:
        ; pattern 0 = ball (8 bytes), pattern 1 = trail dot (8 bytes)
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$78                ; $38 | $40
        STA VDP_CTRL
        LDX #0
@spr:
        LDA ball_sprite,X
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        INX
        CPX #16
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @spr
        RTS

; =============================================
; clear_name_table: fill 768 name-table bytes with CHR_EMPTY
; =============================================
clear_name_table:
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$58                ; $18 | $40
        STA VDP_CTRL
        LDX #3                  ; 3 pages
        LDY #0
        LDA #CHR_EMPTY
@lp:
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        INY
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @lp
        DEX
        BNE @lp
        RTS

; =============================================
; apply_velocity
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
; check_collision
; =============================================
check_collision:
        LDA px_hi
        BMI @lose
        CMP #GRID_W
        BCS @lose
        LDA py_hi
        BMI @lose
        CMP #GRID_H
        BCS @lose

        ; Target? (tolerance = +/- 1 cell)
        LDA px_hi
        SEC
        SBC target_x
        BPL @tx_ap
        EOR #$FF
        CLC
        ADC #1
@tx_ap: CMP #2
        BCS @check_wells
        LDA py_hi
        SEC
        SBC target_y
        BPL @ty_ap
        EOR #$FF
        CLC
        ADC #1
@ty_ap: CMP #2
        BCS @check_wells
        JMP @win

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
        CMP #2
        BCC @done
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
; apply_gravity
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

        LDA wells_x,X
        SEC
        SBC px_hi
        LDY #0
        BPL @adx_ok
        EOR #$FF
        CLC
        ADC #1
        LDY #$FF
@adx_ok:
        STA adx
        STY sign_dx

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

        LDA d_sq_hi
        BEQ @close
        JMP @next
@close:
        LDX d_sq_lo
        LDA pull_lut,X
        STA pull
        BNE @apply
        JMP @next
@apply:
        LDA adx
        STA mul_a
        LDA pull
        STA mul_b
        JSR umult_8x8
        LDX #4
        JSR lsr_16_x

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
; compute_initial_velocity
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
; umult_8x8 / smult_8x8 / asr_16_x / lsr_16_x
; (identical to HGR port)
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

smult_8x8:
        LDA #0
        STA tmp
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
        SEC
        LDA #0
        SBC mul_lo
        STA mul_lo
        LDA #0
        SBC mul_hi
        STA mul_hi
@pos:
        RTS

asr_16_x:
@lp:
        LDA mul_hi
        CMP #$80
        ROR mul_hi
        ROR mul_lo
        DEX
        BNE @lp
        RTS

lsr_16_x:
@lp:
        LSR mul_hi
        ROR mul_lo
        DEX
        BNE @lp
        RTS

; =============================================
; wait_key / delay
; =============================================
wait_key:
        LDA KBDCR
        BPL wait_key
        LDA KBD
        RTS

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
; load_level
; =============================================
load_level:
        LDX lvl_idx
        LDA levels_lo,X
        STA lvl_lo
        LDA levels_hi,X
        STA lvl_hi

        LDY #0
        LDA (lvl_lo),Y
        STA num_wells

        LDY #1
        LDX #0
@wx:    LDA (lvl_lo),Y
        STA wells_x,X
        INY
        INX
        CPX #3
        BNE @wx

        LDX #0
@wy:    LDA (lvl_lo),Y
        STA wells_y,X
        INY
        INX
        CPX #3
        BNE @wy

        LDA (lvl_lo),Y
        STA target_x
        INY
        LDA (lvl_lo),Y
        STA target_y
        INY
        LDA (lvl_lo),Y
        STA ball_start_x
        INY
        LDA (lvl_lo),Y
        STA ball_start_y
        RTS

; =============================================
; DATA
; =============================================

; ---- VDP register values (Graphics I, 16K, screen on, no int) ----
vdp_regs:
        .byte $00, $C0, $06, $80, $00, $36, $07, $01

; ---- Glyph bitmaps (8 bytes per char, bit 7 = leftmost pixel) ----
; Chars 0..5: empty, ball, well, target, trail, aim tip
glyph_data:
        ; 0: empty
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        ; 1: ball (3x3 filled centered at cols 3-5, rows 2-4)
        .byte $00, $00, $38, $38, $38, $00, $00, $00
        ; 2: well (star/asterisk)
        .byte $00, $10, $54, $38, $7C, $38, $54, $10
        ; 3: target (hollow ring)
        .byte $00, $38, $44, $44, $44, $38, $00, $00
        ; 4: trail (single dot near center)
        .byte $00, $00, $00, $10, $00, $00, $00, $00
        ; 5: aim tip (3 pixels horizontal)
        .byte $00, $38, $00, $00, $00, $00, $00, $00

; ---- Sprite patterns (uploaded to VRAM $3800) ----
; Pattern 0 = ball (filled 8x8 circle)
; Pattern 1 = trail dot (tiny 2x2 square centered)
ball_sprite:
        .byte $18, $3C, $7E, $FF, $FF, $7E, $3C, $18
        .byte $00, $00, $00, $18, $18, $00, $00, $00

; ---- Angle tables (32 directions, CCW from East, values scaled to ~64) ----
dx_table:
        .byte  64,  63,  59,  53,  45,  36,  24,  12
        .byte   0, <-12, <-24, <-36, <-45, <-53, <-59, <-63
        .byte <-64, <-63, <-59, <-53, <-45, <-36, <-24, <-12
        .byte   0,  12,  24,  36,  45,  53,  59,  63
dy_table:
        .byte   0, <-12, <-24, <-36, <-45, <-53, <-59, <-63
        .byte <-64, <-63, <-59, <-53, <-45, <-36, <-24, <-12
        .byte   0,  12,  24,  36,  45,  53,  59,  63
        .byte  64,  63,  59,  53,  45,  36,  24,  12

; ---- Gravity LUT (identical to HGR port) ----
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
        .res 94, 0

; ---- Levels (identical to HGR port) ----
levels_lo:
        .byte <level_0, <level_1, <level_2
levels_hi:
        .byte >level_0, >level_1, >level_2

level_0:
        .byte 1
        .byte 16,  0,  0
        .byte 12,  0,  0
        .byte 28, 12
        .byte  2, 12

level_1:
        .byte 2
        .byte 12, 22,  0
        .byte 18,  6,  0
        .byte 29,  4
        .byte  2, 20

level_2:
        .byte 3
        .byte 10, 18, 24
        .byte  6, 18,  6
        .byte 29, 12
        .byte  2, 12
