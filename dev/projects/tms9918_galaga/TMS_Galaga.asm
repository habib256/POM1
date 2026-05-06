; =============================================
; TMS GALAGA - P-LAB TMS9918 Graphic Card
; VERHILLE Arnaud - 2026
; Hardware-sprite Galaga: a player ship at the bottom + 3 increasingly
; tough aliens hovering in formation that periodically dive at the
; player. HP per type: 2 / 4 / 6. Three player lives.
; =============================================
; Assemble with cc65:
;   ca65 -o build/TMS_Galaga.o software/tms9918/TMS_Galaga.asm
;   ld65 -C software/tms9918/apple1_galaga.cfg \
;        -o build/TMS_Galaga.bin build/TMS_Galaga.o
;
; Or just: python3 software/tms9918/emit_TMS_Galaga_txt.py
;
; Load via File > Load Memory (TMS_Galaga.txt), then 280R. The TMS9918
; card must be enabled (Hardware menu).
;
; Display: TMS9918 Graphics I, 16x16 sprites, no magnify (R1 = $C2).
;   Sprite Attribute Table at $1B00 (R5=$36).
;   Sprite Pattern  Table at $3800 (R6=$07).
;   Backdrop = black, name table cleared (no tile-based playfield).
;
; Sprite slots used:
;   0       player ship          (16x16, yellow)
;   1..3    aliens 0/1/2         (16x16, distinct colours)
;   4..5    player bullets x2    (16x16, white, mostly transparent)
;   6..8    enemy bullets x3     (16x16, red,   mostly transparent)
;   9       chain terminator     (Y = $D0)
;
; Hidden sprites get Y = $C0 (off-screen, chain continues).
; =============================================

; --- Apple 1 I/O ---
        .import tms9918_pad40  ; silicon-strict pad40 (helper from tms9918_pad.asm)
ECHO     = $FFEF
KBD      = $D010
KBDCR    = $D011

; --- TMS9918 I/O ---
VDP_DATA = $CC00
VDP_CTRL = $CC01

; --- Geometry / tuning ---
PLAYER_Y       = 168            ; sprite top edge
PLAYER_MIN_X   = 4
PLAYER_MAX_X   = 236            ; 256 - 16 - 4
PLAYER_STEP    = 2              ; px per move tick

FORM_Y         = 24             ; baseline of formation
FORM_AMP       = 24             ; +/- swing amplitude
FORM_TICKS_PER_PX = 4           ; ticks per 1 px of formation move

DIVE_DY        = 3              ; dive descent speed (px/tick)
DIVE_SIMPLE    = 0              ; pattern: descend straight toward player
DIVE_ZIGZAG    = 1              ; pattern: oscillating X during descent
DIVE_DIAG      = 2              ; pattern: cross-screen sweep (uses wrap)
DIVE_LOOP      = 3              ; pattern: descend / sweep / ascend U-turn
ENEMY_FLASH_TICKS = 75          ; ~1.5 s of flashing on a non-killing hit
EXP_TICKS      = 14             ; ~280 ms hit-explosion sprite flash
FORM_FIRE_COOLDOWN = 80         ; ~1.6 s between formation potshots (gentler)
DIVE_FIRE_AT_Y = 80             ; enemy bullet fired when diver crosses this Y
DIVE_COOLDOWN  = 240            ; ~4.8 s between dives (formations potshots fill the gap)
DIVE_MIN_Y     = 200            ; dive ends when Y exceeds this

PB_SPEED       = 6
EB_SPEED       = 3

FIRE_COOLDOWN  = 10
COLLIDE_DIST   = 12             ; centre-to-centre overlap threshold (px)

INITIAL_LIVES  = 5
WIN_WAVE       = 22             ; clear this wave -> game victory (twin super bosses)
SUPER_WAVE_1   = 11             ; single super boss
SUPER_WAVE_2   = 22             ; twin super bosses (final)
ENEMY1_HP      = 2
ENEMY2_HP      = 4
ENEMY3_HP      = 6
SCORE_E1_KILL  = 10
SCORE_E2_KILL  = 25
SCORE_E3_KILL  = 50
SCORE_HIT      = 1

; --- Sprite pattern names (multiples of 4 - 16x16 sprites use 4 patterns each) ---
P_PLAYER  = 0
P_ENEMY1  = 4
P_ENEMY2  = 8
P_ENEMY3  = 12
P_PBULLET = 16
P_EBULLET = 20
P_EXP     = 24
P_BONUS_DOUBLE = 28             ; bonus / malus drop sprites (16x16)
P_BONUS_TRIPLE = 32
P_BONUS_SHIELD = 36
P_MALUS_SKULL  = 40
P_ENEMY1_ALT   = 44             ; 2nd-frame animation patterns
P_ENEMY2_ALT   = 48
P_ENEMY3_ALT   = 52
P_POPUP        = 56             ; score popup arrow (single pattern, colour per kill)
P_SHIELD_RING  = 60             ; ring drawn around the player while shielded
P_EXP_ALT      = 64             ; second frame for the explosion animation
P_PLAYER_TH    = 68             ; player ship with engine thrust visible
P_SUPER_TL     = 72             ; super boss (4x 16x16 quadrants stitched)
P_SUPER_TR     = 76
P_SUPER_BL     = 80
P_SUPER_BR     = 84

SUPER_HP       = 30             ; super boss takes a beating
SUPER_FIRE_CD  = 6              ; ticks between rotating shots
SUPER_X_INIT   = 96             ; top-left of the 32x32 super sprite
SUPER_Y_INIT   = 16
COL_SUPER      = 9              ; light red, menacing
SUPER_HITBOX   = 18             ; centre-to-centre tolerance for PB hits
SUPER_DX_INIT  = 4              ; super boss horizontal speed (px/tick)
SUPER_X_MAX    = 224            ; rightmost super_x (256 - 32 sprite width)
SUPER2_Y_INIT  = 64             ; second super boss Y (32-tall sprite, sits below boss 1)
SUPER2_X_INIT  = 128            ; second boss starts mirrored across the centre

BOMB_PHASES    = 6              ; how many cascade explosions to spawn

; --- Sprite colours -------------------------------------------------
; Palette tuned so each on-screen role has a unique hue:
;   Player          : light blue  (heroic, distinct from any alien)
;   E1 scout        : light yellow (warning)
;   E2 fighter      : medium red (clear threat)
;   E3 / boss       : magenta     (boss-iconic, single role)
;   Player bullet   : white
;   Enemy bullet    : light red   (eye-catching incoming)
;   Explosion       : dark yellow (kill-burst, distinct from player)
;   Double bonus    : cyan        (cool tone, distinct from player blue)
;   Triple bonus    : light green
;   Shield drop     : white       (universal pickup glyph)
;   Skull malus     : dark red    (danger, distinct from EB)
;   Shield ring     : gray        (subtle outline around the ship)
;   Smart bomb      : dark green  (rare, distinct)
; --------------------------------------------------------------------
COL_PLAYER = 5                  ; light blue
COL_E1     = 11                 ; light yellow
COL_E2     = 8                  ; medium red
COL_E3     = 13                 ; magenta
COL_PB     = 15                 ; white
COL_EB     = 9                  ; light red
COL_EXP    = 10                 ; dark yellow
COL_BD     = 7                  ; cyan
COL_BT     = 3                  ; light green
COL_BS     = 15                 ; white
COL_MS     = 6                  ; dark red
COL_SHIELD = 14                 ; gray

DROP_SPEED      = 2             ; drop fall speed (px/tick)
SHIELD_TICKS    = 250           ; ~5 s of shield-ring + hit absorption
WEAPON_TICKS    = 250           ; ~5 s of double / triple shot

; --- Off-screen Y for hidden sprite + chain terminator ---
HIDDEN_Y  = $C0
TERM_Y    = $D0

; =============================================
; Zero page
; =============================================
.zeropage
temp:           .res 1
temp2:          .res 1
temp3:          .res 1
src_lo:         .res 1
src_hi:         .res 1
sptr_lo:        .res 1
sptr_hi:        .res 1
str_lo:         .res 1
str_hi:         .res 1

player_x:       .res 1
player_lives:   .res 1
fire_cd:        .res 1
flash_cd:       .res 1          ; player respawn flash counter

score_lo:       .res 1
score_hi:       .res 1

; --- Per-enemy state (3 entries each) ---
enemy_state:    .res 3          ; 0=formation, 1=diving, 2=dead
enemy_hp:       .res 3
enemy_x:        .res 3          ; active X (used during dive)
enemy_y:        .res 3          ; active Y (used during dive)
enemy_flash:    .res 3          ; non-zero = strobe in render_sprites

; --- Formation movement ---
form_off:       .res 1          ; signed offset, -FORM_AMP..+FORM_AMP
form_dir:       .res 1          ; 1 or $FF
form_tick:      .res 1

; --- Dive scheduler ---
dive_idx:       .res 1          ; $FF = none diving
dive_cd:        .res 1
dive_target_x:  .res 1
dive_did_fire:  .res 1          ; 0 = hasn't fired yet during this dive
dive_phase:     .res 1          ; 0 = descending, 1 = ascending (E2/E3)
dive_pattern:   .res 1          ; 0=SIMPLE 1=ZIGZAG 2=DIAGONAL 3=LOOP
dive_ticks:     .res 1          ; tick counter inside the current dive

; --- Formation potshot scheduler ---
form_fire_cd:   .res 1

; --- Player bullets (3, supports triple shot) ---
pb_active:      .res 3
pb_x:           .res 3
pb_y:           .res 3

; --- Enemy bullets (3) ---
eb_active:      .res 3
eb_x:           .res 3
eb_y:           .res 3

; --- Hit explosion (single shared sprite) ---
exp_active:     .res 1
exp_x:          .res 1
exp_y:          .res 1
exp_t:          .res 1

; --- Animation tick (Pack 1.a) ---
anim_tick:      .res 1

; --- Score popup (Pack 1.b) ---
popup_active:   .res 1
popup_x:        .res 1
popup_y:        .res 1
popup_color:    .res 1
popup_ttl:      .res 1

; --- Pack 2.a: per-wave difficulty values ---
cur_dive_cd_max: .res 1
cur_eb_speed:    .res 1
cur_form_ticks:  .res 1

; --- Pack 2.b: combo streak ---
streak:          .res 1

; --- Power-up timers (persist across waves, decremented every tick) ---
; weapon_ttl: 0 -> single shot. >0 -> weapon_mode stays valid until 0.
; shield_t  : 0 -> no shield. >0 -> hits absorbed and ring rendered.
weapon_ttl:      .res 1
shield_t:        .res 1

; --- Pack 3: boss wave state ---
is_boss:         .res 1          ; 0 / 1
boss_x_dir:      .res 1          ; +1 / -1
boss_x_tick:     .res 1
boss_fire_cd:    .res 1

; --- Starfield (8 stars, 4 colours via chars 96/104/112/120) ---
star_x:          .res 8
star_y:          .res 8
scroll_tick:     .res 1

; --- Smart-bomb cascade explosion ---
bomb_active:     .res 1          ; 1 = bomb sequence in flight (gates wave-clear)
bomb_phase:      .res 1          ; 0..(BOMB_PHASES-1)
bomb_t:          .res 1          ; ticks until the next phase fires

; --- Super boss(es): 2-element arrays (wave 11 = idx 0; wave 22 = idx 0+1) ---
is_super_boss:   .res 2          ; index 0 / 1 = boss 1 / boss 2 active flag
super_x:         .res 2
super_y:         .res 2
super_hp:        .res 2
super_fire_cd:   .res 2
super_angle:     .res 2          ; 0..15
super_dx:        .res 2          ; signed horizontal velocity

; --- Per-bullet velocity for enemy bullets (signed) ---
; Default for normal eb is (dx=0, dy=cur_eb_speed). Super boss spawns
; bullets with arbitrary (dx, dy) sourced from angle_dx / angle_dy.
eb_dx:           .res 3
eb_dy:           .res 3

; --- Power-up state ---
weapon_mode:    .res 1          ; 0 single, 1 double, 2 triple

; --- Falling drop (single slot) ---
drop_active:    .res 1
drop_type:      .res 1          ; 0=double, 1=triple, 2=shield, 3=skull
drop_x:         .res 1
drop_y:         .res 1

; --- Game state ---
game_over:      .res 1
won:            .res 1
wave:           .res 1

prng_lo:        .res 1
prng_hi:        .res 1

key_left_code:  .res 1
key_right_code: .res 1
key_stop_code:  .res 1
key_fire_code:  .res 1

; --- Player movement (direct direction set on press) ---
; Apple-1 keyboards have no key-release event, so each direction key
; flips player_dir to that direction; the ship moves continuously
; until the player presses the opposite direction (reverse) or S
; (full stop + aim shot).
;   $FF = moving left, $01 = moving right, $00 = stopped
player_dir:     .res 1

; --- Pending fire (one-shot per SPACE press) ---
pend_fire:      .res 1

; --- Score digit scratch (5 digits MSB-first) ---
; MUST live in RAM. Previously declared with `.res 5` inside the .code
; segment, which silently breaks the CodeTank ROM build: the score adds
; correctly to score_lo/score_hi but score_to_digits' STAs write into
; ROM and the HUD always shows zero. ZP costs 5 bytes; saved 2 below by
; aliasing print_ptr_lo/hi onto the unused str_lo/str_hi pair.
score_digits:    .res 5

; print.asm uses print_ptr_lo/hi (2 ZP bytes). Alias them onto the
; unused str_lo/str_hi pair declared above so the include's `.ifndef`
; guard skips the fresh reservation. Net: 0 extra bytes for the printer.
print_ptr_lo = str_lo
print_ptr_hi = str_hi

; =============================================
; CODE
; =============================================
.code

; =============================================
; main
; =============================================
main:
        JSR init_vdp
        JSR draw_title_tms
        JSR draw_title_sprites

        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax
        LDA #<str_layout
        LDX #>str_layout
        JSR print_str_ax

@kb_wait:
        JSR title_wait_key
        PHA
        EOR prng_lo
        STA prng_lo
        PLA
        CMP #'1'
        BEQ @qwerty
        CMP #'2'
        BEQ @azerty
        JMP @kb_wait
@qwerty:
        LDA #'A'
        STA key_left_code
        LDA #'D'
        STA key_right_code
        JMP @begin
@azerty:
        LDA #'Q'
        STA key_left_code
        LDA #'D'
        STA key_right_code
@begin:
        LDA #'S'                ; S = stop (both layouts)
        STA key_stop_code
        LDA #' '                ; space = fire (same on both layouts)
        STA key_fire_code
        LDA #$5B                ; non-zero seed init (key timing also mixes in)
        STA prng_lo
        LDA #$A3
        STA prng_hi

        ; --- Title page 2: bonus drops help ---
        JSR draw_help_tms
        JSR draw_help_sprites
        JSR wait_key            ; any key dismisses the help
        EOR prng_lo             ; mix key timing into PRNG
        STA prng_lo

new_game:
        ; Clear the name table so the title / game-over splash text
        ; doesn't bleed through behind the sprites during play.
        JSR clear_name_table
        LDA #$00
        STA score_lo
        STA score_hi
        STA game_over
        STA won
        ; Power-up state is fresh at game start (persists across waves
        ; thereafter, but a brand-new game starts with no upgrades).
        STA weapon_mode
        STA weapon_ttl
        STA shield_t
        STA drop_active
        STA bomb_active
        STA is_super_boss+0
        STA is_super_boss+1
        LDA #INITIAL_LIVES
        STA player_lives
        LDA #$00
        STA wave

new_wave:
        ; Wipe the name table BEFORE we paint any gameplay sprite or
        ; HUD glyph. Otherwise the "GET READY / WAVE NN" splash sits
        ; behind the action for the first frames of play_loop.
        JSR clear_name_table
        JSR init_starfield                ; new random scrolling stars
        LDA #$00
        STA scroll_tick
        INC wave
        JSR reset_arena
        JSR render_sprites
        JSR draw_hud

play_loop:
        JSR delay_and_input
        INC anim_tick                  ; sprite-frame strobe (Pack 1.a)
        JSR tick_starfield             ; scrolling stars (~330 ms/step)
        JSR handle_player_input
        JSR update_pbullets
        JSR update_formation
        JSR tick_enemy_flash
        JSR tick_explosion
        JSR tick_bomb                  ; smart-bomb cascade
        JSR tick_popup                 ; (Pack 1.b)
        JSR tick_powerups
        JSR maybe_formation_fire
        JSR maybe_start_dive
        JSR update_dive
        JSR update_boss
        JSR update_super_boss
        JSR update_super_boss2
        JSR update_ebullets
        JSR update_drop
        JSR check_pb_vs_enemies
        JSR check_pb_vs_super
        JSR check_pb_vs_super2
        JSR check_eb_vs_player
        JSR check_dive_vs_player
        JSR render_sprites

        LDA game_over
        BNE @over
        JSR all_dead
        BEQ @keep
        ; All enemies dead -- but defer the wave-clear splash if a
        ; drop is still falling OR if a smart-bomb cascade is still
        ; popping enemies one by one.
        LDA drop_active
        BNE @keep
        LDA bomb_active
        BNE @keep
        JMP @cleared
@keep:
        JMP play_loop

@cleared:
        ; +100 bonus, refresh HUD.
        LDA #100
        JSR score_add
        JSR draw_hud
        ; Game-win check: did the player just clear the final wave?
        LDA wave
        CMP #WIN_WAVE
        BCC @normal_clear
        JMP @victory
@normal_clear:
        JSR hide_all_sprites
        JSR draw_wave_clear_tms
        LDA #<str_wave
        LDX #>str_wave
        JSR print_str_ax
        JSR pause_1500ms
        JMP new_wave

