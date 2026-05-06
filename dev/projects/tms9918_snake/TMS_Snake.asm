; =============================================
; SNAKE for P-LAB TMS9918 Graphic Card
; VERHILLE Arnaud - 2026
; Classic snake: eat the apples, grow longer, don't bite yourself
; or hit the wall. Score = food eaten.
; Inspired by TMS_Sokoban.asm (same TMS9918 boilerplate, char map).
; =============================================
; Assemble with cc65:
;   ca65 -o build/TMS_Snake.o software/tms9918/TMS_Snake.asm
;   ld65 -C software/tms9918/apple1_snake.cfg \
;        -o build/TMS_Snake.bin build/TMS_Snake.o
;
; Or just: python3 software/tms9918/emit_TMS_Snake_txt.py
;
; Load in POM1 via File > Load Memory (TMS_Snake.txt), then 280R.
; The TMS9918 card must be enabled (Hardware menu).
;
; Footprint: program + ring buffers fit inside the stock 4 KB DRAM
; ($0280-$0DFF, see apple1_snake.cfg). Earlier revisions kept a 768 B
; cell_grid for collision lookup; that's been replaced by algorithmic
; checks (boundary math for walls, head_x/y compare for food, ring
; walk for the snake's own body) so the whole game runs on a 1976
; Apple-1 with no expansion RAM.
;
; Display: 32x24 Graphics I mode. Row 0 is the SCORE HUD; rows 1..23
; carry the playfield. The walls form a 32x22 rectangle, leaving a
; 30x21 playable area where the snake roams.
;
; Tiles (also TMS char codes):
;   0  empty   group 0  bg=black, fg unused
;   1  wall    group 0  fg=gray brick texture
;   8  body    group 1  fg=light green
;   16 head    group 2  fg=light yellow (eyes glyph)
;   24 food    group 3  fg=medium red (apple glyph)
; Each tile sits in its own colour group (chars 0,8,16,24) so the four
; tile types pick up four different palette slots without juggling
; colour bytes per cell.
; =============================================

; --- Apple 1 I/O ---
        .import tms9918_pad40  ; silicon-strict pad40 (helper from tms9918_pad.asm)
ECHO    = $FFEF
KBD     = $D010
KBDCR   = $D011

; --- TMS9918 I/O ---
VDP_DATA = $CC00
VDP_CTRL = $CC01

; --- Geometry ---
NCOLS   = 32
NROWS   = 24
PLAY_TOP    = 2         ; first playable row (row 1 = top wall)
PLAY_BOT    = 22        ; last  playable row (row 23 = bottom wall)
PLAY_LEFT   = 1
PLAY_RIGHT  = 30

; --- Tile codes (also TMS char codes) ---
CELL_EMPTY     = 0
CELL_WALL      = 1      ; deadly border (solid brick) - chosen when wrap_mode=0
CELL_WALL_PASS = 2      ; passable border (small dot) - chosen when wrap_mode=1
CELL_BODY      = 8
CELL_HEAD      = 16
CELL_FOOD      = 24

; --- Game tuning ---
INITIAL_LEN    = 4
INITIAL_SPEED  = 60     ; outer delay count (~180 ms / tick)
SPEED_FLOOR    = 12     ; fastest tick cap
SPEED_STEP_EVERY = 4    ; speed up after this many foods
SCORE_WIN      = 250    ; game over (you win!) at this length

; =============================================
; Off-board buffers (defined in linker cfg)
; =============================================
.segment "SNXSEG"
snake_x:    .res 256            ; ring buffer of segment columns ($0C00)

.segment "SNYSEG"
snake_y:    .res 256            ; ring buffer of segment rows    ($0D00)

; =============================================
; Zero page
; =============================================
.zeropage
temp:           .res 1
temp2:          .res 1
src_lo:         .res 1
src_hi:         .res 1
sptr_lo:        .res 1
sptr_hi:        .res 1
str_lo:         .res 1
str_hi:         .res 1

head_idx:       .res 1          ; index of head segment
tail_idx:       .res 1          ; index of tail segment (oldest)
length:         .res 1          ; current snake length (saturates at $FF)
foods_since:   .res 1           ; foods eaten since last speed-up

dir_dx:         .res 1          ; current direction
dir_dy:         .res 1
pend_dx:        .res 1          ; pending direction (queued from KB)
pend_dy:        .res 1

new_x:          .res 1
new_y:          .res 1
food_x:         .res 1
food_y:         .res 1

score:          .res 1          ; foods eaten (0..255)
speed:          .res 1          ; outer delay count
game_over:      .res 1
won:            .res 1

prng_lo:        .res 1
prng_hi:        .res 1

key_up_code:    .res 1
key_left_code:  .res 1
wrap_mode:      .res 1          ; 0 = walls deadly, 1 = walls wrap-around
hi_score:       .res 1          ; best score this session (persists across new_game)

draw_row:       .res 1
draw_col:       .res 1

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

        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax
        LDA #<str_layout
        LDX #>str_layout
        JSR print_str_ax

@kb_wait:
        JSR wait_key
        PHA                     ; save key
        EOR prng_lo             ; mix key timing into PRNG seed
        STA prng_lo
        PLA                     ; restore key
        CMP #'1'
        BEQ @qwerty
        CMP #'2'
        BEQ @azerty
        JMP @kb_wait
@qwerty:
        LDA #'W'
        STA key_up_code
        LDA #'A'
        STA key_left_code
        JMP @ask_walls
@azerty:
        LDA #'Z'
        STA key_up_code
        LDA #'Q'
        STA key_left_code

@ask_walls:
        ; Refresh the TMS splash so the wall-mode choice is visible
        ; in graphics output too — overwrite the keyboard prompt rows
        ; in-place. Strings are sized to fully clobber the old text.
        LDA #<title_walls_select_tms
        STA sptr_lo
        LDA #>title_walls_select_tms
        STA sptr_hi
        LDA #$E8                ; $19E8 = row 15 col 8
        LDX #$59
        JSR draw_str_tms
        LDA #<title_walls_keys_tms
        STA sptr_lo
        LDA #>title_walls_keys_tms
        STA sptr_hi
        LDA #$48                ; $1A48 = row 18 col 8
        LDX #$5A
        JSR draw_str_tms

        LDA #<str_walls
        LDX #>str_walls
        JSR print_str_ax
@walls_wait:
        JSR wait_key
        PHA
        EOR prng_lo
        STA prng_lo
        PLA
        CMP #'1'
        BEQ @deadly
        CMP #'2'
        BEQ @wrap
        JMP @walls_wait
@deadly:
        LDA #$00
        STA wrap_mode
        JMP @begin
@wrap:
        LDA #$01
        STA wrap_mode

@begin:
        LDA #$A5
        STA prng_lo
        LDA #$3C
        STA prng_hi
        LDA #$00
        STA hi_score            ; reset once per power-on; new_game keeps it

new_game:
        LDA #$00
        STA score
        STA game_over
        STA won
        STA foods_since
        LDA #INITIAL_SPEED
        STA speed
        JSR init_arena
        JSR init_snake
        JSR spawn_food
        JSR draw_hud

play_loop:
        JSR delay_and_input
        ; Apply pending direction iff (a) it's nonzero and (b) it's
        ; not a 180 deg about-face on the current direction.
        LDA pend_dx
        BNE @maybe
        LDA pend_dy
        BEQ @keep               ; nothing pending
@maybe:
        LDA pend_dx
        CLC
        ADC dir_dx
        BNE @apply
        LDA pend_dy
        CLC
        ADC dir_dy
        BNE @apply
        JMP @keep
@apply:
        LDA pend_dx
        STA dir_dx
        LDA pend_dy
        STA dir_dy
@keep:
        JSR move_snake
        LDA game_over
        BNE @over
        LDA won
        BNE @win
        JSR draw_hud
        JMP play_loop

@over:
        JSR draw_gameover_tms
        LDA #<str_over
        LDX #>str_over
        JSR print_str_ax
        JSR wait_key
        JMP new_game
@win:
        JSR draw_win_tms
        LDA #<str_won
        LDX #>str_won
        JSR print_str_ax
        JSR wait_key
        JMP new_game


; =============================================
; init_arena: clear the TMS name table, then paint the wall border.
; Walls are visual only — collision checks them by coordinate
; (col 0/31, row 1/23) so we don't need a backing tile-code grid.
; The border tile reflects wrap_mode: solid brick when walls are
; deadly (wrap_mode=0), porous dot pattern when the snake wraps
; through to the opposite side (wrap_mode=1). The chosen char is
; cached in temp2 (which plot_cell never touches) so each of the
; four border passes can reload it cheaply.
; =============================================
init_arena:
        LDA #CELL_WALL
        LDX wrap_mode
        BEQ @wall_picked
        LDA #CELL_WALL_PASS
@wall_picked:
        STA temp2

        ; --- Clear TMS name table $1800 (3 pages) ---
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$58                ; $18 | $40
        STA VDP_CTRL
        LDX #$03
        LDA #$00
@np:    LDY #$00
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
@nb:    STA VDP_DATA
        INY
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BNE @nb
        DEX
        BNE @np

        ; --- Top wall: row 1, cols 0..31 ---
        LDA #$01
        STA draw_row
        LDA #$00
        STA draw_col
@top:
        LDA temp2
        JSR plot_cell
        INC draw_col
        LDA draw_col
        CMP #NCOLS
        BCC @top

        ; --- Bottom wall: row 23, cols 0..31 ---
        LDA #23
        STA draw_row
        LDA #$00
        STA draw_col
@bot:
        LDA temp2
        JSR plot_cell
        INC draw_col
        LDA draw_col
        CMP #NCOLS
        BCC @bot

        ; --- Left wall: col 0, rows 2..22 ---
        LDA #$00
        STA draw_col
        LDA #$02
        STA draw_row
@left:
        LDA temp2
        JSR plot_cell
        INC draw_row
        LDA draw_row
        CMP #23
        BCC @left

        ; --- Right wall: col 31, rows 2..22 ---
        LDA #31
        STA draw_col
        LDA #$02
        STA draw_row
@right:
        LDA temp2
        JSR plot_cell
        INC draw_row
        LDA draw_row
        CMP #23
        BCC @right
        RTS


; =============================================
; init_snake: lay a 4-segment horizontal snake at row 12, cols 12..15,
; moving right. snake_x[0..3] / snake_y[0..3] hold the segments.
; Tail = index 0, head = index 3.
; =============================================
init_snake:
        LDA #$01
        STA dir_dx
        LDA #$00
        STA dir_dy
        STA pend_dx
        STA pend_dy
        STA tail_idx

        ; Body segments at (12,12), (12,13), (12,14).
        ; plot_cell clobbers X (it does LDX draw_row), so we stash the
        ; loop counter in temp2 across each call.
        LDX #$00
@build:
        STX temp2
        TXA
        CLC
        ADC #$0C                ; col = 12 + i
        STA snake_x,X
        LDA #$0C                ; row = 12
        STA snake_y,X
        STA draw_row
        LDA temp2
        CLC
        ADC #$0C
        STA draw_col
        LDA #CELL_BODY
        JSR plot_cell
        LDX temp2
        INX
        CPX #INITIAL_LEN-1
        BCC @build

        ; Head segment at (12, 15) — X = INITIAL_LEN-1 = 3 on entry.
        LDA #$0F                ; col 15
        STA snake_x,X
        LDA #$0C
        STA snake_y,X
        STA draw_row
        LDA #$0F
        STA draw_col
        STX temp2               ; save 3 for head_idx after plot_cell
        LDA #CELL_HEAD
        JSR plot_cell
        LDX temp2
        STX head_idx            ; head_idx = 3

        LDA #INITIAL_LEN
        STA length
        RTS


; =============================================
; move_snake: advance one tick.
;
; Collision checks are algorithmic (no backing tile-code grid):
;   * Wall : new_x == 0 or 31, or new_y == 1 or 23. In wrap mode,
;            reflect to the opposite playfield edge instead of dying.
;   * Food : (new_x, new_y) == (food_x, food_y) -> grow + spawn next.
;   * Body : walk snake_x/y from tail_idx+1 .. head_idx. The tail is
;            excluded because it's about to be trimmed, which makes
;            "head into vacating tail cell" legal (classic snake).
;
; Sets game_over or won on terminal states.
; =============================================
move_snake:
        ; new_x = head.x + dir_dx, new_y = head.y + dir_dy
        LDX head_idx
        LDA snake_x,X
        CLC
        ADC dir_dx
        STA new_x
        LDA snake_y,X
        CLC
        ADC dir_dy
        STA new_y

        ; --- Wall check by coordinate ---
        LDA new_x
        BEQ @at_wall                    ; col 0
        CMP #31
        BEQ @at_wall                    ; col 31
        LDA new_y
        CMP #$01
        BEQ @at_wall                    ; row 1
        CMP #23
        BEQ @at_wall                    ; row 23
        JMP @no_wall
@at_wall:
        LDA wrap_mode
        BEQ @die
        JSR wrap_through_wall
@no_wall:

        ; --- Food check ---
        LDA new_x
        CMP food_x
        BNE @not_food
        LDA new_y
        CMP food_y
        BEQ @ate
@not_food:

        ; --- Body collision (skip tail segment, it is being trimmed) ---
        LDX tail_idx
@bs:
        CPX head_idx
        BEQ @no_body
        INX
        LDA snake_x,X
        CMP new_x
        BNE @bs
        LDA snake_y,X
        CMP new_y
        BNE @bs
        ; Match — collision with own body
@die:
        LDA #$01
        STA game_over
        RTS

@no_body:
        ; --- Empty cell: trim tail, repaint old head, push new head ---
        JSR erase_tail_cell
        INC tail_idx
        JSR head_to_body
        JSR push_new_head
        RTS

@ate:
        ; Food eaten: snake grows by 1. No tail trim.
        INC score
        LDA score
        CMP hi_score
        BCC @no_hi_update
        STA hi_score            ; new personal best — sticks across new_game
@no_hi_update:
        INC foods_since
        ; Speed up every SPEED_STEP_EVERY foods (cap at SPEED_FLOOR).
        LDA foods_since
        CMP #SPEED_STEP_EVERY
        BCC @no_speedup
        LDA #$00
        STA foods_since
        LDA speed
        CMP #SPEED_FLOOR+1
        BCC @no_speedup
        DEC speed
@no_speedup:
        ; length++ (saturate at $FF, win at SCORE_WIN)
        LDA length
        CMP #$FF
        BEQ @no_lensat
        INC length
@no_lensat:
        LDA length
        CMP #SCORE_WIN
        BCC @grow_continue
        LDA #$01
        STA won
        RTS
@grow_continue:
        JSR head_to_body
        JSR push_new_head
        JSR spawn_food
        RTS


; wrap_through_wall: in wrap mode, called when (new_x, new_y) sits on
; the wall border. Snake only moves on one axis per tick, so exactly
; one of new_x / new_y is at a wall coord (col 0/31 or row 1/23).
; Reflect it to the opposite playfield edge so the snake "comes out
; the other side".
wrap_through_wall:
        LDA new_x
        BNE @nx_right
        LDA #PLAY_RIGHT
        STA new_x
        RTS
@nx_right:
        CMP #31
        BNE @ny_top
        LDA #PLAY_LEFT
        STA new_x
        RTS
@ny_top:
        LDA new_y
        CMP #$01
        BNE @ny_bot
        LDA #PLAY_BOT
        STA new_y
        RTS
@ny_bot:
        LDA #PLAY_TOP
        STA new_y
        RTS


; erase_tail_cell: paint EMPTY at snake_x/y[tail_idx]
erase_tail_cell:
        LDX tail_idx
        LDA snake_x,X
        STA draw_col
        LDA snake_y,X
        STA draw_row
        LDA #CELL_EMPTY
        JSR plot_cell
        RTS


; head_to_body: repaint current head cell as BODY (it's about to stop
; being the head as we push a new segment onto the ring).
head_to_body:
        LDX head_idx
        LDA snake_x,X
        STA draw_col
        LDA snake_y,X
        STA draw_row
        LDA #CELL_BODY
        JSR plot_cell
        RTS


; push_new_head: head_idx++, write new_x/new_y as HEAD.
push_new_head:
        INC head_idx
        LDX head_idx
        LDA new_x
        STA snake_x,X
        STA draw_col
        LDA new_y
        STA snake_y,X
        STA draw_row
        LDA #CELL_HEAD
        JSR plot_cell
        RTS


; =============================================
; spawn_food: pick a random EMPTY cell in the playfield, mark FOOD.
; Uses a 16-bit Galois LFSR (poly $B400) seeded from key timing.
; food_x in [1..30], food_y in [2..22]. Rejection-samples on:
;   - out-of-range PRNG draws
;   - any current snake segment (walk tail_idx .. head_idx, inclusive)
; =============================================
spawn_food:
@retry:
        JSR prng16
        LDA prng_lo
        AND #$1F                ; 0..31
        BEQ @retry              ; reject 0
        CMP #31
        BCS @retry              ; reject 31
        STA new_x
        JSR prng16
        LDA prng_hi
        AND #$1F                ; 0..31
        CMP #PLAY_TOP
        BCC @retry              ; reject 0..1
        CMP #23
        BCS @retry              ; reject 23..31
        STA new_y

        ; --- Reject if (new_x, new_y) overlaps any snake segment ---
        LDX tail_idx
@sf_scan:
        LDA snake_x,X
        CMP new_x
        BNE @sf_next
        LDA snake_y,X
        CMP new_y
        BEQ @retry
@sf_next:
        CPX head_idx
        BEQ @sf_clear
        INX
        JMP @sf_scan
@sf_clear:
        LDA new_x
        STA food_x
        STA draw_col
        LDA new_y
        STA food_y
        STA draw_row
        LDA #CELL_FOOD
        JSR plot_cell
        RTS


; =============================================
; prng16 — promoted to dev/lib/m6502/prng.asm (Tier 2.2 mutualization).
; =============================================
.include "prng16.asm"


; =============================================
; delay_and_input: throttle the game loop and slurp keys.
; Outer = `speed`, inner = 256 iterations of ~12 cyc each ~= 3 ms.
; Each inner iteration polls KBDCR; if a key is ready, route it into
; the pending-direction state via handle_key. Mixes key timing into
; the PRNG seed so food placement varies between sessions.
; =============================================
delay_and_input:
        LDX speed
@outer:
        LDY #$00
@inner:
        LDA KBDCR
        BPL @nokey
        LDA KBD
        AND #$7F
        STA temp
        EOR prng_lo
        STA prng_lo             ; entropy mix
        LDA temp
        JSR handle_key
@nokey:
        DEY
        BNE @inner
        DEX
        BNE @outer
        RTS


; handle_key: A = ASCII (bit 7 stripped). Updates pend_dx/pend_dy.
; Recognises W/A/S/D (or Z/Q/S/D after AZERTY pick), and 'P' to
; pause/freeze the game until any other key resumes it.
handle_key:
        CMP #'P'
        BEQ @pause
        CMP key_up_code
        BEQ @up
        CMP #'S'
        BEQ @down
        CMP key_left_code
        BEQ @left
        CMP #'D'
        BEQ @right
        RTS
@pause:
        ; Block on the keyboard until the user hits anything. The
        ; resume key is dropped (no direction change), and its timing
        ; is mixed into the PRNG so pauses jitter food placement.
        JSR wait_key
        EOR prng_lo
        STA prng_lo
        RTS
@up:
        LDA #$00
        STA pend_dx
        LDA #$FF
        STA pend_dy
        RTS
@down:
        LDA #$00
        STA pend_dx
        LDA #$01
        STA pend_dy
        RTS
@left:
        LDA #$FF
        STA pend_dx
        LDA #$00
        STA pend_dy
        RTS
@right:
        LDA #$01
        STA pend_dx
        LDA #$00
        STA pend_dy
        RTS


; =============================================
; init_vdp: program 8 VDP registers, upload the 4 tile patterns + HUD
; glyphs, install colour groups, clear name table, disable sprites.
; =============================================
init_vdp:
        ; --- 8 VDP registers ---
        ; The register loop must survive entry with R1 already display-ON
        ; (e.g. CodeTank re-launch from another game that left $C0/$C2 in
        ; R1). The auto-patcher injects JSR pad40 intra-pair (between
        ; ORA #$80 and STA VDP_CTRL cmd) and inter-iter (loop-back
        ; detection drops a pad before BNE @regloop). Once iter 1 (X=1)
        ; commits R1 with bit 6 cleared, the gate drops to 16c for the
        ; rest of init.
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
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA VDP_CTRL
        INX
        CPX #$08
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BNE @regloop

        ; --- Upload the 6 tile patterns at chars 0, 1, 2, 8, 16, 24 ---
        ; Each pattern is 8 bytes (single 8x8 glyph).
        LDX #$00
@tlp:
        LDA tile_vram_lo,X
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA tile_vram_hi,X
        ORA #$40
        STA VDP_CTRL
        ; src = tile_patterns + X*8
        TXA
        PHA
        ASL A
        ASL A
        ASL A
        CLC
        ADC #<tile_patterns
        STA src_lo
        LDA #>tile_patterns
        ADC #$00
        STA src_hi
        LDY #$00
@tb:    LDA (src_lo),Y
        STA VDP_DATA
        INY
        CPY #$08
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BCC @tb
        PLA
        TAX
        INX
        CPX #$06
        BNE @tlp

        ; --- Upload HUD glyph patterns at char 56 (VRAM $01C0) ---
        ; 39 glyphs * 8 bytes = 312 bytes (chars 56..94).
        LDA #$C0
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$41                ; $01 | $40
        STA VDP_CTRL
        LDX #$00
@hp1:   LDA hud_patterns,X
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA VDP_DATA
        INX
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BNE @hp1                ; first 256 bytes
        LDX #$00
@hp2:   LDA hud_patterns+256,X
        STA VDP_DATA
        INX
        CPX #56                 ; remaining 312-256
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BCC @hp2

        ; --- Colour table at $2000 (12 entries for groups 0..11) ---
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$60                ; $20 | $40
        STA VDP_CTRL
        LDX #$00
@cl:    LDA tile_colors,X
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA VDP_DATA
        INX
        CPX #$0C
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BNE @cl

        ; --- Clear name table $1800 (3 pages) ---
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
        INY
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BNE @nb
        DEX
        BNE @np

        ; --- Disable sprites: first sprite Y = $D0 stops the chain ---
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$5B                ; $1B | $40
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$D0
        STA VDP_DATA

        ; --- Final: re-arm R1 with table value (display ON). Display stays
        ;     OFF until the cmd byte commits — threshold = 2c through both
        ;     STAs, no inline pad needed. The caller's next VDP write picks
        ;     up 16c gating, with the pad inserted in caller code.
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA vdp_regs+1
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$81
        STA VDP_CTRL
        RTS


; =============================================
; plot_cell: write A (TMS char code) into the name table at (draw_row,
; draw_col). No backing grid -- collisions are checked algorithmically.
; Inputs:
;   A          = char code
;   draw_row   = 0..23
;   draw_col   = 0..31
; Trashes: A, X, temp.
; =============================================
plot_cell:
        STA temp
        LDX draw_row
        ; VDP write addr = $1800 + row*32 + col
        LDA row_x32_lo,X
        CLC
        ADC draw_col
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA row_x32_hi,X
        ADC #$18
        ORA #$40
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA temp
        STA VDP_DATA
        RTS


; =============================================
; draw_hud: render "SCORE:NNN" at row 0 cols 0..8 and "HI:NNN" at
; cols 23..28 (right side of the HUD bar, above the playfield).
; Called after every successful move (cheap, the row is small).
; The 3-digit decimal emitter is factored into emit_3digit_vdp so
; both readouts share it, which paid for the hi-score addition
; without overflowing the 2 304 B CodeTank slot.
; =============================================
draw_hud:
        ; VRAM addr $1800 (row 0, col 0)
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$58
        STA VDP_CTRL
        ; "SCORE:" — 6 chars from a fixed table.
        LDX #$00
@hud_lp:
        LDA hud_score_str,X
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA VDP_DATA
        INX
        CPX #$06
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BCC @hud_lp
        LDA score
        JSR emit_3digit_vdp

        ; VRAM addr $1817 (row 0, col 23) — start of "HI:NNN"
        LDA #$17
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA #imm bridge)
        LDA #$58
        STA VDP_CTRL
        LDX #$00
@hi_lp:
        LDA hud_hi_str,X
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA VDP_DATA
        INX
        CPX #$03
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BCC @hi_lp
        LDA hi_score
        JMP emit_3digit_vdp     ; tail-call

; emit_3digit_vdp: A in 0..255 -> 3 ASCII digits to VDP_DATA.
; CMP sets carry when A >= operand, so the SBC #imm chains work
; without an explicit SEC. Trashes A, X, temp.
emit_3digit_vdp:
        LDX #$00
@h:     CMP #100
        BCC @hd
        SBC #100
        INX
        JMP @h
@hd:    STA temp
        TXA
        CLC
        ADC #HUD_C_D0
        STA VDP_DATA            ; hundreds
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA temp
        LDX #$00
@t:     CMP #$0A
        BCC @td
        SBC #$0A
        INX
        JMP @t
@td:    STA temp
        TXA
        CLC
        ADC #HUD_C_D0
        STA VDP_DATA            ; tens
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (before LDA zp/abs bridge)
        LDA temp
        CLC
        ADC #HUD_C_D0
        STA VDP_DATA            ; ones
        RTS

hud_score_str:
        .byte HUD_C_S, HUD_C_C, HUD_C_O, HUD_C_R, HUD_C_E, HUD_C_CL
hud_hi_str:
        .byte HUD_C_H, HUD_C_I, HUD_C_CL


; =============================================
; draw_title_tms: splash on the TMS9918 while the Apple-1 text screen
; prints credits + layout prompt. Five centred lines using HUD glyphs.
; The first init_arena call wipes them with a name-table clear.
; =============================================
draw_title_tms:
        LDA #<title_snake_tms
        STA sptr_lo
        LDA #>title_snake_tms
        STA sptr_hi
        LDA #$AC                ; $18AC = row 5 col 12
        LDX #$58
        JSR draw_str_tms

        LDA #<title_card_tms
        STA sptr_lo
        LDA #>title_card_tms
        STA sptr_hi
        LDA #$2A                ; $192A = row 9 col 10
        LDX #$59
        JSR draw_str_tms

        LDA #<title_author_tms
        STA sptr_lo
        LDA #>title_author_tms
        STA sptr_hi
        LDA #$87                ; $1987 = row 12 col 7
        LDX #$59
        JSR draw_str_tms

        LDA #<title_select_tms
        STA sptr_lo
        LDA #>title_select_tms
        STA sptr_hi
        LDA #$E8                ; $19E8 = row 15 col 8
        LDX #$59
        JSR draw_str_tms

        LDA #<title_keys_tms
        STA sptr_lo
        LDA #>title_keys_tms
        STA sptr_hi
        LDA #$48                ; $1A48 = row 18 col 8
        LDX #$5A
        JMP draw_str_tms        ; tail-call


; draw_gameover_tms: "GAME OVER" + "PRESS A KEY" splash.
draw_gameover_tms:
        JSR clear_name_table
        LDA #<title_over_tms
        STA sptr_lo
        LDA #>title_over_tms
        STA sptr_hi
        LDA #$4B                ; $194B = row 10 col 11
        LDX #$59
        JSR draw_str_tms
        LDA #<title_press_tms
        STA sptr_lo
        LDA #>title_press_tms
        STA sptr_hi
        LDA #$AA                ; $19AA = row 13 col 10
        LDX #$59
        JMP draw_str_tms


; draw_win_tms: "YOU WIN!" splash.
draw_win_tms:
        JSR clear_name_table
        LDA #<title_win_tms
        STA sptr_lo
        LDA #>title_win_tms
        STA sptr_hi
        LDA #$4C                ; $194C = row 10 col 12
        LDX #$59
        JSR draw_str_tms
        LDA #<title_press_tms
        STA sptr_lo
        LDA #>title_press_tms
        STA sptr_hi
        LDA #$AA
        LDX #$59
        JMP draw_str_tms


; clear_name_table: 768 bytes of char 0 at $1800.
clear_name_table:
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
        INY
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        BNE @nb
        DEX
        BNE @np
        RTS


; draw_str_tms: program VRAM write addr (A=lo, X=hi|$40 already set
; by caller), then emit raw char codes from (sptr_lo/hi) until $FF.
draw_str_tms:
        STA VDP_CTRL
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STX VDP_CTRL
        LDY #$00
@lp:    LDA (sptr_lo),Y
        CMP #$FF
        BEQ @done
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        STA VDP_DATA
        INY
        JSR     tms9918_pad40   ; +40c silicon-strict pad40 (back-to-back VDP store)
        JMP @lp
@done:  RTS


; =============================================
; wait_key / print_str_ax: stock Apple-1 helpers.
; =============================================
; wait_key / poll_key — promoted to dev/lib/apple1/kbd.asm.
; print_str_ax — promoted to dev/lib/apple1/print.asm.
; ZP-tight project: alias print.asm's slot pair to our existing str_lo/str_hi.
print_ptr_lo = str_lo
print_ptr_hi = str_hi
.include "kbd.asm"
.include "print.asm"


; =============================================
; DATA
; =============================================

; --- TMS9918 Graphics I register set ---
; R0 = $00 mode 1 base, R1 = $C0 (16K, display on, Graphics I)
; R2 = $06 name table     = $1800
; R3 = $80 colour table   = $2000
; R4 = $00 pattern table  = $0000
; R5 = $36 sprite attr    = $1B00 (unused, sprites disabled)
; R6 = $07 sprite pattern = $3800
; R7 = $01 backdrop=black, text colour unused in Graphics I
vdp_regs:
        .byte $00, $C0, $06, $80, $00, $36, $07, $01

; --- Pattern-table VRAM offsets for the 6 tiles ---
;     char 0 EMPTY      $0000
;     char 1 WALL       $0008
;     char 2 WALL_PASS  $0010
;     char 8 BODY       $0040
;     char 16 HEAD      $0080
;     char 24 FOOD      $00C0
; The earlier rev shipped only 4 entries (chars 0/8/16/24) which left
; char 1 uninitialised, so deadly walls rendered invisible and BODY/
; HEAD/FOOD inherited the wrong patterns.
tile_vram_lo:
        .byte $00, $08, $10, $40, $80, $C0
tile_vram_hi:
        .byte $00, $00, $00, $00, $00, $00

; --- row * 32 (cell_grid stride / name-table stride) for rows 0..23 ---
row_x32_lo:
        .byte $00, $20, $40, $60, $80, $A0, $C0, $E0
        .byte $00, $20, $40, $60, $80, $A0, $C0, $E0
        .byte $00, $20, $40, $60, $80, $A0, $C0, $E0
row_x32_hi:
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $01, $01, $01, $01, $01, $01, $01, $01
        .byte $02, $02, $02, $02, $02, $02, $02, $02

; --- Tile patterns (4 tiles x 8 bytes = 32 bytes) ---
tile_patterns:
        ; --- Tile 0 EMPTY: all background ---
        .byte $00, $00, $00, $00, $00, $00, $00, $00

        ; --- Tile 1 WALL: classic offset brick mortar (deadly mode) ---
        ;  XXXXXXXX  $FF
        ;  X..X..X.  $92
        ;  X..X..X.  $92
        ;  X..X..X.  $92
        ;  XXXXXXXX  $FF
        ;  ..X..X..  $24
        ;  ..X..X..  $24
        ;  ..X..X..  $24
        .byte $FF, $92, $92, $92, $FF, $24, $24, $24

        ; --- Tile 2 WALL_PASS: porous "wrap-mode" boundary marker ---
        ;  ........  $00
        ;  ........  $00
        ;  ........  $00
        ;  ...XX...  $18
        ;  ...XX...  $18
        ;  ........  $00
        ;  ........  $00
        ;  ........  $00
        ; A single 2x2 dot at the centre of every cell. Reads as a
        ; dotted line on the playfield border, clearly distinct from
        ; the solid-brick deadly wall, so the player sees at a glance
        ; whether they can wrap or will die.
        .byte $00, $00, $00, $18, $18, $00, $00, $00

        ; --- Tile 8 BODY: rounded 6x6 filled square ---
        ;  ........  $00
        ;  ..XXXX..  $3C
        ;  .XXXXXX.  $7E
        ;  .XXXXXX.  $7E
        ;  .XXXXXX.  $7E
        ;  .XXXXXX.  $7E
        ;  ..XXXX..  $3C
        ;  ........  $00
        .byte $00, $3C, $7E, $7E, $7E, $7E, $3C, $00

        ; --- Tile 16 HEAD: rounded square with two "eyes" + mouth ---
        ;  ..XXXX..  $3C
        ;  .XXXXXX.  $7E
        ;  .X.XX.X.  $5A   eyes (gaps)
        ;  .XXXXXX.  $7E
        ;  .XXXXXX.  $7E
        ;  .XX..XX.  $66   mouth
        ;  .XXXXXX.  $7E
        ;  ..XXXX..  $3C
        .byte $3C, $7E, $5A, $7E, $7E, $66, $7E, $3C

        ; --- Tile 24 FOOD: apple (round body + tiny stem) ---
        ;  ...X....  $10   stem
        ;  ..XX....  $30
        ;  .XXXXXX.  $7E
        ;  XXXXXXXX  $FF
        ;  XXXXXXXX  $FF
        ;  XXXXXXXX  $FF
        ;  .XXXXXX.  $7E
        ;  ..XXXX..  $3C
        .byte $10, $30, $7E, $FF, $FF, $FF, $7E, $3C

; --- Colour groups (12 entries, byte = fg<<4 | bg) ---
; Groups 0..3 cover the 4 tile types. Groups 4..6 (chars 32..55) are
; unused so we leave them transparent. Groups 7..11 paint chars 56..95
; (HUD + title glyphs) white-on-black.
tile_colors:
        .byte $E1       ; group 0  empty + wall   fg=14 gray, bg=1 black
        .byte $31       ; group 1  body           fg=3  light green
        .byte $B1       ; group 2  head           fg=11 light yellow
        .byte $81       ; group 3  food           fg=8  medium red
        .byte $11       ; group 4  unused
        .byte $11       ; group 5  unused
        .byte $11       ; group 6  unused
        .byte $F1       ; group 7  chars 56..63   fg=15 white
        .byte $F1       ; group 8  chars 64..71
        .byte $F1       ; group 9  chars 72..79
        .byte $F1       ; group 10 chars 80..87
        .byte $F1       ; group 11 chars 88..95

; =============================================
; HUD glyph char map (lifted from TMS_Sokoban for shape compatibility):
;   56=M  57=V  58=:  59..68='0'..'9'  69=S  70=O  71=K  72=B  73=A
;   74=N  75=P  76=R  77=E  78=L  79=space  80=G  81=H  82=T  83=D
;   84=Y  85=I  86=U  87=Q  88=W  89=Z  90=C  91=X  92=F  93=(  94=)
; =============================================
HUD_C_M   = 56
HUD_C_V   = 57
HUD_C_CL  = 58
HUD_C_D0  = 59
HUD_C_S   = 69
HUD_C_O   = 70
HUD_C_K   = 71
HUD_C_B   = 72
HUD_C_A   = 73
HUD_C_N   = 74
HUD_C_P   = 75
HUD_C_R   = 76
HUD_C_E   = 77
HUD_C_L   = 78
HUD_C_SP  = 79
HUD_C_G   = 80
HUD_C_H   = 81
HUD_C_T   = 82
HUD_C_D   = 83
HUD_C_Y   = 84
HUD_C_I   = 85
HUD_C_U   = 86
HUD_C_Q   = 87
HUD_C_W   = 88
HUD_C_Z   = 89
HUD_C_C   = 90
HUD_C_X   = 91
HUD_C_F   = 92
HUD_C_LP  = 93
HUD_C_RP  = 94

; --- HUD + title glyph patterns (8x8, uploaded to VRAM $01C0) ---
hud_patterns:
        ; char 56 'M'
        .byte $44, $6C, $54, $44, $44, $44, $44, $00
        ; char 57 'V'
        .byte $44, $44, $44, $44, $28, $28, $10, $00
        ; char 58 ':'
        .byte $00, $00, $10, $00, $00, $10, $00, $00
        ; char 59 '0'
        .byte $38, $44, $44, $44, $44, $44, $38, $00
        ; char 60 '1'
        .byte $10, $30, $10, $10, $10, $10, $38, $00
        ; char 61 '2'
        .byte $38, $44, $04, $08, $10, $20, $7C, $00
        ; char 62 '3'
        .byte $38, $44, $04, $38, $04, $44, $38, $00
        ; char 63 '4'
        .byte $44, $44, $44, $7C, $04, $04, $04, $00
        ; char 64 '5'
        .byte $7C, $40, $40, $78, $04, $44, $38, $00
        ; char 65 '6'
        .byte $18, $20, $40, $78, $44, $44, $38, $00
        ; char 66 '7'
        .byte $7C, $04, $08, $10, $20, $20, $20, $00
        ; char 67 '8'
        .byte $38, $44, $44, $38, $44, $44, $38, $00
        ; char 68 '9'
        .byte $38, $44, $44, $3C, $04, $08, $30, $00
        ; char 69 'S'
        .byte $38, $44, $40, $38, $04, $44, $38, $00
        ; char 70 'O'
        .byte $38, $44, $44, $44, $44, $44, $38, $00
        ; char 71 'K'
        .byte $44, $48, $50, $60, $50, $48, $44, $00
        ; char 72 'B'
        .byte $78, $44, $44, $78, $44, $44, $78, $00
        ; char 73 'A'
        .byte $38, $44, $44, $7C, $44, $44, $44, $00
        ; char 74 'N'
        .byte $44, $64, $54, $4C, $44, $44, $44, $00
        ; char 75 'P'
        .byte $78, $44, $44, $78, $40, $40, $40, $00
        ; char 76 'R'
        .byte $78, $44, $44, $78, $50, $48, $44, $00
        ; char 77 'E'
        .byte $7C, $40, $40, $78, $40, $40, $7C, $00
        ; char 78 'L'
        .byte $40, $40, $40, $40, $40, $40, $7C, $00
        ; char 79 ' '
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        ; char 80 'G'
        .byte $38, $44, $40, $4E, $44, $44, $38, $00
        ; char 81 'H'
        .byte $44, $44, $44, $7C, $44, $44, $44, $00
        ; char 82 'T'
        .byte $7C, $10, $10, $10, $10, $10, $10, $00
        ; char 83 'D'
        .byte $78, $44, $44, $44, $44, $44, $78, $00
        ; char 84 'Y'
        .byte $44, $44, $28, $10, $10, $10, $10, $00
        ; char 85 'I'
        .byte $38, $10, $10, $10, $10, $10, $38, $00
        ; char 86 'U'
        .byte $44, $44, $44, $44, $44, $44, $38, $00
        ; char 87 'Q'
        .byte $38, $44, $44, $44, $54, $48, $34, $00
        ; char 88 'W'
        .byte $44, $44, $44, $54, $54, $6C, $44, $00
        ; char 89 'Z'
        .byte $7C, $04, $08, $10, $20, $40, $7C, $00
        ; char 90 'C'
        .byte $38, $44, $40, $40, $40, $44, $38, $00
        ; char 91 'X'
        .byte $44, $44, $28, $10, $28, $44, $44, $00
        ; char 92 'F'
        .byte $7C, $40, $40, $78, $40, $40, $40, $00
        ; char 93 '('
        .byte $08, $10, $20, $20, $20, $10, $08, $00
        ; char 94 ')'
        .byte $20, $10, $08, $08, $08, $10, $20, $00

; --- TMS title strings (raw TMS char codes, $FF terminated) ---
title_snake_tms:
        ; S  N  A  K  E
        .byte 69,74,73,71,77, $FF
title_card_tms:
        ; T M S 9 9 1 8 _ V D P
        .byte 82,56,69,68,68,60,67,79,57,83,75, $FF
title_author_tms:
        ; B  Y  _  V  E  R  H  I  L  L  E  _  A  R  N  A  U  D
        .byte 72,84,79,57,77,76,81,85,78,78,77,79,73,76,74,73,86,83, $FF
title_select_tms:
        ; S E L E C T _ K E Y B O A R D
        .byte 69,77,78,77,90,82,79,71,77,84,72,70,73,76,83, $FF
title_keys_tms:
        ; 1 _ ( W A S D )  2 _ ( Z Q S D )
        ; layout fits 16 chars centred at col 8
        .byte 60,79,93,88,73,69,83,94,79,61,79,93,89,87,69,83,94, $FF
title_walls_select_tms:
        ; S E L E C T _ W A L L S _ _ _   (15 chars to clobber "SELECT KEYBOARD")
        .byte 69,77,78,77,90,82,79,88,73,78,78,69,79,79,79, $FF
title_walls_keys_tms:
        ; 1 _ ( D E A D L Y ) _ 2 _ ( W R A P )   (19 chars, covers old 17-char prompt)
        .byte 60,79,93,83,77,73,83,78,84,94,79,61,79,93,88,76,73,75,94, $FF
title_over_tms:
        ; G A M E _ O V E R
        .byte 80,73,56,77,79,70,57,77,76, $FF
title_win_tms:
        ; Y O U _ W I N
        .byte 84,70,86,79,88,85,74, $FF
title_press_tms:
        ; P R E S S _ A _ K E Y
        .byte 75,76,77,69,69,79,73,79,71,77,84, $FF

; --- Apple-1 text strings (ASCII; print_str_ax sets bit 7) ---
str_title:
        .byte $0D, " SNAKE FOR APPLE 1 + TMS9918", $0D
        .byte " V.ARNAUD 26  EAT THE APPLES!", $0D
        .byte " WALLS AND TAIL ARE FATAL.", $0D
        .byte " P = PAUSE   HI = BEST SCORE", $0D, 0

str_layout:
        .byte $0D, " KEYBOARD: 1 QWERTY  2 AZERTY", $0D, 0

str_walls:
        .byte $0D, " WALLS:    1 DEADLY  2 WRAP", $0D, 0

str_over:
        .byte $0D, " GAME OVER -- KEY=NEW GAME", $0D, 0

str_won:
        .byte $0D, " YOU WIN! -- KEY=NEW GAME", $0D, 0
