; ============================================================================
; TMS_Asteroids.asm  --  vector-style ship demo on the TMS9918
;                        (c) 2026 VERHILLE Arnaud
; ============================================================================
; First foundation milestone (V0.1): a single rotating ship sprite that
; the player rotates with ,/. and thrusts with '/'. The ship inherits
; classic Asteroids physics: thrust adds velocity in the heading direction,
; no friction, screen wraps on all four edges. No asteroids, no bullets,
; no collision yet -- those land in V0.2+.
;
; The point of V0.1 is to validate the sprite_triangle pipeline end to
; end (math.asm trig + Bresenham-into-RAM + VRAM upload + sprite attribute
; write at 60 Hz on a real Apple-1 + TMS9918), and to set up the project
; structure (16K dev DRAM cfg, separate .o per module, math/VDP libs
; pulled in from dev/lib/).
;
; Build & run:
;   cd dev/projects/tms9918_asteroids && make
;   ./POM1 --preset 8       (P-LAB Apple-1 with TMS9918 + CodeTank)
;     then in Woz Monitor paste the .txt and type 0280R.
;
; Controls (V0.2, AZERTY ZQSD):
;     Q     rotate left  (-6 deg per tap)
;     D     rotate right (+6 deg per tap)
;     Z     thrust       (+1 unit velocity per tap, in current heading)
;     S     brake + fire (decay velocity 12.5% AND spawn bullet at tip)
;     ESC   quit -> Wozmon prompt
;
; ZQSD is the French AZERTY equivalent of WASD. SPACE and ESC are
; layout-universal. S is dual-purpose (brake + fire) so the player keeps
; one finger on the firing key while still being able to slow the ship.
; ============================================================================

; --- I/O equates -----------------------------------------------------------
        .import tms9918_pad12  ; silicon-strict pad12-v3 (helper from tms9918_pad.asm)
.include "apple1.inc"
.include "tms9918.inc"
; kbd.asm (wait_key / poll_key) is .include'd AT THE BOTTOM of this file
; so main: lands at $0280. .include emits the lib's body into the CODE
; segment in the order it's seen; including it at the top would push
; main: down and a Wozmon `0280R` would jump straight into wait_key.

; --- Imports from libs -----------------------------------------------------
;
; tms9918m2.asm -- Mode-2 bitmap driver. Asteroids sets up Mode 2 + a
; permanently-black bitmap so sprites are the only visible foreground.
.import   init_vdp_g2, clear_bitmap, disable_sprites
.importzp pen_color
;
; math.asm -- 16-bit angle helpers.
.import   roll_lfsr, mod360_arg
;
; sprite_triangle.asm -- the rotating-ship sprite renderer.
.import   sprite_triangle_render, sprite_buf_upload, sprite_attr_write
.import   sprite_buf
.import   tri_angle_lo, tri_angle_hi, tri_x, tri_y, tri_slot, tri_color
.import   ship_tip_x, ship_tip_y, ship_dir_x, ship_dir_y

; --- Exports consumed by the libs (math.asm + sprite_triangle.asm
;     + tms9918m2.asm) ----------------------------------------------------
.exportzp tmp, tmp2, arg_lo, arg_hi, arg2_lo, arg2_hi, th_lo, th_hi
.export   prod_lo, prod_hi, sign_flag, lfsr_lo, lfsr_hi
.export   plot_mode      ; tms9918m2 line_xy reads it; we don't draw lines