@victory:
        JSR hide_all_sprites
        JSR draw_victory_tms
        LDA #<str_win
        LDX #>str_win
        JSR print_str_ax
        JSR wait_key
        JMP main

@over:
        JSR hide_all_sprites
        JSR draw_gameover_tms
        LDA #<str_over
        LDX #>str_over
        JSR print_str_ax
        JSR wait_key
        ; Full restart: hand back to the title screen so the player can
        ; switch keyboard layout, re-read the help, etc.
        JMP main


; =============================================
; reset_arena: reset enemies, player, bullets, formation, dive state.
; =============================================
reset_arena:
        ; Player ship at centre bottom, stopped, no fire pending
        LDA #120
        STA player_x
        LDA #$00
        STA fire_cd
        STA flash_cd
        STA player_dir
        STA pend_fire

        ; --- Wave-driven enemy spawn ---
        ; Lookup the bitmask for this wave (clamped to last entry on
        ; waves >= 7 so the climax stays "all 3" forever).
        LDA wave
        SEC
        SBC #$01
        CMP #21
        BCC @idx_ok
        LDA #20
@idx_ok:
        TAX
        LDA wave_masks,X
        STA temp                ; wave mask in temp
        ; Pack 2.a: load per-wave difficulty values
        LDA wave_dive_cd,X
        STA cur_dive_cd_max
        LDA wave_eb_speed,X
        STA cur_eb_speed
        LDA wave_form_ticks,X
        STA cur_form_ticks

        ; --- Wave 11 / Wave 22: super-boss showdowns -----------------
        ; Wave 11 = single boss (idx 0); Wave 22 = twin (idx 0 + idx 1).
        ; Both is_super_boss[] entries default to 0.
        LDA #$00
        STA is_super_boss+0
        STA is_super_boss+1
        LDA wave
        CMP #SUPER_WAVE_1
        BEQ @do_super
        CMP #SUPER_WAVE_2
        BEQ @do_super
        JMP @no_super
@do_super:
        ; --- Boss 1 (always present on a super wave) ---
        LDA #$01
        STA is_super_boss+0
        LDA #SUPER_X_INIT
        STA super_x+0
        LDA #SUPER_Y_INIT
        STA super_y+0
        LDA #SUPER_HP
        STA super_hp+0
        LDA #SUPER_FIRE_CD
        STA super_fire_cd+0
        LDA #$00
        STA super_angle+0
        LDA #SUPER_DX_INIT
        STA super_dx+0
        ; --- Boss 2 (only on wave 22) ---
        LDA wave
        CMP #SUPER_WAVE_2
        BNE @sb1_only
        LDA #$01
        STA is_super_boss+1
        LDA #SUPER2_X_INIT
        STA super_x+1
        LDA #SUPER2_Y_INIT
        STA super_y+1
        LDA #SUPER_HP
        STA super_hp+1
        LDA #SUPER_FIRE_CD/2          ; out-of-phase with boss 1
        STA super_fire_cd+1
        LDA #$08                       ; angle-table mid-point: opposite spray
        STA super_angle+1
        LDA #$FC                       ; -SUPER_DX_INIT (mirrored direction)
        STA super_dx+1
@sb1_only:
        ; All 3 enemy slots dead -- the super boss(es) are the only hostiles
        LDA #$02
        STA enemy_state+0
        STA enemy_state+1
        STA enemy_state+2
        LDA #$00
        STA enemy_hp+0
        STA enemy_hp+1
        STA enemy_hp+2
        STA enemy_flash+0
        STA enemy_flash+1
        STA enemy_flash+2
        STA is_boss
        JMP @en_after_all
@no_super:

        ; --- Pack 3.a: boss wave detection (every 5 waves) ---
        ; Default: not a boss wave. Then test `wave mod 5 == 0`.
        LDA #$00
        STA is_boss
        LDA wave
@mod_lp:
        CMP #$05
        BCC @mod_done
        SEC
        SBC #$05
        JMP @mod_lp
@mod_done:
        BNE @no_boss_setup
        ; wave % 5 == 0 -> boss wave overrides the formation mask.
        LDA #$01
        STA is_boss
        LDA #$01
        STA boss_x_dir
        LDA #$00
        STA boss_x_tick
        LDA #70                         ; ~1.4 s between spreads (was 45)
        STA boss_fire_cd
        ; E1 / E2 dead, E3 = boss centred.
        LDA #$02
        STA enemy_state+0
        STA enemy_state+1
        LDA #$00
        STA enemy_hp+0
        STA enemy_hp+1
        STA enemy_flash+0
        STA enemy_flash+1
        STA enemy_flash+2
        ; Boss = E3 slot, state=1 so render/collision use enemy_x/y.
        LDA #$01
        STA enemy_state+2
        LDA #112
        STA enemy_x+2
        LDA #FORM_Y
        STA enemy_y+2
        LDA #12                         ; was 16, easier to take down
        STA enemy_hp+2
        JMP @en_after_all
@no_boss_setup:

        LDX #$00
@en_lp:
        LDA enemy_mask_bit,X
        AND temp
        BEQ @en_dead
        ; Bit set: alive in formation, full HP, no flash
        LDA #$00
        STA enemy_state,X
        STA enemy_x,X
        STA enemy_y,X
        STA enemy_flash,X
        LDA enemy_hp_init,X
        STA enemy_hp,X
        JMP @en_after
@en_dead:
        ; Bit clear: enemy doesn't appear this wave
        LDA #$02
        STA enemy_state,X
        LDA #$00
        STA enemy_flash,X
        STA enemy_hp,X
@en_after:
        INX
        CPX #$03
        BCC @en_lp
@en_after_all:

        ; Formation neutral
        LDA #$00
        STA form_off
        STA form_tick
        LDA #$01
        STA form_dir

        ; Dive scheduler
        LDA #$FF
        STA dive_idx
        LDA cur_dive_cd_max
        STA dive_cd
        LDA #$00
        STA dive_phase
        ; Formation potshot scheduler
        LDA #FORM_FIRE_COOLDOWN
        STA form_fire_cd

        ; Bullets all inactive (3 PB slots, 3 EB slots)
        LDX #$02
        LDA #$00
@pb_lp:
        STA pb_active,X
        DEX
        BPL @pb_lp
        LDX #$02
@eb_lp:
        STA eb_active,X
        DEX
        BPL @eb_lp
        ; Per-wave reset (drop / weapon / shield persist via new_game)
        LDA #$00
        STA exp_active
        STA exp_t
        STA popup_active
        STA anim_tick
        STA streak
        RTS


; =============================================
; handle_player_input: apply continuous-direction movement and any
; pending fire request. The joystick-style direction is set by
; handle_key when keys arrive; this routine just walks the ship one
; player_dx px per tick (signed momentum). Decays toward 0 each tick.
; Wraps to opposite edge on overshoot (no auto-stop on wall
; -- ship just sticks to the edge until the player presses something
; else).
; =============================================
handle_player_input:
        LDA player_dir
        BEQ @no_move
        BMI @go_left
        ; --- Move right (wrap to left edge past pixel 240) ---
        LDA player_x
        CLC
        ADC #PLAYER_STEP
        CMP #241
        BCC @ok_r
        LDA #$00                ; wrapped past right edge
@ok_r:
        STA player_x
        JMP @no_move
@go_left:
        LDA player_x
        SEC
        SBC #PLAYER_STEP
        BCC @wrap_l             ; underflow -> wrap
        STA player_x
        JMP @no_move
@wrap_l:
        LDA #240
        STA player_x
@no_move:

        ; --- Fire (gated by cooldown; weapon_mode picks the spread) ---
        LDA pend_fire
        BEQ @no_f
        LDA #$00
        STA pend_fire
        LDA fire_cd
        BNE @no_f

        LDA weapon_mode
        CMP #$02
        BEQ @triple
        CMP #$01
        BEQ @double

        ; Single: one bullet at player_x
        LDA player_x
        JSR spawn_pb_at_x
        JMP @after_fire

@double:
        LDA player_x
        SEC
        SBC #$04
        JSR spawn_pb_at_x
        LDA player_x
        CLC
        ADC #$04
        JSR spawn_pb_at_x
        JMP @after_fire

@triple:
        LDA player_x
        SEC
        SBC #$06
        JSR spawn_pb_at_x
        LDA player_x
        JSR spawn_pb_at_x
        LDA player_x
        CLC
        ADC #$06
        JSR spawn_pb_at_x

@after_fire:
        LDA #FIRE_COOLDOWN
        STA fire_cd
@no_f:
        ; Tick cooldowns / invuln flash.
        LDA fire_cd
        BEQ @no_dec_fc
        DEC fire_cd
@no_dec_fc:
        LDA flash_cd
        BEQ @no_dec_fl
        DEC flash_cd
@no_dec_fl:
        RTS


; =============================================
; spawn_pb_at_x: A = X position. Find a free PB slot and place a
; bullet at (A, PLAYER_Y - 8). No-op if all 3 PB slots are full.
; Trashes A, X, temp.
; =============================================
spawn_pb_at_x:
        STA temp
        LDX #$00
@find:
        LDA pb_active,X
        BEQ @go
        INX
        CPX #$03
        BCC @find
        RTS
@go:
        LDA #$01
        STA pb_active,X
        LDA temp
        STA pb_x,X
        LDA #PLAYER_Y
        SEC
        SBC #$08
        STA pb_y,X
        RTS


; =============================================
; update_pbullets: advance every active player bullet by PB_SPEED upward.
; Deactivate when off-screen-top.
; =============================================
update_pbullets:
        LDX #$00
@lp:
        LDA pb_active,X
        BEQ @next
        LDA pb_y,X
        SEC
        SBC #PB_SPEED
        BCC @off                ; underflow -> off screen
        STA pb_y,X
        CMP #8                  ; near top of screen
        BCS @next
@off:
        LDA #$00
        STA pb_active,X
        STA streak              ; missed shot -> combo broken
@next:
        INX
        CPX #$03
        BCC @lp
        RTS


; =============================================
; update_formation: every FORM_TICKS_PER_PX ticks, shift form_off by
; form_dir; reverse direction at +/- FORM_AMP.
; =============================================
update_formation:
        INC form_tick
        LDA form_tick
        CMP cur_form_ticks
        BCC @done
        LDA #$00
        STA form_tick

        LDA form_off
        CLC
        ADC form_dir
        STA form_off

        ; Reverse if at the limits.
        ; form_off is signed: bit 7 set = negative.
        LDA form_off
        BMI @neg
        CMP #FORM_AMP
        BCC @done
        ; Hit positive limit -> dir = -1
        LDA #$FF
        STA form_dir
        JMP @done
@neg:
        ; Two's-complement compare with -FORM_AMP: if form_off <= -FORM_AMP
        ; (i.e. form_off as signed is <= -FORM_AMP), flip dir to +1.
        CMP #(256 - FORM_AMP) & $FF
        BCS @done               ; not yet past the limit
        LDA #$01
        STA form_dir
@done:
        RTS


; =============================================
; tick_enemy_flash: count down each per-enemy hit-flash timer.
; Drives the visibility strobe in render_sprites.
; =============================================
tick_enemy_flash:
        LDX #$00
@lp:
        LDA enemy_flash,X
        BEQ @next
        SEC
        SBC #$01
        STA enemy_flash,X
@next:
        INX
        CPX #$03
        BCC @lp
        RTS


; =============================================
; maybe_formation_fire: count form_fire_cd down. When it hits 0, pick
; a random alive formation enemy (state==0) and have it spit a bullet
; straight down from its current formation slot. The cooldown is reset
; whether we found a victim or not -- otherwise an empty wave would
; spam attempts every tick.
; =============================================
maybe_formation_fire:
        LDA form_fire_cd
        BEQ @ready
        SEC
        SBC #$01
        STA form_fire_cd
        RTS
@ready:
        LDA #FORM_FIRE_COOLDOWN
        STA form_fire_cd
        ; Up to 4 random picks for a formation-state enemy.
        LDX #$04
@pick:
        JSR prng16
        LDA prng_lo
        AND #$03
        CMP #$03
        BCS @next
        TAY
        LDA enemy_state,Y
        BEQ @go                 ; state==0 (formation)
@next:
        DEX
        BNE @pick
        RTS                     ; nobody available
@go:
        ; Find a free enemy-bullet slot.
        LDX #$00
@find:
        LDA eb_active,X
        BEQ @spawn
        INX
        CPX #$03
        BCC @find
        RTS                     ; all eb slots in flight
@spawn:
        LDA #$01
        STA eb_active,X
        LDA form_base_x,Y
        CLC
        ADC form_off
        STA eb_x,X
        LDA #FORM_Y
        STA eb_y,X
        ; Velocity: straight down at the per-wave eb speed
        LDA #$00
        STA eb_dx,X
        LDA cur_eb_speed
        STA eb_dy,X
        RTS


; =============================================
; tick_bomb: cascade explosions across the 3 enemy slots after a
; smart-bomb pickup. Each phase reuses the single explosion sprite
; slot (overwrites previous), delayed by bomb_t ticks. When phase
; reaches 3, the wave-clear gate is released by clearing bomb_active.
; =============================================
tick_bomb:
        LDA bomb_active
        BEQ @done
        LDA bomb_t
        BEQ @advance
        SEC
        SBC #$01
        STA bomb_t
        RTS
@advance:
        ; Time to fire the next phase (or end the cascade).
        LDA bomb_phase
        CMP #BOMB_PHASES
        BCS @end_bomb
        TAX
        LDA bomb_seq_x,X
        STA exp_x
        LDA bomb_seq_y,X
        STA exp_y
        LDA #EXP_TICKS
        STA exp_t
        LDA #$01
        STA exp_active
        INC bomb_phase
        LDA #10                 ; gap to the next blast (~200 ms)
        STA bomb_t
        RTS
@end_bomb:
        LDA #$00
        STA bomb_active
@done:
        RTS


; =============================================
; tick_explosion: count exp_t down to 0; clear exp_active when expired.
; =============================================
tick_explosion:
        LDA exp_active
        BEQ @done
        LDA exp_t
        BEQ @off
        SEC
        SBC #$01
        STA exp_t
        BNE @done
@off:
        LDA #$00
        STA exp_active
@done:
        RTS


; =============================================
; tick_powerups: tick the weapon TTL and shield duration each frame.
; When weapon_ttl decays to 0, weapon_mode reverts to single shot.
; =============================================
tick_powerups:
        LDA shield_t
        BEQ @no_shield
        SEC
        SBC #$01
        STA shield_t
@no_shield:
        LDA weapon_ttl
        BEQ @no_weapon
        SEC
        SBC #$01
        STA weapon_ttl
        BNE @no_weapon
        LDA #$00
        STA weapon_mode         ; TTL hit zero -> back to single shot
        JSR draw_hud            ; refresh weapon icon
@no_weapon:
        RTS


; =============================================
; tick_popup: count ttl down, drift Y up by 1 px each tick. Deactivate
; when ttl reaches 0.
; =============================================
tick_popup:
        LDA popup_active
        BEQ @done
        LDA popup_y
        BEQ @end                        ; reached top of screen
        SEC
        SBC #$01
        STA popup_y
        DEC popup_ttl
        BNE @done
@end:
        LDA #$00
        STA popup_active
@done:
        RTS


; =============================================
; spawn_popup: arm the score-popup sprite at (A=x, X=y) with colour
; in temp. Replaces any in-flight popup (single slot).
; =============================================
spawn_popup:
        STA popup_x
        STX popup_y
        LDA temp
        STA popup_color
        LDA #30
        STA popup_ttl
        LDA #$01
        STA popup_active
        RTS


; =============================================
; spawn_drop: arm the falling drop sprite at the current enemy's
; position. Y register = enemy index, called WHILE enemy_state[Y] is
; still 0 (formation) or 1 (diving) so we can pick the correct
; rendering position. Drop type is uniform random 0..3:
;   0=double  1=triple  2=shield  3=skull
; If a drop is already active, we replace it (single drop slot).
; Preserves Y. Trashes A, X.
; =============================================
spawn_drop:
        LDA enemy_state,Y
        BEQ @form
        LDA enemy_x,Y
        STA drop_x
        LDA enemy_y,Y
        STA drop_y
        JMP @set
@form:
        LDA form_base_x,Y
        CLC
        ADC form_off
        STA drop_x
        LDA #FORM_Y
        STA drop_y
@set:
        ; Random type pick: 4/32 = 12.5 % smart bomb (type 4),
        ; otherwise the 4 regular drops (~22 % each).
        STY temp                ; preserve Y across prng16
        JSR prng16
        LDY temp
        AND #$1F                ; 0..31
        CMP #28
        BCC @std_type
        LDA #$04                ; smart bomb (rare)
        STA drop_type
        JMP @arm
@std_type:
        AND #$03                ; 0..3 -> regular drop
        STA drop_type
@arm:
        LDA #$01
        STA drop_active
        RTS


; =============================================
; update_drop: advance the drop one tick. On player overlap, apply the
; effect. On off-screen-bottom, deactivate.
; =============================================
update_drop:
        LDA drop_active
        BEQ @done
        LDA drop_y
        CLC
        ADC #DROP_SPEED
        STA drop_y
        CMP #200
        BCC @check_hit
        LDA #$00
        STA drop_active
        RTS
@check_hit:
        ; |drop_center_x - player_center_x| < COLLIDE_DIST ?
        LDA drop_x
        CLC
        ADC #$08
        STA temp
        LDA player_x
        CLC
        ADC #$08
        SEC
        SBC temp
        BCS @x_pos
        EOR #$FF
        ADC #$00
@x_pos:
        CMP #COLLIDE_DIST
        BCS @done
        ; |drop_center_y - player_center_y| < COLLIDE_DIST ?
        LDA drop_y
        CLC
        ADC #$08
        STA temp
        LDA #PLAYER_Y+8
        SEC
        SBC temp
        BCS @y_pos
        EOR #$FF
        ADC #$00
