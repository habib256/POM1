; =============================================
; GEN2 HGR port — HGR_Maze3D, ported from sketchs/tms9918/game_maze3d/
; TMS_Maze3D.asm (the CodeTank bank build). The game logic — DFS maze,
; pseudo-3D wireframe renderer, map view, HUD, turn-based combat — is
; carried over verbatim; only the Graphics II bitmap PRIMITIVES
; (plot/line/hline/vline/char/x2-text/sprite-blit/clears) are swapped
; for HGR equivalents with the same contracts. Single-region image at
; $6000 on the "GEN2 HGR Color" 48 KB machine (chess model), state in
; low RAM ($0E00 segments), 6000R boots it.
;
; Pixel-space mapping: the TMS 256x192 bitmap becomes 32 HGR byte
; columns (4..35, centred in the 40-byte row). Each 8-px TMS byte
; column lands on one 7-px HGR byte column: bytes go through rev7_tab
; (bit 7 = leftmost -> bit 0 = leftmost, rightmost pixel dropped);
; per-pixel plots clamp x%8 == 7 onto bit 6 so wall edges on those
; columns still render. Text cells / sprite tiles stay 8-aligned in
; TMS coordinates and byte-aligned in HGR — no shifting anywhere.
; The Graphics II per-tile colour table has no HGR counterpart:
; color_rect / fill_color_white are stubs (monochrome; the depth cues
; survive via the stipple fills).
; =============================================
; HGR MAZE 3D - Wizardry-style line maze
; Uncle Bernie's GEN2 HGR Color Card - POM1 / Apple 1
; VERHILLE Arnaud - 2026
;
; A 1976-style first-person dungeon crawler with monsters.
; Backtracker-DFS maze (11x7 cells), pseudo-3D wireframe with
; depth shading (stipple/hatching), top-down map toggle and
; turn-based combat against three monster archetypes.
;
; Controls (IJKL — same physical keys on QWERTY and AZERTY):
;   I = forward          J = turn left
;   K = backward         L = turn right
;   M = toggle map / 3D view
;   A = attack (combat)  F = flee  (combat)
;   ESC = quit
;
; =============================================
; Assemble: `make` in this directory (ca65 + sprites libs + ld65 with
; apple1_maze3d_hgr.cfg -> "software/Graphic HGR"/HGR_Maze3D.{bin,txt}).
;
; Run in POM1: --preset 11 --load 6000:"software/Graphic HGR/HGR_Maze3D.bin"
; --run 6000, or File > Load the .txt (auto-enables the GEN2 card) then
; 6000R from Wozmon.
; =============================================

; ---- Apple 1 I/O ----
        ; HGR port: tms9918_pad12 / vdp_display_off / vdp_display_on are
        ; local no-op stubs (next to the clear routines) — the GEN2 card
        ; has no VDP bus timing and no display-enable bit.
        ; SCROLL-O-SPRITES 16x16 monster patterns (dev/lib/tms9918/sprites_*.asm,
        ; linked via the Makefile's EXTRA_ASM) — drawn as BITMAPS into the
        ; Graphics II pattern table by draw_sprite16_x2/_x4, NOT as hardware
        ; sprites. Layout: 32 bytes = left column rows 0..15, right column
        ; rows 0..15; bit 7 = leftmost (same bit order as the bitmap).
        ; Re-cast juillet 2026 after the community-name audit (the old picks,
        ; made under the pre-audit labels, were actually a Golem, a Hobgoblin
        ; and a FEMALE ARCHER): now the archetypes wear their true faces.
        .import troll_goblin_pat        ; GOBLIN    (sprites_trollkind.asm 01/4)
        .import troll_orc_pat           ; ORC       (sprites_trollkind.asm 04/4)
        .import char_necromancer_m_pat  ; DARK MAGE (sprites_characters.asm 31/33)
.include "apple1.inc"

; ---- GEN2 HGR soft-switch equates (READ-only switches!) ----
.include "gen2.inc"

; The TMS build VBlank-gated a couple of rebuild bursts; no such
; concept on the framebuffer card.
.macro WAIT_VBLANK_SAFE
.endmacro

; ---- Keys (Apple 1 ASCII | $80, upper-cased by the keyboard) ----
KEY_ESC   = $9B
KEY_FWD   = $C9       ; 'I' forward
KEY_BACK  = $CB       ; 'K' backward
KEY_LEFT  = $CA       ; 'J' turn left
KEY_RIGHT = $CC       ; 'L' turn right
KEY_M     = $CD       ; toggle map / 3D
KEY_H     = $C8       ; help screen (in game)
KEY_A     = $C1       ; attack
KEY_F     = $C6       ; flee
KEY_SPACE = $A0
KEY_RET   = $8D

; ---- Maze geometry ----
NCOLS   = 11
NROWS   = 7
NCELLS  = 77

NORTH_BIT = $01       ; bit 0 of cell = north passage open
EAST_BIT  = $02       ; bit 1 = east passage open
VISITED   = $80       ; bit 7 = DFS visited flag

; ---- Direction codes ----
DIR_N = 0
DIR_E = 1
DIR_S = 2
DIR_W = 3

; ---- Game states ----
ST_TITLE  = 0
ST_HELP   = 1
ST_PLAY3D = 2
ST_PLAYMAP= 3
ST_COMBAT = 4
ST_WIN    = 5
ST_LOSE   = 6
ST_QUIT   = $FF

; ---- Combat ----
NUM_MOBS  = 8
MOB_DEAD  = $FF
MAX_DEPTH = 4

; ---- Player tuning ----
PLAYER_MAX_HP = 20

; =============================================
; Off-board RAM (bss, not in output binary)
; =============================================
.segment "GRIDSEG"
grid:       .res NCELLS

.segment "STKSEG"
dfs_stk:    .res NCELLS

.segment "MOBSEG"
mob_col:    .res NUM_MOBS
mob_row:    .res NUM_MOBS
mob_type:   .res NUM_MOBS    ; 0=goblin 1=orc 2=mage 3=dragon ; $FF dead
mob_hp:     .res NUM_MOBS

; =============================================
; Zero page
; =============================================
.zeropage
; --- generic scratch ---
tmp:        .res 1     ; $00
tmp2:       .res 1     ; $01
ptr_lo:     .res 1     ; $02
ptr_hi:     .res 1     ; $03
str_lo:     .res 1     ; $04
str_hi:     .res 1     ; $05

; --- PRNG (Galois LFSR + entropy) ---
prng_lo:    .res 1     ; $06
prng_hi:    .res 1     ; $07

; --- VDP helpers ---
vdp_addr_lo:.res 1     ; $08
vdp_addr_hi:.res 1     ; $09

; --- pixel-plot scratch ---
pix_x:      .res 1     ; $0A
pix_y:      .res 1     ; $0B
pix_byte:   .res 1     ; $0C  cached byte under cursor
pix_mask:   .res 1     ; $0D
pix_addr_lo:.res 1     ; $0E
pix_addr_hi:.res 1     ; $0F

; --- monster-bitmap blit scratch (draw_sprite16_x2/_x4) ---
; sp_ptr / sp_x / sp_y + the blit scratch now live in
; dev/lib/gen2/hgr_sprite16.asm (included at the bottom), which owns the
; whole sprite pipeline. Its colour attributes (sp_cm_ev / sp_cm_od /
; sp_cbit) are also read by x2_put for the tinted x2 title text.
; --- monster cluster (draw_mob_indicator): up to 3 mobs on one cell ---
mob_depth:  .res 1     ; depth 1..3 of the nearest occupied cell (0=none)
mob_scan_d: .res 1     ; corridor-scan depth cursor (check_front_wall-proof)
mob_cnt:    .res 1     ; monsters found on that cell (0..3)
mob_base:   .res 1     ; (mob_depth-1)*3 — cluster table row
mob_slot_i: .res 1     ; cluster draw loop counter
mob_slot0:  .res 1     ; mob indices on the cell (slot0/1/2 CONTIGUOUS)
mob_slot1:  .res 1
mob_slot2:  .res 1
mob_sz:     .res 1     ; current monster magnify 1/2/4
mob_cur:    .res 1     ; current monster index (for archetype colour)
mob_step:   .res 1     ; row pitch (monster width px = sz*16)
mob_spy:    .res 1     ; shared sp_y for the whole row (same height)
mob_curx:   .res 1     ; running x while laying out the row

; --- double-size text (title "MAZE 3D") ---
x2_src:     .res 1     ; source glyph row (0 or 4)
x2_nib:     .res 1     ; 0=high nibble (left tile), 1=low (right tile)
x2_cx:      .res 1     ; dest cell col/row
x2_cy:      .res 1
x2_row:     .res 1     ; loop cursors
x2_cnt:     .res 1
x2_byte:    .res 1     ; doubled byte (written twice = vertical double)
last_mob_depth: .res 1 ; depth coloured last frame (0=none) -> reset target
; --- colour-table fill (color_rect) ---
cr_x:       .res 1     ; rect origin/size (pixels, multiples of 8)
cr_y:       .res 1
cr_w:       .res 1
cr_h:       .res 1
cr_col:     .res 1     ; colour byte (fg<<4 | bg)
cr_cx:      .res 1     ; color_rect loop cursors
cr_cy:      .res 1

; --- line / Bresenham ---
ln_x0:      .res 1     ; $10
ln_y0:      .res 1
ln_x1:      .res 1
ln_y1:      .res 1
ln_dx:      .res 1     ; $14
ln_dy:      .res 1
ln_sx:      .res 1
ln_sy:      .res 1
ln_err:     .res 1     ; $18  (signed, 16-bit since the GAME6 fix)
ln_err_hi:  .res 1     ;      high byte — 2*err overflows 8 bits for
                       ;      any |dy| > 63 (see line_xy bug note)

; --- DFS gen state ---
cur_row:    .res 1     ; $19
cur_col:    .res 1
stkp:       .res 1
num_dirs:   .res 1
cell_idx:   .res 1     ; $1D
dir_buf:    .res 4     ; $1E-$21

; --- Player state ---
p_col:      .res 1     ; $22
p_row:      .res 1
p_face:     .res 1     ; 0=N 1=E 2=S 3=W
p_hp:       .res 1     ; $25
p_atk:      .res 1
p_def:      .res 1
p_lvl:      .res 1
p_xp:       .res 1     ; $29
p_gold:     .res 1     ; loot collected from slain monsters (0..255)
xp_next:    .res 1     ; total-XP threshold for the next level-up

; --- Event message shown at the top of the 3D view ---
msg_lo:     .res 1     ; pointer to the current message string (ZP indirect)
msg_hi:     .res 1

; --- Game state ---
gstate:     .res 1     ; $2A
prev_state: .res 1     ; previous gameplay state for combat return
quit_flag:  .res 1
view_mode:  .res 1     ; 0=3D, 1=MAP
hud_dirty:  .res 1     ; 1 = HUD text/colour must be rebuilt (stats/facing
                       ; changed, or we just entered the 3D view); 0 = it
                       ; persists (clear_viewport spares the HUD zone), so
                       ; plain forward/back moves skip it entirely.

; --- Combat scratch ---
cur_mob:    .res 1     ; $2E   index of current foe ($FF=none)
ev_dmg:     .res 1

; --- Render scratch ---
rd_depth:   .res 1     ; $32
rd_col:     .res 1
rd_row:     .res 1
rd_dx:      .res 1
rd_dy:      .res 1     ; $36
rd_face:    .res 1
rd_cell:    .res 1     ; cached cell byte
rd_blocked: .res 1     ; non-zero once front blocked

; --- write_char scratch ---
ch_cx:      .res 1     ; $3A    column 0..31
ch_cy:      .res 1     ; $3B    row    0..23
ch_code:    .res 1     ; $3C
ch_idx:     .res 1     ; $3D

; --- movement scratch ---
mv_dir:     .res 1     ; try_move's direction. MUST NOT live in tmp:
                       ; cell_index_xy does STX tmp and would clobber it
                       ; (the historical game-breaking movement bug)

; --- wait_key timeout counter (bits 16-23) ---
wk_hi:      .res 1

; --- scratch for fill / map ---
fl_y0:      .res 1     ; $3E
fl_y1:      .res 1
fl_x0:      .res 1
fl_x1:      .res 1     ; $41

; --- fast vline scratch (batched 8-row read-modify-write) ---
vl_mask:    .res 1     ; column bit mask
vl_cnt:     .res 1     ; rows in current pattern group (1..8)
vbuf:       .res 8     ; staging buffer (unused on HGR; kept for layout)

; ---- HGR port ZP ----
pix_col:    .res 1     ; dest byte column (4 + pix_x/8) from calc_pix_addr
; Forward ZP references into hgr_sprite16.asm (included at EOF): tell
; ca65 these live in the zero page so x2_put gets short addressing.
.globalzp sp_ptr, sp_x, sp_y, sp_cm_ev, sp_cm_od, sp_cbit, sp_px
.globalzp ht_col, ht_sl, ht_left, ht_wrap, ht_font_lo, ht_font_hi, ht_rev
.globalzp ht_cm_ev, ht_cm_od, ht_cbit, ht_page

.code

; =============================================
; main entry
; =============================================
main:
        ; PRNG seed is a CONSTANT and wait_key mixes in KEY VALUES only
        ; (never a polling counter — see the entropy contract at wait_key),
        ; so a scripted --paste-at-cycle session is fully deterministic
        ; regardless of host-load paste jitter (noise-invariance gate).
        ; Real hardware gets variety the TMS_Snake way: title/help accept
        ; ANY key, each distinct keycode seeds a different dungeon.
        LDA #$5A
        STA prng_lo
        LDA #$3C
        STA prng_hi
        LDA #0
        STA quit_flag
        STA view_mode
        ; MUST zero before the first render: color_reset_last reads
        ; last_mob_depth, and a garbage value indexes the reset_* tables
        ; out of bounds -> color_rect with wild bounds that can write into
        ; the NAME TABLE ($3800+), which is built once at init and never
        ; rebuilt -> PERMANENT corruption (missing wall spans + HUD on a
        ; warm/noisy boot; headless zeroes RAM, so it hid in tests).
        STA last_mob_depth
        ; GEN2 init: park the display on TEXT, wipe HGR page 1 (power-on
        ; SRAM is junk and the card has no display-enable bit), then flip
        ; GRAPHICS + HIRES + PAGE1 + MIXOFF.
        JSR gen2_hgr_init_clear
        ; hgr_text8 setup: the game's own font (TMS bit order -> ht_rev=1);
        ; write_char positions every glyph explicitly, so no wrap (40).
        LDA #<font_base
        STA ht_font_lo
        LDA #>font_base
        STA ht_font_hi
        LDA #1
        STA ht_rev
        LDA #4
        STA ht_left
        LDA #40
        STA ht_wrap
        LDA #$7F                ; text colour: white (pass-through)
        STA ht_cm_ev
        STA ht_cm_od
        LDA #0
        STA ht_cbit
        STA ht_page             ; single-buffered: page 1 (page 2 is the
                                ; vdp_display_off blanking screen)
        ; Zero HGR page 2 — vdp_display_off shows it as the blanking
        ; screen during every full redraw (see the routine).
        LDA #$40
        STA pix_addr_hi
        LDA #0
        STA pix_addr_lo
        TAY
@zp2:   STA (pix_addr_lo),Y
        INY
        BNE @zp2
        INC pix_addr_hi
        LDX pix_addr_hi
        CPX #$60
        BNE @zp2
        ; drain stale keystrokes left over from Woz Monitor / paste buffer
        JSR drain_kb

main_loop:
        LDA quit_flag
        BEQ @cont
        ; ESC quit: blank the display (R1=$80 idiom) and hand control back
        ; to the Woz Monitor. We were launched with JMP (runv) by the GAME6
        ; menu — there is NO caller frame, so the old bare RTS here popped
        ; stale stack bytes and jumped into the weeds on real hardware.
        JSR gen2_text_restore   ; monitor-visible state (TEXT + PAGE1)
        JMP WOZMON
@cont:
        ; ONE intro screen (juillet 2026): the title only. The controls +
        ; objective page is no longer forced here -- it moved behind the
        ; in-game H key (see play_input), and the title points to it.
        JSR show_title
        LDA quit_flag
        BNE main_loop

        JSR new_game
        LDA quit_flag
        BNE main_loop

        ; on victory or defeat we fall back to title
        JMP main_loop

; =============================================
; drain_kb: read & ignore any pending keystroke until KBDCR is clear,
; then loop a few thousand cycles to let new keys settle.
; =============================================
drain_kb:
        LDX #0
@lp:    LDA KBDCR
        BPL @nokey
        LDA KBD
@nokey: INX
        BNE @lp
        RTS

; =============================================
; new_game - generate maze, init player, place mobs, run gameplay loop
; =============================================
new_game:
        JSR generate_maze
        JSR place_mobs
        LDA #0
        STA p_col
        STA p_row
        LDA #DIR_E
        STA p_face
        LDA #PLAYER_MAX_HP
        STA p_hp
        LDA #4
        STA p_atk
        LDA #2
        STA p_def
        LDA #1
        STA p_lvl
        LDA #0
        STA p_xp
        STA view_mode
        STA last_mob_depth      ; fresh maze: nothing coloured yet
        STA p_gold              ; no loot yet
        LDA #10
        STA xp_next             ; first level-up at 10 total XP
        LDA #MSG_IDLE           ; narrator sets the epic tone
        LDX #MSG_POOL
        JSR msg_rand
        JSR fill_color_white    ; wipe the title screen's colours
        LDA #1
        STA hud_dirty           ; first 3D frame must build the HUD
        LDA #ST_PLAY3D
        STA gstate

play_loop:
        LDA quit_flag
        BEQ @go
        RTS
@go:
        LDA gstate
        CMP #ST_PLAY3D
        BNE @c1
        JSR render_3d
        JSR play_input
        JMP play_loop
@c1:    CMP #ST_PLAYMAP
        BNE @c2
        JSR render_map
        JSR play_input
        JMP play_loop
@c2:    CMP #ST_COMBAT
        BNE @c3
        JSR run_combat
        JMP play_loop
@c3:    CMP #ST_WIN
        BNE @c4
        JSR show_win
        RTS
@c4:    CMP #ST_LOSE
        BNE @done
        JSR show_lose
        RTS
@done:
        RTS

; =============================================
; play_input - read one key, update state
; =============================================
play_input:
        JSR wait_key
        CMP #KEY_ESC
        BNE @n1
        INC quit_flag
        RTS
@n1:    CMP #KEY_M
        BNE @n2
        ; toggle view. Wipe colours ONCE here (not in the per-frame
        ; render_map/render_3d): otherwise the 3D monster/HUD tint would
        ; bleed onto the map grid and vice-versa.
        JSR fill_color_white
        LDA #1
        STA hud_dirty           ; the map full-clears the HUD zone; rebuild
                                ; it when we return to 3D
        LDA view_mode
        EOR #$01
        STA view_mode
        BNE @ismap
        LDA #ST_PLAY3D
        STA gstate
        RTS
@ismap:
        LDA #ST_PLAYMAP
        STA gstate
        RTS
@n2:    CMP #KEY_LEFT
        BNE @n3
        ; turn left
        LDA p_face
        SEC
        SBC #1
        AND #$03
        STA p_face
        JSR hush_narrator       ; turning in place silences the narrator
        RTS
@n3:    CMP #KEY_RIGHT
        BNE @n4
        ; turn right
        LDA p_face
        CLC
        ADC #1
        AND #$03
        STA p_face
        JSR hush_narrator
        RTS
@n4:    CMP #KEY_FWD
        BNE @n5
        ; forward. Fresh narrator patter as we advance (a fight, if it is
        ; triggered, overrides it with a WIN/PERIL line afterwards).
        JSR narrate_step
        LDA p_face
        JSR try_move
        RTS
@n5:    CMP #KEY_BACK
        BNE @n6
        ; backward = move opposite of facing
        JSR narrate_step
        LDA p_face
        CLC
        ADC #2
        AND #$03
        JSR try_move
        RTS
@n6:    CMP #KEY_H
        BNE @other
        ; help screen on demand; when it returns, play_loop redraws the
        ; current view (gstate unchanged).
        JSR show_help
        RTS
@other: JMP play_input          ; unknown key, or wait_key's synthetic
                                ; timeout SPACE: wait again WITHOUT
                                ; returning — the old fall-through RTS made
                                ; play_loop rebuild the whole frame every
                                ; ~0.7 s even with no input, so the screen
                                ; was mid-repaint most of the time
                                ; (half-drawn-frame bug).

; =============================================
; try_move: attempt to move in direction A.
; If blocked by wall, ignore. After move, check exit + mob spawn.
; BUG HISTORY (juillet 2026): the direction used to be saved in tmp —
; but cell_index_xy does STX tmp, so the direction was silently replaced
; by p_col before the very first compare. Every move key thus moved in
; the direction equal to the player's COLUMN NUMBER (usually blocked
; north at col 0): the game was never walkable. Direction now lives in
; the dedicated mv_dir.
; =============================================
try_move:
        STA mv_dir              ; save direction (cell_index_xy-proof)
        LDX p_col
        LDY p_row
        JSR cell_index_xy       ; A = idx (clobbers tmp!)
        TAX
        LDA grid,X
        STA tmp2                ; current cell flags
        LDA mv_dir
        CMP #DIR_N
        BNE @ne
        LDA tmp2
        AND #NORTH_BIT
        BEQ @blocked
        DEC p_row
        JMP @arrive
