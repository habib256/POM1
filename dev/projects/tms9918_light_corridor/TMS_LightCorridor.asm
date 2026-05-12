; ============================================================================
; TMS_LightCorridor — wireframe-tunnel paddle game, original implementation
;                     inspired by the Light Corridor (Infogrames 1990) concept.
;
; Target: Apple 1 + P-LAB TMS9918 Graphic Card, shipped as Codetank_GAME4.rom
;         lower bank ($4000-$7FFF, run-in-place from ROM).
;
; Concept (original code, original assets):
;   - 256x192 Mode-2 bitmap holds a static wireframe perspective tunnel
;     (vanishing point at screen centre, 5 nested rectangles + 4 corner
;     connectors). Drawn once via line_xy at level load — ~26 lines /
;     ~40 kc = 2-3 frames; invisible to the player.
;   - Paddle and ball are TMS9918 sprites (16x16 unmag) so they move
;     without touching the bitmap.
;   - Ball is rendered through 4 different patterns (big/med/small/tiny)
;     swapped by ball_z. The pattern swap fakes 3D scaling without any
;     per-frame raster work.
;   - Ball physics: 8-bit signed velocity per axis, position in 16-bit
;     subpixel. AABB collision against paddle when ball_z reaches the
;     player end of the tunnel.
;   - 3 difficulty levels (slower → faster ball, smaller paddle). Lose
;     a life when the paddle misses; victory after surviving N hits.
;
; Controls:
;   A / Q  → paddle left      (Q for AZERTY keyboards)
;   D      → paddle right
;   W / Z  → paddle up        (Z for AZERTY)
;   S      → paddle down
;   SPACE  → launch / restart
;   ESC    → exit to Wozmon
;
; Memory map (Parmigiani dual-bank low RAM):
;   $0000-$003F  ZP (game state)
;   $0040-$00FF  ZP (tms9918m2.asm: pix_*/ln_*/pen_color + tmp/tmp2)
;   $0100-$01FF  6502 stack
;   $0280-$0FFF  free
;   $4000-$7FFF  ROM (this image)
;
; Build: `python3 tools/build_codetank_rom.py --rom=4` from POM1 root.
; ============================================================================

        .import init_vdp_g2, clear_bitmap, disable_sprites
        .import vdp_set_write, vdp_set_read, line_xy
        .importzp pix_addr_lo, pix_addr_hi
        .importzp ln_x0, ln_y0, ln_x1, ln_y1
        .import tms9918_pad12
        .import vdp_display_off, vdp_display_on

.include "apple1.inc"
.include "tms9918.inc"

; --- key codes (Apple-1 PIA forces bit 7, uppercase) ------------------------
KEY_ESC   = $9B
KEY_SPACE = $A0
KEY_CR    = $8D
KEY_A     = $C1
KEY_D     = $C4
KEY_W     = $D7
KEY_S     = $D3
KEY_Q     = $D1
KEY_Z     = $DA

; --- TMS9918 sprite layout --------------------------------------------------
; Sprite Pattern Table = $1800-$1FFF (R6=$03, 64 patterns of 32 B for 16x16).
; Sprite Attribute Table = $3B00-$3B7F (R5=$76, 32 entries of 4 B each).
;
; Pattern allocation (16x16 needs name-index multiple of 4):
;   0   paddle (filled rectangle frame)
;   4   ball   — big   (12x12 ish)
;   8   ball   — medium (8x8)
;   12  ball   — small (4x4)
;   16  ball   — tiny  (2x2)
;   20  obstacle — diamond
SPT_BASE       = $1800
SAT_BASE       = $3B00
PAT_PADDLE     = 0
PAT_BALL_BIG   = 4
PAT_BALL_MED   = 8
PAT_BALL_SMALL = 12
PAT_BALL_TINY  = 16
PAT_OBSTACLE   = 20

; --- game-state constants ---------------------------------------------------
ST_TITLE    = 0
ST_PLAY     = 1
ST_GAMEOVER = 2
ST_VICTORY  = 3