@y_pos:
        CMP #COLLIDE_DIST
        BCS @done
        ; Caught it.
        JSR apply_drop
        LDA #$00
        STA drop_active
@done:
        RTS


; =============================================
; apply_drop: dispatch on drop_type. The shield re-uses flash_cd as
; the invuln timer (shared with post-hit blink) -- so picking up a
; shield while flashing simply extends the window.
; =============================================
apply_drop:
        LDA drop_type
        BEQ @double
        CMP #$01
        BEQ @triple
        CMP #$02
        BEQ @shield
        CMP #$03
        BEQ @skull
        ; type 4: smart bomb
        JMP @bomb
@double:
        LDA #$01
        STA weapon_mode
        LDA #WEAPON_TICKS
        STA weapon_ttl
        JMP draw_hud            ; refresh weapon icon (tail-call)
@triple:
        LDA #$02
        STA weapon_mode
        LDA #WEAPON_TICKS
        STA weapon_ttl
        JMP draw_hud
@shield:
        LDA #SHIELD_TICKS
        STA shield_t
        RTS
@skull:
        ; -> player_hit (shield-aware via shield_t)
        JSR player_hit
        RTS
@bomb:
        ; Smart bomb: clear all enemy bullets, kill every alive enemy
        ; with full kill score, then drive a centred cascade of
        ; BOMB_PHASES explosions via tick_bomb (positions in
        ; bomb_seq_x / bomb_seq_y data tables).
        LDA #$00
        STA eb_active+0
        STA eb_active+1
        STA eb_active+2
        LDX #$00
@bomb_kill_lp:
        LDA enemy_state,X
        CMP #$02
        BEQ @bomb_kill_next
        LDA #$02
        STA enemy_state,X
        LDA enemy_kill_score,X
        JSR score_add
        ; Boss bonus on E3 slot when is_boss
        LDA is_boss
        BEQ @bomb_kill_next
        CPX #$02
        BNE @bomb_kill_next
        LDA #200
        JSR score_add
        LDA #200
        JSR score_add
        LDA #50
        JSR score_add
@bomb_kill_next:
        INX
        CPX #$03
        BCC @bomb_kill_lp
        ; First explosion at central position 0; remaining phases fire
        ; from tick_bomb on a cooldown.
        LDA bomb_seq_x+0
        STA exp_x
        LDA bomb_seq_y+0
        STA exp_y
        LDA #EXP_TICKS
        STA exp_t
        LDA #$01
        STA exp_active
        STA bomb_active
        STA bomb_phase          ; next sequence index = 1
        LDA #10                 ; ticks until the next blast (~200 ms)
        STA bomb_t
        JMP draw_hud            ; refresh score


; =============================================
; spawn_exp_at_xy: arm the explosion sprite at (A=x, X=y).
; =============================================
spawn_exp_at_xy:
        STA exp_x
        STX exp_y
        LDA #EXP_TICKS
        STA exp_t
        LDA #$01
        STA exp_active
        RTS


; =============================================
; maybe_start_dive: when no enemy is currently diving and dive_cd has
; expired, pick a random alive enemy and start its dive.
; =============================================
maybe_start_dive:
        LDA dive_idx
        CMP #$FF
        BNE @done               ; already diving

        LDA dive_cd
        BEQ @ready
        DEC dive_cd
        RTS
@ready:
        ; Try up to 3 random picks for an alive enemy.
        LDX #$03
@pick:
        JSR prng16
        LDA prng_lo
        AND #$03
        CMP #$03
        BCS @next_try           ; rejected (only 0..2 valid)
        TAY
        LDA enemy_state,Y
        BNE @next_try           ; not in formation
        ; Got one: start dive
        STY dive_idx
        LDA #$01
        STA enemy_state,Y
        ; Snapshot start position
        LDA form_base_x,Y
        CLC
        ADC form_off
        STA enemy_x,Y
        LDA #FORM_Y
        STA enemy_y,Y
        ; Snapshot target X = current player_x
        LDA player_x
        STA dive_target_x
        LDA #$00
        STA dive_did_fire
        STA dive_phase
        STA dive_ticks
        ; --- Pick dive pattern based on wave (progression) ---
        ;   wave 1-3 : SIMPLE only
        ;   wave 4-6 : SIMPLE or ZIGZAG (random)
        ;   wave 7+  : any of 4 patterns
        LDA wave
        CMP #$04
        BCC @pat_simple
        CMP #$07
        BCC @pat_two
        ; wave 7+: 0..3
        STY temp                ; preserve Y across prng16
        JSR prng16
        LDY temp
        AND #$03
        STA dive_pattern
        RTS
@pat_two:
        STY temp
        JSR prng16
        LDY temp
        AND #$01
        STA dive_pattern
        RTS
@pat_simple:
        LDA #DIVE_SIMPLE
        STA dive_pattern
        RTS
@next_try:
        DEX
        BNE @pick
@done:
        RTS


; =============================================
; update_dive: if an enemy is currently diving, advance its position
; toward (dive_target_x, bottom). Spawn an enemy bullet once when it
; crosses DIVE_FIRE_AT_Y. End the dive when it goes off-screen-bottom
; -> respawn into formation slot.
; =============================================
update_dive:
        LDA is_boss
        BEQ @no_boss_skip
        RTS                              ; boss waves bypass dive logic
@no_boss_skip:
        LDX dive_idx
        CPX #$FF
        BNE @active
        RTS
@active:
        INC dive_ticks
        ; All patterns share the ascending return: once dive_phase = 1
        ; the shared @ascend drifts the alien back up to formation Y
        ; with X drifting to its slot. Pattern-specific logic only runs
        ; while dive_phase = 0 (descent / sweep / loop).
        LDA dive_phase
        BEQ @phase0
        JMP @ascend
@phase0:
        LDA dive_pattern
        BEQ @descend            ; SIMPLE -> straight descend toward player
        CMP #DIVE_ZIGZAG
        BNE @disp1
        JMP @do_zigzag
@disp1:
        CMP #DIVE_DIAG
        BNE @disp2
        JMP @do_diag
@disp2:
        JMP @do_loop            ; DIVE_LOOP

; -----------------------------------------------------------------
; Descent phase: Y += DIVE_DY, X drifts toward dive_target_x.
; Fires one enemy bullet when crossing DIVE_FIRE_AT_Y.
; When enemy_y reaches enemy_dive_max_y[X]:
;   * if enemy_does_ascend[X] = 0  -> end dive (E1 just popped off-bottom)
;   * if enemy_does_ascend[X] != 0 -> switch to ascending phase
; -----------------------------------------------------------------
@descend:
        LDA enemy_y,X
        CLC
        ADC #DIVE_DY
        STA enemy_y,X

        LDA enemy_x,X
        CMP dive_target_x
        BEQ @no_dx
        BCC @bias_right
        DEC enemy_x,X
        ; Wrap on left underflow (DEC of 0 yielded $FF)
        LDA enemy_x,X
        CMP #$F8
        BCC @after_dx
        LDA #240
        STA enemy_x,X
        JMP @after_dx
@bias_right:
        INC enemy_x,X
        ; Wrap on right overflow (INC past 240)
        LDA enemy_x,X
        CMP #241
        BCC @after_dx
        LDA #$00
        STA enemy_x,X
@after_dx:
@no_dx:
        ; Fire once during descent.
        LDA dive_did_fire
        BNE @check_max
        LDA enemy_y,X
        CMP #DIVE_FIRE_AT_Y
        BCC @check_max
        JSR spawn_eb
        LDX dive_idx
        LDA #$01
        STA dive_did_fire
@check_max:
        LDA enemy_y,X
        CMP enemy_dive_max_y,X
        BCC @done

        ; Reached max depth.
        LDA enemy_does_ascend,X
        BEQ @end_dive            ; E1 - dive ends here
        ; E2/E3: switch to ascending phase
        LDA #$01
        STA dive_phase
        JMP @done

; -----------------------------------------------------------------
; Ascent phase (E2/E3 only): Y -= DIVE_DY, X drifts back toward this
; enemy's formation X slot. When Y reaches FORM_Y, end the dive.
; -----------------------------------------------------------------
@ascend:
        LDA enemy_y,X
        SEC
        SBC #DIVE_DY
        STA enemy_y,X

        ; Drift back to formation slot
        LDA form_base_x,X
        CLC
        ADC form_off
        STA temp                 ; current formation X for slot X
        LDA enemy_x,X
        CMP temp
        BEQ @no_dx_a
        BCC @asc_right
        DEC enemy_x,X
        LDA enemy_x,X
        CMP #$F8
        BCC @after_dx_a
        LDA #240
        STA enemy_x,X
        JMP @after_dx_a
@asc_right:
        INC enemy_x,X
        LDA enemy_x,X
        CMP #241
        BCC @after_dx_a
        LDA #$00
        STA enemy_x,X
@after_dx_a:
@no_dx_a:
        LDA enemy_y,X
        CMP #FORM_Y+1
        BCS @done                ; still above formation level, keep ascending
        ; Reached formation Y -> end dive

@end_dive:
        ; If still alive, restore formation state. If dead, leave state=2.
        LDA enemy_hp,X
        BEQ @kill_dive
        LDA #$00
        STA enemy_state,X
@kill_dive:
        LDA #$FF
        STA dive_idx
        LDA cur_dive_cd_max
        STA dive_cd
        LDA #$00
        STA dive_phase
@done:
        RTS


; -----------------------------------------------------------------
; ZIGZAG : descend with X oscillating left/right (sin-like, cheap).
; bit 3 of dive_ticks toggles direction every 8 ticks (~ 0.16 s).
; End condition: enemy_y reaches enemy_dive_max_y[X] -> @end_dive.
; -----------------------------------------------------------------
@do_zigzag:
        LDA enemy_y,X
        CLC
        ADC #DIVE_DY
        STA enemy_y,X
        ; X oscillation: +/-4 px/tick, direction flips every 16 ticks
        ; (peak-to-peak ~ 64 px -- noticeably wide).
        LDA dive_ticks
        AND #$10
        BEQ @zz_left
        LDA enemy_x,X
        CLC
        ADC #$04
        CMP #241
        BCC @zz_x_set
        LDA #$00                 ; wrap right
@zz_x_set:
        STA enemy_x,X
        JMP @zz_after_x
@zz_left:
        LDA enemy_x,X
        SEC
        SBC #$04
        BCS @zz_x_set2
        LDA #240                 ; wrap left
@zz_x_set2:
        STA enemy_x,X
@zz_after_x:
        ; Fire once when crossing fire threshold
        LDA dive_did_fire
        BNE @zz_check
        LDA enemy_y,X
        CMP #DIVE_FIRE_AT_Y
        BCC @zz_check
        JSR spawn_eb
        LDX dive_idx
        LDA #$01
        STA dive_did_fire
@zz_check:
        LDA enemy_y,X
        CMP enemy_dive_max_y,X
        BCC @zz_done
        ; Reached the bottom of the dive. If this enemy ascends, hand
        ; off to the shared ascent. Otherwise (E1 scout) just end now.
        LDA enemy_does_ascend,X
        BEQ @zz_kill
        LDA #$01
        STA dive_phase
        RTS
@zz_kill:
        JMP @end_dive
@zz_done:
        RTS


; -----------------------------------------------------------------
; DIAGONAL : cross-screen sweep. Y descends slowly (1 px every 4
; ticks), X moves fast (4 px/tick) toward dive_target_x then keeps
; going past it (using wrap). End after 80 ticks (~1.6 s).
; -----------------------------------------------------------------
@do_diag:
        ; Slow vertical drift (1 px every 4 ticks)
        LDA dive_ticks
        AND #$03
        BNE @diag_x
        INC enemy_y,X
@diag_x:
        ; Fast horizontal: 4 px/tick toward target initially, with wrap
        LDA enemy_x,X
        CMP dive_target_x
        BCS @diag_left
        ; right
        CLC
        ADC #$04
        CMP #241
        BCC @diag_x_set
        LDA #$00
@diag_x_set:
        STA enemy_x,X
        JMP @diag_check
@diag_left:
        SEC
        SBC #$04
        BCS @diag_x_set2
        LDA #240
@diag_x_set2:
        STA enemy_x,X
@diag_check:
        ; Fire once at fire threshold
        LDA dive_did_fire
        BNE @diag_end_check
        LDA enemy_y,X
        CMP #DIVE_FIRE_AT_Y
        BCC @diag_end_check
        JSR spawn_eb
        LDX dive_idx
        LDA #$01
        STA dive_did_fire
@diag_end_check:
        LDA dive_ticks
        CMP #80
        BCC @diag_done
        ; Sweep complete -> hand off to the shared ascent so the alien
        ; drifts back up toward its formation slot.
        LDA enemy_does_ascend,X
        BEQ @diag_kill
        LDA #$01
        STA dive_phase
        RTS
@diag_kill:
        JMP @end_dive
@diag_done:
        RTS


; -----------------------------------------------------------------
; LOOP : 3-phase U-turn.
;   ticks 0..30   : descend (Y+=DIVE_DY)
;   ticks 30..50  : turn (Y stable, X+=4)
;   ticks 50..80  : ascend (Y-=DIVE_DY)
;   tick  >= 80   : end_dive
; The X drift makes a wide arc rather than a vertical line.
; -----------------------------------------------------------------
@do_loop:
        LDA dive_ticks
        CMP #30
        BCC @loop_descend
        CMP #50
        BCC @loop_turn
        ; Past the turn -> hand off to the shared ascent (drifts back
        ; toward formation X / FORM_Y).
        LDA enemy_does_ascend,X
        BEQ @loop_kill
        LDA #$01
        STA dive_phase
        RTS
@loop_kill:
        JMP @end_dive
@loop_descend:
        LDA enemy_y,X
        CLC
        ADC #DIVE_DY
        STA enemy_y,X
        ; Mild rightward drift during descent (1 px/tick)
        INC enemy_x,X
        LDA enemy_x,X
        CMP #241
        BCC @loop_d_done
        LDA #$00
        STA enemy_x,X
@loop_d_done:
        ; Fire once at fire threshold
        LDA dive_did_fire
        BNE @loop_d_ret
        LDA enemy_y,X
        CMP #DIVE_FIRE_AT_Y
        BCC @loop_d_ret
        JSR spawn_eb
        LDX dive_idx
        LDA #$01
        STA dive_did_fire
@loop_d_ret:
        RTS
@loop_turn:
        ; X sweeps faster (4 px/tick), Y stays still
        LDA enemy_x,X
        CLC
        ADC #$04
        CMP #241
        BCC @loop_t_set
        LDA #$00
@loop_t_set:
        STA enemy_x,X
        RTS


; =============================================
; spawn_eb: spawn an enemy bullet at the diving enemy's current pos,
; into the first free eb slot. Trashes A, X, Y. dive_idx must be valid.
; =============================================
spawn_eb:
        LDY dive_idx
        LDX #$00
@find:
        LDA eb_active,X
        BEQ @go
        INX
        CPX #$03
        BCC @find
        RTS                     ; all 3 enemy bullets active
@go:
        LDA #$01
        STA eb_active,X
        LDA enemy_x,Y
        STA eb_x,X
        LDA enemy_y,Y
        STA eb_y,X
        ; Default velocity: straight down at the per-wave eb speed
        LDA #$00
        STA eb_dx,X
        LDA cur_eb_speed
        STA eb_dy,X
        RTS


; =============================================
; spawn_eb_at: A=x, X=y -- find a free EB slot and spawn there.
; Trashes A, X, Y; uses temp/temp2 for parameter passing.
; =============================================
spawn_eb_at:
        STA temp                ; eb x
        STX temp2               ; eb y
        LDX #$00
@find:
        LDA eb_active,X
        BEQ @go
        INX
        CPX #$03
        BCC @find
        RTS
@go:
        LDA #$01
        STA eb_active,X
        LDA temp
        STA eb_x,X
        LDA temp2
        STA eb_y,X
        ; Default velocity: straight down at the per-wave eb speed
        LDA #$00
        STA eb_dx,X
        LDA cur_eb_speed
        STA eb_dy,X
        RTS


; =============================================
; update_super_boss: rotating-fire + horizontal ping-pong for the
; super boss(es). Parameterised by N (0 or 1) so wave 11 (one boss)
; and wave 22 (two bosses) share the routine. Each tick: (1) shuffle
; horizontally bouncing between 0 and SUPER_X_MAX, (2) decrement
; super_fire_cd[N], (3) on cooldown=0 spawn one enemy bullet at the
; boss centre using angle_{dx,dy}[angle], (4) angle = (angle+1) & 15.
; The boss index is saved in temp3 because the EB-slot loop reuses Y.
; =============================================
update_super_boss:
        LDX #$00
        JMP super_n
update_super_boss2:
        LDX #$01
super_n:
        LDA is_super_boss,X
        BNE @go
        RTS
@go:
        STX temp3                       ; remember boss idx
        ; --- Horizontal motion ---
        LDA super_dx,X
        BMI @move_left
        LDA super_x,X
        CLC
        ADC super_dx,X
        CMP #SUPER_X_MAX+1
        BCC @save_x_right
        LDA #SUPER_X_MAX
        STA super_x,X
        LDA #$FC                        ; -SUPER_DX_INIT
        STA super_dx,X
        JMP @after_move
@save_x_right:
        STA super_x,X
        JMP @after_move
@move_left:
        LDA super_x,X
        CLC
        ADC super_dx,X
        BCC @hit_left
        STA super_x,X
        JMP @after_move
@hit_left:
        LDA #$00
        STA super_x,X
        LDA #SUPER_DX_INIT
        STA super_dx,X