; --- Game tunables ---------------------------------------------------------
; Q sets rot_dir = $FF (rotating left), D sets rot_dir = $01 (rotating
; right), S clears it. Each game tick rotates by ROT_STEP degrees in
; rot_dir's direction (no rotation if rot_dir = 0). Same Galaga pattern
; for thrust: Z sets thrust_active = 1, S clears. Apple-1 keyboards
; have no key-release event, so this state machine is the standard way
; to give the player "hold-to-act" feel from a tap-only input device.
SPEED          = 4       ; delay_and_input outer iters; ~10 ms per tick
ROT_STEP       = 3       ; degrees per tick while rot_dir != 0
THRUST_SHIFT   = 5       ; ship_dir / 32 = thrust per tick while active
BRAKE_SHIFT    = 3       ; v -= v / 8 on S press
BULLET_TTL     = 60      ; frames the bullet stays alive
BULLET_COUNT   = 1       ; single bullet, but every S press always spawns
                         ; (overwrite the in-flight bullet, no queueing
                         ; -- "il faut pas qu'il y ait de la tête")
SHIP_INIT_X    = 128
SHIP_INIT_Y    = 96
; Sprite layout in the attribute table:
;   slot 0      = ship  (rasterised by sprite_triangle_render)
;   slot 1..4   = bullets (share the static pattern at slot 1's pattern
;                 cell -> name = 4 in 16x16 sprite-mode addressing)
;   slot 5      = scan-chain terminator (Y = $D0)
BULLET_PATSLOT = 1       ; pattern slot index (name = PATSLOT * 4 = 4)
SPRITE_TERMSLOT = 5      ; first slot after all bullets, terminates scan
SPR_HIDE_Y     = $C8     ; Y in [192..207] = off-screen, scan continues
SPR_TERM_Y     = $D0     ; Y = 208 = chip stops scanning sprite chain

; --- ZP scalars ------------------------------------------------------------
.segment "ZEROPAGE"
tmp:           .res 1
tmp2:          .res 1
arg_lo:        .res 1
arg_hi:        .res 1
arg2_lo:       .res 1
arg2_hi:       .res 1
th_lo:         .res 1
th_hi:         .res 1

; --- BSS ------------------------------------------------------------------
.segment "BSS"
prod_lo:       .res 1
prod_hi:       .res 1
sign_flag:     .res 1
lfsr_lo:       .res 1
lfsr_hi:       .res 1
plot_mode:     .res 1     ; 0 = OR, 1 = XOR (unused -- never draw lines)

; Ship state -- 16-bit position with low byte = subpixel fraction so
; small velocities still produce smooth motion.
ship_x_frac:   .res 1
ship_x_int:    .res 1
ship_y_frac:   .res 1
ship_y_int:    .res 1
ship_vx:       .res 1     ; signed 8-bit velocity (subpixels per frame)
ship_vy:       .res 1
ship_angle_lo: .res 1     ; 16-bit heading 0..359 (north=0, east=90)
ship_angle_hi: .res 1

frame_ctr:     .res 1     ; 8-bit frame counter, wraps

; --- Input state machine (Galaga-style: keys SET state, ticks act on it).
;     Apple-1 keyboards have no key-release event, so polling can't tell
;     when the user lets go. We compensate by latching the action: Q/D
;     start a rotation that continues until S stops it (or D/Q reverses
;     it). Z latches thrust_active until S clears it.
rot_dir:       .res 1     ; $00 = no rotation, $FF = left, $01 = right
thrust_active: .res 1     ; 0 = coasting, 1 = thrusting each tick

; Bullet pool: BULLET_COUNT bullets in flight simultaneously. Parallel
; arrays indexed 0..BULLET_COUNT-1, mapped to sprite slots 1..BULLET_COUNT.
; All bullets share the static pattern at pattern-slot BULLET_PATSLOT
; (= sprite name 4 in 16x16 mode). On S press we scan for the first
; idle slot and spawn there; if all slots are busy we overwrite slot 0
; (cheapest re-fire policy -- the player gets a fresh bullet every S
; press, never a "no fire" silence).
bullet_active: .res BULLET_COUNT
bullet_x:      .res BULLET_COUNT
bullet_y:      .res BULLET_COUNT
bullet_vx:     .res BULLET_COUNT
bullet_vy:     .res BULLET_COUNT
bullet_ttl:    .res BULLET_COUNT