@ne:    CMP #DIR_E
        BNE @ns
        LDA tmp2
        AND #EAST_BIT
        BEQ @blocked
        INC p_col
        JMP @arrive
@ns:    CMP #DIR_S
        BNE @nw
        ; SOUTH: south neighbor's NORTH passage
        LDA p_row
        CMP #(NROWS-1)
        BCS @blocked
        LDX p_col
        LDY p_row
        INY
        JSR cell_index_xy
        TAX
        LDA grid,X
        AND #NORTH_BIT
        BEQ @blocked
        INC p_row
        JMP @arrive
@nw:    ; WEST: west neighbor's EAST passage
        LDA p_col
        BEQ @blocked
        LDX p_col
        DEX
        LDY p_row
        JSR cell_index_xy
        TAX
        LDA grid,X
        AND #EAST_BIT
        BEQ @blocked
        DEC p_col
@arrive:
        ; reached exit?
        LDA p_col
        CMP #(NCOLS-1)
        BNE @nowin
        LDA p_row
        CMP #(NROWS-1)
        BNE @nowin
        LDA #ST_WIN
        STA gstate
        RTS
@nowin:
        ; mob on this cell?
        JSR find_mob_here
        BMI @no_mob              ; A=$FF -> no mob
        STA cur_mob
        LDA gstate
        STA prev_state
        LDA #ST_COMBAT
        STA gstate
@no_mob:
@blocked:
        RTS

; =============================================
; cell_index_xy: X=col, Y=row -> A = row*NCOLS + col
; =============================================
cell_index_xy:
        LDA row_offset,Y
        STX tmp
        CLC
        ADC tmp
        RTS

; =============================================
; find_mob_here: returns A = mob index or $FF if none
; =============================================
find_mob_here:
        LDX #0
@lp:    LDA mob_type,X
        CMP #MOB_DEAD
        BEQ @next
        LDA mob_col,X
        CMP p_col
        BNE @next
        LDA mob_row,X
        CMP p_row
        BNE @next
        TXA
        RTS
@next:  INX
        CPX #NUM_MOBS
        BNE @lp
        LDA #$FF
        RTS

; =============================================
; place_mobs - random monster placement on free cells
; (avoids start (0,0) and exit (NCOLS-1, NROWS-1))
; =============================================
place_mobs:
        LDX #0
@lp:    JSR random
        ; pick column 1..NCOLS-1
        AND #$0F
        CMP #NCOLS
        BCC @okc
        SBC #NCOLS
@okc:   STA mob_col,X
        JSR random
        AND #$07
        CMP #NROWS
        BCC @okr
        SBC #NROWS
@okr:   STA mob_row,X
        ; reject start
        LDA mob_col,X
        ORA mob_row,X
        BEQ @retry
        ; reject exit
        LDA mob_col,X
        CMP #(NCOLS-1)
        BNE @keep
        LDA mob_row,X
        CMP #(NROWS-1)
        BEQ @retry
@keep:
        ; assign type round-robin: 0,1,2,0,1,2,...
        TXA
        STA tmp
        LDY #0
@modlp: LDA tmp
        CMP #3
        BCC @okmod
        SBC #3
        STA tmp
        JMP @modlp
@okmod:
        STA mob_type,X
        ; HP = 4 + 3*type + 1d4
        ASL                    ; type * 2
        CLC
        ADC tmp                ; +type -> 3*type
        CLC
        ADC #4
        STA tmp2
        JSR random
        AND #$03
        CLC
        ADC tmp2
        STA mob_hp,X
        INX
        CPX #NUM_MOBS
        BNE @lp
        RTS
@retry:
        ; reroll without advancing X
        JMP @lp

; =============================================
; generate_maze: recursive backtracker DFS
; (port of Maze2_Backtracker.asm to 11x7)
; =============================================
generate_maze:
        LDX #0
        TXA
@cl:    STA grid,X
        INX
        CPX #NCELLS
        BNE @cl

        LDA #0
        STA cur_row
        STA cur_col
        STA stkp
        STA cell_idx
        LDA #VISITED
        STA grid

dfs_loop:
        LDY #0
        LDA cur_row
        BEQ @sn
        LDA cell_idx
        SEC
        SBC #NCOLS
        TAX
        LDA grid,X
        BMI @sn
        LDA #DIR_N
        STA dir_buf,Y
        INY
@sn:    LDA cur_col
        CMP #(NCOLS-1)
        BCS @se
        LDX cell_idx
        INX
        LDA grid,X
        BMI @se
        LDA #DIR_E
        STA dir_buf,Y
        INY
@se:    LDA cur_row
        CMP #(NROWS-1)
        BCS @ss
        LDA cell_idx
        CLC
        ADC #NCOLS
        TAX
        LDA grid,X
        BMI @ss
        LDA #DIR_S
        STA dir_buf,Y
        INY
@ss:    LDA cur_col
        BEQ @sw
        LDX cell_idx
        DEX
        LDA grid,X
        BMI @sw
        LDA #DIR_W
        STA dir_buf,Y
        INY
@sw:    STY num_dirs
        CPY #0
        BEQ @bt
        ; pick random direction modulo num_dirs
        JSR random
        AND #$03
@mod:   CMP num_dirs
        BCC @okm
        SEC
        SBC num_dirs
        JMP @mod
@okm:   TAX
        LDA dir_buf,X
        PHA
        LDX stkp
        LDA cell_idx
        STA dfs_stk,X
        INC stkp
        PLA
        CMP #DIR_E
        BEQ @ge
        CMP #DIR_S
        BEQ @gs
        CMP #DIR_W
        BEQ @gw
        ; NORTH
        LDX cell_idx
        LDA grid,X
        ORA #NORTH_BIT
        STA grid,X
        DEC cur_row
        JMP @mark
@ge:    LDX cell_idx
        LDA grid,X
        ORA #EAST_BIT
        STA grid,X
        INC cur_col
        JMP @mark
@gs:    LDA cell_idx
        CLC
        ADC #NCOLS
        TAX
        LDA grid,X
        ORA #NORTH_BIT
        STA grid,X
        INC cur_row
        JMP @mark
@gw:    LDX cell_idx
        DEX
        LDA grid,X
        ORA #EAST_BIT
        STA grid,X
        DEC cur_col
@mark:  LDX cur_row
        LDA row_offset,X
        CLC
        ADC cur_col
        STA cell_idx
        TAX
        LDA grid,X
        ORA #VISITED
        STA grid,X
        JMP dfs_loop
@bt:    LDA stkp
        BEQ @done
        DEC stkp
        LDX stkp
        LDA dfs_stk,X
        ; recover row,col by div/mod NCOLS
        LDX #0
@dv:    CMP #NCOLS
        BCC @dvd
        SEC
        SBC #NCOLS
        INX
        JMP @dv
@dvd:   STA cur_col
        STX cur_row
        LDX cur_row
        LDA row_offset,X
        CLC
        ADC cur_col
        STA cell_idx
        JMP dfs_loop
@done:  RTS

; =============================================
; random: 16-bit Galois LFSR
; =============================================
random:
        LDA prng_lo
        ASL
        ROL prng_hi
        BCC @nf
        EOR #$2D
@nf:    STA prng_lo
        ; mix prng_hi a bit so place_mobs doesn't loop
        LDA prng_hi
        ASL
        ADC prng_lo
        STA prng_hi
        LDA prng_lo
        RTS

; =============================================
; wait_key: spin until key, stir entropy with the KEY VALUE (only).
; Reads KBD only once (a second read clears the strobe and would
; consume a fresh queued character if any).
;
; ENTROPY CONTRACT (juillet 2026, Snake idiom): the polling loop must
; NOT touch the PRNG. An older revision INC'd prng_lo every iteration
; ("raw timer" seeding) — under POM1's --paste-at-cycle a few thousand
; cycles of host-load slice jitter in key delivery then produced a
; COMPLETELY different maze per run, breaking the noise-invariance /
; determinism gate. Mixing only the key VALUE is paste-jitter-proof:
; the dungeon depends solely on WHICH keys were pressed before
; generation. On real hardware variety comes from the same place as
; TMS_Snake's: the title/help screens accept ANY key, so each distinct
; key press seeds a different dungeon, and in-game combat rolls keep
; consuming draws state-dependently.
;
; A 24-bit polling counter gives the loop a hard stop after ~3 s of
; CPU time; if it fires we return $A0 (a synthetic SPACE) so the
; title / help / win / lose screens can never wedge the game on a
; sticky keyboard / focus issue (and double as a slow attract mode).
; In normal use a real keypress trips the KBDCR test long before the
; counter saturates. In gameplay/combat the synthetic SPACE is ignored
; WITHOUT a repaint (see play_input / run_combat).
; =============================================
wait_key:
        LDA #0
        STA vdp_addr_lo         ; wkey bits 0-7
        STA vdp_addr_hi         ; wkey bits 8-15
        STA wk_hi               ; wkey bits 16-23
@spin:
        LDA KBDCR
        BPL @nokey
        LDA KBD
        PHA
        EOR prng_lo
        STA prng_lo
        PLA
        RTS
@nokey:
        INC vdp_addr_lo
        BNE @spin
        INC vdp_addr_hi
        BNE @spin
        INC wk_hi
        LDA wk_hi
        CMP #3                  ; 3 * 65536 iters * ~15c = ~2.9 s at 1 MHz
        BCC @spin
        ; timeout: synthetic SPACE so screens still advance
        LDA #$A0
        RTS

; wait_key_real: block until a REAL key -- no timeout. The menu screens
; (title/help/win/lose) use this so they actually WAIT for the player
; instead of auto-advancing on wait_key's ~3 s synthetic SPACE. Mixing
; the key VALUE into the PRNG also seeds the maze from WHICH key was
; pressed (real-hardware variety, the wait_key entropy contract).
wait_key_real:
@spin:  LDA KBDCR
        BPL @spin
        LDA KBD
        PHA
        EOR prng_lo
        STA prng_lo
        PLA
        RTS

; =============================================
; init_vdp_g2 - Graphics II (bitmap) mode
;   pattern  $0000-$17FF (6144 B)
;   color    $2000-$37FF (6144 B)
;   name     $3800-$3AFF (768 B, linear: cell N -> pattern N within third)
;   sprite attr $3B00, sprite gen $1800
;
; R0=$02 M3=1, R1=$C0 16K+screen on,
; R2=$0E -> $3800 name table
; R3=$FF color table at $2000 (6K)
; R4=$03 pattern table at $0000 (6K)
; R5=$76 sprite attr at $3B00
; R6=$03 sprite gen at $1800
; R7=$F1 fg=white bg=black
; =============================================
; TMS9918 compatibility stubs — the pad JSRs sprinkled through the kept
; game code and the display-blank pair become no-ops on the GEN2 card.
; =============================================
tms9918_pad12:
        RTS