@after_move:
        LDA super_fire_cd,X
        BEQ @fire
        SEC
        SBC #$01
        STA super_fire_cd,X
        RTS
@fire:
        LDA #SUPER_FIRE_CD
        STA super_fire_cd,X
        ; Find a free EB slot using Y; X stays = boss idx
        LDY #$00
@find:
        LDA eb_active,Y
        BEQ @spawn
        INY
        CPY #$03
        BCC @find
        JMP @advance                    ; no slot free, still rotate angle
@spawn:
        LDA #$01
        STA eb_active,Y
        LDA super_x,X
        CLC
        ADC #$10
        STA eb_x,Y
        LDA super_y,X
        CLC
        ADC #$10
        STA eb_y,Y
        ; Velocity from angle table: trash X to hold the angle, then
        ; restore boss idx from temp3 below.
        STY temp                        ; save eb slot
        LDA super_angle,X
        TAX                             ; X = angle 0..15
        LDA angle_dx,X
        LDY temp
        STA eb_dx,Y
        LDA angle_dy,X                  ; X still = angle
        STA eb_dy,Y
        LDX temp3                       ; restore boss idx
@advance:
        LDA super_angle,X
        CLC
        ADC #$01
        AND #$0F
        STA super_angle,X
        RTS


; =============================================
; check_pb_vs_super: parameterised PB-vs-super collision (X = boss N
; on entry). Wraps into check_pb_vs_super / check_pb_vs_super2 entry
; points so the game loop touches both. On kill: clears is_super_boss
; [N], adds +1000, big explosion at boss centre.
; =============================================
check_pb_vs_super:
        LDX #$00
        JMP super_chk_n
check_pb_vs_super2:
        LDX #$01
super_chk_n:
        LDA is_super_boss,X
        BNE @scan
        RTS
@scan:
        STX temp3                       ; boss idx (preserved across the loop)
        LDY #$00                        ; pb iterator
@lp:
        LDA pb_active,Y
        BNE @do
        JMP @next
@do:
        ; |pb_x+8 - (super_x[N]+16)| < SUPER_HITBOX ?
        LDX temp3
        LDA pb_x,Y
        CLC
        ADC #$08
        STA temp                        ; pb cx
        LDA super_x,X
        CLC
        ADC #$10
        SEC
        SBC temp
        BCS @x_pos
        EOR #$FF
        ADC #$00
@x_pos:
        CMP #SUPER_HITBOX
        BCC @y_chk
        JMP @next
@y_chk:
        LDA pb_y,Y
        CLC
        ADC #$08
        STA temp                        ; pb cy
        LDA super_y,X
        CLC
        ADC #$10
        SEC
        SBC temp
        BCS @y_pos
        EOR #$FF
        ADC #$00
@y_pos:
        CMP #SUPER_HITBOX
        BCC @hit
        JMP @next
@hit:
        LDA #$00
        STA pb_active,Y
        ; Explosion at impact
        LDA pb_x,Y
        STA exp_x
        LDA pb_y,Y
        STA exp_y
        LDA #EXP_TICKS
        STA exp_t
        LDA #$01
        STA exp_active
        ; Hit score
        LDA #$05
        JSR score_add
        LDX temp3
        DEC super_hp,X
        BNE @hit_done
        ; Killed!
        LDA #$00
        STA is_super_boss,X
        ; +1000 victory bonus (5 x 200)
        LDA #200
        JSR score_add
        LDA #200
        JSR score_add
        LDA #200
        JSR score_add
        LDA #200
        JSR score_add
        LDA #200
        JSR score_add
        ; Massive central explosion
        LDX temp3
        LDA super_x,X
        CLC
        ADC #$10
        STA exp_x
        LDA super_y,X
        CLC
        ADC #$10
        STA exp_y
        LDA #30
        STA exp_t
        LDA #$01
        STA exp_active
@hit_done:
@next:
        INY
        CPY #$03
        BCS @done
        JMP @lp
@done:
        RTS


; --- Smart-bomb cascade positions (clustered near screen centre) ---
; 6 explosions, indexed by bomb_phase, deliberately central so the
; bomb feels like a screen-wide concussive shockwave rather than
; isolated pops where the aliens stood.
bomb_seq_x: .byte 128,  88, 168, 104, 152, 128
bomb_seq_y: .byte  96,  72,  72, 116, 116,  60


; --- 16-direction velocity table for super-boss bullets ---
; Magnitudes ~4 px/tick. Lower semicircle only (0..180 deg) so boss
; bullets always head TOWARD the player (never up at the HUD).
;   idx  0: right        idx  8: straight down (south)
;   idx  4: down-right   idx 12: down-left
;   idx 15: ~ left
; Cycle 0->15->0 sweeps right -> down -> left then snaps back right.
; Both dy values are positive (>= 0), enforcing the constraint.
angle_dx:
        .byte $04, $04, $04, $03, $03, $02, $02, $01    ; 0..7  (right -> down)
        .byte $00, $FF, $FE, $FE, $FD, $FD, $FC, $FC    ; 8..15 (down -> left)
angle_dy:
        .byte $00, $01, $02, $02, $03, $03, $04, $04
        .byte $04, $04, $04, $03, $03, $02, $02, $01


; =============================================
; update_boss: oscillate boss X horizontally and fire a 3-bullet
; spread on cooldown. No-op when is_boss == 0.
; =============================================
update_boss:
        LDA is_boss
        BNE @go
        RTS
@go:
        ; Move every 3 ticks for a smooth slide
        INC boss_x_tick
        LDA boss_x_tick
        CMP #$03
        BCC @no_move
        LDA #$00
        STA boss_x_tick
        LDA enemy_x+2
        CLC
        ADC boss_x_dir
        STA enemy_x+2
        ; Reverse near edges
        CMP #210
        BCC @ck_left
        LDA #$FF
        STA boss_x_dir
        JMP @no_move
@ck_left:
        CMP #16
        BCS @no_move
        LDA #$01
        STA boss_x_dir
@no_move:
        ; Fire 3-bullet spread on cooldown
        LDA boss_fire_cd
        BEQ @fire
        SEC
        SBC #$01
        STA boss_fire_cd
        RTS
@fire:
        LDA #70                 ; gentler boss spread cadence
        STA boss_fire_cd
        ; Spawn 3 enemy bullets at (boss_x-8, fy), (boss_x, fy), (boss_x+8, fy)
        LDA enemy_x+2
        SEC
        SBC #$08
        LDX enemy_y+2
        JSR spawn_eb_at
        LDA enemy_x+2
        LDX enemy_y+2
        JSR spawn_eb_at
        LDA enemy_x+2
        CLC
        ADC #$08
        LDX enemy_y+2
        JSR spawn_eb_at
        RTS


; =============================================
; update_ebullets: advance every active enemy bullet by its (dx, dy).
; Default for normal eb is (dx=0, dy=cur_eb_speed) set at spawn time.
; Off-screen test: y > 192 OR x > 240 (catches both wrap-underflow on
; small values and overshoot of the playfield).
; =============================================
update_ebullets:
        LDX #$00
@lp:
        LDA eb_active,X
        BEQ @next
        ; X += dx (signed)
        LDA eb_x,X
        CLC
        ADC eb_dx,X
        STA eb_x,X
        ; Y += dy (signed)
        LDA eb_y,X
        CLC
        ADC eb_dy,X
        STA eb_y,X
        ; Off-screen check
        CMP #DIVE_MIN_Y
        BCS @off                ; y >= 200
        LDA eb_x,X
        CMP #241
        BCS @off                ; x >= 241 (wrapped or overshoot)
        JMP @next
@off:
        LDA #$00
        STA eb_active,X
@next:
        INX
        CPX #$03
        BCC @lp
        RTS


; =============================================
; check_pb_vs_enemies: for each active player bullet, test against each
; alive enemy. On hit, decrement HP, deactivate bullet, score +1. On
; HP=0 set state=2 (dead) and add per-type kill bonus.
; =============================================
check_pb_vs_enemies:
        LDX #$00
@pb_lp:
        LDA pb_active,X
        BNE @do_check
        JMP @pb_next            ; long branch
@do_check:
        ; bullet center: pbcx = pb_x + 8, pbcy = pb_y + 8
        LDA pb_x,X
        CLC
        ADC #$08
        STA temp                ; pbcx
        LDA pb_y,X
        CLC
        ADC #$08
        STA temp2               ; pbcy

        ; Check vs each enemy
        LDY #$00
@en_lp:
        LDA enemy_state,Y
        CMP #2
        BNE @en_alive
        JMP @en_next            ; dead, skip (long branch)
@en_alive:
        ; Compute enemy center X = (state==1 ? enemy_x[Y] : form_base_x[Y]+form_off) + 8
        LDA enemy_state,Y
        BEQ @form_x
        LDA enemy_x,Y
        JMP @cx_done
@form_x:
        LDA form_base_x,Y
        CLC
        ADC form_off
@cx_done:
        CLC
        ADC #$08
        STA temp3               ; encx

        ; |pbcx - encx| < COLLIDE_DIST ?
        LDA temp                ; pbcx
        SEC
        SBC temp3
        BCS @x_pos
        EOR #$FF
        ADC #$00
@x_pos:
        CMP #COLLIDE_DIST
        BCC @x_in_range
        JMP @en_next            ; long branch (function grew with streak/popup)
@x_in_range:

        ; Enemy center Y = (state==1 ? enemy_y[Y] : FORM_Y) + 8
        LDA enemy_state,Y
        BEQ @form_y
        LDA enemy_y,Y
        JMP @cy_done
@form_y:
        LDA #FORM_Y
@cy_done:
        CLC
        ADC #$08

        ; |pbcy - ency| < COLLIDE_DIST ?
        STA temp3               ; ency
        LDA temp2               ; pbcy
        SEC
        SBC temp3
        BCS @y_pos
        EOR #$FF
        ADC #$00
@y_pos:
        CMP #COLLIDE_DIST
        BCC @y_in_range
        JMP @en_next            ; long branch
@y_in_range:

        ; HIT
        LDA #$00
        STA pb_active,X
        ; +1 score for the hit (still scores even on a non-kill)
        JSR score_add_1
        ; Spawn explosion at the bullet's last position (visually
        ; close to the enemy centre). exp_active stays single-slot --
        ; rapid hits just retrigger the same sprite.
        LDA pb_x,X
        STA exp_x
        LDA pb_y,X
        STA exp_y
        LDA #EXP_TICKS
        STA exp_t
        LDA #$01
        STA exp_active
        ; Decrement HP (DEC enemy_hp,Y is not a valid 6502 mode)
        LDA enemy_hp,Y
        SEC
        SBC #$01
        STA enemy_hp,Y
        BNE @hit_alive          ; HP > 0 -> alive, flash for 3 s
        ; Killed: snapshot enemy position for drop spawn, then state=2.
        JSR spawn_drop          ; reads enemy_state[Y]; preserves Y/X
        ; Inline score-popup at the ENEMY position (offset right by 16
        ; so it pops up "next to" the alien instead of overlapping it).
        ; For diving enemies enemy_x/y is current; for formation aliens
        ; we recompose form_base_x[Y]+form_off.
        LDA enemy_state,Y
        BEQ @pop_form
        LDA enemy_x,Y
        STA popup_x
        LDA enemy_y,Y
        STA popup_y
        JMP @pop_done
@pop_form:
        LDA form_base_x,Y
        CLC
        ADC form_off
        STA popup_x
        LDA #FORM_Y
        STA popup_y
@pop_done:
        ; Nudge right so the arrow is beside the alien sprite.
        LDA popup_x
        CLC
        ADC #16
        STA popup_x
        LDA enemy_color,Y
        STA popup_color
        LDA #30
        STA popup_ttl
        LDA #$01
        STA popup_active
        ; Now finish the kill -- X and Y still valid for the labels below.
        LDA #$02
        STA enemy_state,Y
        ; Bump streak (Pack 2.b) — saturate at 99 — BEFORE applying the
        ; multiplier so the third kill in a row is the first to score x2.
        LDA streak
        CMP #99
        BCS @kill_no_inc
        INC streak
@kill_no_inc:
        LDA enemy_kill_score,Y
        JSR score_add_with_mul
        ; Boss bonus: +450 extra on E3 slot when is_boss
        LDA is_boss
        BEQ @no_boss_bonus
        CPY #$02
        BNE @no_boss_bonus
        LDA #200                ; saturating add: +200 +200 +50 = 450
        JSR score_add
        LDA #200
        JSR score_add
        LDA #50
        JSR score_add
@no_boss_bonus:
        CPY dive_idx
        BNE @no_dive_end
        LDA #$FF
        STA dive_idx
        LDA cur_dive_cd_max
        STA dive_cd
@no_dive_end:
        JMP @pb_next
@hit_alive:
        ; Arm the per-enemy hit flash (~1.5 s strobe). Successful hit
        ; (even non-killing) bumps the combo streak.
        LDA #ENEMY_FLASH_TICKS
        STA enemy_flash,Y
        LDA streak
        CMP #99
        BCS @hit_no_inc
        INC streak
@hit_no_inc:
        JMP @pb_next
@en_next:
        INY
        CPY #$03
        BCS @pb_next            ; finished enemy scan
        JMP @en_lp              ; long branch
@pb_next:
        INX
        CPX #$03
        BCS @pb_done
        JMP @pb_lp              ; long branch
@pb_done:
        ; HUD refresh
        JSR draw_hud
        RTS


; =============================================
; check_eb_vs_player: any active enemy bullet overlapping the player
; sprite costs one life and deactivates the bullet. Player gets a
; brief flash window so it doesn't die instantly twice in a row.
; =============================================
check_eb_vs_player:
        LDA flash_cd
        BNE @done
        LDA shield_t
        BNE @done

        LDX #$00
@lp:
        LDA eb_active,X
        BEQ @next
        ; bullet center vs player center
        LDA eb_x,X
        CLC
        ADC #$08
        STA temp                ; ebcx
        LDA player_x
        CLC
        ADC #$08
        SEC
        SBC temp
        BCS @x_pos
        EOR #$FF
        ADC #$00
@x_pos:
        CMP #COLLIDE_DIST
        BCS @next
        LDA eb_y,X
        CLC
        ADC #$08
        STA temp
        LDA #PLAYER_Y+8
        SEC
        SBC temp
        BCS @y_pos
        EOR #$FF
        ADC #$00
@y_pos:
        CMP #COLLIDE_DIST
        BCS @next
        ; HIT
        LDA #$00
        STA eb_active,X
        JSR player_hit
        RTS
@next:
        INX
        CPX #$03
        BCC @lp
@done:
        RTS


; =============================================
; check_dive_vs_player: a diving enemy ramming the ship also costs a life.
; =============================================
check_dive_vs_player:
        LDA flash_cd
        BEQ @flash_ok
        JMP @done                ; long branch (ram-kill body grew with popup)
@flash_ok:
        LDA shield_t
        BEQ @shield_ok
        JMP @done
@shield_ok:
        LDA dive_idx
        CMP #$FF
        BNE @diving
        JMP @done
@diving:
        TAX
        ; dive enemy center vs player center
        LDA enemy_x,X
        CLC
        ADC #$08
        STA temp
        LDA player_x
        CLC
        ADC #$08
        SEC
        SBC temp
        BCS @x_pos
        EOR #$FF
        ADC #$00
@x_pos:
        CMP #COLLIDE_DIST
        BCS @done
        LDA enemy_y,X
        CLC
        ADC #$08
        STA temp
        LDA #PLAYER_Y+8
        SEC
        SBC temp
        BCS @y_pos
        EOR #$FF
        ADC #$00
@y_pos:
        CMP #COLLIDE_DIST
        BCS @done
        ; Player rammed -- enemy takes 1 damage too. Spawn explosion at
        ; the diving enemy's current position.
        LDA enemy_x,X
        STA exp_x
        LDA enemy_y,X
        STA exp_y
        LDA #EXP_TICKS
        STA exp_t
        LDA #$01
        STA exp_active
        DEC enemy_hp,X
        LDA enemy_hp,X
        BNE @ram_alive
        ; Killed via ram: spawn_drop + popup at the enemy position.
        TXA
        TAY                     ; spawn_drop wants Y = enemy idx
        JSR spawn_drop
        LDA enemy_x,X
        CLC
        ADC #16                 ; nudge popup beside the alien sprite
        STA popup_x
        LDA enemy_y,X
        STA popup_y
        LDA enemy_color,X
        STA popup_color
        LDA #30
        STA popup_ttl
        LDA #$01
        STA popup_active
        LDA #$02
        STA enemy_state,X
        LDA streak
        CMP #99
        BCS @ram_kill_no_inc
        INC streak
@ram_kill_no_inc:
        LDA enemy_kill_score,X
        JSR score_add_with_mul
        LDA #$FF
        STA dive_idx
        LDA cur_dive_cd_max
        STA dive_cd
        JMP @hit_player
@ram_alive:
        LDA #ENEMY_FLASH_TICKS
        STA enemy_flash,X
@hit_player:
        JSR player_hit
@done:
        RTS


; =============================================
; player_hit: lose a life, brief invulnerability flash.
;   lives reaches 0 -> game_over.
; =============================================
player_hit:
        LDA player_lives
        BEQ @over
        DEC player_lives
        LDA #20
        STA flash_cd
        ; Lose all power-ups on hit: weapon, shield, streak.
        LDA #$00
        STA weapon_mode
        STA weapon_ttl
        STA shield_t
        STA streak
        LDA player_lives
        BEQ @over
        JSR draw_hud
        RTS
@over:
        LDA #$01
        STA game_over
        JSR draw_hud
        RTS


; =============================================
; all_dead: A = $00 if at least one enemy alive, A != 0 if all dead.
; =============================================
all_dead:
        ; Super boss wave: alive until BOTH bosses (if present) are
        ; defeated. Wave 22 sets both flags; wave 11 sets only boss 1.
        LDA is_super_boss+0
        ORA is_super_boss+1
        BEQ @check_enemies
        LDA #$00
        RTS