; ============================================================================
.segment "CODE"
; ============================================================================
; Entry point ($0280)
; ============================================================================
main:
        SEI
        CLD
        LDX #$FF
        TXS
        ; --- VDP setup: Mode 2 bitmap, black background, sprites enabled ---
        JSR init_vdp_g2          ; default R1=$C0 = sprite-8
        JSR clear_bitmap         ; wipe the 6 KB pattern table to 0 (black)
        JSR disable_sprites      ; clear ghost sprites from VRAM noise
        ; --- Force R1 = $C2 (16K | DISP | sprite-16). sprite_triangle
        ;     emits 32-byte 16x16 patterns; sprite-8 would only display
        ;     the top-left quadrant.
        LDA #$C2
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$81                 ; write to register 1
        STA VDP_CTRL
        ; --- LFSR seed for future RANDOM (asteroid spawn) ---
        LDA #$AC
        STA lfsr_lo
        LDA #$E1
        STA lfsr_hi
        ; --- Ship initial state ---
        LDA #SHIP_INIT_X
        STA ship_x_int
        LDA #SHIP_INIT_Y
        STA ship_y_int
        LDA #0
        STA ship_x_frac
        STA ship_y_frac
        STA ship_vx
        STA ship_vy
        STA ship_angle_lo        ; heading = 0 (north / up)
        STA ship_angle_hi
        STA frame_ctr
        ; --- Clear bullet pool, set up sprite chain ---
        LDA #0
        LDX #BULLET_COUNT - 1
@cb:    STA bullet_active,X
        DEX
        BPL @cb
        JSR init_bullet_pattern  ; uploads 4x4 dot to pattern slot 1 ($1820)
        JSR init_sprite_chain    ; hides bullets, terminates scan at slot 5
        ; --- Configure the sprite_triangle rasterizer for the ship ---
        LDA #0
        STA tri_slot             ; slot 0 = ship
        LDA #$0F                 ; white
        STA tri_color
        ; Render once so ship_dir_x / ship_dir_y are initialised (the
        ; first thrust / fire would otherwise read 0 on frame 1).
        LDA #SHIP_INIT_X
        STA tri_x
        LDA #SHIP_INIT_Y
        STA tri_y
        LDA #0
        STA tri_angle_lo
        STA tri_angle_hi
        JSR sprite_triangle_render

; ----------------------------------------------------------------------------
; Game loop -- per tick:
;   1. Apply rotation if rot_dir != 0 (state-driven, Galaga pattern)
;   2. Apply thrust if thrust_active
;   3. Integrate velocity into position with screen wrap
;   4. Update bullet
;   5. Render the ship sprite
;   6. delay_and_input -- throttles to ~70 ticks/s AND drains keys into
;      the rot_dir / thrust_active state machine. Polling at ~1500 Hz
;      during the delay catches every key press without busy-waiting.
; ----------------------------------------------------------------------------
game_loop:
        INC frame_ctr

        ; --- 1. State-driven rotation ---
        LDA rot_dir
        BEQ @no_rot
        BMI @rot_l
        ; right: angle += ROT_STEP, mod 360
        CLC
        LDA ship_angle_lo
        ADC #ROT_STEP
        STA arg_lo
        LDA ship_angle_hi
        ADC #0
        STA arg_hi
        JSR mod360_arg
        LDA arg_lo
        STA ship_angle_lo
        LDA arg_hi
        STA ship_angle_hi
        JMP @no_rot
@rot_l: ; left: angle += (360 - ROT_STEP), mod 360
        CLC
        LDA ship_angle_lo
        ADC #<(360 - ROT_STEP)
        STA arg_lo
        LDA ship_angle_hi
        ADC #>(360 - ROT_STEP)
        STA arg_hi
        JSR mod360_arg
        LDA arg_lo
        STA ship_angle_lo
        LDA arg_hi
        STA ship_angle_hi