; vdp_display_off / _on: the TMS build blanked the display (R1 bit 6)
; around every full redraw so the player never watched the frame being
; drawn. The GEN2 card has no display-enable bit — but HGR page 2
; ($4000-$5FFF) sits unused and is zeroed once at boot, so "off" shows
; that black page while we draw page 1, and "on" flips back: the redraw
; is never visible, same UX as the TMS blank for the price of two
; soft-switch reads. (Switches are READ-only — LDA, never STA.)
vdp_display_off:
        LDA GEN2_PAGE2
        RTS
vdp_display_on:
        LDA GEN2_PAGE1
        RTS

; hgr_bitmask: pixel (x & 7) -> HGR bit mask (bit 0 = leftmost pixel,
; 7 px/byte, bit 7 = palette group kept clear). The TMS bitmap byte is
; 8 px wide; HGR shows 7 — pixel 7 of each source byte column CLAMPS
; onto bit 6 instead of vanishing, so a wall edge sitting on an
; x = 7 (mod 8) column still renders (merged with column 6).
hgr_bitmask:
        .byte $01, $02, $04, $08, $10, $20, $40, $40

; (rev7_tab — TMS bit order -> HGR — comes from hgr_sprite16.asm;
; write_char and x2_tile below use it for their 8-px-grid glyphs.)

; =============================================
; clear_viewport / clear_bitmap / clear_hud — zero a scanline band of
; the HGR framebuffer (full 40-byte rows; the never-drawn side margins
; are black anyway). Same row semantics as the TMS versions:
;   clear_viewport  y 0..159  (3D corridor; HUD zone spared)
;   clear_bitmap    y 0..191  (map / combat / menus)
;   clear_hud       y 160..191
; cr_cx / cr_cy are free outside color_rect (a stub here) and serve as
; end/current scanline scratch. Clobbers A, X, Y.
; =============================================
clear_viewport:
        LDA #0
        LDX #160
        JMP clear_span
clear_bitmap:
        LDA #0
        LDX #192
        JMP clear_span
clear_hud:
        LDA #160
        LDX #192
clear_span:
        STA cr_cy              ; current scanline
        STX cr_cx              ; end scanline (exclusive)
@row:   LDY cr_cy
        LDA hgr_lo,Y
        STA pix_addr_lo
        LDA hgr_hi,Y
        STA pix_addr_hi
        LDA #0
        LDY #39
@b:     STA (pix_addr_lo),Y
        DEY
        BPL @b
        INC cr_cy
        LDA cr_cy
        CMP cr_cx
        BNE @row
        RTS

; =============================================
; calc_pix_addr  (input pix_x, pix_y)
; HGR port: pix_addr_lo/hi = hgr scanline base for pix_y (interleaved
; layout via the hgr_lo/hi tables), pix_col = dest byte column
; 4 + pix_x/8 — each 8-px TMS byte column maps onto one 7-px HGR byte
; column, the whole 256-px TMS screen centred as byte columns 4..35.
; =============================================
calc_pix_addr:
        LDY pix_y
        LDA hgr_lo,Y
        STA pix_addr_lo
        LDA hgr_hi,Y
        STA pix_addr_hi
        LDA pix_x
        LSR
        LSR
        LSR
        CLC
        ADC #4
        STA pix_col
        RTS

; =============================================
; plot_set  (pix_x, pix_y) — OR one pixel into the framebuffer.
; =============================================
plot_set:
        ; bounds check (Y only; X is unsigned 0..255 always in range)
        LDA pix_y
        CMP #192
        BCS plot_done
        JSR calc_pix_addr
        LDA pix_x
        AND #$07
        TAX
        LDA hgr_bitmask,X
        LDY pix_col
        ORA (pix_addr_lo),Y
        STA (pix_addr_lo),Y
plot_done:
        RTS

; =============================================
; line_xy: draw line from (ln_x0,ln_y0) to (ln_x1,ln_y1)
; Bresenham, integer arithmetic, signed err in [-255..255] using
; absolute deltas + sign flags.
; =============================================
line_xy:
        ; dx = |x1 - x0|, sx = sign  (use carry, not sign bit, for unsigned compare)
        SEC
        LDA ln_x1
        SBC ln_x0
        BCS @sxp                ; carry set -> x1 >= x0 -> positive
        EOR #$FF
        CLC
        ADC #1
        STA ln_dx
        LDA #$FF
        STA ln_sx
        JMP @dy
@sxp:   STA ln_dx
        LDA #$01
        STA ln_sx
@dy:    SEC
        LDA ln_y1
        SBC ln_y0
        BCS @syp
        EOR #$FF
        CLC
        ADC #1
        STA ln_dy
        LDA #$FF
        STA ln_sy
        JMP @init
@syp:   STA ln_dy
        LDA #$01
        STA ln_sy