@check_enemies:
        LDX #$00
@lp:
        LDA enemy_state,X
        CMP #2
        BNE @alive
        INX
        CPX #$03
        BCC @lp
        LDA #$01
        RTS
@alive:
        LDA #$00
        RTS


; =============================================
; score_add_1 / score_add: add A to 16-bit score (with cap at $FFFF).
; score_add_with_mul: applies the combo-streak multiplier to A (1x, 2x
; or 3x) by calling score_add 1, 2 or 3 times. Trashes temp.
; =============================================
score_add_1:
        LDA #$01
        ; fall through
score_add:
        CLC
        ADC score_lo
        STA score_lo
        LDA score_hi
        ADC #$00
        STA score_hi
        RTS

score_add_with_mul:
        STA temp                ; base score
        LDX streak
        CPX #6
        BCS @x3
        CPX #3
        BCS @x2
        ; x1
        LDA temp
        JMP score_add
@x2:
        LDA temp
        JSR score_add
        LDA temp
        JMP score_add
@x3:
        LDA temp
        JSR score_add
        LDA temp
        JSR score_add
        LDA temp
        JMP score_add


; =============================================
; hide_slot_4: write a hidden SAT slot (Y=HIDDEN_Y, X=0, name=0, color=0).
; Called only from render_sprites — see SKIP annotation inside.
; =============================================
hide_slot_4:
        ; SILICON_STRICT_SKIP — only ever called from render_sprites'
        ; VBlank-gated body. The 2c gate inherits via the JSR/RTS
        ; boundary, so back-to-back STA VDP_DATA writes need no padding.
        LDA #HIDDEN_Y           ; preserve X and Y for render_sprites' @en_lp /
        STA VDP_DATA            ; @pb_lp / @eb_lp loops which iterate via X
        LDA #$00
        STA VDP_DATA
        STA VDP_DATA
        STA VDP_DATA
        RTS


; =============================================
; render_sprites: rebuild the entire sprite attribute table at $1B00.
; Slot order: player, e0, e1, e2, pb0, pb1, eb0, eb1, eb2, terminator.
; Hidden sprites get Y=$C0 (off-screen, chain continues).
; =============================================
render_sprites:
        ; SILICON_STRICT_SKIP — body runs during the VBlank window. The
        ; 2-step F-flag dance at entry (drain stale + wait fresh) puts
        ; us at the START of the chip's vertical retrace, when the
        ; silicon-strict gate drops to 2c (cf. dev/SILICONBUGS.md §2.5
        ; paranoid-mode + §16 F-flag timing). All VDP_DATA / VDP_CTRL
        ; writes between @v_wait and the function's RTS complete in
        ; <600 cycles, far below the 4554c (60Hz NTSC) VBlank budget.
        ; No JSR pad40 calls anywhere in the body — the auto-patcher
        ; sees SILICON_STRICT_SKIP and leaves this routine alone.
        ; Saves ~50× pad40 (2000c per frame + ~150 ROM bytes) vs the
        ; previous padded design.

        ; VBlank sync. First BIT drains any stale F flag carried from
        ; the previous frame's render (also clears 5S/C — Galaga never
        ; reads them, so collateral is fine). Inner loop spins until F
        ; sets at the START of the next VBlank.
        BIT VDP_CTRL
@v_wait:
        BIT VDP_CTRL
        BPL @v_wait
        ; Set VDP write address = $1B00 (sprite attribute table)
        LDA #$00
        STA VDP_CTRL
        LDA #$5B                ; $1B | $40
        STA VDP_CTRL

        ; --- Slot 0: player (hidden during flash blink-off) ---
        LDA flash_cd
        AND #$04                ; alternate every 4 ticks of the flash
        BEQ @show_p
        JSR hide_slot_4
        JMP @after_p
@show_p:
        LDA #PLAYER_Y
        STA VDP_DATA
        LDA player_x
        STA VDP_DATA
        ; Pattern: thrust flicker when the ship is actually moving.
        LDA player_dir
        BEQ @plain_p
        LDA anim_tick
        AND #$04
        BEQ @plain_p
        LDA #P_PLAYER_TH
        JMP @write_p
@plain_p:
        LDA #P_PLAYER
@write_p:
        STA VDP_DATA
        LDA #COL_PLAYER
        STA VDP_DATA
@after_p:

        ; --- Slots 1..3: enemies ---
        LDX #$00
@en_lp:
        LDA enemy_state,X
        CMP #2
        BNE @en_alive
        ; Dead -> hidden
        JSR hide_slot_4
        JMP @en_next
@en_alive:
        ; Hit-flash strobe: alternate visibility every 4 ticks while
        ; enemy_flash[i] is non-zero (~3 s after a non-killing hit).
        LDA enemy_flash,X
        BEQ @en_paint
        AND #$04
        BNE @en_paint           ; bit set -> show frame
        JSR hide_slot_4
        JMP @en_next
@en_paint:
        ; Y
        LDA enemy_state,X
        BEQ @form_y
        LDA enemy_y,X
        JMP @yw
@form_y:
        LDA #FORM_Y
@yw:
        STA VDP_DATA
        ; X
        LDA enemy_state,X
        BEQ @form_x
        LDA enemy_x,X
        JMP @xw
@form_x:
        LDA form_base_x,X
        CLC
        ADC form_off
@xw:
        STA VDP_DATA
        ; Pattern (toggle frame 0 / frame 1 via anim_tick bit 4)
        LDA enemy_pat,X
        STA temp
        LDA anim_tick
        AND #$10
        BEQ @no_alt
        LDA temp
        CLC
        ADC #(P_ENEMY1_ALT - P_ENEMY1)
        STA temp
@no_alt:
        LDA temp
        STA VDP_DATA
        ; Colour (E3 is already magenta in the palette so the boss
        ; uses the same hue as a regular E3 -- no override needed).
        LDA enemy_color,X
        STA VDP_DATA
@en_next:
        INX
        CPX #$03
        BCS @en_done
        JMP @en_lp              ; long branch (function grew with boss colour override)
@en_done:

        ; --- Slots 4..6: player bullets (3 slots, supports triple shot) ---
        LDX #$00
@pb_lp:
        LDA pb_active,X
        BNE @pb_show
        JSR hide_slot_4
        JMP @pb_next
@pb_show:
        LDA pb_y,X
        STA VDP_DATA
        LDA pb_x,X
        STA VDP_DATA
        LDA #P_PBULLET
        STA VDP_DATA
        LDA #COL_PB
        STA VDP_DATA
@pb_next:
        INX
        CPX #$03
        BCC @pb_lp

        ; --- Slots 7..9: enemy bullets ---
        LDX #$00
@eb_lp:
        LDA eb_active,X
        BNE @eb_show
        JSR hide_slot_4
        JMP @eb_next
@eb_show:
        LDA eb_y,X
        STA VDP_DATA
        LDA eb_x,X
        STA VDP_DATA
        LDA #P_EBULLET
        STA VDP_DATA
        LDA #COL_EB
        STA VDP_DATA
@eb_next:
        INX
        CPX #$03
        BCC @eb_lp

        ; --- Slot 10: hit explosion (2-frame animation) ---
        LDA exp_active
        BEQ @exp_hide
        LDA exp_y
        STA VDP_DATA
        LDA exp_x
        STA VDP_DATA
        ; Big frame for the first half of the burst, smaller "fade"
        ; frame in the second half.
        LDA exp_t
        CMP #$08
        BCC @exp_alt_pat
        LDA #P_EXP
        JMP @write_exp
@exp_alt_pat:
        LDA #P_EXP_ALT
@write_exp:
        STA VDP_DATA
        LDA #COL_EXP
        STA VDP_DATA
        JMP @after_exp
@exp_hide:
        JSR hide_slot_4
@after_exp:

        ; --- Slot 11: falling drop (bonus or skull) ---
        LDA drop_active
        BEQ @drop_hide
        LDA drop_y
        STA VDP_DATA
        LDA drop_x
        STA VDP_DATA
        LDY drop_type
        LDA drop_pat,Y
        STA VDP_DATA
        LDA drop_color,Y
        STA VDP_DATA
        JMP @after_drop
@drop_hide:
        JSR hide_slot_4
@after_drop:

        ; --- Slot 12: score popup (single shared sprite) ---
        LDA popup_active
        BEQ @popup_hide
        LDA popup_y
        STA VDP_DATA
        LDA popup_x
        STA VDP_DATA
        LDA #P_POPUP
        STA VDP_DATA
        LDA popup_color
        STA VDP_DATA
        JMP @after_popup
@popup_hide:
        JSR hide_slot_4
@after_popup:

        ; --- Slot 13: shield ring (drawn around the ship) ---
        LDA shield_t
        BEQ @shield_hide
        LDA #PLAYER_Y
        STA VDP_DATA
        LDA player_x
        STA VDP_DATA
        LDA #P_SHIELD_RING
        STA VDP_DATA
        LDA #COL_SHIELD
        STA VDP_DATA
        JMP @after_shield
@shield_hide:
        JSR hide_slot_4