@no_rot:

        ; --- 2. State-driven thrust (clamped at signed 8-bit overflow) ---
        ; Before clamping the bug looked like the ship "bouncing": ship_vx
        ; accumulates +2/tick while Z is held, hits +127, the next ADC
        ; wraps it to -128 → ship suddenly flies backward. BVS after ADC
        ; tests the 6502 V flag (signed overflow) and skips the STA when
        ; it would push velocity past the byte's signed range.
        LDA thrust_active
        BEQ apply_motion
        LDA ship_dir_x
        JSR asr_5
        CLC
        ADC ship_vx
        BVS @no_x_thrust
        STA ship_vx
@no_x_thrust:
        LDA ship_dir_y
        JSR asr_5
        CLC
        ADC ship_vy
        BVS @no_y_thrust
        STA ship_vy
@no_y_thrust:
        ; fall through

; ----------------------------------------------------------------------------
; apply_motion: pos += sign_extend(v) every frame. Then wrap Y at 192
;   (X wraps for free at 256 via 8-bit overflow).
; ----------------------------------------------------------------------------
apply_motion:
        ; --- X axis (signed 16-bit add) ---
        LDA ship_vx
        BPL @posx
        CLC
        LDA ship_x_frac
        ADC ship_vx
        STA ship_x_frac
        LDA ship_x_int
        ADC #$FF                 ; sign-extend: vx negative -> add -1 to high
        STA ship_x_int
        JMP @do_y
@posx:  CLC
        LDA ship_x_frac
        ADC ship_vx
        STA ship_x_frac
        LDA ship_x_int
        ADC #0
        STA ship_x_int
@do_y:  ; --- Y axis ---
        LDA ship_vy
        BPL @posy
        CLC
        LDA ship_y_frac
        ADC ship_vy
        STA ship_y_frac
        LDA ship_y_int
        ADC #$FF
        STA ship_y_int
        JMP wrap_y
@posy:  CLC
        LDA ship_y_frac
        ADC ship_vy
        STA ship_y_frac
        LDA ship_y_int
        ADC #0
        STA ship_y_int

; ----------------------------------------------------------------------------
; wrap_y: keep ship_y_int in [0..191]. After signed add, ship_y_int
;   could land in [192..223] (drifted past bottom) or [224..255] (drifted
;   past top, i.e. underflowed below 0). Both ranges fold back into
;   [0..191] by ±192.
; ----------------------------------------------------------------------------
wrap_y:
        LDA ship_y_int
        CMP #192
        BCC render               ; in range
        CMP #224
        BCS @neg_wrap            ; underflowed below 0 -> +192
        SEC
        SBC #192                 ; drifted off bottom -> -192
        STA ship_y_int
        JMP render
@neg_wrap:
        CLC
        ADC #192
        STA ship_y_int
        ; fall through

; ----------------------------------------------------------------------------
; render: feed the sprite_triangle rasterizer for the ship (slot 0),
;   then update + render the bullet (slot 1).
; ----------------------------------------------------------------------------
render:
        ; Frame pacing — sync to TMS9918 VBlank before the per-frame
        ; sprite + bullet rebuild. Drains the stale F flag and waits
        ; for the next vertical retrace (~4554c VRAM-write window).
        WAIT_VBLANK
        LDA ship_x_int
        STA tri_x
        LDA ship_y_int
        STA tri_y
        LDA ship_angle_lo
        STA tri_angle_lo
        LDA ship_angle_hi
        STA tri_angle_hi
        ; tri_slot was set to 0 at boot and is not touched by anything
        ; that doesn't restore it. Re-pin it here so update_bullet's
        ; sprite_attr_write (slot 1) can't leak into the ship render.
        LDA #0
        STA tri_slot
        LDA #$0F
        STA tri_color
        JSR sprite_triangle_render
        JSR update_bullet
        JSR delay_and_input      ; throttle + drain keys into state machine
        JMP game_loop