@init:  ; err = dx - dy, kept SIGNED 16-BIT since the GAME6 resurrection.
        ; BUG HISTORY (juillet 2026): the original kept err in 8 bits and
        ; computed e2 = 2*err with a lone ASL. For any |dy| > 63 the very
        ; first e2 = 2*(dx-dy) overflows 8 bits (e.g. the title screen's
        ; vertical wireframe edges, dy=65: err=-65, ASL gives +126), both
        ; step tests then fail, neither coordinate advances, and line_xy
        ; spins forever — the shipped TMS_Maze3D.txt never got past its
        ; own title screen (frame hash static, wait_key unreachable).
        ; err in [-255, 255] and e2 in [-510, 510] need 16 bits; the
        ; comparisons below add/subtract in 16 bits and read the sign off
        ; the high byte (no 16-bit overflow possible at these magnitudes).
        SEC
        LDA ln_dx
        SBC ln_dy
        STA ln_err
        LDA #0
        SBC #0                  ; sign-extend the borrow
        STA ln_err_hi
        ; copy current point to pix_x/y (we'll plot)
        LDA ln_x0
        STA pix_x
        LDA ln_y0
        STA pix_y

@step:
        JSR plot_set
        ; if (x0==x1 && y0==y1) done
        LDA ln_x0
        CMP ln_x1
        BNE @do
        LDA ln_y0
        CMP ln_y1
        BEQ @end
@do:    ; e2 = 2*err (16-bit) in tmp (lo) / tmp2 (hi)
        LDA ln_err
        ASL
        STA tmp
        LDA ln_err_hi
        ROL
        STA tmp2
        ; --- x test: e2 > -dy  <=>  e2 + dy > 0 (16-bit signed) ---
        CLC
        LDA tmp
        ADC ln_dy
        TAX                     ; low byte of e2 + dy
        LDA tmp2
        ADC #0                  ; high byte of e2 + dy
        BMI @no_x               ; negative -> not >
        BNE @do_x               ; high > 0 -> definitely >
        CPX #0
        BEQ @no_x               ; e2 + dy == 0 -> not strictly >
@do_x:  ; err -= dy (16-bit)
        SEC
        LDA ln_err
        SBC ln_dy
        STA ln_err
        LDA ln_err_hi
        SBC #0
        STA ln_err_hi
        ; x0 += sx
        LDA ln_sx
        BPL @sxp2
        DEC ln_x0
        JMP @after_x
@sxp2:  INC ln_x0
@after_x:
        LDA ln_x0
        STA pix_x
@no_x:  ; --- y test: e2 < dx  <=>  e2 - dx < 0 (16-bit signed) ---
        SEC
        LDA tmp
        SBC ln_dx
        LDA tmp2
        SBC #0
        BPL @no_y               ; >= 0 -> not <
        ; err += dx (16-bit)
        CLC
        LDA ln_err
        ADC ln_dx
        STA ln_err
        LDA ln_err_hi
        ADC #0
        STA ln_err_hi
        LDA ln_sy
        BPL @syp2
        DEC ln_y0
        JMP @after_y
@syp2:  INC ln_y0
@after_y:
        LDA ln_y0
        STA pix_y
@no_y:  JMP @step
@end:   RTS

; =============================================
; hline: horizontal line from (fl_x0,fl_y0) to (fl_x1,fl_y0), x0 <= x1.
; Byte-oriented: an 8-aligned full span stores one $7F byte (7 lit px —
; the 8th source pixel falls in the dropped column); edge pixels go
; through plot_set.
; =============================================
hline:
        LDA fl_y0
        STA pix_y
        LDA fl_x0
        STA pix_x
@lp:    LDA pix_x
        AND #$07
        BNE @single             ; not byte-aligned -> single pixel
        LDA fl_x1
        SEC
        SBC pix_x
        CMP #7
        BCC @single             ; fewer than 8 px left -> single pixel
        JSR calc_pix_addr
        LDY pix_col
        LDA #$7F
        STA (pix_addr_lo),Y
        LDA pix_x
        CLC
        ADC #8
        STA pix_x
        BCS @done               ; wrapped past x=255 (fl_x1=255 case)
        BCC @chk
@single:
        JSR plot_set
        INC pix_x
        BEQ @done               ; wrapped past x=255
@chk:   LDA pix_x
        CMP fl_x1
        BCC @lp
        BEQ @lp
@done:  RTS

; =============================================
; vline: vertical line at x=fl_x0 from y=fl_y0 to fl_y1, y0 <= y1.
; HGR port: one read-modify-write per scanline at a fixed byte column
; (the interleaved layout has no cheap "8 consecutive rows" run).
; =============================================
vline:
        LDA fl_y1
        CMP #192
        BCC @yok
        LDA #191                ; clamp to the bitmap
        STA fl_y1
@yok:   LDA fl_x0
        AND #$07
        TAX
        LDA hgr_bitmask,X
        STA vl_mask
        LDA fl_x0
        LSR
        LSR
        LSR
        CLC
        ADC #4
        STA pix_col
        LDA fl_y0
        STA pix_y
@lp:    LDY pix_y
        LDA hgr_lo,Y
        STA pix_addr_lo
        LDA hgr_hi,Y
        STA pix_addr_hi
        LDY pix_col
        LDA (pix_addr_lo),Y
        ORA vl_mask
        STA (pix_addr_lo),Y
        LDA pix_y
        CMP fl_y1
        BCS @done               ; just drew the last row
        INC pix_y
        JMP @lp
@done:  RTS

; =============================================
; write_char: place 8x8 glyph at cell (ch_cx, ch_cy); ch_code = ASCII.
; Thin wrapper over hgr_putc8 (dev/lib/gen2/hgr_text8.asm): byte column
; 4 + cx, top scanline cy*8; the game font is TMS bit order, so main:
; arms ht_rev = 1 once at boot (glyph rows pass through rev7_tab).
; =============================================
write_char:
        LDA ch_cx
        CLC
        ADC #4
        STA ht_col
        LDA ch_cy
        ASL
        ASL
        ASL
        STA ht_sl
        LDA ch_code
        JMP hgr_putc8

; =============================================
; draw_str_x2: print the NUL-terminated string (str_lo/hi) at DOUBLE size
; starting at cell (ch_cx, ch_cy). Each glyph becomes 16x16 (a 2x2 cell
; block); the cursor advances 2 cells per character. Used for the big
; "MAZE 3D" title. ch_cy is preserved; ch_cx is advanced.
; =============================================
draw_str_x2:
@lp:    LDY #0
        LDA (str_lo),Y
        BEQ @done
        STA ch_code
        JSR draw_x2char
        LDA ch_cx
        CLC
        ADC #2
        STA ch_cx
        INC str_lo
        BNE @lp
        INC str_hi
        JMP @lp
@done:  RTS

; draw_x2char: draw ch_code's glyph doubled (16x16) with top-left at cell
; (ch_cx, ch_cy). Four dest tiles: top rows from source rows 0..3, bottom
; from 4..7; left tile = high nibble doubled, right = low nibble (dblnib).
draw_x2char:
        LDA ch_code
        SEC
        SBC #$20
        STA tmp
        LDA #0
        STA tmp2
        ASL tmp
        ROL tmp2
        ASL tmp
        ROL tmp2
        ASL tmp
        ROL tmp2                ; (code-$20)*8
        LDA tmp
        CLC
        ADC #<font_base
        STA ptr_lo
        LDA tmp2
        ADC #>font_base
        STA ptr_hi
        ; TL
        LDA #0
        STA x2_src
        STA x2_nib
        LDA ch_cx
        STA x2_cx
        LDA ch_cy
        STA x2_cy
        JSR x2_tile
        ; TR
        LDA #0
        STA x2_src
        LDA #1
        STA x2_nib
        INC x2_cx
        JSR x2_tile
        ; BL
        LDA #4
        STA x2_src
        LDA #0
        STA x2_nib
        LDA ch_cx
        STA x2_cx
        LDA ch_cy
        CLC
        ADC #1
        STA x2_cy
        JSR x2_tile
        ; BR
        LDA #4
        STA x2_src
        LDA #1
        STA x2_nib
        INC x2_cx
        JSR x2_tile
        RTS

; x2_tile: draw one 8x8 dest tile at cell (x2_cx, x2_cy) that doubles
; source glyph rows x2_src..x2_src+3, taking the high (x2_nib=0) or low
; (x2_nib=1) nibble of each and doubling it horizontally (dblnib), each
; row written twice for the vertical double. HGR port: two stores per
; source row at scanlines cy*8 + 2k / 2k+1, byte column 4 + x2_cx.
x2_tile:
        LDA x2_src
        STA x2_row
        LDA #0
        STA x2_cnt              ; output row-pair index 0..3
@r:     LDY x2_row
        LDA (ptr_lo),Y
        LDX x2_nib
        BEQ @hi
        AND #$0F
        JMP @dbl
@hi:    LSR
        LSR
        LSR
        LSR
@dbl:   TAX
        LDA dblnib,X
        TAY
        LDA rev7_tab,Y          ; doubled TMS byte -> HGR bit order
        STA x2_byte
        LDA x2_cy
        ASL
        ASL
        ASL
        STA tmp                 ; tile top scanline = cy*8
        LDA x2_cnt
        ASL
        CLC
        ADC tmp                 ; first scanline of the pair
        JSR x2_put
        LDA x2_cnt
        ASL
        CLC
        ADC tmp
        CLC
        ADC #1                  ; second scanline (vertical double)
        JSR x2_put
        INC x2_row
        INC x2_cnt
        LDA x2_cnt
        CMP #4
        BNE @r
        RTS

; x2_put: store x2_byte at scanline A, byte column 4 + x2_cx —
; through the sprite colour attributes (parity mask + palette bit),
; so draw_str_x2 text can be tinted like the monsters. The doubled
; glyph pixels are 2-px runs; the parity mask keeps one of each pair,
; so the shape survives at half density (a pure HGR colour's price).
; ((x2_cx + 4) & 1) == (x2_cx & 1) — the +4 margin is even.
x2_put:
        TAY
        LDA hgr_lo,Y
        STA pix_addr_lo
        LDA hgr_hi,Y
        STA pix_addr_hi
        LDA x2_cx
        AND #1
        BNE @od
        LDA sp_cm_ev
        JMP @msk
@od:    LDA sp_cm_od
@msk:   AND x2_byte
        ORA sp_cbit
        STA sp_px
        LDA x2_cx
        CLC
        ADC #4
        TAY
        LDA sp_px
        STA (pix_addr_lo),Y
        RTS

; =============================================
; write_str: print zero-terminated string at cell (ch_cx, ch_cy)
; ptr in str_lo / str_hi.  write_char clobbers tmp/tmp2, so we
; preserve the loop index in ch_idx (a dedicated scratch byte).
; =============================================
write_str:
        LDA #0
        STA ch_idx
@lp:    LDY ch_idx
        LDA (str_lo),Y
        BEQ @done
        STA ch_code
        JSR write_char
        INC ch_cx
        INC ch_idx
        BNE @lp
@done:  RTS

; helper: print string (ax pointer) at (ch_cx, ch_cy)
print_str_ax:
        STA str_lo
        STX str_hi
        JMP write_str

; set_msg: A=lo, X=hi -> current event message pointer (shown on row 23).
set_msg:
        STA msg_lo
        STX msg_hi
        RTS

; Narrator pools (index into msg_ptr_lo/hi): base + count.
MSG_IDLE  = 0          ; exploring (32)
MSG_WIN   = 32         ; a kill, some loot (32)
MSG_PERIL = 64         ; low HP, a nasty bite (32)
MSG_POOL  = 32         ; lines per pool (msg_rand count)

; msg_rand: A = pool base index, X = pool count. Picks a random line from
; the pool and points the narrator (msg_lo/hi) at it. Clobbers A/X/Y, tmp.
msg_rand:
        STA tmp                 ; base
        STX tmp2                ; count
        JSR random
@mod:   CMP tmp2                ; A mod count (count is small)
        BCC @have
        SEC
        SBC tmp2
        JMP @mod
@have:  CLC
        ADC tmp                 ; base + (rand mod count)
        TAX
        LDA msg_ptr_lo,X
        STA msg_lo
        LDA msg_ptr_hi,X
        STA msg_hi
        RTS

; narrate_step: a fresh IDLE line as the hero advances, and mark the HUD
; dirty so row 23 is rebuilt (a plain move otherwise leaves it untouched).
narrate_step:
        LDA #MSG_IDLE
        LDX #MSG_POOL
        JSR msg_rand
        LDA #1
        STA hud_dirty
        RTS

; hush_narrator: blank row 23 (turning in place says nothing) and mark the
; HUD dirty so the DIR change + the now-empty message are redrawn. The
; narrator speaks again on the next actual step (narrate_step).
hush_narrator:
        LDA #<str_empty
        STA msg_lo
        LDA #>str_empty
        STA msg_hi
        LDA #1
        STA hud_dirty
        RTS

; draw_str_centered: print the NUL-terminated string at (A=lo, X=hi) centered
; on char row Y. Clobbers A/X/Y, str_lo/hi, tmp. Used for the top-centre
; direction word and the row-23 message.
draw_str_centered:
        STA str_lo
        STX str_hi
        STY tmp                 ; target row
        LDY #0
@len:   LDA (str_lo),Y
        BEQ @lend
        INY
        BNE @len
@lend:  TYA                     ; A = length
        LSR                     ; length/2
        STA tmp2
        LDA #16                 ; 32/2 = centre column
        SEC
        SBC tmp2
        STA ch_cx
        LDA tmp
        STA ch_cy
        LDA str_lo
        LDX str_hi
        JMP print_str_ax

; draw_direction: the compass heading spelled out (NORTH/EAST/SOUTH/WEST),
; centred on row 1 at the top, tinted cyan. Redrawn every 3D frame (it
; lives in the cleared viewport).
draw_direction:
        LDX p_face
        LDA dir_word_lo,X
        PHA
        LDA dir_word_hi,X
        TAX                     ; X = hi
        PLA                     ; A = lo
        LDY #1                  ; row 1
        JSR draw_str_centered
        LDA #48                 ; tint the centre band cyan (avoids the
        STA cr_x                ; ceiling diagonals at the row's edges)
        LDA #8                  ; row 1 -> y=8
        STA cr_y
        LDA #160
        STA cr_w
        LDA #8
        STA cr_h
        LDA #$71                ; cyan
        STA cr_col
        JMP color_rect

; =============================================
; show_title: title screen on bitmap
; =============================================
show_title:
        JSR vdp_display_off     ; draw the whole title blanked, reveal at end
        JSR fill_color_white    ; clean colour slate (may arrive from the game)
        JSR clear_bitmap
        ; Banner box (white)
        LDA #24
        STA fl_x0
        LDA #232
        STA fl_x1
        LDA #16
        STA fl_y0
        JSR     tms9918_pad12
        JSR hline
        LDA #48
        STA fl_y0
        JSR hline
        LDA #24
        STA fl_x0
        LDA #16
        STA fl_y0
        LDA #48
        STA fl_y1
        JSR vline
        LDA #232
        STA fl_x0
        JSR vline

        ; "MAZE 3D" at DOUBLE size (16x16 glyphs). 7 chars * 2 = 14 cells
        ; wide -> centred at col 9, rows 3-4 (inside the banner box 2-5).
        LDA #9
        STA ch_cx
        LDA #3
        STA ch_cy
        LDA #<str_title1
        STA str_lo
        LDA #>str_title1
        STA str_hi
        LDA #HSPR_ORANGE        ; the HGR "red"
        JSR hgr_spr16_color_a
        JSR draw_str_x2
        ; subtitle (rows 7, 9)
        LDA #9
        STA ch_cx
        LDA #7
        STA ch_cy
        LDA #<str_title2
        LDX #>str_title2
        JSR print_str_ax
        LDA #8
        STA ch_cx
        LDA #9
        STA ch_cy
        LDA #<str_title3
        LDX #>str_title3
        JSR print_str_ax
        ; goblin mascot (x2, centred) -- replaces the old wireframe that
        ; overlapped the credits line. Tinted goblin-green (archetype 0).
        LDY #0
        JSR set_sprite_color_y
        LDA #<troll_goblin_pat
        STA sp_ptr
        LDA #>troll_goblin_pat
        STA sp_ptr+1
        LDA #112
        STA sp_x
        LDA #88
        STA sp_y
        JSR hgr_spr16_x2
        JSR sprite_color_white  ; don't leak the tint to later blits
        ; credits (row 16)
        LDA #2
        STA ch_cx
        LDA #16
        STA ch_cy
        LDA #<str_title4
        LDX #>str_title4
        JSR print_str_ax
        ; help hint (row 18)
        LDA #8
        STA ch_cx
        LDA #18
        STA ch_cy
        LDA #<str_title_hint
        LDX #>str_title_hint
        JSR print_str_ax
        ; press any key (row 21)
        LDA #8
        STA ch_cx
        LDA #21
        STA ch_cy
        LDA #<str_press_any
        LDX #>str_press_any
        JSR print_str_ax
        ; author signature (row 23)
        LDA #<str_title_author
        LDX #>str_title_author
        LDY #23
        JSR draw_str_centered

        ; --- colours ---
        ; MAZE 3D (doubled: col 9..22, rows 3-4) -> yellow
        LDA #72
        STA cr_x
        LDA #24
        STA cr_y
        LDA #112
        STA cr_w
        LDA #16
        STA cr_h
        LDA #$B1
        STA cr_col
        JSR color_rect
        ; subtitle line 1 -> green
        LDA #56
        STA cr_x
        LDA #56
        STA cr_y
        LDA #152
        STA cr_w
        LDA #8
        STA cr_h
        LDA #$31
        STA cr_col
        JSR color_rect
        ; subtitle line 2 -> green
        LDA #56
        STA cr_x
        LDA #72
        STA cr_y
        LDA #152
        STA cr_w
        LDA #8
        STA cr_h
        LDA #$31
        STA cr_col
        JSR color_rect
        ; mascot -> green
        LDA #112
        STA cr_x
        LDA #88
        STA cr_y
        LDA #32
        STA cr_w
        LDA #32
        STA cr_h
        LDA #$31
        STA cr_col
        JSR color_rect
        ; credits -> cyan
        LDA #16
        STA cr_x
        LDA #128
        STA cr_y
        LDA #216
        STA cr_w
        LDA #8
        STA cr_h
        LDA #$71
        STA cr_col
        JSR color_rect
        ; hint -> yellow
        LDA #64
        STA cr_x
        LDA #144
        STA cr_y
        LDA #128
        STA cr_w
        LDA #8
        STA cr_h
        LDA #$B1
        STA cr_col
        JSR color_rect
        ; press any key -> magenta
        LDA #64
        STA cr_x
        LDA #168
        STA cr_y
        LDA #128
        STA cr_w
        LDA #8
        STA cr_h
        LDA #$D1
        STA cr_col
        JSR color_rect
        ; author (row 23) -> light green
        LDA #0
        STA cr_x
        LDA #184
        STA cr_y
        LDA #248
        STA cr_w
        LDA #8
        STA cr_h
        LDA #$31
        STA cr_col
        JSR color_rect

        JSR vdp_display_on      ; reveal the finished title
        JSR wait_key_real
        CMP #KEY_ESC
        BNE @ok
        INC quit_flag
@ok:    RTS

; =============================================
; show_help: instructions screen
; =============================================
show_help:
        JSR vdp_display_off     ; hide the redraw
        JSR fill_color_white   ; wipe colours from the game/previous screen
        JSR clear_bitmap

        LDA #8
        STA ch_cx
        LDA #1
        STA ch_cy
        LDA #<str_help_h1
        LDX #>str_help_h1
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR print_str_ax

        LDA #1
        STA ch_cx
        LDA #4
        STA ch_cy
        LDA #<str_help_l1
        LDX #>str_help_l1
        JSR print_str_ax
        LDA #1
        STA ch_cx
        LDA #5
        STA ch_cy
        LDA #<str_help_l2
        LDX #>str_help_l2
        JSR print_str_ax
        LDA #1
        STA ch_cx
        LDA #6
        STA ch_cy
        LDA #<str_help_l3
        LDX #>str_help_l3
        JSR print_str_ax
        LDA #1
        STA ch_cx
        LDA #7
        STA ch_cy
        LDA #<str_help_l4
        LDX #>str_help_l4
        JSR print_str_ax
        LDA #1
        STA ch_cx
        LDA #8
        STA ch_cy
        LDA #<str_help_l5
        LDX #>str_help_l5
        JSR print_str_ax

        LDA #1
        STA ch_cx
        LDA #11
        STA ch_cy
        LDA #<str_help_l6
        LDX #>str_help_l6
        JSR print_str_ax
        LDA #1
        STA ch_cx
        LDA #12
        STA ch_cy
        LDA #<str_help_l7
        LDX #>str_help_l7
        JSR print_str_ax
        LDA #1
        STA ch_cx
        LDA #13
        STA ch_cy
        LDA #<str_help_l8
        LDX #>str_help_l8
        JSR print_str_ax

        LDA #1
        STA ch_cx
        LDA #16
        STA ch_cy
        LDA #<str_help_l9
        LDX #>str_help_l9
        JSR print_str_ax
        LDA #1
        STA ch_cx
        LDA #17
        STA ch_cy
        LDA #<str_help_l10
        LDX #>str_help_l10
        JSR print_str_ax

        LDA #4
        STA ch_cx
        LDA #21
        STA ch_cy
        LDA #<str_press_any
        LDX #>str_press_any
        JSR print_str_ax

        JSR vdp_display_on      ; reveal the finished screen
        JSR wait_key_real
        CMP #KEY_ESC
        BNE @ok
        INC quit_flag
@ok:    RTS

; =============================================
; show_win
; =============================================
show_win:
        JSR vdp_display_off     ; hide the redraw
        JSR fill_color_white   ; wipe colours from the game/previous screen
        JSR clear_bitmap
        LDA #6
        STA ch_cx
        LDA #6
        STA ch_cy
        LDA #<str_win1
        LDX #>str_win1
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR print_str_ax
        LDA #4
        STA ch_cx
        LDA #9
        STA ch_cy
        LDA #<str_win2
        LDX #>str_win2
        JSR print_str_ax
        LDA #6
        STA ch_cx
        LDA #12
        STA ch_cy
        LDA #<str_win3
        LDX #>str_win3
        JSR print_str_ax
        LDA #4
        STA ch_cx
        LDA #20
        STA ch_cy
        LDA #<str_press_any
        LDX #>str_press_any
        JSR print_str_ax
        JSR vdp_display_on      ; reveal the finished screen
        JSR wait_key_real
        CMP #KEY_ESC
        BNE @ok
        INC quit_flag
@ok:    RTS

; =============================================
; show_lose
; =============================================
show_lose:
        JSR vdp_display_off     ; hide the redraw
        JSR fill_color_white   ; wipe colours from the game/previous screen
        JSR clear_bitmap
        LDA #6
        STA ch_cx
        LDA #8
        STA ch_cy
        LDA #<str_lose1
        LDX #>str_lose1
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR print_str_ax
        LDA #6
        STA ch_cx
        LDA #11
        STA ch_cy
        LDA #<str_lose2
        LDX #>str_lose2
        JSR print_str_ax
        LDA #4
        STA ch_cx
        LDA #20
        STA ch_cy
        LDA #<str_press_any
        LDX #>str_press_any
        JSR print_str_ax
        JSR vdp_display_on      ; reveal the finished screen
        JSR wait_key_real
        CMP #KEY_ESC
        BNE @ok
        INC quit_flag
@ok:    RTS

; =============================================
; render_3d - main scene
; =============================================
render_3d:
        ; Sync to VBlank before the full 3D-scene rebuild burst.
        WAIT_VBLANK_SAFE
        ; Blank the display for the whole redraw so the player never sees
        ; the frame being drawn -- it reappears complete when we unblank.
        JSR vdp_display_off
        JSR clear_viewport      ; only rows 0..19; HUD zone persists

        ; (No full-width horizon lines here: they crossed every wall and
        ; passage without occlusion and turned the scene to mush — the
        ; nested frame geometry alone carries the perspective, Wizardry
        ; style. Fixed juillet 2026 together with the front-wall frame
        ; off-by-one below.)

        ; precompute facing -> dx,dy (rd_dx, rd_dy)
        JSR setup_face_deltas

        ; iterate depth from 0..MAX_DEPTH-1
        LDA #0
        STA rd_depth
        STA rd_blocked
        LDA p_col
        STA rd_col
        LDA p_row
        STA rd_row

@dloop:
        LDA rd_blocked
        BNE @after_depth
        ; check OOB
        JSR check_oob
        BNE @oob_block
        ; draw side walls at this depth
        JSR draw_left_wall
        JSR draw_right_wall
        ; check front
        JSR check_front_wall
        BEQ @no_front
        ; front blocked: the wall sits on the FAR edge of cell d, so the
        ; closing rectangle lives at frame d+1 — drawing it at frame d
        ; (the old off-by-one) painted a rectangle one whole cell too
        ; near (full-screen when d=0!) across the side walls just drawn.
        ; rd_depth <= MAX_DEPTH-1 here, so d+1 stays inside the 5-entry
        ; frame tables. rd_blocked stops all further drawing, so the
        ; stale rd_depth value after this is never used for geometry.
        INC rd_depth
        JSR draw_front_wall
        LDA #1
        STA rd_blocked
        JMP @after_depth
@oob_block:
        JSR draw_front_wall
        LDA #1
        STA rd_blocked
        JMP @after_depth
@no_front:
        ; advance to next cell
        JSR step_forward
@after_depth:
        INC rd_depth
        LDA rd_depth
        CMP #MAX_DEPTH
        BCC @dloop

        ; corridor still open at max view depth: close the perspective
        ; with the vanishing rectangle (frame entry MAX_DEPTH) so a long
        ; corridor reads as depth instead of trailing off into nothing
        LDA rd_blocked
        BNE @closed
        LDA #MAX_DEPTH
        STA rd_depth
        JSR draw_front_wall
@closed:
        ; compass heading spelled out, top-centre
        JSR draw_direction
        ; HUD: HP, ATK, DEF, LVL, XP, GOLD + event message
        JSR draw_hud_3d
        ; monsters visible in the corridor ahead (up to 3, on the floor)
        JSR draw_mob_indicator

        JSR vdp_display_on      ; reveal the finished frame in one go
        RTS

; =============================================
; setup_face_deltas: based on p_face, set rd_dx, rd_dy
; =============================================
setup_face_deltas:
        LDA p_face
        STA rd_face
        CMP #DIR_N
        BNE @ne
        LDA #0
        STA rd_dx
        LDA #$FF
        STA rd_dy
        RTS
@ne:    CMP #DIR_E
        BNE @ns
        LDA #1
        STA rd_dx
        LDA #0
        STA rd_dy
        RTS
@ns:    CMP #DIR_S
        BNE @nw
        LDA #0
        STA rd_dx
        LDA #1
        STA rd_dy
        RTS
@nw:    LDA #$FF
        STA rd_dx
        LDA #0
        STA rd_dy
        RTS

; =============================================
; check_oob: A!=0 if (rd_col, rd_row) is out of maze
; =============================================
check_oob:
        LDA rd_col
        BMI @oob
        CMP #NCOLS
        BCS @oob
        LDA rd_row
        BMI @oob
        CMP #NROWS
        BCS @oob
        LDA #0
        RTS
@oob:   LDA #1
        RTS

; =============================================
; step_forward: rd_col += rd_dx, rd_row += rd_dy
; =============================================
step_forward:
        CLC
        LDA rd_col
        ADC rd_dx
        STA rd_col
        CLC
        LDA rd_row
        ADC rd_dy
        STA rd_row
        RTS

; =============================================
; check_front_wall: returns A!=0 if wall blocks forward
; based on rd_col, rd_row, p_face. Uses cell flags.
; OOB outside is treated as wall (caller handles).
; Wall check rules:
;   facing N: wall = !(cell.NORTH passage open)  with cell at rd_row, rd_col
;             -> need to check: from current cell heading N, the wall is THIS cell's NORTH
;   facing E: wall = !(cell.EAST)
;   facing S: wall = !(south neighbor.NORTH) but easier: cell.SOUTH = south_neighbor.NORTH
;   facing W: wall = !(west neighbor.EAST)
; =============================================
check_front_wall:
        ; load current cell at (rd_col, rd_row)
        LDX rd_col
        LDY rd_row
        JSR cell_index_xy
        TAX
        LDA grid,X
        STA rd_cell
        LDA p_face
        CMP #DIR_N
        BNE @ne
        LDA rd_cell
        AND #NORTH_BIT
        BEQ @blk
        LDA #0
        RTS
@ne:    CMP #DIR_E
        BNE @ns
        LDA rd_cell
        AND #EAST_BIT
        BEQ @blk
        LDA #0
        RTS
@ns:    CMP #DIR_S
        BNE @nw
        ; south neighbor's NORTH passage
        LDA rd_row
        CMP #(NROWS-1)
        BCS @blk
        LDX rd_col
        LDY rd_row
        INY
        JSR cell_index_xy
        TAX
        LDA grid,X
        AND #NORTH_BIT
        BEQ @blk
        LDA #0
        RTS
@nw:    ; west neighbor's EAST passage
        LDA rd_col
        BEQ @blk
        LDX rd_col
        DEX
        LDY rd_row
        JSR cell_index_xy
        TAX
        LDA grid,X
        AND #EAST_BIT
        BEQ @blk
        LDA #0
        RTS
@blk:   LDA #1
        RTS

; =============================================
; check_left_wall: A!=0 if wall on player's LEFT at (rd_col, rd_row)
; facing N: left=W; facing E: left=N; facing S: left=E; facing W: left=S
; =============================================
check_left_wall:
        LDX rd_col
        LDY rd_row
        JSR cell_index_xy
        TAX
        LDA grid,X
        STA rd_cell
        LDA p_face
        CMP #DIR_N
        BNE @ne
        ; left=W: west neighbor's EAST
        LDA rd_col
        BEQ @blk
        LDX rd_col
        DEX
        LDY rd_row
        JSR cell_index_xy
        TAX
        LDA grid,X
        AND #EAST_BIT
        BEQ @blk
        LDA #0
        RTS
@ne:    CMP #DIR_E
        BNE @ns
        ; left=N: cell.NORTH
        LDA rd_cell
        AND #NORTH_BIT
        BEQ @blk
        LDA #0
        RTS
@ns:    CMP #DIR_S
        BNE @nw
        ; left=E: cell.EAST
        LDA rd_cell
        AND #EAST_BIT
        BEQ @blk
        LDA #0
        RTS
@nw:    ; left=S: south neighbor's NORTH
        LDA rd_row
        CMP #(NROWS-1)
        BCS @blk
        LDX rd_col
        LDY rd_row
        INY
        JSR cell_index_xy
        TAX
        LDA grid,X
        AND #NORTH_BIT
        BEQ @blk
        LDA #0
        RTS
@blk:   LDA #1
        RTS

; =============================================
; check_right_wall: mirror of left
; facing N: right=E; facing E: right=S; facing S: right=W; facing W: right=N
; =============================================
check_right_wall:
        LDX rd_col
        LDY rd_row
        JSR cell_index_xy
        TAX
        LDA grid,X
        STA rd_cell
        LDA p_face
        CMP #DIR_N
        BNE @ne
        LDA rd_cell
        AND #EAST_BIT
        BEQ @blk
        LDA #0
        RTS
@ne:    CMP #DIR_E
        BNE @ns
        ; right=S
        LDA rd_row
        CMP #(NROWS-1)
        BCS @blk
        LDX rd_col
        LDY rd_row
        INY
        JSR cell_index_xy
        TAX
        LDA grid,X
        AND #NORTH_BIT
        BEQ @blk
        LDA #0
        RTS
@ns:    CMP #DIR_S
        BNE @nw
        ; right=W
        LDA rd_col
        BEQ @blk
        LDX rd_col
        DEX
        LDY rd_row
        JSR cell_index_xy
        TAX
        LDA grid,X
        AND #EAST_BIT
        BEQ @blk
        LDA #0
        RTS
@nw:    LDA rd_cell
        AND #NORTH_BIT
        BEQ @blk
        LDA #0
        RTS
@blk:   LDA #1
        RTS

; =============================================
; draw_left_wall: if wall on left at depth d, draw outline of the
; trapezoid d..d+1 (no inner fill - keep it Wizardry-clean).
; If OPEN, draw the side passage instead (juillet 2026 coherence fix):
; the far plane of the crossing corridor — ceiling and floor edges at
; depth d+1 height spanning the gap, plus the near vertical corner.
; The old code drew NOTHING for an open side, so openings were
; indistinguishable from unrendered space.
; =============================================
draw_left_wall:
        JSR check_left_wall
        BNE @wall
        ; open: side-passage far plane
        LDX rd_depth
        LDA frame_lx,X
        STA fl_x0
        LDA frame_lx+1,X
        STA fl_x1
        LDA frame_ty+1,X
        STA fl_y0
        JSR hline
        LDX rd_depth
        LDA frame_lx,X
        STA fl_x0
        LDA frame_lx+1,X
        STA fl_x1
        LDA frame_by+1,X
        STA fl_y0
        JSR hline
        LDX rd_depth
        LDA frame_lx,X
        STA fl_x0
        LDA frame_ty+1,X
        STA fl_y0
        LDA frame_by+1,X
        STA fl_y1
        JSR vline
        ; far vertical corner of the opening (at lx[d+1]) — without it the
        ; passage box stayed open-ended whenever the NEXT depth had no wall
        ; to supply that edge (its near vertical coincides when it does;
        ; the double draw is a harmless OR).
        LDX rd_depth
        LDA frame_lx+1,X
        STA fl_x0
        LDA frame_ty+1,X
        STA fl_y0
        LDA frame_by+1,X
        STA fl_y1
        JSR vline
        RTS
@wall:
        LDX rd_depth
        LDA frame_lx,X
        STA ln_x0
        LDA frame_ty,X
        STA ln_y0
        INX
        LDA frame_lx,X
        STA ln_x1
        LDA frame_ty,X
        STA ln_y1
        JSR line_xy

        LDX rd_depth
        LDA frame_lx,X
        STA ln_x0
        LDA frame_by,X
        STA ln_y0
        INX
        LDA frame_lx,X
        STA ln_x1
        LDA frame_by,X
        STA ln_y1
        JSR line_xy

        LDX rd_depth
        LDA frame_lx,X
        STA fl_x0
        LDA frame_ty,X
        STA fl_y0
        LDA frame_by,X
        STA fl_y1
        JSR vline
        LDX rd_depth
        INX
        LDA frame_lx,X
        STA fl_x0
        LDA frame_ty,X
        STA fl_y0
        LDA frame_by,X
        STA fl_y1
        JSR vline
@open:  RTS

; =============================================
; draw_right_wall (mirror of draw_left_wall, incl. the open-side
; passage geometry — see the coherence note there)
; =============================================
draw_right_wall:
        JSR check_right_wall
        BNE @wall
        ; open: side-passage far plane
        LDX rd_depth
        LDA frame_rx+1,X
        STA fl_x0
        LDA frame_rx,X
        STA fl_x1
        LDA frame_ty+1,X
        STA fl_y0
        JSR hline
        LDX rd_depth
        LDA frame_rx+1,X
        STA fl_x0
        LDA frame_rx,X
        STA fl_x1
        LDA frame_by+1,X
        STA fl_y0
        JSR hline
        LDX rd_depth
        LDA frame_rx,X
        STA fl_x0
        LDA frame_ty+1,X
        STA fl_y0
        LDA frame_by+1,X
        STA fl_y1
        JSR vline
        ; far vertical corner of the opening (at rx[d+1]) — mirror of the
        ; left-side fix; see the note there.
        LDX rd_depth
        LDA frame_rx+1,X
        STA fl_x0
        LDA frame_ty+1,X
        STA fl_y0
        LDA frame_by+1,X
        STA fl_y1
        JSR vline
        RTS
@wall:
        LDX rd_depth
        LDA frame_rx,X
        STA ln_x0
        LDA frame_ty,X
        STA ln_y0
        INX
        LDA frame_rx,X
        STA ln_x1
        LDA frame_ty,X
        STA ln_y1
        JSR line_xy

        LDX rd_depth
        LDA frame_rx,X
        STA ln_x0
        LDA frame_by,X
        STA ln_y0
        INX
        LDA frame_rx,X
        STA ln_x1
        LDA frame_by,X
        STA ln_y1
        JSR line_xy

        LDX rd_depth
        LDA frame_rx,X
        STA fl_x0
        LDA frame_ty,X
        STA fl_y0
        LDA frame_by,X
        STA fl_y1
        JSR vline
        LDX rd_depth
        INX
        LDA frame_rx,X
        STA fl_x0
        LDA frame_ty,X
        STA fl_y0
        LDA frame_by,X
        STA fl_y1
        JSR vline
@open:  RTS

; =============================================
; draw_front_wall: closing rectangle at depth d using frame_*
; with two simple horizontal seams to suggest stone courses.
; =============================================
draw_front_wall:
        ; hline/vline CLOBBER X (their 8-px batching loops: TAX/INX) — every
        ; frame_*,X load below reloads X from rd_depth after a JSR. The old
        ; code reused X across the calls: the bottom hline picked a random
        ; frame_by and the right edge landed at frame_rx[vl_cnt] (a phantom
        ; vertical at x=183 in every blocked scene — the juillet 2026 render
        ; mess, together with the frame off-by-one fixed in render_3d).
        LDX rd_depth
        LDA frame_lx,X
        STA fl_x0
        LDA frame_rx,X
        STA fl_x1
        LDA frame_ty,X
        STA fl_y0
        JSR hline
        LDX rd_depth
        LDA frame_by,X
        STA fl_y0
        JSR hline
        LDX rd_depth
        LDA frame_lx,X
        STA fl_x0
        LDA frame_ty,X
        STA fl_y0
        LDA frame_by,X
        STA fl_y1
        JSR vline
        LDX rd_depth
        LDA frame_rx,X
        STA fl_x0
        JSR vline
        ; (No mortar seam: the mid-height line across the closing wall read
        ; as clutter, not texture — removed juillet 2026 on request.)
        RTS

; =============================================
; draw_hud_3d - small status under the floor area
; =============================================
draw_hud_3d:
        ; Status panel in the 4-line text zone (rows 20-23, y 160..191):
        ;   y=159      full-width floor line closing the 3D viewport
        ;   row 20     (blank -- 8px of air so the text is not glued
        ;              to the floor line; juillet 2026 request)
        ;   row 21     HP nn    ATK n    DEF n     (combat stats)
        ;   row 22     LVL n    XP nn    DIR X     (progression + facing)
        ;   row 23     free for game messages.
        ; Three aligned columns at cx 1 / 11 / 21, values 2 cells after
        ; their label (write_decimal_2d blanks a leading zero tens digit).
        ; Floor line (y159) closes the viewport -- it lives in the cleared
        ; region (rows 0..19), so it is redrawn EVERY frame.
        LDA #0
        STA fl_x0
        LDA #255
        STA fl_x1
        LDA #159
        STA fl_y0
        JSR hline

        ; The text panel (rows 20..23) is NOT cleared by clear_viewport, so
        ; it persists across plain moves. Rebuild it only when dirty (a
        ; stat/facing change, or a fresh entry into the 3D view).
        LDA hud_dirty
        BNE @rebuild
        RTS
@rebuild:
        LDA #0
        STA hud_dirty
        JSR clear_hud           ; wipe rows 20..23 -> no field-gap remnants

        ; --- row 20: HP / ATK / DEF ---
        LDA #1
        STA ch_cx
        LDA #21
        STA ch_cy
        LDA #<str_hud_hp
        LDX #>str_hud_hp
        JSR print_str_ax
        LDA #5
        STA ch_cx
        LDA #21
        STA ch_cy
        LDA p_hp
        JSR write_decimal_2d

        LDA #11
        STA ch_cx
        LDA #21
        STA ch_cy
        LDA #<str_hud_atk
        LDX #>str_hud_atk
        JSR print_str_ax
        LDA #15
        STA ch_cx
        LDA #21
        STA ch_cy
        LDA p_atk
        JSR write_decimal_2d

        LDA #21
        STA ch_cx
        LDA #21
        STA ch_cy
        LDA #<str_hud_def
        LDX #>str_hud_def
        JSR print_str_ax
        LDA #25
        STA ch_cx
        LDA #21
        STA ch_cy
        LDA p_def
        JSR write_decimal_2d

        ; --- row 21: LVL / XP / DIR ---
        LDA #1
        STA ch_cx
        LDA #22
        STA ch_cy
        LDA #<str_hud_lvl
        LDX #>str_hud_lvl
        JSR print_str_ax
        LDA #5
        STA ch_cx
        LDA #22
        STA ch_cy
        LDA p_lvl
        JSR write_decimal_2d

        LDA #11
        STA ch_cx
        LDA #22
        STA ch_cy
        LDA #<str_hud_xp
        LDX #>str_hud_xp
        JSR print_str_ax
        LDA #15
        STA ch_cx
        LDA #22
        STA ch_cy
        LDA p_xp
        JSR write_decimal_2d

        ; GOLD (facing now lives spelled-out at the top, so the HUD slot
        ; that held DIR shows the loot total instead).
        LDA #21
        STA ch_cx
        LDA #22
        STA ch_cy
        LDA #<str_hud_gold
        LDX #>str_hud_gold
        JSR print_str_ax
        LDA #26
        STA ch_cx
        LDA #22
        STA ch_cy
        LDA p_gold
        JSR write_decimal_2d

        ; Event message, centred on row 23 (yellow).
        LDA msg_lo
        LDX msg_hi
        LDY #23
        JSR draw_str_centered

        ; Colour the HUD rows: vitals (21) green, progression (22) cyan,
        ; message (23) yellow.
        LDA #8
        STA cr_x
        LDA #168                ; row 21 (HP / ATK / DEF)
        STA cr_y
        LDA #216
        STA cr_w
        LDA #8
        STA cr_h
        LDA #$31                ; light green
        STA cr_col
        JSR color_rect
        LDA #8
        STA cr_x
        LDA #176                ; row 22 (LVL / XP / GOLD)
        STA cr_y
        LDA #216
        STA cr_w
        LDA #8
        STA cr_h
        LDA #$71                ; cyan
        STA cr_col
        JSR color_rect
        LDA #0
        STA cr_x
        LDA #184                ; row 23 (message)
        STA cr_y
        LDA #248
        STA cr_w
        LDA #8
        STA cr_h
        LDA #$B1                ; yellow
        STA cr_col
        JSR color_rect
        RTS

face_chars:
        .byte 'N', 'E', 'S', 'W'

; =============================================
; draw_mob_indicator: draw the monsters standing on the NEAREST occupied
; cell straight ahead. A cell can hold several monsters (placement lets
; up to 3 stack -- "the original idea"): all of them are drawn CLUSTERED
; on that one cell, NOT spread one-per-depth down the corridor. Line of
; sight scans depths 1..3 and stops at the first wall OR the first cell
; that holds a monster; that cell's depth sets the sprite size (depth 1
; = x2 / 32x32, depths 2-3 = x1 / 16x16). Monsters stand ON THE FLOOR of
; the cell; no '!' marker. Cluster slots are staggered so the front
; monster (drawn last) overdraws the ones behind (blits are pure stores,
; so overwrite = occlusion).
; =============================================
draw_mob_indicator:
        JSR color_reset_last    ; repaint last frame's coloured tiles white
                                ; (the colour table is NOT wiped by
                                ; clear_bitmap, only the pattern table)
        LDA #0
        STA mob_depth           ; 0 = no monster in view yet
        LDA p_col
        STA rd_col
        LDA p_row
        STA rd_row
        LDA #1
        STA mob_scan_d          ; current depth -- a dedicated var because
                                ; check_front_wall clobbers tmp (via
                                ; cell_index_xy's STX tmp)
@dscan:
        JSR check_front_wall    ; wall ahead of the scan cursor?
        BNE @done               ; view blocked -> nothing more to see
        JSR step_forward        ; advance the cursor one cell
        JSR count_mobs_rd       ; -> A = mobs on (rd_col,rd_row), slots filled
        CMP #0
        BNE @found
        INC mob_scan_d
        LDA mob_scan_d
        CMP #4
        BNE @dscan
@done:  LDA #0
        STA last_mob_depth      ; nothing drawn -> nothing to reset next frame
        RTS
@found:
        LDA mob_scan_d
        STA mob_depth
        STA last_mob_depth      ; remember for next frame's colour reset
        JSR draw_mob_cluster
        RTS

; count_mobs_rd: scan every mob; record up to 3 living ones standing on
; (rd_col, rd_row) into mob_slot0/1/2. Returns A = count (0..3).
count_mobs_rd:
        LDA #0
        STA mob_cnt
        LDX #0
@lp:    LDA mob_type,X
        CMP #MOB_DEAD
        BEQ @nx
        LDA mob_col,X
        CMP rd_col
        BNE @nx
        LDA mob_row,X
        CMP rd_row
        BNE @nx
        LDY mob_cnt
        CPY #3
        BCS @nx                 ; already have 3 -- ignore extras
        TXA
        STA mob_slot0,Y         ; slot0/1/2 contiguous
        INC mob_cnt
@nx:    INX
        CPX #NUM_MOBS
        BNE @lp
        LDA mob_cnt
        RTS

; draw_mob_cluster: draw mob_cnt monsters (mob_slot0..2) on the cell at
; mob_depth. Position/size come from cluster_x/y/sz[(depth-1)*3+slot];
; drawn highest slot first so slot 0 (the big centred FRONT monster) is
; laid last, on top. The two others sit CLOSE BEHIND it -- barely offset
; sideways and one size smaller, their heads poking up above the front's
; shoulders (they sit high enough that the front's box never erases them).
; Size: depth 1 front = x4, its flankers x1; depth 2 front x2 / flankers
; x1; depth 3 all x1. Each monster's tiles are then coloured by archetype.
draw_mob_cluster:
        ; --- pick ONE size for the whole cell: a lone monster gets the
        ; imposing base size (depth 1 = x4); 2-3 monsters all share the
        ; smaller "row" size so they line up SAME SIZE, SAME HEIGHT. ---
        LDX mob_depth           ; 1..3
        LDA mob_cnt
        CMP #2
        BCC @single
        LDA multi_sz-1,X        ; 2+ monsters
        JMP @havesz
@single:
        LDA base_sz-1,X         ; lone monster
@havesz:
        STA mob_sz              ; 1 / 2 / 4
        ASL
        ASL
        ASL
        ASL
        STA mob_step            ; monster width in px = sz*16
        ; common feet line -> shared sp_y (same height for all)
        LDA feet_y-1,X
        SEC
        SBC mob_step
        STA mob_spy
        ; start_x = 128 - cnt*(sz*8), i.e. centre the whole row
        LDA mob_step
        LSR
        STA tmp                 ; sz*8
        LDX mob_cnt
        LDA #0
@w:     CLC
        ADC tmp
        DEX
        BNE @w                  ; A = cnt*sz*8
        STA tmp
        LDA #128
        SEC
        SBC tmp
        STA mob_curx            ; x of the leftmost monster
        LDA #0
        STA mob_slot_i
@dl:    LDY mob_slot_i
        LDA mob_slot0,Y
        STA mob_cur             ; index (for colouring)
        TAX
        JSR mob_sprite_ptr      ; sp_ptr := sprite of that mob's type
        LDA mob_curx
        STA sp_x
        LDA mob_spy
        STA sp_y
        LDA mob_sz
        CMP #4
        BNE @not4
        JSR hgr_spr16_x4
        JMP @colour
@not4:  CMP #2
        BNE @s1
        JSR hgr_spr16_x2
        JMP @colour
@s1:    JSR hgr_spr16_x1
@colour:
        JSR color_current_mob   ; tint this monster's tiles by archetype
        LDA mob_curx
        CLC
        ADC mob_step            ; next monster, same row
        STA mob_curx
        INC mob_slot_i
        LDA mob_slot_i
        CMP mob_cnt
        BNE @dl
        RTS

; color_current_mob: fill the colour table for the mob_sz*16-square block
; at (sp_x, sp_y) with mob_cur's archetype colour.
color_current_mob:
        LDA sp_x
        STA cr_x
        LDA sp_y
        STA cr_y
        LDA mob_sz
        ASL
        ASL
        ASL
        ASL                     ; sz * 16 = side in pixels
        STA cr_w
        STA cr_h
        LDX mob_cur
        LDA mob_type,X
        TAY
        LDA mob_colors,Y
        STA cr_col
        JMP color_rect          ; tail call

; color_reset_last: repaint last frame's cluster region white ($F1) so a
; monster that moved/vanished does not leave a coloured ghost on the
; corridor lines drawn there this frame. last_mob_depth (0 = nothing).
color_reset_last:
        LDA last_mob_depth
        BNE @go
        RTS
@go:    CMP #4                  ; guard: only 1..3 index the reset_* tables.
        BCS @done               ; a stray value must never run color_rect
                                ; with wild bounds (it could reach the name
                                ; table $3800+ and corrupt it permanently).
        TAX
        DEX                     ; depth 1..3 -> row 0..2
        LDA reset_x,X
        STA cr_x
        LDA reset_y,X
        STA cr_y
        LDA reset_w,X
        STA cr_w
        LDA reset_h,X
        STA cr_h
        LDA #$F1                ; white on black
        STA cr_col
        JMP color_rect          ; tail call
@done:  RTS

; color_rect: STUB on the GEN2 HGR port — there is no per-tile colour
; table. Depth cues survive via the stipple/hatch pattern fills; the
; combat portrait / mob clusters render monochrome. Kept as a no-op so
; every caller (draw_direction, color_current_mob, color_reset_last,
; draw_combat_screen) assembles untouched.
color_rect:
        RTS

; fill_color_white: STUB on the GEN2 HGR port (no colour table).
fill_color_white:
        RTS

; Per-depth monster sizing (indexed depth 1..3 via base_sz-1,X):
;   base_sz  = a LONE monster (imposing: adjacent = x4).
;   multi_sz = 2-3 monsters, all this size so they share size + height.
;   feet_y   = the floor line the monsters stand on (same for the whole row).
base_sz:  .byte 4, 2, 1
multi_sz: .byte 2, 1, 1
feet_y:   .byte 128, 112, 96

; Colour-reset boxes (pixel x,y,w,h — mult of 8) covering each depth's
; whole cluster (lone x4 OR a centred row of 3), repainted white next frame.
; depth-1 box starts at y=48 (not 64) so it also covers the x4 COMBAT
; portrait region (96,48..111): after a kill, returning to 3D, color_reset_last
; then wipes the portrait's tint too -- otherwise it bled onto the corridor
; lines in rows 6-7 (the "colour stays on the maze" bug).
reset_x: .byte  80,  96,  96
reset_y: .byte  48,  80,  72
reset_w: .byte  96,  80,  80
reset_h: .byte  80,  48,  40

; Archetype colours (TMS9918 fg<<4 | bg=black): goblin=light green,
; orc=light red, dark mage=magenta.
mob_colors:
        .byte $31, $91, $D1

; mob_sprite_ptr: sp_ptr := SCROLL-O-SPRITES pattern of mob X's archetype
; + the archetype's HGR artifact colour (tail call into
; set_sprite_color_y) — the corridor clusters AND the combat portrait
; both come through here, so the tint follows automatically.
mob_sprite_ptr:
        LDA mob_type,X
        TAY
        LDA mob_sprites_lo,Y
        STA sp_ptr
        LDA mob_sprites_hi,Y
        STA sp_ptr+1
        JMP set_sprite_color_y

; ---- HGR artifact colour per archetype (goblin / orc / dark mage) ----
; The TMS tints ($31 lt-green / $91 lt-red / $D1 magenta) map onto the
; lib's HSPR_* artifact-colour codes (hgr_sprite16.asm).
mob_hues:       .byte HSPR_GREEN, HSPR_ORANGE, HSPR_VIOLET

; set_sprite_color_y: arm the blit colour attributes for archetype Y.
set_sprite_color_y:
        LDA mob_hues,Y
        JMP hgr_spr16_color_a

; sprite_color_white: full-density monochrome blit (both parities lit).
sprite_color_white:
        LDA #HSPR_WHITE
        JMP hgr_spr16_color_a

; =============================================
; write_decimal_2d: A=value, prints two decimal digits at
; (ch_cx, ch_cy); advances ch_cx by 2.
; BUG HISTORY (juillet 2026): the ones digit used to be kept in tmp
; across the tens digit's JSR write_char — but write_char clobbers
; tmp/tmp2 for its font-pointer math, so the ones digit came out as
; garbage (HP 20 printed as "2" + junk glyph; ATK 4 as " 0"; LVL 1 as
; " 0" — the broken-HUD bug). The ones digit now rides the stack.
; =============================================
write_decimal_2d:
        STA tmp
        ; tens
        LDX #0
@dv:    LDA tmp
        CMP #10
        BCC @dvd
        SEC
        SBC #10
        STA tmp
        INX
        JMP @dv
@dvd:   LDA tmp
        PHA                     ; ones digit — write_char-proof
        ; print tens (or space if 0)
        TXA
        BNE @nz
        LDA #' '
        JMP @stz
@nz:    CLC
        ADC #'0'
@stz:   STA ch_code
        JSR write_char
        INC ch_cx
        ; ones
        PLA
        CLC
        ADC #'0'
        STA ch_code
        JSR write_char
        INC ch_cx
        RTS

; =============================================
; render_map - top-down view of the maze with player position
; Cell size 16x16 px; maze 11x7 -> 176x112. Origin (36,20) — NOT (40,24):
; with a 36/20 origin the central 8x8 glyph block of cell (c,r) is exactly
; char cell (5+2c, 3+2r), so the S/E/M/arrow markers sit centered with 4 px
; of air on every side instead of starting ON the left wall line (the
; "letters glued to the walls" report, juillet 2026).
; =============================================
render_map:
        ; Sync to VBlank before the top-down map rebuild burst.
        WAIT_VBLANK_SAFE
        JSR vdp_display_off     ; hide the redraw
        JSR clear_bitmap
        LDA #4
        STA ch_cx
        LDA #1
        STA ch_cy
        LDA #<str_map_title
        LDX #>str_map_title
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR print_str_ax

        ; outer bounds
        LDA #(36)
        STA fl_x0
        LDA #(36+11*16)         ; 212
        STA fl_x1
        LDA #(20)
        STA fl_y0
        JSR hline
        LDA #(20+7*16)          ; 132
        STA fl_y0
        JSR hline

        LDA #36
        STA fl_x0
        LDA #(20)
        STA fl_y0
        LDA #(20+7*16)
        STA fl_y1
        JSR vline
        LDA #(36+11*16)
        STA fl_x0
        JSR vline

        ; walls between cells — loop indices live in rd_col/rd_row (free
        ; during map rendering). BUG HISTORY (juillet 2026): they used to
        ; live in tmp/tmp2, but cell_index_xy, calc_pix_addr and the line
        ; primitives all clobber tmp — after the first drawn wall the
        ; column index turned to garbage and the map came out empty but
        ; for a couple of random ticks.
        LDA #0
        STA rd_row
@yloop: LDA #0
        STA rd_col
@xloop: LDX rd_col
        LDY rd_row
        JSR cell_index_xy       ; A = row*NCOLS + col (clobbers tmp)
        TAX
        LDA grid,X
        STA rd_cell
        ; --- right wall: if EAST passage NOT set and col<NCOLS-1 ---
        LDA rd_col
        CMP #(NCOLS-1)
        BCS @no_right
        LDA rd_cell
        AND #EAST_BIT
        BNE @no_right
        ; vertical line at x = 36+(col+1)*16, y 20+row*16 .. 20+(row+1)*16
        LDA rd_col
        CLC
        ADC #1
        ASL
        ASL
        ASL
        ASL                     ; (col+1)*16
        CLC
        ADC #36
        STA fl_x0
        LDA rd_row
        ASL
        ASL
        ASL
        ASL
        CLC
        ADC #20
        STA fl_y0
        CLC
        ADC #16
        STA fl_y1
        JSR vline
@no_right:
        ; --- bottom wall: if SOUTH passage missing (south neighbor's
        ;     NORTH bit clear) and row<NROWS-1 ---
        LDA rd_row
        CMP #(NROWS-1)
        BCS @no_bot
        LDX rd_col
        LDY rd_row
        INY
        JSR cell_index_xy       ; south neighbor index
        TAX
        LDA grid,X
        AND #NORTH_BIT
        BNE @no_bot
        ; horizontal line at y = 20+(row+1)*16, x 36+col*16 .. +16
        LDA rd_col
        ASL
        ASL
        ASL
        ASL
        CLC
        ADC #36
        STA fl_x0
        CLC
        ADC #16
        STA fl_x1
        LDA rd_row
        CLC
        ADC #1
        ASL
        ASL
        ASL
        ASL
        CLC
        ADC #20
        STA fl_y0
        JSR hline
@no_bot:
        INC rd_col
        LDA rd_col
        CMP #NCOLS
        BCS @yend
        JMP @xloop
@yend:  INC rd_row
        LDA rd_row
        CMP #NROWS
        BCS @ydone
        JMP @yloop
@ydone:

        ; markers: S at (0,0), E at (NCOLS-1, NROWS-1), live mobs as 'M'
        ; (all markers: char cell (5+2c, 3+2r) = the centered 8x8 block)
        LDA #5
        STA ch_cx
        LDA #3
        STA ch_cy
        LDA #'S'
        STA ch_code
        JSR write_char

        ; E in bottom-right cell: (5+2*10, 3+2*6) = (25, 15)
        LDA #25
        STA ch_cx
        LDA #15
        STA ch_cy
        LDA #'E'
        STA ch_code
        JSR write_char

        ; live mobs
        LDX #0
@mlp:   STX ch_idx
        LDA mob_type,X
        CMP #MOB_DEAD
        BEQ @mn
        ; cell coord -> char cell
        LDA mob_col,X
        ASL
        STA tmp                 ; col*2 (each cell ~16 px = 2 char cells)
        CLC
        ADC #5                  ; offset into map (40 px = char col 5)
        STA ch_cx
        LDA mob_row,X
        ASL
        CLC
        ADC #3
        STA ch_cy
        LDA #'M'
        STA ch_code
        JSR write_char
@mn:    LDX ch_idx
        INX
        CPX #NUM_MOBS
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @mlp

        ; Player arrow
        LDA p_col
        ASL
        CLC
        ADC #5
        STA ch_cx
        LDA p_row
        ASL
        CLC
        ADC #3
        STA ch_cy
        LDA p_face
        TAX
        LDA arrow_chars,X
        STA ch_code
        JSR write_char

        ; HUD line
        LDA #0
        STA ch_cx
        LDA #20
        STA ch_cy
        LDA #<str_map_help
        LDX #>str_map_help
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR print_str_ax
        JSR vdp_display_on      ; reveal the finished map
        RTS

arrow_chars:
        .byte '^', '>', 'V', '<'

; =============================================
; run_combat - turn-based against cur_mob
; =============================================
run_combat:
        JSR draw_combat_screen
@wait:  JSR wait_key
        CMP #KEY_ESC
        BNE @n1
        INC quit_flag
        RTS
@n1:    CMP #KEY_F
        BNE @n2
        ; flee: 50% chance
        JSR random
        AND #$01
        BNE @flee_ok
        ; failed flee = monster gets free hit
        JSR mob_attacks
        LDA p_hp
        BEQ @die
        BPL @stay
@die:   LDA #ST_LOSE
        STA gstate
        RTS
@stay:  ; HGR port: only the player-HP digits changed — update them in
        ; place instead of RTSing into play_loop's full combat rebuild
        ; (clear + x4 portrait + every label, ~140k cycles per round).
        JSR combat_update_hp
        JMP @wait
@flee_ok:
        ; pop back to previous gameplay state
        LDA #1
        STA hud_dirty           ; combat wiped the HUD zone + may have changed
                                ; HP; rebuild it on the 3D return
        LDA prev_state
        STA gstate
        RTS
@n2:    CMP #KEY_A
        BEQ @attack
        ; unknown key / wait_key's synthetic timeout: keep waiting
        ; WITHOUT returning — an RTS here made play_loop rebuild the
        ; whole combat screen every ~0.7 s (same flicker bug as
        ; play_input's old fall-through)
        JMP @wait
@attack:
        ; player attacks
        LDX cur_mob
        LDA p_atk
        STA tmp
        ; small random variance 0..1
        JSR random
        AND #$01
        CLC
        ADC tmp
        STA tmp                 ; effective atk
        ; mob defense by type: 0,1,2
        LDX cur_mob
        LDA mob_type,X
        STA tmp2                ; type defense
        LDA tmp
        SEC
        SBC tmp2
        BPL @okd
        LDA #1
@okd:   CMP #1
        BCS @apply
        LDA #1
@apply: STA ev_dmg
        ; subtract from mob_hp
        LDX cur_mob
        SEC
        LDA mob_hp,X
        SBC ev_dmg
        BCS @ok
        LDA #0
@ok:    STA mob_hp,X
        BNE @mob_alive
        ; mob killed
        LDA #MOB_DEAD
        STA mob_type,X
        ; --- loot: gold += (type+1)*2 + 1d4 (tougher foes drop more) ---
        LDX cur_mob
        LDA mob_type,X
        CLC
        ADC #1
        ASL                     ; (type+1)*2
        STA tmp
        JSR random
        AND #$03
        CLC
        ADC tmp
        CLC
        ADC p_gold
        STA p_gold              ; (8-bit; a full clear tops out well under 255)
        ; --- XP is a running TOTAL now (it only ever climbs, so a kill
        ; always visibly rewards). Level up each time it crosses xp_next. ---
        LDA p_xp
        CLC
        ADC #4
        STA p_xp
        LDA #0
        STA tmp2                ; leveled-up flag
@lvlchk:
        LDA p_xp
        CMP xp_next
        BCC @lvldone
        INC p_lvl
        INC p_atk
        ; level-up HEAL: +8 HP (capped at 30). Turns combat into a real
        ; risk/reward loop -- fighting weaker foes to level up lets you
        ; recover HP and take on the dangerous ones, instead of the
        ; "avoid everything and rush the exit" degenerate strategy.
        LDA p_hp
        CLC
        ADC #8
        CMP #31
        BCC @hpok
        LDA #30
@hpok:  STA p_hp
        LDA p_lvl
        AND #$01
        BNE @nodef
        INC p_def
@nodef:
        LDA xp_next
        CLC
        ADC #10                 ; next threshold
        STA xp_next
        LDA #1
        STA tmp2
        JMP @lvlchk
@lvldone:
        ; narrator: if the fight left you battered, an ominous PERIL line;
        ; otherwise a triumphant WIN line. (The LVL bump + heal already
        ; signal the level-up on the HUD.)
        LDA p_hp
        CMP #7
        BCS @winmsg
        LDA #MSG_PERIL
        LDX #MSG_POOL
        JSR msg_rand
        JMP @nolvl
@winmsg:
        LDA #MSG_WIN
        LDX #MSG_POOL
        JSR msg_rand
@nolvl:
        ; A cell can hold up to 3 monsters ("the original idea"): if
        ; another is still standing on the player's cell, fight it too —
        ; stay in ST_COMBAT (cur_mob := next foe) so play_loop re-enters
        ; run_combat and redraws. Only when the cell is clear do we drop
        ; back to the previous state.
        JSR find_mob_here
        BMI @cell_clear
        STA cur_mob
        RTS
@cell_clear:
        LDA #1
        STA hud_dirty           ; combat changed stats + wiped the HUD zone
        LDA prev_state
        STA gstate
        RTS
@mob_alive:
        ; monster's turn
        JSR mob_attacks
        LDA p_hp
        BEQ @die2
        ; HGR port: the round only moved the two HP values — repaint
        ; those four digit cells and loop for the next key. The full
        ; draw_combat_screen stays for entry and next-foe transitions
        ; (different name/portrait), where it is genuinely needed.
        JSR combat_update_hp
        JMP @wait
@die2:  LDA #ST_LOSE
        STA gstate
        RTS

; =============================================
; mob_attacks: mob hits player
; damage = (type+2) - p_def + 0..1
; =============================================
mob_attacks:
        LDX cur_mob
        LDA mob_type,X
        CLC
        ADC #2
        STA tmp                 ; raw atk
        JSR random
        AND #$01
        CLC
        ADC tmp
        STA tmp
        LDA tmp
        SEC
        SBC p_def
        BPL @okd
        LDA #1
@okd:   CMP #1
        BCS @apply
        LDA #1
@apply: STA ev_dmg
        LDA p_hp
        SEC
        SBC ev_dmg
        BCS @okhp
        LDA #0
@okhp:  STA p_hp
        RTS

; =============================================
; draw_combat_screen
; =============================================
draw_combat_screen:
        ; Wipe any monster tint the 3D view left in the colour table (its
        ; depth-1 region is larger than the portrait box, so leftover
        ; colour could tinge the combat text). The portrait gets its own
        ; tint below.
        JSR color_reset_last
        ; Sync to VBlank before the combat-scene rebuild burst.
        WAIT_VBLANK_SAFE
        JSR vdp_display_off     ; hide the redraw
        JSR clear_bitmap
        ; Title bar
        LDA #10
        STA ch_cx
        LDA #1
        STA ch_cy
        LDA #<str_combat_title
        LDX #>str_combat_title
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR print_str_ax

        ; Monster name. BUG HISTORY (juillet 2026): an ASL doubled the
        ; type before indexing, but mob_names_lo/hi are PARALLEL byte
        ; tables indexed by type directly — orcs displayed the mage's
        ; name and mages read past the table ("R 3 ?" garbage).
        LDX cur_mob
        LDA mob_type,X
        TAX
        LDA mob_names_lo,X
        STA str_lo
        LDA mob_names_hi,X
        STA str_hi
        LDA #11
        STA ch_cx
        LDA #4
        STA ch_cy
        JSR write_str

        ; Monster HP — value at cx 7..8: cx 11 would put the ones digit in
        ; cell 12 (x 96..103), exactly under the x4 portrait's first column
        ; (drawn LATER with pure stores), which wiped the digit.
        LDA #4
        STA ch_cx
        LDA #6
        STA ch_cy
        LDA #<str_mob_hp
        LDX #>str_mob_hp
        JSR print_str_ax
        LDA #7
        STA ch_cx
        LDA #6
        STA ch_cy
        LDX cur_mob
        LDA mob_hp,X
        JSR write_decimal_2d

        ; Player HP / ATK
        LDA #5
        STA ch_cx
        LDA #16
        STA ch_cy
        LDA #<str_p_hp
        LDX #>str_p_hp
        JSR print_str_ax
        LDA #11
        STA ch_cx
        LDA #16
        STA ch_cy
        LDA p_hp
        JSR write_decimal_2d

        LDA #16
        STA ch_cx
        LDA #16
        STA ch_cy
        LDA #<str_p_atk
        LDX #>str_p_atk
        JSR print_str_ax
        LDA #21
        STA ch_cx
        LDA #16
        STA ch_cy
        LDA p_atk
        JSR write_decimal_2d

        ; Action prompt
        LDA #4
        STA ch_cx
        LDA #20
        STA ch_cy
        LDA #<str_combat_prompt
        LDX #>str_combat_prompt
        JSR print_str_ax

        ; Monster portrait: the archetype's SCROLL-O-SPRITES image blitted
        ; x4 (64x64) at x 96..159, y 48..111 — between the monster HP line
        ; (row 5) and the player stats line (row 16). Replaces the juillet
        ; 2026 vector portraits (draw_goblin/draw_orc/draw_mage).
        LDX cur_mob
        JSR mob_sprite_ptr      ; sp_ptr + archetype artifact colour
        LDA #96
        STA sp_x
        LDA #48
        STA sp_y
        JSR hgr_spr16_x4
        ; tint the portrait with the foe's archetype colour (and wipe any
        ; stale colour left by the 3D view, whose monster region overlaps
        ; this 64x64 box). Reuse color_rect over the portrait rectangle.
        LDA #96
        STA cr_x
        LDA #48
        STA cr_y
        LDA #64
        STA cr_w
        STA cr_h
        LDX cur_mob
        LDA mob_type,X
        TAY
        LDA mob_colors,Y
        STA cr_col
        JSR color_rect
        JSR vdp_display_on      ; reveal the finished combat screen
        RTS

; combat_update_hp: repaint ONLY the two per-round fields of the combat
; screen — monster HP (cells 7-8 of row 6) and player HP (cells 11-12
; of row 16). write_decimal_2d STORE-overwrites both digit cells, so no
; clearing is needed and the ~1.5k-cycle update needs no display blank.
combat_update_hp:
        LDA #7
        STA ch_cx
        LDA #6
        STA ch_cy
        LDX cur_mob
        LDA mob_hp,X
        JSR write_decimal_2d
        LDA #11
        STA ch_cx
        LDA #16
        STA ch_cy
        LDA p_hp
        JSR write_decimal_2d
        RTS

; =============================================
; Monster-bitmap blits: hgr_spr16_x1 / _x2 / _x4 from
; dev/lib/gen2/hgr_sprite16.asm (included at the bottom of this file) —
; TMS-format 16x16 patterns, rows repacked 7 px/HGR-byte losslessly,
; artifact colour via hgr_spr16_color_a. See the module header.
; =============================================

; archetype -> SCROLL-O-SPRITES pattern (0=goblin 1=orc 2=dark mage)
mob_sprites_lo:
        .byte <troll_goblin_pat, <troll_orc_pat, <char_necromancer_m_pat
mob_sprites_hi:
        .byte >troll_goblin_pat, >troll_orc_pat, >char_necromancer_m_pat

; =============================================
; DATA TABLES
; =============================================

; row_offset[r] = r * NCOLS  (NCOLS=11)
row_offset:
        .byte 0, 11, 22, 33, 44, 55, 66

; depth frame coordinates (5 entries: depth 0..MAX_DEPTH).
; Vertical span 0..159 ONLY: rows 20-23 (y 160..191) are the 4-line text
; zone under the 3D view (HUD on row 20, rows 21-23 free for messages).
; Horizon = 79/80.
; depth 0 = whole screen, 4 = vanishing
frame_lx:
        .byte   0,  40,  72,  96, 112
frame_rx:
        .byte 255, 215, 183, 159, 143
frame_ty:
        .byte   0,  25,  45,  60,  70
frame_by:
        .byte 159, 134, 114,  99,  89

; Monster names
mob_names_lo:
        .byte <str_mob_gob, <str_mob_orc, <str_mob_mage
mob_names_hi:
        .byte >str_mob_gob, >str_mob_orc, >str_mob_mage

; ---- Strings (null-terminated, ASCII < 128) ----
str_title1:   .byte "MAZE 3D",0
str_title2:   .byte "WIZARDRY-STYLE",0
str_title3:   .byte "DUNGEON CRAWLER",0
str_title4:   .byte "FOR GEN2 HGR + APPLE 1",0
str_title_hint:.byte "IN GAME: H=HELP",0
str_press_any:.byte "PRESS ANY KEY...",0
str_title_author:.byte "BY VERHILLE ARNAUD  2026",0

str_help_h1:  .byte "HOW TO PLAY",0
; 31 chars max: printed at ch_cx=1 on the 32-column grid — the old l2 was
; 32 chars, its final 'T' wrapped to the next row ("TK BACKWARD" glitch).
str_help_l1:  .byte "I   FORWARD       J  TURN LEFT",0
str_help_l2:  .byte "K   BACKWARD      L  TURN RIGHT",0
str_help_l3:  .byte "M   TOGGLE MAP / 3D VIEW",0
str_help_l4:  .byte "A   ATTACK   F  FLEE  (COMBAT)",0
str_help_l5:  .byte "ESC  QUIT TO MONITOR",0

str_help_l6:  .byte "FIND THE EXIT MARKED -E-",0
str_help_l7:  .byte "AT THE BOTTOM RIGHT.",0
str_help_l8:  .byte "BEWARE OF THE DUNGEON DENIZENS.",0

str_help_l9:  .byte "GAIN XP TO LEVEL UP YOUR HERO.",0
str_help_l10: .byte "EVERY LEVEL: +1 ATK, +1 DEF/2.",0

str_win1:     .byte "YOU FOUND THE EXIT!",0
str_win2:     .byte "THE LIGHT OF DAY GREETS YOU.",0
str_win3:     .byte "VICTORY!",0

str_lose1:    .byte "YOU HAVE FALLEN.",0
str_lose2:    .byte "THE DUNGEON KEEPS YOU.",0

str_map_title:.byte "DUNGEON MAP",0
str_map_help: .byte "M=BACK TO 3D  ESC=QUIT",0

str_hud_hp:   .byte "HP",0
str_hud_atk:  .byte "ATK",0
str_hud_lvl:  .byte "LVL",0
str_hud_def:  .byte "DEF",0
str_hud_xp:   .byte "XP",0
str_hud_gold: .byte "GOLD",0

; Compass direction spelled out, shown top-centre in colour. Indexed by
; p_face (0=N 1=E 2=S 3=W) via dir_word_lo/hi.
str_dir_n:    .byte "NORTH",0
str_dir_e:    .byte "EAST",0
str_dir_s:    .byte "SOUTH",0
str_dir_w:    .byte "WEST",0
dir_word_lo:  .byte <str_dir_n, <str_dir_e, <str_dir_s, <str_dir_w
dir_word_hi:  .byte >str_dir_n, >str_dir_e, >str_dir_s, >str_dir_w

; ---------------------------------------------------------------------------
; The NARRATOR -- a grandiloquent little fairy who over-hypes your every deed
; on row 23. 96 lines in 3 pools; msg_rand picks one at random. Turning in
; place clears it (str_empty); it returns on the next step.
; Pools: MSG_IDLE (exploring) | MSG_WIN (kill/loot) | MSG_PERIL (low/hurt).
; ---------------------------------------------------------------------------
str_empty: .byte 0
ph_idle0: .byte "THE SHADOWS WHISPER YOUR NAME",0
ph_idle1: .byte "THE DUNGEON HOLDS ITS BREATH",0
ph_idle2: .byte "THE DARK AWAITS YOUR FATE",0
ph_idle3: .byte "STEP BY STEP, A LEGEND GROWS",0
ph_idle4: .byte "DEEPER! THE LEGEND DESCENDS",0
ph_idle5: .byte "DEEPER STILL, MORE HEROIC",0
ph_idle6: .byte "THE ABYSS OPENS ITS ARMS!",0
ph_idle7: .byte "ANOTHER STEP TOWARD GLORY",0
ph_idle8: .byte "BATS FLEE AT YOUR APPROACH",0
ph_idle9: .byte "EVEN THE WALLS ADMIRE YOU",0
ph_idle10: .byte "DESTINY SMELLS FAINTLY DAMP",0
ph_idle11: .byte "YOUR BOOTS ECHO LIKE THUNDER",0
ph_idle12: .byte "THE MAP FEARS YOUR FOOTSTEPS",0
ph_idle13: .byte "ONWARD, O RADIANT ONE!",0
ph_idle14: .byte "SUCH POISE! SUCH DIRECTION!",0
ph_idle15: .byte "A HERO WALKS. SLOWLY. BUT YES",0
ph_idle16: .byte "THE GLOOM PARTS FOR YOU",0
ph_idle17: .byte "LEGENDS ARE MADE OF WALKING",0
ph_idle18: .byte "MIND THE MOSS, GREAT ONE",0
ph_idle19: .byte "THE EXIT DREADS YOUR ARRIVAL",0
ph_idle20: .byte "DUST SETTLES IN YOUR HONOUR",0
ph_idle21: .byte "YOUR SHADOW LOOKS HEROIC TOO",0
ph_idle22: .byte "COBWEBS PART IN REVERENCE",0
ph_idle23: .byte "THE SILENCE APPLAUDS YOU",0
ph_idle24: .byte "A DRAFT! AN OMEN! OR A GAP",0
ph_idle25: .byte "YOU STRIDE WITH PURPOSE-ISH",0
ph_idle26: .byte "THE STONES REMEMBER GIANTS",0
ph_idle27: .byte "FORWARD, INTO SLIGHT DANGER!",0
ph_idle28: .byte "THE TORCHES ENVY YOUR GLOW",0
ph_idle29: .byte "EACH STEP, A VERSE UNWRITTEN",0
ph_idle30: .byte "THE MAZE TREMBLES POLITELY",0
ph_idle31: .byte "DOOM HUMS A CHEERFUL TUNE",0
ph_win0: .byte "A FOE PERISHES. GLORY!",0
ph_win1: .byte "THE BARDS WILL SING OF THIS",0
ph_win2: .byte "SLAIN! THE HALL ACCLAIMS YOU",0
ph_win3: .byte "ONE LESS FOR THE LEGEND",0
ph_win4: .byte "TREASURE WORTHY OF YOUR QUEST",0
ph_win5: .byte "LOOT FIT FOR THE CHOSEN",0
ph_win6: .byte "YOUR GLORY GROWS HEAVIER",0
ph_win7: .byte "TAKEN, WITH FLAIR INTACT",0
ph_win8: .byte "IT NEVER STOOD A CHANCE",0
ph_win9: .byte "SPLAT! MOST MAJESTIC, THAT",0
ph_win10: .byte "ANOTHER STAT FOR THE EPICS",0
ph_win11: .byte "THE CROWD OF ONE GOES WILD",0
ph_win12: .byte "SMOTE! A FINE WORD, NOW",0
ph_win13: .byte "GORGEOUS AND DEADLY. RUDE.",0
ph_win14: .byte "IT REGRETS EVERYTHING NOW",0
ph_win15: .byte "CLEAN KILL. POETS WEEP.",0
ph_win16: .byte "VANQUISHED WITH GOOD POSTURE",0
ph_win17: .byte "GOLD! SHINY! MINE! ...YOURS",0
ph_win18: .byte "COINS FOR THE HERO FUND",0
ph_win19: .byte "PILLAGE BECOMES YOU",0
ph_win20: .byte "THAT WILL BUFF THE LEGEND",0
ph_win21: .byte "A TROPHY FOR THE MANTLE",0
ph_win22: .byte "THE ABYSS COUGHS UP LOOT",0
ph_win23: .byte "RICHER, AND STILL HANDSOME",0
ph_win24: .byte "DISPATCHED. NEXT VICTIM?",0
ph_win25: .byte "HEROIC. ALSO MILDLY MESSY.",0
ph_win26: .byte "THE MONSTER FILED A COMPLAINT",0
ph_win27: .byte "ONE SWING, ONE SONNET",0
ph_win28: .byte "BEHOLD, THE SPOILS OF FATE",0
ph_win29: .byte "VICTORY TASTES LIKE DUST. YAY",0
ph_win30: .byte "ANOTHER BEAST, A NEW BALLAD",0
ph_win31: .byte "IT WILL NOT BE MISSED",0
ph_peril0: .byte "YOUR BREATH FAILS, ALAS",0
ph_peril1: .byte "DEATH LURKS... STAY NOBLE",0
ph_peril2: .byte "ONE STEP FROM AN EPIC END!",0
ph_peril3: .byte "HOLD ON, FALTERING LEGEND",0
ph_peril4: .byte "OUCH! YET YOU STAY SUBLIME",0
ph_peril5: .byte "A BITE UNWORTHY OF YOU",0
ph_peril6: .byte "PAIN FORGES THE HEROES",0
ph_peril7: .byte "YOU STAGGER, MAJESTIC",0
ph_peril8: .byte "MAYBE... RUN? HEROICALLY?",0
ph_peril9: .byte "THAT ONE STUNG THE LEGEND",0
ph_peril10: .byte "BLEEDING, BUT FASHIONABLY",0
ph_peril11: .byte "THE END NEARS. POSTURE!",0
ph_peril12: .byte "PERHAPS A HEALER? A PRAYER?",0
ph_peril13: .byte "STILL PRETTY. LESS ALIVE.",0
ph_peril14: .byte "YOUR EPILOGUE LOOMS CLOSE",0
ph_peril15: .byte "DIGNITY OVER LONGEVITY!",0
ph_peril16: .byte "WOUNDED, YET PHOTOGENIC",0
ph_peril17: .byte "THE GRAVE CLEARS ITS THROAT",0
ph_peril18: .byte "TEETERING ON GLORY'S EDGE",0
ph_peril19: .byte "I'D FLEE. GENTLY. JUST SAYING",0
ph_peril20: .byte "ONE MORE HIT ENDS THE SAGA",0
ph_peril21: .byte "COURAGE! ALSO, BANDAGES!",0
ph_peril22: .byte "THE REAPER TAPS HIS WATCH",0
ph_peril23: .byte "FADING, BUT WITH FLOURISH",0
ph_peril24: .byte "A NOBLE SHADE YOU WILL MAKE",0
ph_peril25: .byte "HP LOW, EGO INTACT",0
ph_peril26: .byte "DEATH IS SO INCONVENIENT",0
ph_peril27: .byte "CLING ON, O SPLENDID ONE",0
ph_peril28: .byte "THE TOMB WARMS UP FOR YOU",0
ph_peril29: .byte "ALMOST A MARTYR. ALMOST.",0
ph_peril30: .byte "GASP! DRAMATIC, YET DIRE",0
ph_peril31: .byte "SURVIVE, FOR THE FANS!",0
msg_ptr_lo:
        .byte <ph_idle0,<ph_idle1,<ph_idle2,<ph_idle3,<ph_idle4,<ph_idle5,<ph_idle6,<ph_idle7
        .byte <ph_idle8,<ph_idle9,<ph_idle10,<ph_idle11,<ph_idle12,<ph_idle13,<ph_idle14,<ph_idle15
        .byte <ph_idle16,<ph_idle17,<ph_idle18,<ph_idle19,<ph_idle20,<ph_idle21,<ph_idle22,<ph_idle23
        .byte <ph_idle24,<ph_idle25,<ph_idle26,<ph_idle27,<ph_idle28,<ph_idle29,<ph_idle30,<ph_idle31
        .byte <ph_win0,<ph_win1,<ph_win2,<ph_win3,<ph_win4,<ph_win5,<ph_win6,<ph_win7
        .byte <ph_win8,<ph_win9,<ph_win10,<ph_win11,<ph_win12,<ph_win13,<ph_win14,<ph_win15
        .byte <ph_win16,<ph_win17,<ph_win18,<ph_win19,<ph_win20,<ph_win21,<ph_win22,<ph_win23
        .byte <ph_win24,<ph_win25,<ph_win26,<ph_win27,<ph_win28,<ph_win29,<ph_win30,<ph_win31
        .byte <ph_peril0,<ph_peril1,<ph_peril2,<ph_peril3,<ph_peril4,<ph_peril5,<ph_peril6,<ph_peril7
        .byte <ph_peril8,<ph_peril9,<ph_peril10,<ph_peril11,<ph_peril12,<ph_peril13,<ph_peril14,<ph_peril15
        .byte <ph_peril16,<ph_peril17,<ph_peril18,<ph_peril19,<ph_peril20,<ph_peril21,<ph_peril22,<ph_peril23
        .byte <ph_peril24,<ph_peril25,<ph_peril26,<ph_peril27,<ph_peril28,<ph_peril29,<ph_peril30,<ph_peril31
msg_ptr_hi:
        .byte >ph_idle0,>ph_idle1,>ph_idle2,>ph_idle3,>ph_idle4,>ph_idle5,>ph_idle6,>ph_idle7
        .byte >ph_idle8,>ph_idle9,>ph_idle10,>ph_idle11,>ph_idle12,>ph_idle13,>ph_idle14,>ph_idle15
        .byte >ph_idle16,>ph_idle17,>ph_idle18,>ph_idle19,>ph_idle20,>ph_idle21,>ph_idle22,>ph_idle23
        .byte >ph_idle24,>ph_idle25,>ph_idle26,>ph_idle27,>ph_idle28,>ph_idle29,>ph_idle30,>ph_idle31
        .byte >ph_win0,>ph_win1,>ph_win2,>ph_win3,>ph_win4,>ph_win5,>ph_win6,>ph_win7
        .byte >ph_win8,>ph_win9,>ph_win10,>ph_win11,>ph_win12,>ph_win13,>ph_win14,>ph_win15
        .byte >ph_win16,>ph_win17,>ph_win18,>ph_win19,>ph_win20,>ph_win21,>ph_win22,>ph_win23
        .byte >ph_win24,>ph_win25,>ph_win26,>ph_win27,>ph_win28,>ph_win29,>ph_win30,>ph_win31
        .byte >ph_peril0,>ph_peril1,>ph_peril2,>ph_peril3,>ph_peril4,>ph_peril5,>ph_peril6,>ph_peril7
        .byte >ph_peril8,>ph_peril9,>ph_peril10,>ph_peril11,>ph_peril12,>ph_peril13,>ph_peril14,>ph_peril15
        .byte >ph_peril16,>ph_peril17,>ph_peril18,>ph_peril19,>ph_peril20,>ph_peril21,>ph_peril22,>ph_peril23
        .byte >ph_peril24,>ph_peril25,>ph_peril26,>ph_peril27,>ph_peril28,>ph_peril29,>ph_peril30,>ph_peril31


str_combat_title:   .byte "COMBAT!",0
str_mob_hp:   .byte "HP",0
str_p_hp:     .byte "HP",0
str_p_atk:    .byte "ATK",0
str_combat_prompt: .byte "A=ATTACK  F=FLEE  ESC=QUIT",0

str_mob_gob:  .byte "GOBLIN",0
str_mob_orc:  .byte "ORC",0
str_mob_mage: .byte "DARK MAGE",0

; =============================================
; FONT (8x8, ASCII $20..$5F = 64 glyphs * 8 = 512 bytes)
; Bit 7 = leftmost pixel.
; =============================================
font_base:
        ; $20 SPACE
        .byte $00,$00,$00,$00,$00,$00,$00,$00
        ; $21 !
        .byte $30,$30,$30,$30,$30,$00,$30,$00
        ; $22 "
        .byte $66,$66,$66,$00,$00,$00,$00,$00
        ; $23 #
        .byte $66,$66,$FF,$66,$FF,$66,$66,$00
        ; $24 $
        .byte $18,$3E,$60,$3C,$06,$7C,$18,$00
        ; $25 %
        .byte $62,$66,$0C,$18,$30,$66,$46,$00
        ; $26 &
        .byte $3C,$66,$3C,$38,$67,$66,$3F,$00
        ; $27 '
        .byte $30,$30,$60,$00,$00,$00,$00,$00
        ; $28 (
        .byte $0C,$18,$30,$30,$30,$18,$0C,$00
        ; $29 )
        .byte $30,$18,$0C,$0C,$0C,$18,$30,$00
        ; $2A *
        .byte $00,$66,$3C,$FF,$3C,$66,$00,$00
        ; $2B +
        .byte $00,$18,$18,$7E,$18,$18,$00,$00
        ; $2C ,
        .byte $00,$00,$00,$00,$00,$18,$18,$30
        ; $2D -
        .byte $00,$00,$00,$7E,$00,$00,$00,$00
        ; $2E .
        .byte $00,$00,$00,$00,$00,$18,$18,$00
        ; $2F /
        .byte $00,$03,$06,$0C,$18,$30,$60,$00
        ; $30 0
        .byte $3C,$66,$6E,$76,$66,$66,$3C,$00
        ; $31 1
        .byte $18,$18,$38,$18,$18,$18,$7E,$00
        ; $32 2
        .byte $3C,$66,$06,$0C,$30,$60,$7E,$00
        ; $33 3
        .byte $3C,$66,$06,$1C,$06,$66,$3C,$00
        ; $34 4
        .byte $06,$0E,$1E,$66,$7F,$06,$06,$00
        ; $35 5
        .byte $7E,$60,$7C,$06,$06,$66,$3C,$00
        ; $36 6
        .byte $3C,$60,$60,$7C,$66,$66,$3C,$00
        ; $37 7
        .byte $7E,$66,$0C,$18,$18,$18,$18,$00
        ; $38 8
        .byte $3C,$66,$66,$3C,$66,$66,$3C,$00
        ; $39 9
        .byte $3C,$66,$66,$3E,$06,$66,$3C,$00
        ; $3A :
        .byte $00,$18,$18,$00,$00,$18,$18,$00
        ; $3B ;
        .byte $00,$18,$18,$00,$00,$18,$18,$30
        ; $3C <
        .byte $0E,$18,$30,$60,$30,$18,$0E,$00
        ; $3D =
        .byte $00,$00,$7E,$00,$7E,$00,$00,$00
        ; $3E >
        .byte $70,$18,$0C,$06,$0C,$18,$70,$00
        ; $3F ?
        .byte $3C,$66,$06,$0C,$18,$00,$18,$00
        ; $40 @
        .byte $3C,$66,$6E,$6E,$60,$62,$3C,$00
        ; $41 A
        .byte $18,$3C,$66,$66,$7E,$66,$66,$00
        ; $42 B
        .byte $7C,$66,$66,$7C,$66,$66,$7C,$00
        ; $43 C
        .byte $3C,$66,$60,$60,$60,$66,$3C,$00
        ; $44 D
        .byte $78,$6C,$66,$66,$66,$6C,$78,$00
        ; $45 E
        .byte $7E,$60,$60,$78,$60,$60,$7E,$00
        ; $46 F
        .byte $7E,$60,$60,$78,$60,$60,$60,$00
        ; $47 G
        .byte $3C,$66,$60,$6E,$66,$66,$3C,$00
        ; $48 H
        .byte $66,$66,$66,$7E,$66,$66,$66,$00
        ; $49 I
        .byte $3C,$18,$18,$18,$18,$18,$3C,$00
        ; $4A J
        .byte $1E,$0C,$0C,$0C,$0C,$6C,$38,$00
        ; $4B K
        .byte $66,$6C,$78,$70,$78,$6C,$66,$00
        ; $4C L
        .byte $60,$60,$60,$60,$60,$60,$7E,$00
        ; $4D M
        .byte $63,$77,$7F,$6B,$63,$63,$63,$00
        ; $4E N
        .byte $66,$76,$7E,$7E,$6E,$66,$66,$00
        ; $4F O
        .byte $3C,$66,$66,$66,$66,$66,$3C,$00
        ; $50 P
        .byte $7C,$66,$66,$7C,$60,$60,$60,$00
        ; $51 Q
        .byte $3C,$66,$66,$66,$66,$3C,$0E,$00
        ; $52 R
        .byte $7C,$66,$66,$7C,$78,$6C,$66,$00
        ; $53 S
        .byte $3C,$66,$60,$3C,$06,$66,$3C,$00
        ; $54 T
        .byte $7E,$5A,$18,$18,$18,$18,$18,$00
        ; $55 U
        .byte $66,$66,$66,$66,$66,$66,$3C,$00
        ; $56 V
        .byte $66,$66,$66,$66,$66,$3C,$18,$00
        ; $57 W
        .byte $63,$63,$63,$6B,$7F,$77,$63,$00
        ; $58 X
        .byte $66,$66,$3C,$18,$3C,$66,$66,$00
        ; $59 Y
        .byte $66,$66,$66,$3C,$18,$18,$18,$00
        ; $5A Z
        .byte $7E,$06,$0C,$18,$30,$60,$7E,$00
        ; $5B [
        .byte $3C,$30,$30,$30,$30,$30,$3C,$00
        ; $5C backslash
        .byte $00,$60,$30,$18,$0C,$06,$03,$00
        ; $5D ]
        .byte $3C,$0C,$0C,$0C,$0C,$0C,$3C,$00
        ; $5E ^
        .byte $18,$3C,$66,$00,$00,$00,$00,$00
        ; $5F _
        .byte $00,$00,$00,$00,$00,$00,$00,$FF

; =============================================
; GEN2 lib modules (textual includes): hgr_lo/hgr_hi scanline tables +
; gen2_hgr_init_clear / gen2_text_restore.
; =============================================
.include "hgr_scanline.inc"
.include "hgr_sprite16.asm"
.include "hgr_text8.asm"
.include "gen2_init.asm"

; END