@after_shield:

        ; --- Slots 14..21: super bosses + early chain terminator ---
        ;
        ; VDP-traffic reduction (May 2026, 40c gate): both supers are
        ; inactive on every wave except W11 + the W22 endgame, so the
        ; old "always render 8 slots + 4-byte terminator" cost was 36
        ; writes per frame in the dominant case. Replace with:
        ;   - both inactive  → 1 STA (Y=$D0 at slot 14)        →  1 write
        ;   - super0 only    → 4 super0 entries + Y=$D0 at 18  → 17 writes
        ;   - super1 only    → 4 hidden + 4 super1 + Y=$D0     → 33 writes
        ;   - both active    → 8 super entries + Y=$D0         → 33 writes
        ; Plus we drop the X/pattern/colour bytes of the chain-terminator
        ; entry: real silicon stops scanning the SAT at the first $D0,
        ; so those 3 bytes never get latched. Saves an extra 3 writes /
        ; frame regardless of super state.
        ;
        ; The pad40 prologue at @term_now covers the cross-JSR caller
        ; case (render_super_n's last STA → JSR + RTS + 2-byte BNE/BEQ
        ; bridge → @term_now's STA = ~12c, < 40c).
        LDA is_super_boss
        ORA is_super_boss+1
        BEQ @term_now           ; both inactive → write Y=$D0 at slot 14, done
        LDA is_super_boss
        BEQ @s0_hidden          ; super0 inactive but super1 active
        LDX #$00
        JSR render_super_n      ; super0 → 4 entries
        JMP @after_s0
@s0_hidden:
        ; super0 inactive, super1 active — emit 4 hidden entries for slots 14-17.
        LDY #$04
@s0h_lp:
        JSR hide_slot_4
        DEY
        BNE @s0h_lp
@after_s0:
        LDA is_super_boss+1
        BEQ @term_now           ; super1 inactive → terminate at slot 18
        ; Cross-JSR cushion for the s0_hidden→s1_active path: hide_slot_4's
        ; last STA + RTS + DEY + BNE + LDA + BEQ + LDX + JSR + LDA + BNE +
        ; LDA + STA = 39c, exactly one cycle short of the 40c gate. The
        ; super0_active path enters render_super_n with 42c slack, but
        ; this hidden→active path doesn't. Add a manual 40c cushion to
        ; cover both call sites uniformly.
        LDX #$01
        JSR render_super_n      ; super1 → 4 entries
@term_now:
        ; Single Y=$D0 ends the SAT scan. Caller's last STA was either
        ; render_super_n's colour write (post-RTS) or a sibling slot —
        ; either way we cross a JSR boundary, so apply a manual cushion.
        LDA #TERM_Y
        STA VDP_DATA
        RTS


; =============================================
; render_super_n: emit 4 sprite-attribute entries for super boss[X]
; (X = 0 or 1). When the boss is inactive, write 4 hidden entries to
; preserve slot ordering.
; =============================================
render_super_n:
        ; SILICON_STRICT_SKIP — only ever called from render_sprites'
        ; VBlank window. Inherits the 2c gate, so auto-patcher pads
        ; inside this routine would be wasted cycles.
        LDA is_super_boss,X
        BNE @show
        ; Hidden -- write 4 hidden entries
        LDY #$04
@hide_lp:
        JSR hide_slot_4
        DEY
        BNE @hide_lp
        RTS
@show:
        ; TL @ (super_x[X], super_y[X])
        LDA super_y,X
        STA VDP_DATA
        LDA super_x,X
        STA VDP_DATA
        LDA #P_SUPER_TL
        STA VDP_DATA
        LDA #COL_SUPER
        STA VDP_DATA
        ; TR @ (super_x[X]+16, super_y[X])
        LDA super_y,X
        STA VDP_DATA
        LDA super_x,X
        CLC
        ADC #$10
        STA VDP_DATA
        LDA #P_SUPER_TR
        STA VDP_DATA
        LDA #COL_SUPER
        STA VDP_DATA
        ; BL @ (super_x[X], super_y[X]+16)
        LDA super_y,X
        CLC
        ADC #$10
        STA VDP_DATA
        LDA super_x,X
        STA VDP_DATA
        LDA #P_SUPER_BL
        STA VDP_DATA
        LDA #COL_SUPER
        STA VDP_DATA
        ; BR @ (super_x[X]+16, super_y[X]+16)
        LDA super_y,X
        CLC
        ADC #$10
        STA VDP_DATA
        LDA super_x,X
        CLC
        ADC #$10
        STA VDP_DATA
        LDA #P_SUPER_BR
        STA VDP_DATA
        LDA #COL_SUPER
        STA VDP_DATA
        RTS


; =============================================
; hide_all_sprites: write a single chain-terminator entry so nothing
; renders. Used on game over so the splash isn't covered by sprites.
; =============================================
hide_all_sprites:
        JSR     tms9918_pad40   ; MANUAL caller-gap cushion (40c)
        ; Set SAT write address = $1B00 then write Y=$D0 in slot 0. Real
        ; silicon stops scanning the SAT at the first $D0, so the X /
        ; pattern / colour bytes of the terminator never matter. Drops
        ; 3 useless writes from the original 4-byte terminator.
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$5B
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #TERM_Y
        STA VDP_DATA
        RTS


; =============================================
; draw_hud: draw "SCORE:NNNNN  LIVES:N  W:N" on row 0 of the name table.
; =============================================
draw_hud:
        JSR     tms9918_pad40   ; MANUAL caller-gap cushion (40c)
        ; VRAM addr $1800 (row 0 col 0)
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$58                ; $18 | $40
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_S
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_C
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_O
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_R
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_E
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_CL
        STA VDP_DATA

        ; 5-digit decimal of score (16-bit, score_lo + score_hi * 256)
        ; Convert via repeated subtraction. Max = 65535.
        ; Use a simple BCD-like conversion: divide by 10000, 1000, 100, 10, 1.
        JSR score_to_digits
        LDX #$00
@digit_lp:
        LDA score_digits,X
        CLC
        ADC #C_D0
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA VDP_DATA
        INX
        CPX #$05
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BCC @digit_lp

        ; --- W:NN at the right edge of row 0 (col 28 -> $181C) ---
        ; (LIVES is shown in full at the bottom-right; no need to
        ; duplicate it on the top row.)
        LDA #$1C
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$58                ; $18 | $40
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_W
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_CL
        STA VDP_DATA
        ; Wave as 2-digit decimal (00..99)
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA wave
        LDX #$00
@wt10:  CMP #$0A
        BCC @wt10d
        SBC #$0A
        INX
        JMP @wt10
@wt10d: STA temp
        TXA
        CLC
        ADC #C_D0
        STA VDP_DATA            ; tens
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA temp
        CLC
        ADC #C_D0
        STA VDP_DATA            ; ones

        ; (HUD bonus indicators removed: streak shows up via the score
        ;  popup that floats from the killed alien, and the weapon
        ;  upgrade is implicit -- no permanent HUD text needed.)

        ; --- Bottom-right "LIVES:N" at row 23 col 24 -> $1AD8 ---
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$D8
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$5A                ; $1A | $40
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_L
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_I
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_V
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_E
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_S
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_CL
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA player_lives
        CLC
        ADC #C_D0
        STA VDP_DATA
        RTS


; =============================================
; score_to_digits: convert 16-bit score to 5 digits (most-significant
; first) into score_digits[0..4]. Repeated-subtraction.
; =============================================
score_to_digits:
        ; Make working copy
        LDA score_lo
        STA temp
        LDA score_hi
        STA temp2
        LDX #$00
@dig_lp:
        ; quotient = 0
        LDA #$00
        STA temp3
@sub_lp:
        ; if (working < divisor): break
        LDA temp2
        CMP score_div_hi,X
        BCC @done_one
        BNE @do_sub
        LDA temp
        CMP score_div_lo,X
        BCC @done_one
@do_sub:
        LDA temp
        SEC
        SBC score_div_lo,X
        STA temp
        LDA temp2
        SBC score_div_hi,X
        STA temp2
        INC temp3
        JMP @sub_lp
@done_one:
        LDA temp3
        STA score_digits,X
        INX
        CPX #$04
        BCC @dig_lp
        ; Last digit = remaining low byte
        LDA temp
        STA score_digits+4
        RTS


; =============================================
; draw_title_tms: paint the title splash on the VDP name table.
; =============================================
draw_title_tms:
        JSR clear_name_table

        ; "A1GALAGA" 8 chars - row 2 col 12 -> $184C
        LDA #<title_galaga
        STA sptr_lo
        LDA #>title_galaga
        STA sptr_hi
        LDA #$4C
        LDX #$58
        JSR draw_str_tms

        ; "APPLE-1 TMS9918" 15 chars - row 4 col 8 -> $1888
        LDA #<title_card_tms
        STA sptr_lo
        LDA #>title_card_tms
        STA sptr_hi
        LDA #$88
        LDX #$58
        JSR draw_str_tms

        ; "BY VERHILLE ARNAUD" - row 6 col 7 -> $18C7
        LDA #<title_author_tms
        STA sptr_lo
        LDA #>title_author_tms
        STA sptr_hi
        LDA #$C7
        LDX #$58
        JSR draw_str_tms

        ; --- Alien roster row (sprites painted by draw_title_sprites
        ; sit at TMS Y=88 = name-table rows 11-13). Names + HP go on
        ; rows 14-15, just below them.
        ;
        ; "SCOUT" centred under yellow sprite (X=48, cols 6-7).
        ; Row 14 col 4 -> $19C4
        LDA #$C4
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$59
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_S
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_C
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_O
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_U
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_T
        STA VDP_DATA

        ; "FIGHTER" centred under red sprite (X=120, cols 15-16).
        ; Row 14 col 12 -> $19CC
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$CC
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$59
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_F
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_I
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_G
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_H
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_T
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_E
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_R
        STA VDP_DATA

        ; "BOSS" centred under cyan sprite (X=192, cols 24-25).
        ; Row 14 col 22 -> $19D6
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$D6
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$59
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_B
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_O
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_S
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_S
        STA VDP_DATA

        ; HP labels under each name. Row 15 -> $19E0
        ; "2HP" col 5 -> $19E5
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$E5
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$59
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_D0+2
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_H
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_P
        STA VDP_DATA
        ; "4HP" col 14 -> $19EE
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$EE
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$59
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_D0+4
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_H
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_P
        STA VDP_DATA
        ; "6HP" col 23 -> $19F7
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$F7
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$59
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_D0+6
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_H
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_P
        STA VDP_DATA

        ; --- Keyboard layout prompt ---
        ; "SELECT KEYBOARD" - row 17 col 8 -> $1A28
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #<title_select_tms
        STA sptr_lo
        LDA #>title_select_tms
        STA sptr_hi
        LDA #$28
        LDX #$5A
        JSR draw_str_tms

        ; "1 QWERTY (A D S)" - row 19 col 8 -> $1A68
        LDA #<title_qwerty_tms
        STA sptr_lo
        LDA #>title_qwerty_tms
        STA sptr_hi
        LDA #$68
        LDX #$5A
        JSR draw_str_tms

        ; "2 AZERTY (Q D S)" - row 20 col 8 -> $1A88
        LDA #<title_azerty_tms
        STA sptr_lo
        LDA #>title_azerty_tms
        STA sptr_hi
        LDA #$88
        LDX #$5A
        JSR draw_str_tms

        ; "SPACE FIRE" - row 22 col 11 -> $1ACB
        LDA #<title_keys_tms
        STA sptr_lo
        LDA #>title_keys_tms
        STA sptr_hi
        LDA #$CB
        LDX #$5A
        JMP draw_str_tms


; =============================================
; draw_title_sprites: place the 3 alien sprites on the title screen.
; Slots 0..2 = scout/fighter/boss at Y=88, picking frame 0 or frame 1
; from anim_tick bit 4 so the title aliens pulse exactly like the
; in-game ones. Slot 3 terminates the chain. The first new_game call
; rewrites the SAT for gameplay so this state is harmless once we're
; in play_loop.
; =============================================
draw_title_sprites:
        JSR     tms9918_pad40   ; MANUAL caller-gap cushion (40c)
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$5B                ; $1B | $40 = SAT $1B00
        STA VDP_CTRL
        ; Slot 0: yellow scout at (48, 88)
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #88
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #48
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #P_ENEMY1
        JSR title_apply_anim
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #COL_E1
        STA VDP_DATA
        ; Slot 1: red fighter at (120, 88)
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #88
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #120
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #P_ENEMY2
        JSR title_apply_anim
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #COL_E2
        STA VDP_DATA
        ; Slot 2: cyan boss at (192, 88)
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #88
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #192
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #P_ENEMY3
        JSR title_apply_anim
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #COL_E3
        STA VDP_DATA
        ; Terminator
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #TERM_Y
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$00
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA VDP_DATA
        RTS


; title_apply_anim: A = base pattern code, returns A = base or base+40
; depending on anim_tick bit 4. Trashes temp.
title_apply_anim:
        STA temp
        LDA anim_tick
        AND #$10
        BEQ @same
        LDA temp
        CLC
        ADC #(P_ENEMY1_ALT - P_ENEMY1)
        RTS
@same:
        LDA temp
        RTS


; title_wait_key: blocking key wait that keeps the title aliens
; animating. Polls KBDCR, and between polls increments anim_tick and
; redraws the SAT after a short delay. Returns A = ASCII (bit 7
; cleared) once a key arrives.
title_wait_key:
@lp:
        LDA KBDCR
        BPL @no_kb
        LDA KBD
        AND #$7F
        RTS
@no_kb:
        INC anim_tick
        JSR draw_title_sprites
        ; Tight delay so anim_tick bit 4 toggles ~ every 0.4 s
        LDX #$10
@d1:    LDY #$00
@d2:    DEY
        BNE @d2
        DEX
        BNE @d1
        JMP @lp


; =============================================
; draw_help_tms / draw_help_sprites: title page 2.
;   - draw_help_tms paints the BONUS DROPS heading + 4 labels and an
;     "ANY KEY" hint into the name table (no animation).
;   - draw_help_sprites writes the 4 drop sprite icons in the SAT, in
;     a vertical column on the left so the text labels sit just to the
;     right of each pickup.
; =============================================
draw_help_tms:
        JSR clear_name_table

        ; "BONUS DROPS" - row 2 col 10 -> $184A
        LDA #<title_help_header
        STA sptr_lo
        LDA #>title_help_header
        STA sptr_hi
        LDA #$4A
        LDX #$58
        JSR draw_str_tms

        ; "DOUBLE SHOT" - row 5 col 9 -> $18A9
        LDA #<title_help_double
        STA sptr_lo
        LDA #>title_help_double
        STA sptr_hi
        LDA #$A9
        LDX #$58
        JSR draw_str_tms

        ; "TRIPLE SHOT" - row 8 col 9 -> $1909
        LDA #<title_help_triple
        STA sptr_lo
        LDA #>title_help_triple
        STA sptr_hi
        LDA #$09
        LDX #$59
        JSR draw_str_tms

        ; "SHIELD" - row 11 col 9 -> $1969
        LDA #<title_help_shield
        STA sptr_lo
        LDA #>title_help_shield
        STA sptr_hi
        LDA #$69
        LDX #$59
        JSR draw_str_tms

        ; "SKULL : - 1 LIFE" - row 14 col 8 -> $19C8
        LDA #<title_help_skull
        STA sptr_lo
        LDA #>title_help_skull
        STA sptr_hi
        LDA #$C8
        LDX #$59
        JSR draw_str_tms

        ; "BOMB : KILL ALL" - row 17 col 9 -> $1A29
        LDA #<title_help_bomb
        STA sptr_lo
        LDA #>title_help_bomb
        STA sptr_hi
        LDA #$29
        LDX #$5A
        JSR draw_str_tms

        ; "ANY KEY - START" - row 20 col 8 -> $1A88
        LDA #<title_help_anykey
        STA sptr_lo
        LDA #>title_help_anykey
        STA sptr_hi
        LDA #$88
        LDX #$5A
        JMP draw_str_tms        ; tail-call


draw_help_sprites:
        JSR     tms9918_pad40   ; MANUAL caller-gap cushion (40c)
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$5B
        STA VDP_CTRL
        ; Slot 0: DOUBLE icon at row 5 (Y=40)
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #40
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #40
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #P_BONUS_DOUBLE
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #COL_BD
        STA VDP_DATA
        ; Slot 1: TRIPLE row 8 (Y=64)
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #64
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #40
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #P_BONUS_TRIPLE
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #COL_BT
        STA VDP_DATA
        ; Slot 2: SHIELD row 11 (Y=88)
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #88
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #40
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #P_BONUS_SHIELD
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #COL_BS
        STA VDP_DATA
        ; Slot 3: SKULL row 14 (Y=112)
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #112
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #40
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #P_MALUS_SKULL
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #COL_MS
        STA VDP_DATA
        ; Slot 4: SMART BOMB row 17 (Y=136) -- starburst, dark green
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #136
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #40
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #P_EXP
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #12                 ; dark green (matches drop_color[4])
        STA VDP_DATA
        ; Terminator
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #TERM_Y
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$00
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA VDP_DATA
        RTS


; =============================================
; draw_wave_clear_tms: single splash combining congrats + score recap +
; next-wave announcement. Held for one 1.5 s pause then new_wave wipes
; it.
; =============================================
draw_wave_clear_tms:
        JSR clear_name_table

        ; "WAVE NN CLEAR" centred row 7 col 9 -> $18E9
        LDA #$E9
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$58
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_W
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_A
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_V
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_E
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_SP
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA wave
        JSR emit_2digit_tms
        LDA #C_SP
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_C
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_L
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_E
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_A
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_R
        STA VDP_DATA

        ; "SCORE NNNNN" row 10 col 10 -> $194A
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$4A
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$59
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_S
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_C
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_O
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_R
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_E
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_SP
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        JSR score_to_digits
        LDX #$00
@dlp:   LDA score_digits,X
        CLC
        ADC #C_D0
        STA VDP_DATA
        INX
        CPX #$05
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BCC @dlp

        ; "NEXT WAVE NN" row 14 col 10 -> $19CA
        LDA #$CA
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$59
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_N
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_E
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_X
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_T
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_SP
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_W
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_A
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_V
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_E
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_SP
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA wave
        CLC
        ADC #$01
        JSR emit_2digit_tms
        RTS


; =============================================
; pause_1500ms: ~1.5 s busy wait at 1 MHz. Trashes A, X, Y, temp.
; Three nested loops (5 outer x 256 mid x 256 inner).
; =============================================
pause_1500ms:
        LDA #$05
        STA temp
@l1:    LDX #$00
@l2:    LDY #$00
@l3:    DEY
        BNE @l3
        DEX
        BNE @l2
        DEC temp
        BNE @l1
        RTS


; emit_2digit_tms: A = number 0..99, write tens then ones digits via
; VDP_DATA. Trashes A, X, temp.
emit_2digit_tms:
        LDX #$00
@t10:   CMP #$0A
        BCC @t10d
        SBC #$0A
        INX
        JMP @t10
@t10d:  STA temp
        TXA
        CLC
        ADC #C_D0
        STA VDP_DATA            ; tens
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA temp
        CLC
        ADC #C_D0
        STA VDP_DATA            ; ones
        RTS


; =============================================
; draw_victory_tms: end-game splash. "VICTORY" + final score + key
; hint. Called from @victory after the player clears WIN_WAVE.
; =============================================
draw_victory_tms:
        JSR clear_name_table

        ; "VICTORY" - row 5 col 12 -> $18AC
        LDA #$AC
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$58
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_V
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_I
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_C
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_T
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_O
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_R
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_Y
        STA VDP_DATA

        ; "YOU WIN" - row 7 col 12 -> $18EC
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$EC
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$58
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_Y
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_O
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_U
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_SP
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_W
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_I
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_N
        STA VDP_DATA

        ; "FINAL SCORE NNNNN" - row 11 col 7 -> $1967
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$67
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$59
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_F
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_I
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_N
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_A
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_L
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_SP
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_S
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_C
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_O
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_R
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_E
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #C_SP
        STA VDP_DATA
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        JSR score_to_digits
        LDX #$00
@dlp:   LDA score_digits,X
        CLC
        ADC #C_D0
        STA VDP_DATA
        INX
        CPX #$05
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BCC @dlp

        ; "PRESS A KEY" centred row 17 col 10 -> $1A2A
        LDA #$2A
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$5A
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #<title_press_tms
        STA sptr_lo
        LDA #>title_press_tms
        STA sptr_hi
        LDY #$00
@plp:   LDA (sptr_lo),Y
        CMP #$FF
        BEQ @pdone
        STA VDP_DATA
        JSR tms9918_pad40       ; silicon-strict 40c (loop-back inner)
        INY
        JMP @plp
@pdone: RTS


draw_gameover_tms:
        JSR clear_name_table
        LDA #<title_over_tms
        STA sptr_lo
        LDA #>title_over_tms
        STA sptr_hi
        LDA #$4B                ; row 10 col 11
        LDX #$59
        JSR draw_str_tms
        LDA #<title_press_tms
        STA sptr_lo
        LDA #>title_press_tms
        STA sptr_hi
        LDA #$AA                ; row 13 col 10
        LDX #$59
        JMP draw_str_tms


clear_name_table:
        JSR     tms9918_pad40   ; MANUAL caller-gap cushion (40c)
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$58
        STA VDP_CTRL
        LDX #$03
        LDA #$00
@np:    LDY #$00
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
@nb:    STA VDP_DATA
        JSR tms9918_pad40       ; silicon-strict 40c (loop-back inner @nb)
        INY
        BNE @nb
        DEX
        BNE @np
        RTS


; =============================================
; upload_star_pattern: emit the 8 starfield dot bytes to VDP_DATA
; (caller has already programmed VDP_CTRL with the destination VRAM
; address). Used by init_vdp to clone the dot at chars 104/112/120.
; =============================================
upload_star_pattern:
        LDX #$00
@lp:    LDA star_pattern_bytes,X
        STA VDP_DATA
        INX
        CPX #$08
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BCC @lp
        RTS

star_pattern_bytes:
        .byte $00, $00, $00, $18, $18, $00, $00, $00


; =============================================
; plot_star: write `temp3` (char) into the name table at (temp = row,
; temp2 = col). Preserves X and Y. Trashes A only.
; =============================================
plot_star:
        LDA temp
        LSR
        LSR
        LSR                             ; row >> 3 (page component)
        PHA
        LDA temp
        AND #$07
        ASL
        ASL
        ASL
        ASL
        ASL                             ; (row & 7) << 5
        CLC
        ADC temp2                       ; + col -> low byte
        STA VDP_CTRL
        PLA
        ADC #$18                        ; + name-table base + carry
        ORA #$40                        ; write flag
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA temp3
        STA VDP_DATA
        RTS


; =============================================
; init_starfield: place 8 stars at random (col, row) and draw them.
; Star i (0..7) uses char C_STAR + (i & 3)*8 -- one of 4 colour groups
; (white / yellow / cyan / green) for visual variety.
; =============================================
init_starfield:
        LDX #$00
@lp:
        JSR prng16
        LDA prng_lo
        AND #$1F
        STA star_x,X
@y_retry:
        JSR prng16
        LDA prng_hi
        AND #$1F
        BEQ @y_retry
        CMP #23
        BCS @y_retry
        STA star_y,X
        ; star char = C_STAR + (X & 3) * 8
        TXA
        AND #$03
        ASL
        ASL
        ASL
        CLC
        ADC #C_STAR
        STA temp3
        LDA star_x,X
        STA temp2
        LDA star_y,X
        STA temp
        JSR plot_star
        INX
        CPX #$08
        BCC @lp
        RTS


; =============================================
; tick_starfield: scroll all 8 stars down by 1 row every 16 ticks. On
; reaching row 23, wrap to row 1 with a freshly randomized column.
; Erases each star's old cell and rewrites it at the new position.
; =============================================
tick_starfield:
        INC scroll_tick
        LDA scroll_tick
        CMP #$04                        ; ~80 ms between scroll steps (4x faster than initial)
        BCC @done
        LDA #$00
        STA scroll_tick
        LDX #$00
@lp:
        ; --- Erase old position (write 0) ---
        LDA #$00
        STA temp3
        LDA star_x,X
        STA temp2
        LDA star_y,X
        STA temp
        JSR plot_star
        ; --- Advance Y, wrap with a new random col ---
        INC star_y,X
        LDA star_y,X
        CMP #23
        BCC @no_wrap
        LDA #1
        STA star_y,X
        JSR prng16
        LDA prng_lo
        AND #$1F
        STA star_x,X
@no_wrap:
        ; --- Write new position ---
        TXA
        AND #$03
        ASL
        ASL
        ASL
        CLC
        ADC #C_STAR
        STA temp3
        LDA star_x,X
        STA temp2
        LDA star_y,X
        STA temp
        JSR plot_star
        INX
        CPX #$08
        BCC @lp
@done:
        RTS


draw_str_tms:
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STX VDP_CTRL
        LDY #$00
@lp:    LDA (sptr_lo),Y
        CMP #$FF
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BEQ @done
        STA VDP_DATA
        INY
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        JMP @lp
@done:  RTS


; =============================================
; init_vdp: program 8 VDP registers, upload sprite + HUD glyph patterns,
; load colour groups, clear name + sprite-attribute tables.
; =============================================
init_vdp:
        ; --- 8 VDP registers ---
        ; SILICON_STRICT_SKIP — register-loop hand-padded so it survives
        ; entry with R1 already display-ON (e.g. CodeTank re-launch from
        ; another game that left $C2 in R1). Intra-pair JSR pad40 clears
        ; the 40c hardened threshold; inter-iter JSR pad40 lifts the value
        ; byte of the next iteration over 40c too. Once X=1 commits R1=$80
        ; (display OFF), threshold drops to 2c for the rest of init —
        ; pads cost ~640 cycles total over the 8-iter loop, all during
        ; display-off init so user-visible cost is zero.
        LDX #$00
@regloop:
        LDA vdp_regs,X
        CPX #1
        BNE @reg_store
        AND #$BF                ; force R1 display=OFF for the loop pass
@reg_store:
        STA VDP_CTRL
        TXA
        ORA #$80
        STA VDP_CTRL
        INX
        CPX #$08
        BNE @regloop

        ; --- Upload sprite patterns at $3800 ---
        LDA #$00
        STA VDP_CTRL
        LDA #$78                ; $38 | $40
        STA VDP_CTRL

        ; 22 sprites x 32 bytes = 704 bytes total. Split into 3
        ; chunks because X is 8-bit (256+256+192).
        LDX #$00
@sp1:   LDA sprite_patterns,X
        STA VDP_DATA
        INX
        BNE @sp1                ; first 256 bytes
        LDX #$00
@sp2:   LDA sprite_patterns+256,X
        STA VDP_DATA
        INX
        BNE @sp2                ; next 256 bytes
        LDX #$00
@sp3:   LDA sprite_patterns+512,X
        STA VDP_DATA
        INX
        CPX #(704-512)          ; remaining 192 bytes
        BCC @sp3

        ; --- Upload HUD glyph patterns at VRAM $01C0 (chars 56..) ---
        LDA #$C0
        STA VDP_CTRL
        LDA #$41                ; $01 | $40
        STA VDP_CTRL
        LDX #$00
@hp1:   LDA hud_patterns,X
        STA VDP_DATA
        INX
        BNE @hp1                ; first 256 bytes
        LDX #$00
@hp2:   LDA hud_patterns+256,X
        STA VDP_DATA
        INX
        CPX #72                 ; 328-256 = 72 bytes (incl '-' and starfield dot)
        BCC @hp2

        ; --- Replicate the star pattern at chars 104, 112, 120 so the
        ; --- 4 starfield colour groups (12-15) all draw the same dot.
        LDA #$40                ; char 104 = VRAM $0340
        STA VDP_CTRL
        LDA #$43
        STA VDP_CTRL
        JSR upload_star_pattern
        LDA #$80                ; char 112 = VRAM $0380
        STA VDP_CTRL
        LDA #$43
        STA VDP_CTRL
        JSR upload_star_pattern
        LDA #$C0                ; char 120 = VRAM $03C0
        STA VDP_CTRL
        LDA #$43
        STA VDP_CTRL
        JSR upload_star_pattern

        ; --- Tile colour groups (chars 0-95): we only use the HUD set ---
        LDA #$00
        STA VDP_CTRL
        LDA #$60                ; $20 | $40
        STA VDP_CTRL
        LDX #$00
@cl:    LDA tile_colors,X
        STA VDP_DATA
        INX
        CPX #$10                ; 16 colour groups (incl. 4 star groups)
        BNE @cl

        ; --- Clear name table $1800 (768 B) ---
        LDA #$00
        STA VDP_CTRL
        LDA #$58
        STA VDP_CTRL
        LDX #$03
        LDA #$00
@np:    LDY #$00
@nb:    STA VDP_DATA
        INY
        BNE @nb
        DEX
        BNE @np

        ; --- Init sprite attribute table: only the chain terminator ---
        LDA #$00
        STA VDP_CTRL
        LDA #$5B                ; $1B | $40
        STA VDP_CTRL
        LDA #TERM_Y
        STA VDP_DATA
        LDA #$00
        STA VDP_DATA
        STA VDP_DATA
        STA VDP_DATA

        ; --- Final: re-arm R1 with the table value (display ON). Display
        ;     stays OFF until the cmd byte commits — threshold = 2c through
        ;     both STAs, no pad needed inline.
        LDA vdp_regs+1
        STA VDP_CTRL
        LDA #$81
        STA VDP_CTRL
        RTS


; =============================================
; delay_and_input: throttle the loop and slurp keys (non-blocking).
; Outer = SPEED constant (~25 ms / tick at default).
; =============================================
SPEED = 6
delay_and_input:
        LDX #SPEED
@outer:
        LDY #$00
@inner:
        LDA KBDCR
        BPL @nokey
        LDA KBD
        AND #$7F
        STA temp
        EOR prng_lo
        STA prng_lo
        LDA temp
        JSR handle_key
@nokey:
        DEY
        BNE @inner
        DEX
        BNE @outer
        RTS


handle_key:
        CMP key_left_code
        BEQ @left
        CMP key_right_code
        BEQ @right
        CMP key_stop_code
        BEQ @stop
        CMP key_fire_code
        BEQ @fire
        RTS
@left:
        LDA #$FF
        STA player_dir
        RTS
@right:
        LDA #$01
        STA player_dir
        RTS
@stop:
        ; S = full stop AND aim-shot (bypass cooldown)
        LDA #$00
        STA player_dir
        STA fire_cd
        LDA #$01
        STA pend_fire
        RTS
@fire:
        LDA #$01
        STA pend_fire
        RTS


; =============================================
; prng16 — promoted to dev/lib/m6502/prng.asm (Tier 2.2 mutualization).
; =============================================
.include "prng16.asm"


; =============================================
; wait_key / print_str_ax (Apple-1 helpers)
; =============================================
; wait_key / poll_key — promoted to dev/lib/apple1/kbd.asm.
; print_str_ax — promoted to dev/lib/apple1/print.asm.
.include "kbd.asm"
.include "print.asm"


; =============================================
; DATA
; =============================================

; --- TMS9918 Graphics I + 16x16 sprites ---
; R0=$00 mode, R1=$C2 (16K, display on, 16x16 sprites no magnify),
; R2=$06 name table $1800, R3=$80 colour table $2000, R4=$00 pattern
; table $0000, R5=$36 sprite attr $1B00, R6=$07 sprite pattern $3800,
; R7=$01 backdrop=black.
vdp_regs:
        .byte $00, $C2, $06, $80, $00, $36, $07, $01

; Per-enemy parallel arrays
enemy_hp_init:    .byte ENEMY1_HP, ENEMY2_HP, ENEMY3_HP
enemy_pat:        .byte P_ENEMY1,  P_ENEMY2,  P_ENEMY3
enemy_color:      .byte COL_E1,    COL_E2,    COL_E3
enemy_kill_score: .byte SCORE_E1_KILL, SCORE_E2_KILL, SCORE_E3_KILL
enemy_mask_bit:   .byte %001, %010, %100   ; bit-mask test for wave_masks
form_base_x:      .byte 56, 120, 184       ; centred 3-slot formation

; --- Per-enemy dive depth (max Y reached during descent) ---
; E1 (yellow scout) dives all the way past the bottom of the screen
; and reappears at its formation slot — that disappearance hides the
; teleport. E2/E3 peel off mid-screen and fly back UP to formation Y
; (see enemy_does_ascend below) so they don't visibly teleport from
; mid-screen back to the top.
enemy_dive_max_y:  .byte 200, 130, 100

; Per-enemy: 0 = teleport-from-bottom (E1), 1 = ascend back up (E2/E3)
enemy_does_ascend: .byte 0, 1, 1

; Drop sprite pattern + colour by drop_type (0..4)
;   0 double  1 triple  2 shield  3 skull  4 smart bomb (rare)
drop_pat:   .byte P_BONUS_DOUBLE, P_BONUS_TRIPLE, P_BONUS_SHIELD, P_MALUS_SKULL, P_EXP
drop_color: .byte COL_BD,         COL_BT,         COL_BS,         COL_MS,        12   ; smart bomb = dark green

; --- Wave progression table ---
; Bit 0 = enemy 0 (E1, 2HP), bit 1 = E2 (4HP), bit 2 = E3 (6HP).
;   Wave 1:    E1 alone     (warm-up)
;   Wave 2:    E2 alone
;   Wave 3:    E3 alone
;   Wave 4:    E1 + E2
;   Wave 5:    E1 + E3
;   Wave 6:    E2 + E3
;   Wave 7-10: all three     (climax act 1)
;   Wave 11:   single super boss
;   Wave 12-21: all three    (climax act 2 -- tables ramp into hard mode)
;   Wave 22:   twin super bosses (final showdown)
wave_masks:
        .byte %001, %010, %100, %011, %101, %110, %111      ; W1-7
        .byte %111, %111, %111, %111                         ; W8-11 (W11 = super1, mask unused)
        .byte %111, %111, %111, %111, %111, %111, %111      ; W12-18
        .byte %111, %111, %111, %111                         ; W19-22 (W22 = super2, mask unused)

; Pack 2.a difficulty ramp (indexed by min(wave-1, 20))
;   wave_dive_cd[]    : 240 -> 70  (4.8 s -> 1.4 s between dives)
;   wave_eb_speed[]   : 3 -> 7     (bullet speed in px/tick)
;   wave_form_ticks[] : 5 -> 1     (formation oscillation period)
;   Indices for boss waves (10 = W11, 21 = W22) carry dummy values --
;   the super-boss code path doesn't read them.
wave_dive_cd:    .byte 240, 230, 220, 210, 200, 190, 180   ; W1-7
                 .byte 170, 160, 150, 150                   ; W8-11
                 .byte 140, 130, 120, 110, 100,  90,  80   ; W12-18
                 .byte  80,  75,  70,  70                   ; W19-22
wave_eb_speed:   .byte   3,   3,   3,   3,   4,   4,   4   ; W1-7
                 .byte   4,   4,   5,   5                   ; W8-11
                 .byte   5,   5,   6,   6,   6,   6,   7   ; W12-18
                 .byte   7,   7,   7,   7                   ; W19-22
wave_form_ticks: .byte   5,   5,   4,   4,   3,   3,   3   ; W1-7
                 .byte   3,   2,   2,   2                   ; W8-11
                 .byte   2,   2,   2,   2,   1,   1,   1   ; W12-18
                 .byte   1,   1,   1,   1                   ; W19-22

; 5 BCD divisors for score_to_digits
score_div_lo: .byte $10, $E8, $64, $0A
score_div_hi: .byte $27, $03, $00, $00
; 10000=$2710  1000=$03E8  100=$0064  10=$000A
; (score_digits scratch moved to .zeropage above so writes land in RAM
;  on the CodeTank ROM build -- otherwise the HUD freezes at 0.)

; --- Sprite pattern data (6 sprites * 32 bytes = 192 bytes) ---
; Each sprite: TL(8) BL(8) TR(8) BR(8) -- the TMS 16x16 quadrant order.
sprite_patterns:
; Pattern 0..3: PLAYER SHIP (yellow delta with engine vents) ---
        ; TL (rows 0-7, cols 0-7)
        .byte $01, $03, $03, $07, $07, $0F, $0F, $1F
        ; BL (rows 8-15, cols 0-7)
        .byte $3F, $7F, $FF, $E7, $C3, $81, $00, $00
        ; TR (rows 0-7, cols 8-15)
        .byte $80, $C0, $C0, $E0, $E0, $F0, $F0, $F8
        ; BR (rows 8-15, cols 8-15)
        .byte $FC, $FE, $FF, $E7, $C3, $81, $00, $00

; Pattern 4..7: ENEMY 1 - flying saucer, 2HP ---
        ; TL
        .byte $00, $00, $00, $03, $07, $0E, $1F, $17
        ; BL
        .byte $0F, $07, $02, $00, $00, $00, $00, $00
        ; TR
        .byte $00, $00, $00, $C0, $E0, $70, $F8, $E8
        ; BR
        .byte $F0, $E0, $40, $00, $00, $00, $00, $00

; Pattern 8..11: ENEMY 2 - red fighter, 4HP ---
        ; TL
        .byte $00, $03, $07, $0E, $1F, $3F, $7F, $DF
        ; BL
        .byte $A7, $3F, $14, $00, $00, $00, $00, $00
        ; TR
        .byte $00, $C0, $E0, $70, $F8, $FC, $FE, $FB
        ; BR
        .byte $E5, $FC, $28, $00, $00, $00, $00, $00

; Pattern 12..15: ENEMY 3 - cyan boss, 6HP ---
        ; TL
        .byte $11, $3F, $7E, $FF, $C3, $7E, $7F, $3F
        ; BL
        .byte $1F, $0E, $0E, $14, $22, $00, $00, $00
        ; TR
        .byte $88, $FC, $7E, $FF, $C3, $7E, $FE, $FC
        ; BR
        .byte $F8, $70, $70, $28, $44, $00, $00, $00

; Pattern 16..19: PLAYER BULLET (vertical white pulse, 4 px tall, centred) ---
        ; TL
        .byte $00, $00, $00, $00, $00, $01, $01, $01
        ; BL
        .byte $01, $01, $01, $00, $00, $00, $00, $00
        ; TR
        .byte $00, $00, $00, $00, $00, $80, $80, $80
        ; BR
        .byte $80, $80, $80, $00, $00, $00, $00, $00

; Pattern 20..23: ENEMY BULLET (small dark-red diamond, centred) ---
        ; TL
        .byte $00, $00, $00, $00, $01, $03, $07, $0F
        ; BL
        .byte $07, $03, $01, $00, $00, $00, $00, $00
        ; TR
        .byte $00, $00, $00, $00, $80, $C0, $E0, $F0
        ; BR
        .byte $E0, $C0, $80, $00, $00, $00, $00, $00

; --- Pattern 28..31 BONUS DOUBLE: two thick vertical bars (II) ---
;   Two 2-px wide bars at cols 4-5 and 10-11, all 16 rows.
;   byte 0 = $0C (cols 4,5);  byte 1 = $30 (cols 10,11)
; --- Pattern 32..35 BONUS TRIPLE: three thin bars (III) ---
;   Bars at cols 2-3, 7-8, 12-13.
;   byte 0 = cols 2,3 + col 7 = $30 | $01 = $31
;   byte 1 = col 8 + cols 12,13 = $80 | $0C = $8C
; --- Pattern 36..39 BONUS SHIELD: pointed shield outline ---
;   16x16 hollow shield -- see byte rows below.
; --- Pattern 40..43 MALUS SKULL: skull silhouette with eye sockets ---
;   16x16 -- see byte rows below.

; Pattern 24..27: EXPLOSION (8-point starburst with filled core) ---
        ; TL (rows 0-7, cols 0-7)
        .byte $01, $01, $21, $11, $0B, $07, $E7, $FF
        ; BL (rows 8-15, cols 0-7)
        .byte $FF, $E7, $07, $0B, $11, $21, $01, $01
        ; TR (rows 0-7, cols 8-15)
        .byte $80, $80, $84, $88, $D0, $E0, $E7, $FF
        ; BR (rows 8-15, cols 8-15)
        .byte $FF, $E7, $E0, $D0, $88, $84, $80, $80

; --- Bonus / malus drops are smaller now: visible content only on
; rows 4..11, transparent border. Looks like little pickups instead
; of fill-the-screen sprites. ---

; Pattern 28..31 BONUS DOUBLE (two short vertical bars, rows 4..11) ---
        ; TL
        .byte $00, $00, $00, $00, $0C, $0C, $0C, $0C
        ; BL
        .byte $0C, $0C, $0C, $0C, $00, $00, $00, $00
        ; TR
        .byte $00, $00, $00, $00, $30, $30, $30, $30
        ; BR
        .byte $30, $30, $30, $30, $00, $00, $00, $00

; Pattern 32..35 BONUS TRIPLE (three short vertical bars, rows 4..11) ---
        ; TL
        .byte $00, $00, $00, $00, $31, $31, $31, $31
        ; BL
        .byte $31, $31, $31, $31, $00, $00, $00, $00
        ; TR
        .byte $00, $00, $00, $00, $8C, $8C, $8C, $8C
        ; BR
        .byte $8C, $8C, $8C, $8C, $00, $00, $00, $00

; Pattern 36..39 BONUS SHIELD (small octagon outline, rows 4..11) ---
;   ......XXXX......
;   ....XX....XX....
;   ...X........X...
;   ...X........X...
;   ...X........X...
;   ...X........X...
;   ....XX....XX....
;   ......XXXX......
        ; TL
        .byte $00, $00, $00, $00, $03, $0C, $10, $10
        ; BL
        .byte $10, $10, $0C, $03, $00, $00, $00, $00
        ; TR
        .byte $00, $00, $00, $00, $C0, $30, $08, $08
        ; BR
        .byte $08, $08, $30, $C0, $00, $00, $00, $00

; Pattern 44..47 ENEMY1 ALT  -- saucer with antennas swapped ---
; Pattern 48..51 ENEMY2 ALT  -- fighter with engine vents flared ---
; Pattern 52..55 ENEMY3 ALT  -- boss with mouth opened ---
; Pattern 56..59 POPUP       -- 16x8 upward arrow used as kill marker ---
; (Definitions appended after MALUS SKULL below.)

; Pattern 40..43 MALUS SKULL (compact skull silhouette, rows 4..11) ---
;   ....XXXXXXXX....
;   ...XXXXXXXXXX...
;   ...XX..XX..XX...
;   ...XX..XX..XX...
;   ...XXXXXXXXXX...
;   ....XXXXXXXX....
;   .....X.XX.X.....
;   ......XXXX......
        ; TL
        .byte $00, $00, $00, $00, $0F, $1F, $19, $19
        ; BL
        .byte $1F, $0F, $05, $03, $00, $00, $00, $00
        ; TR
        .byte $00, $00, $00, $00, $F0, $F8, $98, $98
        ; BR
        .byte $F8, $F0, $A0, $C0, $00, $00, $00, $00

; --- Pattern 44..47 ENEMY1 ALT (saucer, alt frame) ---
;   Same body, antennas tilted the other way + dome blink
        ; TL
        .byte $00, $00, $03, $07, $0E, $1F, $1F, $0F
        ; BL
        .byte $07, $03, $02, $00, $00, $00, $00, $00
        ; TR
        .byte $00, $00, $E0, $70, $30, $F8, $F8, $F0
        ; BR
        .byte $E0, $C0, $40, $00, $00, $00, $00, $00

; --- Pattern 48..51 ENEMY2 ALT (fighter, alt frame) ---
;   Engine vents flared open
        ; TL
        .byte $00, $03, $07, $1E, $3F, $7F, $7F, $DD
        ; BL
        .byte $A5, $7E, $24, $00, $00, $00, $00, $00
        ; TR
        .byte $00, $C0, $E0, $78, $FC, $FE, $FE, $BB
        ; BR
        .byte $A5, $7E, $24, $00, $00, $00, $00, $00

; --- Pattern 52..55 ENEMY3 ALT (boss, alt frame) ---
;   Mouth opened, eye-stalks moved
        ; TL
        .byte $11, $3F, $7F, $FF, $E1, $7F, $3F, $7F
        ; BL
        .byte $0E, $1F, $1F, $0E, $14, $00, $00, $00
        ; TR
        .byte $88, $FC, $FE, $FF, $87, $FE, $FC, $FE
        ; BR
        .byte $70, $F8, $F8, $70, $28, $00, $00, $00

; --- Pattern 56..59 POPUP (small upward arrow, used as kill marker) ---
;   .......XX.......
;   ......XXXX......
;   .....XXXXXX.....
;   ....XX.XX.XX....
;   ...XX..XX..XX...
;   ......XXXX......
;   ......XXXX......
;   ......XXXX......
;   (rows 8-15 blank)
        ; TL
        .byte $01, $03, $07, $0D, $19, $03, $03, $03
        ; BL
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        ; TR
        .byte $80, $C0, $E0, $B0, $98, $C0, $C0, $C0
        ; BR
        .byte $00, $00, $00, $00, $00, $00, $00, $00

; --- Pattern 60..63 SHIELD RING (hollow circle drawn around the ship) ---
;   ....XXXXXXXX....
;   ..XX........XX..
;   .X............X.
;   X..............X
;   X..............X
;   X..............X
;   X..............X
;   X..............X
;   X..............X
;   X..............X
;   X..............X
;   X..............X
;   X..............X
;   .X............X.
;   ..XX........XX..
;   ....XXXXXXXX....
        ; TL
        .byte $0F, $30, $40, $80, $80, $80, $80, $80
        ; BL
        .byte $80, $80, $80, $80, $80, $40, $30, $0F
        ; TR
        .byte $F0, $0C, $02, $01, $01, $01, $01, $01
        ; BR
        .byte $01, $01, $01, $01, $01, $02, $0C, $F0

; --- Pattern 64..67 EXP_ALT (smaller burst, fade frame) ---
;   Same starburst silhouette as P_EXP but stripped down so the
;   explosion clearly shrinks during its second half (t < 8).
;   ......XXXX......
;   .....X....X.....
;   ....X......X....
;   ....X......X....
;   ....X......X....
;   ....X......X....
;   .....X....X.....
;   ......XXXX......
        ; TL
        .byte $00, $00, $00, $00, $03, $04, $08, $08
        ; BL
        .byte $08, $08, $04, $03, $00, $00, $00, $00
        ; TR
        .byte $00, $00, $00, $00, $C0, $20, $10, $10
        ; BR
        .byte $10, $10, $20, $C0, $00, $00, $00, $00

; --- Pattern 68..71 PLAYER WITH THRUST (extra engine flame at bottom) ---
;   Same delta as the regular ship but rows 14-15 have a small two-
;   spot exhaust plume so when the ship moves it looks "thrusted".
;   .......XX.......
;   ......XXXX......
;   ......XXXX......
;   .....XXXXXX.....
;   .....XXXXXX.....
;   ....XXXXXXXX....
;   ....XXXXXXXX....
;   ...XXXXXXXXXX...
;   ..XXXXXXXXXXXX..
;   .XXXXXXXXXXXXXX.
;   XXXXXXXXXXXXXXXX
;   XXX..XXXXXX..XXX
;   XX....XXXX....XX
;   X......XX......X
;   ......X..X......        <- new flame
;   .......XX.......        <- new flame tip
        ; TL
        .byte $01, $03, $03, $07, $07, $0F, $0F, $1F
        ; BL
        .byte $3F, $7F, $FF, $E7, $C3, $81, $02, $01
        ; TR
        .byte $80, $C0, $C0, $E0, $E0, $F0, $F0, $F8
        ; BR
        .byte $FC, $FE, $FF, $E7, $C3, $81, $40, $80

; --- SUPER BOSS bitmap reference (32x32, mechanical Terminator skull) -
;   row 00 ........################........  rounded cranium top
;   row 01 .....######################.....
;   row 02 ...##########################...
;   row 03 ..############################..
;   row 04 .##############################.
;   row 05 ################################
;   row 06 ################################
;   row 07 #######.....########.....#######  deep eye sockets cols 7-11/20-24
;   row 08 #######.....########.....#######
;   row 09 #######.....########.....#######
;   row 10 #######..##..######..##..#######  red pupils cols 9-10/21-22
;   row 11 #######..##..######..##..#######
;   row 12 #######.....########.....#######  eye sockets close
;   row 13 ########...##########...########  socket transitions
;   row 14 ################################  cheekbones solid
;   row 15 ################################
;   row 16 ##############...###############  triangular nose start
;   row 17 #############.....##############
;   row 18 #############.....##############
;   row 19 ##############...###############
;   row 20 ############.........###########  mouth opens
;   row 21 ###########.#.#.#.#.#.##########  alternating teeth
;   row 22 ###########.#.#.#.#.#.##########
;   row 23 ############.........###########
;   row 24 ################################  jaw line
;   row 25 ################################
;   row 26 .##############################.  jaw narrows
;   row 27 ..############################..
;   row 28 ....########################....
;   row 29 ......####################......
;   row 30 ........################........
;   row 31 ............########............  pointed chin

; --- Pattern 72..75 SUPER BOSS TL (top-left 16x16 of 32x32 super sprite) ---
        ; TL  (cols 0-7, rows 0-7)
        .byte $00, $07, $1F, $3F, $7F, $FF, $FF, $FE
        ; BL  (cols 0-7, rows 8-15)
        .byte $FE, $FE, $FE, $FE, $FE, $FF, $FF, $FF
        ; TR  (cols 8-15, rows 0-7)
        .byte $FF, $FF, $FF, $FF, $FF, $FF, $FF, $0F
        ; BR  (cols 8-15, rows 8-15)
        .byte $0F, $0F, $67, $67, $0F, $1F, $FF, $FF

; --- Pattern 76..79 SUPER BOSS TR (top-right 16x16) ---
        ; TL  (cols 16-23, rows 0-7)
        .byte $FF, $FF, $FF, $FF, $FF, $FF, $FF, $F0
        ; BL  (cols 16-23, rows 8-15)
        .byte $F0, $F0, $E6, $E6, $F0, $F8, $FF, $FF
        ; TR  (cols 24-31, rows 0-7)
        .byte $00, $E0, $F8, $FC, $FE, $FF, $FF, $7F
        ; BR  (cols 24-31, rows 8-15)
        .byte $7F, $7F, $7F, $7F, $7F, $FF, $FF, $FF

; --- Pattern 80..83 SUPER BOSS BL (bottom-left 16x16) ---
        ; TL  (cols 0-7, rows 16-23)
        .byte $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF
        ; BL  (cols 0-7, rows 24-31)
        .byte $FF, $FF, $7F, $3F, $0F, $03, $00, $00
        ; TR  (cols 8-15, rows 16-23)
        .byte $FC, $F8, $F8, $FC, $F0, $EA, $EA, $F0
        ; BR  (cols 8-15, rows 24-31)
        .byte $FF, $FF, $FF, $FF, $FF, $FF, $FF, $0F

; --- Pattern 84..87 SUPER BOSS BR (bottom-right 16x16) ---
        ; TL  (cols 16-23, rows 16-23)
        .byte $7F, $3F, $3F, $7F, $07, $AB, $AB, $07
        ; BL  (cols 16-23, rows 24-31)
        .byte $FF, $FF, $FF, $FF, $FF, $FF, $FF, $F0
        ; TR  (cols 24-31, rows 16-23)
        .byte $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF
        ; BR  (cols 24-31, rows 24-31)
        .byte $FF, $FF, $FE, $FC, $F0, $C0, $00, $00


; =============================================
; HUD glyph char map (lifted from TMS_Snake / TMS_Sokoban):
;   56=M  57=V  58=:  59..68='0'..'9'  69=S  70=O  71=K  72=B  73=A
;   74=N  75=P  76=R  77=E  78=L  79=space  80=G  81=H  82=T  83=D
;   84=Y  85=I  86=U  87=Q  88=W  89=Z  90=C  91=X  92=F  93=(  94=)
; =============================================
C_M   = 56
C_V   = 57
C_CL  = 58
C_D0  = 59
C_S   = 69
C_O   = 70
C_K   = 71
C_B   = 72
C_A   = 73
C_N   = 74
C_P   = 75
C_R   = 76
C_E   = 77
C_L   = 78
C_SP  = 79
C_G   = 80
C_H   = 81
C_T   = 82
C_D   = 83
C_Y   = 84
C_I   = 85
C_U   = 86
C_Q   = 87
C_W   = 88
C_Z   = 89
C_C   = 90
C_X   = 91
C_F   = 92
C_LP  = 93
C_RP  = 94
C_DASH = 95
C_STAR = 96             ; small dot used by the scrolling starfield

; --- Tile colour groups: only HUD glyphs are used ---
tile_colors:
        .byte $11       ; group 0 unused
        .byte $11       ; group 1 unused
        .byte $11       ; group 2
        .byte $11       ; group 3
        .byte $11       ; group 4
        .byte $11       ; group 5
        .byte $11       ; group 6
        .byte $F1       ; group 7  chars 56..63  white on black
        .byte $F1       ; group 8  chars 64..71
        .byte $F1       ; group 9  chars 72..79
        .byte $F1       ; group 10 chars 80..87
        .byte $F1       ; group 11 chars 88..95
        .byte $F1       ; group 12 chars 96..103 starfield (white)
        .byte $B1       ; group 13 chars 104..111 starfield (light yellow)
        .byte $71       ; group 14 chars 112..119 starfield (cyan)
        .byte $31       ; group 15 chars 120..127 starfield (light green)

; --- HUD + title glyph patterns (8x8, uploaded to VRAM $01C0) ---
hud_patterns:
        ; 56 'M'
        .byte $44, $6C, $54, $44, $44, $44, $44, $00
        ; 57 'V'
        .byte $44, $44, $44, $44, $28, $28, $10, $00
        ; 58 ':'
        .byte $00, $00, $10, $00, $00, $10, $00, $00
        ; 59 '0'
        .byte $38, $44, $44, $44, $44, $44, $38, $00
        ; 60 '1'
        .byte $10, $30, $10, $10, $10, $10, $38, $00
        ; 61 '2'
        .byte $38, $44, $04, $08, $10, $20, $7C, $00
        ; 62 '3'
        .byte $38, $44, $04, $38, $04, $44, $38, $00
        ; 63 '4'
        .byte $44, $44, $44, $7C, $04, $04, $04, $00
        ; 64 '5'
        .byte $7C, $40, $40, $78, $04, $44, $38, $00
        ; 65 '6'
        .byte $18, $20, $40, $78, $44, $44, $38, $00
        ; 66 '7'
        .byte $7C, $04, $08, $10, $20, $20, $20, $00
        ; 67 '8'
        .byte $38, $44, $44, $38, $44, $44, $38, $00
        ; 68 '9'
        .byte $38, $44, $44, $3C, $04, $08, $30, $00
        ; 69 'S'
        .byte $38, $44, $40, $38, $04, $44, $38, $00
        ; 70 'O'
        .byte $38, $44, $44, $44, $44, $44, $38, $00
        ; 71 'K'
        .byte $44, $48, $50, $60, $50, $48, $44, $00
        ; 72 'B'
        .byte $78, $44, $44, $78, $44, $44, $78, $00
        ; 73 'A'
        .byte $38, $44, $44, $7C, $44, $44, $44, $00
        ; 74 'N'
        .byte $44, $64, $54, $4C, $44, $44, $44, $00
        ; 75 'P'
        .byte $78, $44, $44, $78, $40, $40, $40, $00
        ; 76 'R'
        .byte $78, $44, $44, $78, $50, $48, $44, $00
        ; 77 'E'
        .byte $7C, $40, $40, $78, $40, $40, $7C, $00
        ; 78 'L'
        .byte $40, $40, $40, $40, $40, $40, $7C, $00
        ; 79 ' '
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        ; 80 'G'
        .byte $38, $44, $40, $4E, $44, $44, $38, $00
        ; 81 'H'
        .byte $44, $44, $44, $7C, $44, $44, $44, $00
        ; 82 'T'
        .byte $7C, $10, $10, $10, $10, $10, $10, $00
        ; 83 'D'
        .byte $78, $44, $44, $44, $44, $44, $78, $00
        ; 84 'Y'
        .byte $44, $44, $28, $10, $10, $10, $10, $00
        ; 85 'I'
        .byte $38, $10, $10, $10, $10, $10, $38, $00
        ; 86 'U'
        .byte $44, $44, $44, $44, $44, $44, $38, $00
        ; 87 'Q'
        .byte $38, $44, $44, $44, $54, $48, $34, $00
        ; 88 'W'
        .byte $44, $44, $44, $54, $54, $6C, $44, $00
        ; 89 'Z'
        .byte $7C, $04, $08, $10, $20, $40, $7C, $00
        ; 90 'C'
        .byte $38, $44, $40, $40, $40, $44, $38, $00
        ; 91 'X'
        .byte $44, $44, $28, $10, $28, $44, $44, $00
        ; 92 'F'
        .byte $7C, $40, $40, $78, $40, $40, $40, $00
        ; 93 '('
        .byte $08, $10, $20, $20, $20, $10, $08, $00
        ; 94 ')'
        .byte $20, $10, $08, $08, $08, $10, $20, $00
        ; 95 '-'
        .byte $00, $00, $00, $7C, $00, $00, $00, $00
        ; 96 small dot (starfield)
        .byte $00, $00, $00, $18, $18, $00, $00, $00


; --- TMS title strings (raw TMS char codes, $FF terminated) ---
; Char map (lifted from snake/sokoban):
;   56=M 57=V 58=: 59..68='0'..'9' 69=S 70=O 71=K 72=B 73=A 74=N
;   75=P 76=R 77=E 78=L 79=space 80=G 81=H 82=T 83=D 84=Y 85=I
;   86=U 87=Q 88=W 89=Z 90=C 91=X 92=F 93=( 94=)
title_galaga:
        ; A 1 G A L A G A   (8 chars)
        .byte 73,60,80,73,78,73,80,73, $FF
title_card_tms:
        ; A P P L E - 1 _ T M S 9 9 1 8   (15 chars)
        .byte 73,75,75,78,77,95,60,79,82,56,69,68,68,60,67, $FF
title_author_tms:
        ; B Y _ V E R H I L L E _ A R N A U D
        .byte 72,84,79,57,77,76,81,85,78,78,77,79,73,76,74,73,86,83, $FF
title_hp_tms:
        ; H P _ 2 _ 4 _ 6
        .byte 81,75,79,61,79,63,79,65, $FF
title_select_tms:
        ; S E L E C T _ K E Y B O A R D
        .byte 69,77,78,77,90,82,79,71,77,84,72,70,73,76,83, $FF
title_qwerty_tms:
        ; 1 _ Q W E R T Y _ ( A _ D _ S )
        .byte 60,79,87,88,77,76,82,84,79,93,73,79,83,79,69,94, $FF
title_azerty_tms:
        ; 2 _ A Z E R T Y _ ( Q _ D _ S )
        .byte 61,79,73,89,77,76,82,84,79,93,87,79,83,79,69,94, $FF
title_keys_tms:
        ; S P A C E _ F I R E
        .byte 69,75,73,90,77,79,92,85,76,77, $FF
title_over_tms:
        ; G A M E _ O V E R
        .byte 80,73,56,77,79,70,57,77,76, $FF
title_press_tms:
        ; P R E S S _ A _ K E Y
        .byte 75,76,77,69,69,79,73,79,71,77,84, $FF
title_bonus_tms:
        ; +  1 0 0  _  B O N U S       (no '+' glyph -> use ':')
        ; Approximate "  100 BONUS"
        .byte 79,60,59,59,79,72,70,74,86,69, $FF

; --- Title page 2: bonus drops help screen ---
title_help_header:
        ; B O N U S _ D R O P S
        .byte 72,70,74,86,69,79,83,76,70,75,69, $FF
title_help_double:
        ; D O U B L E _ S H O T
        .byte 83,70,86,72,78,77,79,69,81,70,82, $FF
title_help_triple:
        ; T R I P L E _ S H O T
        .byte 82,76,85,75,78,77,79,69,81,70,82, $FF
title_help_shield:
        ; S H I E L D
        .byte 69,81,85,77,78,83, $FF
title_help_skull:
        ; S K U L L _ : _ - _ 1 _ L I F E
        .byte 69,71,86,78,78,79,58,79,95,79,60,79,78,85,92,77, $FF
title_help_anykey:
        ; A N Y _ K E Y _ - _ S T A R T
        .byte 73,74,84,79,71,77,84,79,95,79,69,82,73,76,82, $FF
title_help_bomb:
        ; B O M B _ : _ K I L L _ A L L
        .byte 72,70,56,72,79,58,79,71,85,78,78,79,73,78,78, $FF


; --- Apple-1 text strings (ASCII; print_str_ax sets bit 7) ---
str_title:
        .byte $0D, " A1GALAGA  V.ARNAUD 2026", $0D, 0

str_layout:
        .byte $0D, " 1 QWERTY  2 AZERTY", $0D, 0

str_over:
        .byte $0D, " GAME OVER -- KEY", $0D, 0

str_wave:
        .byte $0D, " WAVE CLEARED  +100", $0D, 0

str_win:
        .byte $0D, " *** VICTORY! GALAXY SAVED ***", $0D
        .byte " KEY=TITLE", $0D, 0