; ----------------------------------------------------------------------------
; update_bullet: integrate the bullet's pos + vel, decrement TTL, and
;   write the slot 1 sprite attribute (visible position OR Y=$D0 hide).
;   When the bullet expires (TTL reaches 0), bullet_active is cleared so
;   the next S press can re-spawn.
; ----------------------------------------------------------------------------
update_bullet:
        ; Show-first / advance-after. The spawn frame writes the
        ; attribute at the ship's tip BEFORE the bullet moves a single
        ; pixel -- otherwise the bullet's first visible position would
        ; be tip + ship_dir/8 (≈ 8 px ahead of the ship), which the
        ; player perceives as fire-with-latency. Subsequent frames also
        ; show the current position then advance for the next tick, so
        ; the bullet's last on-screen frame still renders cleanly before
        ; off-screen / TTL despawn hides it on the following tick.
        LDA #BULLET_PATSLOT
        STA tri_slot
        LDA #$0F
        STA tri_color
        LDX #0
@loop:  LDA bullet_active,X
        BEQ @hide_x
        ; --- 1. Show at current pos ---
        LDA bullet_x,X
        STA tri_x
        LDA bullet_y,X
        STA tri_y
        TXA
        CLC
        ADC #1                   ; A = attr slot = bullet_idx + 1
        STX tmp
        JSR sprite_attr_write
        LDX tmp
        ; --- 2. Advance X with wrap-detection (despawn off-screen) ---
        LDA bullet_vx,X
        BMI @nx
        CLC
        LDA bullet_x,X
        ADC bullet_vx,X
        STA bullet_x,X
        BCS @expire              ; vx>=0, carry = wrapped past 255
        JMP @ydo
@nx:    CLC
        LDA bullet_x,X
        ADC bullet_vx,X
        STA bullet_x,X
        BCC @expire              ; vx<0, !carry = wrapped past 0
@ydo:   ; --- 3. Advance Y; >= 192 = off-screen ---
        CLC
        LDA bullet_y,X
        ADC bullet_vy,X
        STA bullet_y,X
        CMP #192
        BCS @expire
        ; --- 4. TTL countdown ---
        DEC bullet_ttl,X
        BNE @next
@expire:
        ; Mark inactive; next frame's @hide_x writes Y=$C8 to clear the
        ; sprite. We don't write the hide here so the LAST visible
        ; frame still renders the bullet at its on-screen position.
        LDA #0
        STA bullet_active,X
        JMP @next
@hide_x:
        TXA
        CLC
        ADC #1
        ASL
        ASL                      ; A = (X+1) * 4 = attr addr lo
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$3B | $40
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #SPR_HIDE_Y
        STA VDP_DATA
@next:
        INX
        CPX #BULLET_COUNT
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @loop
        RTS

; ----------------------------------------------------------------------------
; init_sprite_chain: hide all 4 bullet slots (Y=SPR_HIDE_Y) and write
;   Y=$D0 at slot SPRITE_TERMSLOT to terminate sprite scanning. Slot 0
;   is the ship and gets its own attribute from sprite_triangle_render.
; ----------------------------------------------------------------------------
init_sprite_chain:
        LDX #1                   ; first bullet slot
@hl:    TXA
        ASL
        ASL                      ; A = slot * 4 = attr addr lo
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$3B | $40
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #SPR_HIDE_Y
        STA VDP_DATA
        INX
        CPX #SPRITE_TERMSLOT
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @hl
        ; --- terminator at slot SPRITE_TERMSLOT ---
        LDA #SPRITE_TERMSLOT * 4
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$3B | $40
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #SPR_TERM_Y
        STA VDP_DATA
        RTS

; ----------------------------------------------------------------------------
; init_bullet_pattern: copy the static 4x4 bullet sprite into the
;   sprite_triangle staging buffer, then upload to pattern slot 1
;   ($1820). All BULLET_COUNT in-flight bullets share this pattern via
;   sprite name = 4 (= PATSLOT * 4 in 16x16 sprite-mode addressing).
;   sprite_triangle_render reuses sprite_buf each frame for the ship,
;   but the bullet's VRAM pattern stays untouched after boot.
; ----------------------------------------------------------------------------
init_bullet_pattern:
        LDX #31