; Tunnel perspective: vanishing point + slice rectangles. Hardcoded LUT
; below (tunnel_lut). The closest slice is slice 0 (full screen); the
; farthest is slice 4 (a few pixels around the vanishing point).
TUNNEL_VPX = 128
TUNNEL_VPY = 96
TUNNEL_SLICES = 5

; Paddle constraints (player-end of the tunnel = slice 0's inner rect):
PAD_X_MIN = 24      ; left wall of slice 0 + small margin
PAD_X_MAX = 224     ; right wall - 16-pixel sprite width
PAD_Y_MIN = 24
PAD_Y_MAX = 152
PAD_X_INIT = 120
PAD_Y_INIT = 88
PAD_DELTA = 4

; Ball z-depth thresholds (z=0 = at the player paddle, z=255 = far).
; The sprite pattern is picked from a lookup based on these breakpoints.
Z_BIG    = 64       ; ball_z <  64 → big   sprite
Z_MED    = 128      ; ball_z < 128 → med   sprite
Z_SMALL  = 192      ; ball_z < 192 → small sprite
                    ;          else → tiny sprite

; ============================================================================
.segment "ZEROPAGE"
        .res 2                  ; $00-$01 reserved (Wozmon LBVAL/HBVAL)
tmp:    .res 1
tmp2:   .res 1
.exportzp tmp, tmp2

; Game state
state:        .res 1            ; ST_TITLE / ST_PLAY / ST_GAMEOVER / ST_VICTORY
level:        .res 1            ; 1..MAX_LEVEL
lives:        .res 1
score:        .res 1            ; 0..255 (hits accumulated)
hits_needed:  .res 1            ; per-level victory threshold

; Paddle (16x16 sprite at SAT slot 0)
pad_x:        .res 1            ; screen X, top-left of sprite
pad_y:        .res 1            ; screen Y, top-left of sprite

; Ball (16x16 sprite at SAT slot 1)
ball_x:       .res 1            ; screen X (top-left of the 16x16 sprite quad)
ball_y:       .res 1
ball_z:       .res 1            ; depth: 0 = at player, 255 = at vanishing
ball_vx:      .res 1            ; signed velocity X (8-bit two's complement)
ball_vy:      .res 1            ; signed velocity Y
ball_vz:      .res 1            ; signed velocity Z (negative = approaching)
ball_speed:   .res 1            ; magnitude of vz at spawn (per level)

; Input + frame counter
last_key:     .res 1
frame_ctr:    .res 1

; Scratch
sc0:          .res 1
sc1:          .res 1

; ============================================================================
.segment "CODE"
; ============================================================================

; Entry point at $4000 — Codetank GAME4 lower bank.
start:
        SEI
        CLD
        LDX #$FF
        TXS

        ; Greeting on the Apple-1 native PIA display (visible even before
        ; the TMS9918 wakes — useful as a "ROM loaded" indicator).
        LDA #<greeting
        LDX #>greeting
        JSR print_str_ax

        ; Boot the TMS9918 into Mode 2, clear bitmap, install sprite
        ; patterns, set 16x16 sprite mode.
        JSR init_vdp_g2
        JSR disable_sprites             ; defensive SAT terminator
        JSR clear_bitmap
        JSR upload_sprite_patterns
        JSR vdp_set_sprite_16x16

        ; Game-state init.
        LDA #ST_TITLE
        STA state
        LDA #1
        STA level
        LDA #3
        STA lives
        LDA #0
        STA score
        STA frame_ctr
        STA last_key

        JSR show_title_tunnel

main_loop:
        JSR handle_input
        JSR check_esc

        LDA state
        CMP #ST_PLAY
        BNE @notplay
        JSR update_ball
        JSR write_sat
@notplay:
        INC frame_ctr
        JSR delay_one_frame
        JMP main_loop


; ----------------------------------------------------------------------------
; check_esc — bail to Wozmon if ESC is in last_key. Cleared on consume.
; ----------------------------------------------------------------------------
check_esc:
        LDA last_key
        CMP #KEY_ESC
        BEQ exit_to_wozmon
        RTS

exit_to_wozmon:
        JSR vdp_display_off
        JMP WOZMON


; ----------------------------------------------------------------------------
; handle_input — non-blocking poll of $D011 / $D010, stash in last_key,
; dispatch to per-state handlers.
; ----------------------------------------------------------------------------
handle_input:
        LDA KBDCR
        BPL @nokey
        LDA KBD
        STA last_key
        ; Dispatch on state
        LDX state
        CPX #ST_PLAY
        BEQ @play
        CPX #ST_TITLE
        BEQ @title
        CPX #ST_GAMEOVER
        BEQ @gameover_in
        CPX #ST_VICTORY
        BEQ @victory_in
        RTS
@nokey: LDA #0
        STA last_key
        RTS

@title:
        LDA last_key
        CMP #KEY_SPACE
        BEQ @start_game
        CMP #KEY_CR
        BEQ @start_game
        RTS
@start_game:
        JSR load_level
        LDA #ST_PLAY
        STA state
        RTS

@gameover_in:
@victory_in:
        LDA last_key
        CMP #KEY_SPACE
        BEQ @back_to_title
        CMP #KEY_CR
        BEQ @back_to_title
        RTS
@back_to_title:
        LDA #1
        STA level
        LDA #3
        STA lives
        LDA #0
        STA score
        LDA #ST_TITLE
        STA state
        JSR show_title_tunnel
        RTS

@play:
        LDA last_key
        CMP #KEY_A
        BEQ @left
        CMP #KEY_Q
        BEQ @left
        CMP #KEY_D
        BEQ @right
        CMP #KEY_W
        BEQ @up
        CMP #KEY_Z
        BEQ @up
        CMP #KEY_S
        BEQ @down
        RTS
@left:
        LDA pad_x
        SEC
        SBC #PAD_DELTA
        CMP #PAD_X_MIN
        BCC @clamp_xmin
        STA pad_x
        RTS
@clamp_xmin:
        LDA #PAD_X_MIN
        STA pad_x
        RTS
@right:
        LDA pad_x
        CLC
        ADC #PAD_DELTA
        CMP #PAD_X_MAX+1
        BCS @clamp_xmax
        STA pad_x
        RTS
@clamp_xmax:
        LDA #PAD_X_MAX
        STA pad_x
        RTS
@up:
        LDA pad_y
        SEC
        SBC #PAD_DELTA
        CMP #PAD_Y_MIN
        BCC @clamp_ymin
        STA pad_y
        RTS
@clamp_ymin:
        LDA #PAD_Y_MIN
        STA pad_y
        RTS
@down:
        LDA pad_y
        CLC
        ADC #PAD_DELTA
        CMP #PAD_Y_MAX+1
        BCS @clamp_ymax
        STA pad_y
        RTS
@clamp_ymax:
        LDA #PAD_Y_MAX
        STA pad_y
        RTS


; ----------------------------------------------------------------------------
; show_title_tunnel — draw the title-screen tunnel. Same wireframe as the
; play screen but with the title splash printed on the Apple-1 PIA side.
; ----------------------------------------------------------------------------
show_title_tunnel:
        JSR vdp_display_off
        JSR clear_bitmap
        JSR draw_tunnel
        ; SAT[0] = terminator → no sprites on title.
        JSR sat_hide_all
        JSR vdp_display_on
        LDA #<title_msg
        LDX #>title_msg
        JSR print_str_ax
        RTS


; ----------------------------------------------------------------------------
; load_level — set ball_speed / hits_needed from level table, spawn ball
; far away, centre paddle, redraw tunnel.
; ----------------------------------------------------------------------------
load_level:
        JSR vdp_display_off
        JSR clear_bitmap
        JSR draw_tunnel

        ; Read level data: 2 bytes per level (ball_speed, hits_needed).
        LDA level
        SEC
        SBC #1
        ASL                           ; idx = (level-1) * 2
        TAX
        LDA level_table,X
        STA ball_speed
        INX
        LDA level_table,X
        STA hits_needed

        LDA #PAD_X_INIT
        STA pad_x
        LDA #PAD_Y_INIT
        STA pad_y

        JSR spawn_ball
        JSR write_sat
        JSR vdp_display_on
        RTS


; ----------------------------------------------------------------------------
; spawn_ball — place the ball at the vanishing point, moving toward player
; with the per-level speed.
; ----------------------------------------------------------------------------
spawn_ball:
        LDA #TUNNEL_VPX-8             ; centre 16x16 sprite on vanishing pt
        STA ball_x
        LDA #TUNNEL_VPY-8
        STA ball_y
        LDA #240                      ; ball_z near 255 = at vanishing point
        STA ball_z

        ; Velocity: vx/vy small random-ish drift (deterministic from
        ; frame_ctr to keep it predictable). vz = -ball_speed (toward
        ; player).
        LDA frame_ctr
        AND #$03
        SEC
        SBC #1                        ; A = -1..2
        STA ball_vx
        LDA frame_ctr
        LSR
        LSR
        AND #$03
        SEC
        SBC #1
        STA ball_vy
        LDA #0
        SEC
        SBC ball_speed                ; vz = -ball_speed
        STA ball_vz
        RTS


; ----------------------------------------------------------------------------
; update_ball — advance ball position by velocity, handle wall bounces in
; XY, handle Z arrival at the paddle plane (collision check or life loss).
; ----------------------------------------------------------------------------
update_ball:
        ; --- X axis ---
        LDA ball_x
        CLC
        ADC ball_vx                   ; signed add (ADC works on bytes)
        STA ball_x
        ; Bounce on screen edges (rough — tunnel walls are tilted but the
        ; ball is closer to centre when far away so this approximates).
        CMP #16
        BCS @x_ok_lo
        LDA #16
        STA ball_x
        JSR flip_vx
        JMP @y
@x_ok_lo:
        CMP #224
        BCC @y
        LDA #224
        STA ball_x
        JSR flip_vx

@y:     LDA ball_y
        CLC
        ADC ball_vy
        STA ball_y
        CMP #16
        BCS @y_ok_lo
        LDA #16
        STA ball_y
        JSR flip_vy
        JMP @z
@y_ok_lo:
        CMP #168
        BCC @z
        LDA #168
        STA ball_y
        JSR flip_vy

@z:     LDA ball_z
        CLC
        ADC ball_vz
        STA ball_z
        ; If vz < 0 and z wraps (went below 0): paddle plane reached.
        LDA ball_vz
        BPL @z_far
        ; vz < 0: detect underflow (carry clear after ADC means wrap from
        ; positive small → large positive). We check: if old ball_z was
        ; small (< |vz|) we arrived.
        LDA ball_z
        CMP #16                       ; arrived if z < 16
        BCS @done
        JMP paddle_hit_check
@z_far: ; vz >= 0: detect z near max — bounce off back wall.
        LDA ball_z
        CMP #248
        BCC @done
        LDA #248
        STA ball_z
        LDA #0
        SEC
        SBC ball_vz                   ; vz = -vz
        STA ball_vz
@done:  RTS

flip_vx:
        LDA #0
        SEC
        SBC ball_vx
        STA ball_vx
        RTS
flip_vy:
        LDA #0
        SEC
        SBC ball_vy
        STA ball_vy
        RTS


; ----------------------------------------------------------------------------
; paddle_hit_check — ball has reached the player plane. Test AABB between
; ball centre and paddle 16x16 box. Hit → bounce + score. Miss → lose life.
; ----------------------------------------------------------------------------
paddle_hit_check:
        ; |ball_x_centre - pad_x_centre| <= 16 and same for Y.
        LDA ball_x
        SEC
        SBC pad_x
        BCS @absx_pos
        EOR #$FF
        CLC
        ADC #1
@absx_pos:
        CMP #16
        BCS @miss
        LDA ball_y
        SEC
        SBC pad_y
        BCS @absy_pos
        EOR #$FF
        CLC
        ADC #1
@absy_pos:
        CMP #16
        BCS @miss

        ; HIT — bounce ball back, add spin from offset, increment score.
        INC score
        LDA score
        CMP hits_needed
        BCC @bounce
        JSR level_up
        RTS
@bounce:
        LDA #16
        STA ball_z
        LDA #0
        SEC
        SBC ball_vz                   ; vz = +ball_speed
        STA ball_vz
        ; Spin: ball_vx += (ball_x - pad_x) / 8
        LDA ball_x
        SEC
        SBC pad_x
        ; arithmetic shift right by 3 (sign-preserving)
        CMP #$80
        ROR
        CMP #$80
        ROR
        CMP #$80
        ROR
        CLC
        ADC ball_vx
        STA ball_vx
        LDA ball_y
        SEC
        SBC pad_y
        CMP #$80
        ROR
        CMP #$80
        ROR
        CMP #$80
        ROR
        CLC
        ADC ball_vy
        STA ball_vy
        RTS
@miss:
        DEC lives
        BEQ @over
        JSR spawn_ball
        RTS
@over:
        LDA #ST_GAMEOVER
        STA state
        JSR sat_hide_all
        LDA #<gameover_msg
        LDX #>gameover_msg
        JSR print_str_ax
        RTS

level_up:
        INC level
        LDA level
        CMP #(MAX_LEVEL+1)
        BCS @victory
        LDA #0
        STA score
        JSR load_level
        RTS
@victory:
        LDA #ST_VICTORY
        STA state
        JSR sat_hide_all
        LDA #<victory_msg
        LDX #>victory_msg
        JSR print_str_ax
        RTS


; ----------------------------------------------------------------------------
; vdp_set_sprite_16x16 — flip R1 to enable 16x16 sprites (bit 1 = 1).
; init_vdp_g2 leaves R1 = $C0 (display on, 8x8). We want $C2.
; ----------------------------------------------------------------------------
vdp_set_sprite_16x16:
        JSR     tms9918_pad12
        LDA     #$C2
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81                  ; reg 1
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS


; ----------------------------------------------------------------------------
; upload_sprite_patterns — copy patterns_data (192 B = 6 × 32 B) to VRAM
; $1800 (sprite pattern table base). Bytes are written one-by-one with
; pad12 between each to keep silicon-strict happy.
; ----------------------------------------------------------------------------
upload_sprite_patterns:
        JSR vdp_display_off
        ; Set VRAM write address = $1800.
        LDA #$00
        STA pix_addr_lo
        LDA #$18
        STA pix_addr_hi
        JSR vdp_set_write
        LDX #0
@lp:    LDA patterns_data,X
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #PATTERNS_LEN
        BNE @lp
        JSR vdp_display_on
        RTS


; ----------------------------------------------------------------------------
; write_sat — refresh sprite attribute table for paddle + ball. SAT layout:
;   slot 0: paddle  (Y, X, pattern, colour)
;   slot 1: ball    (Y, X, pattern, colour)
;   slot 2: terminator Y=$D0
; ----------------------------------------------------------------------------
write_sat:
        ; --- pick ball pattern from ball_z thresholds ---
        LDA ball_z
        CMP #Z_BIG
        BCC @big
        CMP #Z_MED
        BCC @med
        CMP #Z_SMALL
        BCC @small
        LDA #PAT_BALL_TINY
        JMP @gotpat
@big:   LDA #PAT_BALL_BIG
        JMP @gotpat
@med:   LDA #PAT_BALL_MED
        JMP @gotpat
@small: LDA #PAT_BALL_SMALL
@gotpat:
        STA sc0                       ; sc0 = ball sprite pattern

        ; Set VRAM write address = $3B00 (SAT).
        LDA #$00
        STA pix_addr_lo
        LDA #$3B
        STA pix_addr_hi
        JSR vdp_set_write

        ; --- slot 0: paddle ---
        LDA pad_y
        STA VDP_DATA
        JSR tms9918_pad12
        LDA pad_x
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #PAT_PADDLE
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0F                      ; colour: white
        STA VDP_DATA
        JSR tms9918_pad12

        ; --- slot 1: ball ---
        LDA ball_y
        STA VDP_DATA
        JSR tms9918_pad12
        LDA ball_x
        STA VDP_DATA
        JSR tms9918_pad12
        LDA sc0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0A                      ; colour: dark yellow
        STA VDP_DATA
        JSR tms9918_pad12

        ; --- slot 2: terminator ---
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12
        RTS


; ----------------------------------------------------------------------------
; sat_hide_all — write $D0 at SAT[0].Y so the sprite scan stops immediately.
; ----------------------------------------------------------------------------
sat_hide_all:
        LDA #$00
        STA pix_addr_lo
        LDA #$3B
        STA pix_addr_hi
        JSR vdp_set_write
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12
        RTS


; ----------------------------------------------------------------------------
; draw_tunnel — TUNNEL_SLICES concentric rectangles via line_xy + 4
; vanishing-point connector lines from the outermost corners to the
; inner-most rectangle's matching corners. Each slice = 4 bytes (x0/y0/x1/y1)
; in tunnel_lut; sc0 holds the per-iteration base index so it survives
; line_xy's clobbers.
; ----------------------------------------------------------------------------
draw_tunnel:
        LDA #0
        STA sc0                       ; sc0 = current slice base offset
@rect_loop:
        LDX sc0
        JSR draw_rect_at_x
        LDA sc0
        CLC
        ADC #4
        STA sc0
        CMP #(TUNNEL_SLICES*4)
        BCC @rect_loop

        ; --- 4 vanishing-point connectors: outer (slice 0) → inner (slice 4)
        LDA #0
        STA sc0                       ; sc0 = outer slice base
        LDA #(4*4)                    ; inner slice base = 16
        STA sc1
        JSR draw_corner_link_tl
        JSR draw_corner_link_tr
        JSR draw_corner_link_br
        JSR draw_corner_link_bl
        RTS


; draw_rect_at_x — draw the 4 sides of the rectangle whose bounds live at
; tunnel_lut[X..X+3]. X is preserved across line_xy via the stack.
draw_rect_at_x:
        TXA
        PHA
        ; --- top side: (x0,y0) → (x1,y0) ---
        LDA tunnel_lut,X
        STA ln_x0
        LDA tunnel_lut+1,X
        STA ln_y0
        STA ln_y1
        LDA tunnel_lut+2,X
        STA ln_x1
        JSR line_xy
        PLA
        TAX
        TXA
        PHA
        ; --- right side: (x1,y0) → (x1,y1) ---
        LDA tunnel_lut+2,X
        STA ln_x0
        STA ln_x1
        LDA tunnel_lut+1,X
        STA ln_y0
        LDA tunnel_lut+3,X
        STA ln_y1
        JSR line_xy
        PLA
        TAX
        TXA
        PHA
        ; --- bottom side: (x0,y1) → (x1,y1) ---
        LDA tunnel_lut,X
        STA ln_x0
        LDA tunnel_lut+2,X
        STA ln_x1
        LDA tunnel_lut+3,X
        STA ln_y0
        STA ln_y1
        JSR line_xy
        PLA
        TAX
        ; --- left side: (x0,y0) → (x0,y1) ---
        LDA tunnel_lut,X
        STA ln_x0
        STA ln_x1
        LDA tunnel_lut+1,X
        STA ln_y0
        LDA tunnel_lut+3,X
        STA ln_y1
        JMP line_xy                   ; tail call


; Connector helpers: sc0 = outer slice base, sc1 = inner slice base.
draw_corner_link_tl:
        LDX sc0
        LDA tunnel_lut,X
        STA ln_x0
        LDA tunnel_lut+1,X
        STA ln_y0
        LDX sc1
        LDA tunnel_lut,X
        STA ln_x1
        LDA tunnel_lut+1,X
        STA ln_y1
        JMP line_xy

draw_corner_link_tr:
        LDX sc0
        LDA tunnel_lut+2,X
        STA ln_x0
        LDA tunnel_lut+1,X
        STA ln_y0
        LDX sc1
        LDA tunnel_lut+2,X
        STA ln_x1
        LDA tunnel_lut+1,X
        STA ln_y1
        JMP line_xy

draw_corner_link_br:
        LDX sc0
        LDA tunnel_lut+2,X
        STA ln_x0
        LDA tunnel_lut+3,X
        STA ln_y0
        LDX sc1
        LDA tunnel_lut+2,X
        STA ln_x1
        LDA tunnel_lut+3,X
        STA ln_y1
        JMP line_xy

draw_corner_link_bl:
        LDX sc0
        LDA tunnel_lut,X
        STA ln_x0
        LDA tunnel_lut+3,X
        STA ln_y0
        LDX sc1
        LDA tunnel_lut,X
        STA ln_x1
        LDA tunnel_lut+3,X
        STA ln_y1
        JMP line_xy


; ----------------------------------------------------------------------------
; delay_one_frame — coarse software delay to approximate a 60 Hz tick. Real
; framerate will run lower than 60 Hz when the ball is updating the bitmap
; (it isn't — sprites only), but the delay keeps the game from running at
; 1 MHz raw which would be unplayable.
;
; ~17 000 cycles per frame at 1.022 MHz; this loop is calibrated by hand.
; ----------------------------------------------------------------------------
delay_one_frame:
        LDY #20
@outer: LDX #200
@inner: DEX
        BNE @inner
        DEY
        BNE @outer
        RTS


; ----------------------------------------------------------------------------
; print_str_ax — print null-terminated string at A:X via Wozmon ECHO.
; ----------------------------------------------------------------------------
print_str_ax:
        STA sc0
        STX sc0+1
        LDY #0
@lp:    LDA (sc0),Y
        BEQ @end
        ORA #$80
        JSR ECHO
        INY
        BNE @lp
@end:   RTS


; ============================================================================
.segment "RODATA"
; ============================================================================

; --- Tunnel LUT: 5 slices × 4 bytes (x0, y0, x1, y1) ------------------------
; Slice 0 = closest (full bitmap); slice 4 = innermost near vanishing point.
; Half-widths/heights chosen so the perspective looks plausible at 256x192:
;   slice 0: 224 × 168  (outermost frame)
;   slice 1: 144 × 108
;   slice 2:  96 × 72
;   slice 3:  56 × 42
;   slice 4:  24 × 18
tunnel_lut:
        .byte  16,  16, 239, 175       ; slice 0
        .byte  56,  42, 199, 149       ; slice 1
        .byte  80,  60, 175, 131       ; slice 2
        .byte 100,  75, 155, 116       ; slice 3
        .byte 116,  87, 139, 104       ; slice 4

; --- Level table: 2 bytes per level (ball_speed, hits_needed) --------------
MAX_LEVEL = 3
level_table:
        .byte 2,  5                    ; lvl 1: slow ball, 5 hits to clear
        .byte 3,  7                    ; lvl 2: medium
        .byte 4, 10                    ; lvl 3: fast


; --- Sprite patterns: 6 × 32 bytes = 192 B ---------------------------------
; 16x16 pattern layout: 4 quadrants of 8 bytes each, in this order:
;   quad TL (rows 0-7, cols 0-7)   bytes 0..7
;   quad BL (rows 8-15, cols 0-7)  bytes 8..15
;   quad TR (rows 0-7, cols 8-15)  bytes 16..23
;   quad BR (rows 8-15, cols 8-15) bytes 24..31
; Within each byte, col N = bit (7-N).
patterns_data:

; --- Pattern 0: paddle ($1800) -- 12x12 filled rectangle centred -----------
        .byte $00, $00, $3F, $3F, $3F, $3F, $3F, $3F  ; TL: rows 0-7
        .byte $3F, $3F, $3F, $3F, $3F, $3F, $00, $00  ; BL: rows 8-15
        .byte $00, $00, $FC, $FC, $FC, $FC, $FC, $FC  ; TR: rows 0-7
        .byte $FC, $FC, $FC, $FC, $FC, $FC, $00, $00  ; BR: rows 8-15

; --- Pattern 4: ball big ($1880) -- 12x12 filled ---------------------------
        .byte $00, $00, $3F, $3F, $3F, $3F, $3F, $3F
        .byte $3F, $3F, $3F, $3F, $3F, $3F, $00, $00
        .byte $00, $00, $FC, $FC, $FC, $FC, $FC, $FC
        .byte $FC, $FC, $FC, $FC, $FC, $FC, $00, $00

; --- Pattern 8: ball medium ($1900) -- 8x8 centred -------------------------
        .byte $00, $00, $00, $00, $0F, $0F, $0F, $0F  ; TL: rows 4-7 cols 4-7
        .byte $0F, $0F, $0F, $0F, $00, $00, $00, $00  ; BL: rows 8-11 cols 4-7
        .byte $00, $00, $00, $00, $F0, $F0, $F0, $F0  ; TR: rows 4-7 cols 8-11
        .byte $F0, $F0, $F0, $F0, $00, $00, $00, $00  ; BR: rows 8-11 cols 8-11

; --- Pattern 12: ball small ($1980) -- 4x4 centred -------------------------
        .byte $00, $00, $00, $00, $00, $00, $03, $03  ; TL: rows 6-7 cols 6-7
        .byte $03, $03, $00, $00, $00, $00, $00, $00  ; BL: rows 8-9 cols 6-7
        .byte $00, $00, $00, $00, $00, $00, $C0, $C0  ; TR: rows 6-7 cols 8-9
        .byte $C0, $C0, $00, $00, $00, $00, $00, $00  ; BR: rows 8-9 cols 8-9

; --- Pattern 16: ball tiny ($1A00) -- 2x2 centred --------------------------
        .byte $00, $00, $00, $00, $00, $00, $00, $01  ; TL: row 7 col 7
        .byte $01, $00, $00, $00, $00, $00, $00, $00  ; BL: row 8 col 7
        .byte $00, $00, $00, $00, $00, $00, $00, $80  ; TR: row 7 col 8
        .byte $80, $00, $00, $00, $00, $00, $00, $00  ; BR: row 8 col 8

; --- Pattern 20: obstacle (diamond — for future use, kept silent in MVP) ---
        .byte $00, $01, $03, $07, $0F, $1F, $3F, $7F
        .byte $7F, $3F, $1F, $0F, $07, $03, $01, $00
        .byte $00, $80, $C0, $E0, $F0, $F8, $FC, $FE
        .byte $FE, $FC, $F8, $F0, $E0, $C0, $80, $00

PATTERNS_LEN = * - patterns_data       ; = 192


; --- Banner text ------------------------------------------------------------
greeting:
        .byte $0D
        .byte "TMS_LIGHT_CORRIDOR (POM1 CODETANK GAME4)", $0D
        .byte "INSPIRED BY INFOGRAMES 1990", $0D
        .byte "ORIGINAL CODE / ORIGINAL ASSETS", $0D
        .byte $00

title_msg:
        .byte $0D
        .byte "*** LIGHT CORRIDOR ***", $0D
        .byte "AQZ S D W = MOVE PADDLE (AZERTY+QWERTY)", $0D
        .byte "SPACE = LAUNCH BALL", $0D
        .byte "ESC = EXIT TO WOZMON", $0D
        .byte $0D
        .byte "PRESS SPACE TO START", $0D
        .byte $00

gameover_msg:
        .byte $0D
        .byte "*** GAME OVER ***", $0D
        .byte "PRESS SPACE TO RETURN TO TITLE", $0D
        .byte $00

victory_msg:
        .byte $0D
        .byte "*** TUNNEL CLEARED ***", $0D
        .byte "PRESS SPACE TO RETURN TO TITLE", $0D
        .byte $00


; ============================================================================
; plot_mode export — required by tms9918m2.asm even though we never call
; plot_set directly (line_xy uses it internally via OR mode). Place AT THE
; END of CODE so $4000 lands on `start:` which is the first label in the
; .segment "CODE" stream.
; ============================================================================
.export plot_mode
plot_mode: .byte 0