@cp:    LDA bullet_sprite_data,X
        STA sprite_buf,X
        DEX
        BPL @cp
        LDA #BULLET_PATSLOT
        JMP sprite_buf_upload    ; tail call

; ----------------------------------------------------------------------------
; Bullet sprite: 4x4 white square at the centre of the 16x16 grid.
; Layout = TMS9918 native quadrants: TL [0..7], BL [8..15], TR [16..23],
; BR [24..31]. Each row byte is one row of 8 pixels (MSB = leftmost).
; ----------------------------------------------------------------------------
bullet_sprite_data:
        ; TL: rows 0..7. Pixels at (6,6) (7,6) (6,7) (7,7) -> bits 1,0
        .byte $00, $00, $00, $00, $00, $00, $03, $03
        ; BL: rows 8..15 (rows 0..7 of BL). Pixels at (6,8) (7,8) (6,9) (7,9)
        .byte $03, $03, $00, $00, $00, $00, $00, $00
        ; TR: rows 0..7. Pixels at (8,6) (9,6) (8,7) (9,7) -> bits 7,6
        .byte $00, $00, $00, $00, $00, $00, $C0, $C0
        ; BR: rows 8..15. Pixels at (8,8) (9,8) (8,9) (9,9)
        .byte $C0, $C0, $00, $00, $00, $00, $00, $00

; ----------------------------------------------------------------------------
quit:
        JSR disable_sprites
        JMP WOZMON

; ============================================================================
; Helpers: arithmetic shift right (sign-preserving). Used to scale signed
;   velocities by 1/2^N. Y preserved, X clobbered, A in/out.
; ============================================================================
asr_5:                            ; A = signed >> 5  (divide by 32)
        LDX #5
        BNE asr_n
asr_3:                            ; A = signed >> 3  (divide by 8)
        LDX #3
asr_n:
        CMP #$80                  ; CF = sign bit
        ROR
        DEX
        BNE asr_n
        RTS

; ============================================================================
; delay_and_input: Galaga-style throttle + key drain. The outer loop runs
;   SPEED times; the inner loop polls the keyboard 256 times per outer
;   iter, dispatching every pending press to handle_key. Tick period
;   ≈ SPEED × 256 × ~10c ≈ 10 ms at SPEED = 4, giving ~70 ticks/s.
;   Polling at ~1500 Hz inside the throttle catches every key press
;   without busy-waiting elsewhere. KBD reads also stir the LFSR (free
;   entropy from human-keystroke timing).
; ============================================================================
delay_and_input:
        LDX #SPEED
@outer:
        LDY #$00
@inner:
        LDA KBDCR
        BPL @nokey
        LDA KBD
        AND #$7F
        STA tmp
        EOR lfsr_lo              ; mix keystroke timing into PRNG
        STA lfsr_lo
        LDA tmp
        JSR handle_key
@nokey:
        DEY
        BNE @inner
        DEX
        BNE @outer
        RTS

; ============================================================================
; handle_key: dispatch a single key press into the input state machine.
;   Q       latch rot_dir = $FF (left). Doesn't touch thrust.
;   D       latch rot_dir = $01 (right). Doesn't touch thrust.
;   Z       commit forward: stop rotation, engage thrust_active.
;   S       commit fire + retro: stop rotation, stop thrust, kick the
;           ship slightly backward (-ship_dir/32 to vel) and spawn a
;           bullet. ALWAYS spawns -- the multi-bullet pool absorbs
;           rapid fire without "no bullet" silences.
;   ESC     quit -> Wozmon.
; Z and S both carry the "directional commit" semantic: pressing them
; means "I want this now, stop turning". Q/D are pure rotation commands.
; The `quit` path drops delay_and_input's return slot off the 6502 stack
; so the JMP doesn't leave a stranded JSR frame.
; ============================================================================
handle_key:
        CMP #'Q'
        BEQ @left
        CMP #'D'
        BEQ @right
        CMP #'Z'
        BEQ @thrust
        CMP #'S'
        BEQ @stop_fire
        CMP #$1B
        BEQ @quit
        RTS
@left:
        LDA #$FF
        STA rot_dir
        RTS
@right:
        LDA #$01
        STA rot_dir
        RTS
@thrust:
        ; Z = commit forward: stop rotating, engage thrust.
        LDA #0
        STA rot_dir
        LDA #1
        STA thrust_active
        RTS
@stop_fire:
        ; S = stop rotation + stop thrust + retro impulse + fire.
        LDA #0
        STA rot_dir
        STA thrust_active
        ; --- retro: ship_v -= ship_dir / 32 (clamp at signed overflow) ---
        LDA ship_dir_x
        JSR asr_5
        STA tmp
        LDA ship_vx
        SEC
        SBC tmp
        BVS @no_x_retro
        STA ship_vx
@no_x_retro:
        LDA ship_dir_y
        JSR asr_5
        STA tmp
        LDA ship_vy
        SEC
        SBC tmp
        BVS @no_y_retro
        STA ship_vy
@no_y_retro:
        ; --- fire bullet (always; multi-pool absorbs the call) ---
        JMP spawn_bullet         ; tail call: RTS in spawn_bullet returns to caller
@quit:
        PLA
        PLA                      ; drop delay_and_input's return slot
        JMP quit

; ============================================================================
; spawn_bullet: insert a fresh bullet into the pool and IMMEDIATELY write
;   its sprite attribute to VRAM. Walks the bullet array for the first
;   inactive slot; if all are in flight, clobber slot 0 (cheapest
;   "always responsive to S" policy -- the player never gets a silent
;   press, no queueing).
;
;   The immediate VRAM write is what gives a < 1 ms visible-bullet
;   latency. Without it the bullet wouldn't appear until the next
;   render() call, ~10 ms after S press while delay_and_input is still
;   polling its inner loop. The player perceived that gap as the ship
;   "freezing" between S press and bullet appearance. Now the TMS9918
;   sees the new attribute on the very next scanline.
; ============================================================================
spawn_bullet:
        LDX #0
@scan:  LDA bullet_active,X
        BEQ @found
        INX
        CPX #BULLET_COUNT
        BNE @scan
        LDX #0                   ; pool full -> overwrite slot 0
@found:
        LDA #1
        STA bullet_active,X
        LDA ship_tip_x
        STA bullet_x,X
        LDA ship_tip_y
        STA bullet_y,X
        LDA ship_dir_x
        JSR asr_3
        STA bullet_vx,X
        LDA ship_dir_y
        JSR asr_3
        STA bullet_vy,X
        LDA #BULLET_TTL
        STA bullet_ttl,X
        ; --- Render attribute now, mid-tick. tri_x / tri_y / tri_slot /
        ;     tri_color get clobbered here, but the next render() at the
        ;     start of the following game_loop iteration re-pins them
        ;     for the ship triangle, so no leakage.
        LDA #BULLET_PATSLOT
        STA tri_slot
        LDA #$0F
        STA tri_color
        LDA bullet_x,X
        STA tri_x
        LDA bullet_y,X
        STA tri_y
        TXA
        CLC
        ADC #1                   ; A = attr slot = bullet idx + 1
        JMP sprite_attr_write    ; tail call

; ============================================================================
; Now pull in lib/apple1/kbd.asm. Placed at the BOTTOM so main: above
; lands at $0280 (the .include emits wait_key + poll_key into the CODE
; segment in source order; including it at the top would push main: down
; past wait_key and a Wozmon `0280R` would run wait_key with a stale
; 6502 stack instead of starting the game).
; ============================================================================
.include "kbd.asm"
