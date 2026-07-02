; =============================================
; CodeTank GAME6 copy — TMS_Maze3D, RESURRECTED verbatim from git
; 686fe03^:dev/projects/tms9918_maze3d/TMS_Maze3D.asm (source deleted in
; the 686fe03 refactor, never migrated to sketchs/). Re-added here as a
; RUN-IN-PLACE CodeTank bank program because the shipped Woz-hex image
; (software/Graphic TMS9918/TMS_Maze3D.txt, $0280-$1A78 + BSS $1B00)
; CANNOT be RAM-loaded on Parmigiani dual-bank machines: everything in
; [$1000, $8000) is out-of-range there (Memory.cpp strict OOR), so the
; GAME6 ROM->RAM packer would lose the image's upper 2.7 kB. Linked at
; $4200 in ROM instead (apple1_maze3d_codetank_game6_bank.cfg), with
; GRID/STK/MOBS relocated into the low 4 kB bank ($0E00-$0EFF).
; Code is ROM-clean: every runtime store targets ZP or those segments.
; =============================================
; =============================================
; TMS MAZE 3D - Wizardry-style line maze
; P-LAB TMS9918 Graphic Card - POM1 / Apple 1
; VERHILLE Arnaud - 2026
;
; A 1976-style first-person dungeon crawler with monsters.
; Backtracker-DFS maze (11x7 cells), pseudo-3D wireframe with
; depth shading (stipple/hatching), top-down map toggle and
; turn-based combat against three monster archetypes.
;
; Controls (AZERTY):
;   Z = forward          Q = turn left
;   S = backward         D = turn right
;   M = toggle map / 3D view
;   A = attack (combat)  F = flee  (combat)
;   ESC = quit
;
; =============================================
; Assemble:
;   ca65 -o build/TMS_Maze3D.o software/tms9918/TMS_Maze3D.asm
;   ld65 -C software/tms9918/apple1_maze3d.cfg \
;        -o build/TMS_Maze3D.bin build/TMS_Maze3D.o
; or:
;   python3 software/tms9918/emit_TMS_Maze3D_txt.py
;
; Run in POM1: TMS9918 auto-enables when loading from
; software/tms9918/. File > Load Memory TMS_Maze3D.txt, then 280R.
; =============================================

; ---- Apple 1 I/O ----
        .import tms9918_pad12  ; silicon-strict pad12-v3 (helper from tms9918_pad.asm)
        .import vdp_display_off ; R1=$80 blanking idiom (tms9918_pad.asm)
.include "apple1.inc"

; ---- TMS9918 MMIO (VDP_DATA / VDP_CTRL + WAIT_VBLANK macro) ----
.include "tms9918.inc"

; ---- Keys (Apple 1 ASCII | $80, upper-cased by the keyboard) ----
KEY_ESC   = $9B
KEY_Z     = $DA       ; forward
KEY_S     = $D3       ; backward
KEY_Q     = $D1       ; turn left
KEY_D     = $C4       ; turn right
KEY_M     = $CD       ; toggle map / 3D
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

; --- Game state ---
gstate:     .res 1     ; $2A
prev_state: .res 1     ; previous gameplay state for combat return
quit_flag:  .res 1
view_mode:  .res 1     ; 0=3D, 1=MAP

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
vbuf:       .res 8     ; staging buffer for one 8-byte VRAM group

.code

; =============================================
; main entry
; =============================================
main:
        BIT VDP_CTRL            ; reset the CTRL-port write-pair flip-flop and
                                ; drain a stale status byte — on real silicon a
                                ; half-cocked address pair left by an aborted
                                ; previous program would shift every register
                                ; write below by one byte. POM1 masks this;
                                ; the chip does not.
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
        JSR init_vdp_g2         ; regs + name/color tables, display kept OFF
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR clear_bitmap
        JSR init_sat            ; SAT[0].Y=$D0 + 127x$D1 scrub — without it,
                                ; warm-DRAM power-on noise in the SAT at $3B00
                                ; renders up to 32 ghost sprites over the
                                ; whole game (TMS9918-SPRITE_INIT.md gold
                                ; standard; POM1 --vram-noise reproduces)
        ; Display ON only now that pattern/color/name/SAT are all valid
        ; (best-practices init order: display-off -> tables -> SAT -> on).
        LDA #$C0                ; R1 = 16K | display ON (Mode II: M1/M2=0)
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        LDA #$81
        STA VDP_CTRL
        ; drain stale keystrokes left over from Woz Monitor / paste buffer
        JSR drain_kb

main_loop:
        LDA quit_flag
        BEQ @cont
        ; ESC quit: blank the display (R1=$80 idiom) and hand control back
        ; to the Woz Monitor. We were launched with JMP (runv) by the GAME6
        ; menu — there is NO caller frame, so the old bare RTS here popped
        ; stale stack bytes and jumped into the weeds on real hardware.
        JSR vdp_display_off
        JMP WOZMON
@cont:
        JSR show_title
        LDA quit_flag
        BNE main_loop
        JSR show_help
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
        ; toggle view
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
@n2:    CMP #KEY_Q
        BNE @n3
        ; turn left
        LDA p_face
        SEC
        SBC #1
        AND #$03
        STA p_face
        RTS
@n3:    CMP #KEY_D
        BNE @n4
        ; turn right
        LDA p_face
        CLC
        ADC #1
        AND #$03
        STA p_face
        RTS
@n4:    CMP #KEY_Z
        BNE @n5
        ; forward
        LDA p_face
        JSR try_move
        RTS
@n5:    CMP #KEY_S
        BNE @other
        ; backward = move opposite of facing
        LDA p_face
        CLC
        ADC #2
        AND #$03
        JSR try_move
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
init_vdp_g2:
        LDX #0
@rg:    LDA vdp2_regs,X
        CPX #1
        BNE @nomask
        AND #$BF                ; R1: keep display OFF for the whole init —
                                ; hides power-on VRAM garbage on real
                                ; silicon and rides the free ScreenOff
                                ; slot table; main: re-arms $C0 once the
                                ; tables + SAT are valid.
@nomask:
        STA VDP_CTRL
        TXA
        ORA #$80
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_CTRL
        INX
        CPX #8
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @rg

        ; --- write linear name table at $3800 ---
        ; name[row*32+col] = (row & 7)*32 + col
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$78               ; $38 | $40
        STA VDP_CTRL
        LDX #3                 ; 3 thirds
@th:    LDY #0                 ; bytes 0..255 in this third
@nm:    TYA
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        INY
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @nm
        DEX
        BNE @th

        ; --- color table: $F1 everywhere ($2000-$37FF, 6144 bytes) ---
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$60               ; $20 | $40
        STA VDP_CTRL
        LDX #24                ; 24 pages of 256
        LDY #0
@cl:    LDA #$F1
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        INY
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @cl
        DEX
        BNE @cl
        RTS

vdp2_regs:
        .byte $02, $C0, $0E, $FF, $03, $76, $03, $F1

; =============================================
; init_sat - SAT scrub at $3B00 (R5=$76): SAT[0].Y = $D0 chain
; terminator + $D1 fill for the remaining 127 bytes, so power-on VRAM
; noise can never render ghost sprites (the game itself never writes
; the SAT — one boot-time scrub covers every title/help/game redraw).
; Runs with the display still blanked, so no pads strictly needed, but
; pad12 keeps the write cadence uniform with the rest of the file.
; =============================================
init_sat:
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$7B                ; $3B | $40 = write to $3B00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        LDA #$D0                ; SAT[0].Y = chain terminator
        STA VDP_DATA
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        LDA #$D1                ; scrub bytes 1..127 (gold standard)
        LDX #127
@sc:    STA VDP_DATA
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        DEX
        BNE @sc
        RTS

; =============================================
; clear_bitmap - write 0 to all 6144 pattern bytes
; =============================================
clear_bitmap:
        LDX #24                ; 24 char rows * 256 B = 6144 B. NOTE: a
                               ; "skip the HUD row" variant is NOT safe —
                               ; the 3D view bleeds into rows 22-23
                               ; (frame_by[0] = 191), stale wall art would
                               ; linger under the HUD.
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +18c silicon-strict (back-to-back CTRL store)
        LDA #$40               ; $00 | $40 = write start of VRAM
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +18c silicon-strict (CTRL -> first DATA store)
        LDY #64                ; 64 * 4 unrolled bytes = 256 per char row
        LDA #$00
@lp:    STA VDP_DATA
        JSR     tms9918_pad12   ; single pad per store: 22c+ STA->STA, clears
                                ; the 16c silicon floor with margin. The old
                                ; loop was DOUBLE-padded (47c/byte) — the
                                ; full-screen clear alone took ~283 ms.
        STA VDP_DATA
        JSR     tms9918_pad12
        STA VDP_DATA
        JSR     tms9918_pad12
        STA VDP_DATA
        JSR     tms9918_pad12
        DEY
        BNE @lp
        LDY #64
        DEX
        BNE @lp
        RTS

; =============================================
; vdp_set_write_addr_xy
;  pix_addr_lo / pix_addr_hi already computed
; sends low byte then (high | $40) to VDP_CTRL
; =============================================
vdp_set_write:
        LDA pix_addr_lo
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA pix_addr_hi
        ORA #$40
        STA VDP_CTRL
        RTS

vdp_set_read:
        LDA pix_addr_lo
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA pix_addr_hi
        STA VDP_CTRL
        RTS

; =============================================
; calc_pix_addr  (input pix_x, pix_y)
;   addr = (y >> 6)*2048 + ((y >> 3) & 7)*256 + (x & ~7) + (y & 7)
; result in pix_addr_lo / pix_addr_hi
; =============================================
calc_pix_addr:
        ; low = (x & ~7) + (y & 7)
        LDA pix_x
        AND #$F8
        STA tmp
        LDA pix_y
        AND #$07
        ORA tmp
        STA pix_addr_lo
        ; high = (y & ~7) >> ?    in fact  ((y >> 3) & 7) gives bits 0..2 of high byte (0,1,...,7 = 0x00..0x07)
        ;        plus (y >> 6)*8 gives bits 3..4 (third 0,1,2 = 0x00, 0x08, 0x10)
        ; Equivalent: high = ((y & $F8) >> 0)  shifted... compute directly:
        ; high_byte = (y & $F8) shifted right by 0... let's just do:
        LDA pix_y
        AND #$F8                ; clears low 3 bits, leaves bits 3..7
        ; need to shift right by 0 then... addr_hi = (y >> 0) & $F8 ?? let me re-derive:
        ; total addr = ((y>>6)<<11) + (((y>>3)&7)<<8) + (x & $F8) + (y&7)
        ;            = ((y & $C0) << 5) + ((y & $38) << 5) + (x & $F8) + (y & 7)
        ; (y>>6)<<11 = y & $C0 << 5  (since y>>6 takes bits 6,7 to bits 0,1; <<11 puts them at bits 11,12 = high byte bits 3,4)
        ; ((y>>3)&7)<<8: takes bits 3,4,5 of y to bits 0,1,2; <<8 puts them at high byte bits 0,1,2
        ; So high byte = (y & $F8) >> 3 << 3 ?? Hmm easier:
        ; high byte = ((y >> 3) & 7) | ((y >> 6) << 3) but bits don't overlap so it's literally
        ;   ((y >> 3) & 0x1F) since y<192 means y>>3 is 0..23.
        ; (y>>3) ranges 0..23 = 0x00..0x17 - fits perfectly.
        LDA pix_y
        LSR
        LSR
        LSR                     ; A = y >> 3 (0..23)
        STA pix_addr_hi
        RTS

; =============================================
; plot_pixel  (pix_x, pix_y, A=mode 0=set, 1=clear)
; uses tmp as mode flag
; =============================================
plot_set:
        ; bounds check (Y only; X is unsigned 0..255 always in range)
        LDA pix_y
        CMP #192
        BCS plot_done
        JSR calc_pix_addr
        ; compute mask = $80 >> (x & 7)
        LDA pix_x
        AND #$07
        TAX
        LDA bitmask,X
        STA pix_mask
        ; read existing byte
        JSR vdp_set_read
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA VDP_DATA
        ORA pix_mask
        STA pix_byte
        JSR vdp_set_write
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA pix_byte
        STA VDP_DATA
plot_done:
        RTS

bitmask:
        .byte $80, $40, $20, $10, $08, $04, $02, $01

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
; Byte-oriented since juillet 2026: 8-aligned full spans are written as
; single $FF bytes (1 VRAM access per 8 px instead of 16 per px through
; plot_set's read-modify-write) — the repaint-speed fix. Edge pixels
; still go through plot_set.
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
        ; full 8-px byte: write $FF in one access
        JSR calc_pix_addr
        JSR vdp_set_write
        JSR     tms9918_pad12   ; +18c silicon-strict (CTRL -> DATA store)
        LDA #$FF
        STA VDP_DATA
        JSR     tms9918_pad12   ; +18c silicon-strict (DATA -> next access)
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
; Batched since juillet 2026: inside one 8-row pattern group the VRAM
; addresses are consecutive, so we stream-read up to 8 bytes, OR the
; column mask in, and stream-write them back — 2 address setups per
; 8 px instead of 2 per px (the repaint-speed fix).
; =============================================
vline:
        LDA fl_y1
        CMP #192
        BCC @yok
        LDA #191                ; clamp to the bitmap (plot_set used to)
        STA fl_y1
@yok:   LDA fl_x0
        STA pix_x
        AND #$07
        TAX
        LDA bitmask,X
        STA vl_mask
        LDA fl_y0
        STA pix_y
@grp:   ; vl_cnt = min(8 - (y & 7), fl_y1 - y + 1)
        LDA pix_y
        AND #$07
        STA tmp
        LDA #8
        SEC
        SBC tmp
        STA vl_cnt
        LDA fl_y1
        SEC
        SBC pix_y
        CLC
        ADC #1
        CMP vl_cnt
        BCS @full
        STA vl_cnt
@full:  JSR calc_pix_addr
        JSR vdp_set_read
        JSR     tms9918_pad12   ; +18c silicon-strict (CTRL -> first DATA read)
        LDX #0
@rd:    LDA VDP_DATA
        ORA vl_mask
        STA vbuf,X
        JSR     tms9918_pad12   ; +18c silicon-strict (33c read->read)
        INX
        CPX vl_cnt
        BNE @rd
        JSR vdp_set_write
        JSR     tms9918_pad12   ; +18c silicon-strict (CTRL -> first DATA store)
        LDX #0
@wr:    LDA vbuf,X
        STA VDP_DATA
        JSR     tms9918_pad12   ; +18c silicon-strict (30c STA->STA)
        INX
        CPX vl_cnt
        BNE @wr
        LDA pix_y
        CLC
        ADC vl_cnt
        BCS @done               ; wrapped past y=255
        STA pix_y
        CMP fl_y1
        BCC @grp
        BEQ @grp
@done:  RTS

; =============================================
; write_char: place 8x8 glyph at cell (ch_cx, ch_cy)
; ch_code = ASCII code
; =============================================
write_char:
        ; addr = (cy >> 3)*2048 + (cy & 7)*256 + cx*8
        LDA ch_cy
        AND #$07
        STA pix_addr_lo         ; low byte: (cy & 7)*256 -> high byte only
        LDA ch_cx
        ASL
        ASL
        ASL                     ; cx*8
        STA pix_addr_lo
        ; high = (cy >> 3)*8 + (cy & 7)
        LDA ch_cy
        AND #$F8                ; (cy & ~7)
        STA tmp                 ; (cy & ~7) value, equiv (cy>>3)*8
        LDA ch_cy
        AND #$07
        ORA tmp
        STA pix_addr_hi
        ; lookup font: 8*(code - $20)
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
        ROL tmp2                ; tmp:tmp2 = (code-$20)*8
        CLC
        LDA tmp
        ADC #<font_base
        STA ptr_lo
        LDA tmp2
        ADC #>font_base
        STA ptr_hi
        ; set VDP write addr
        JSR vdp_set_write
        JSR     tms9918_pad12   ; +18c silicon-strict (CTRL -> first DATA store)
        LDY #0
@lp:    LDA (ptr_lo),Y
        STA VDP_DATA
        JSR     tms9918_pad12   ; single pad: 30c STA->STA (pad + INY/CPY/BNE
                                ; + LDA), clears the 16c silicon floor
        INY
        CPY #8
        BNE @lp
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

; =============================================
; show_title: title screen on bitmap
; =============================================
show_title:
        JSR clear_bitmap
        ; Banner box
        LDA #20
        STA fl_x0
        LDA #235
        STA fl_x1
        LDA #28
        STA fl_y0
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR hline
        LDA #60
        STA fl_y0
        JSR hline
        LDA #20
        STA fl_x0
        LDA #28
        STA fl_y0
        LDA #60
        STA fl_y1
        JSR vline
        LDA #235
        STA fl_x0
        JSR vline

        ; "MAZE 3D" big text (use 8x8 font, centered)
        LDA #11
        STA ch_cx
        LDA #4
        STA ch_cy
        LDA #<str_title1
        LDX #>str_title1
        JSR print_str_ax

        LDA #6
        STA ch_cx
        LDA #9
        STA ch_cy
        LDA #<str_title2
        LDX #>str_title2
        JSR print_str_ax

        LDA #7
        STA ch_cx
        LDA #11
        STA ch_cy
        LDA #<str_title3
        LDX #>str_title3
        JSR print_str_ax

        LDA #5
        STA ch_cx
        LDA #14
        STA ch_cy
        LDA #<str_title4
        LDX #>str_title4
        JSR print_str_ax

        LDA #4
        STA ch_cx
        LDA #20
        STA ch_cy
        LDA #<str_press_any
        LDX #>str_press_any
        JSR print_str_ax

        ; small dungeon decoration: 3D wireframe of a closed room
        LDA #160
        STA ln_x0
        LDA #105
        STA ln_y0
        LDA #220
        STA ln_x1
        LDA #105
        STA ln_y1
        JSR line_xy
        LDA #220
        STA ln_x0
        LDA #105
        STA ln_y0
        LDA #220
        STA ln_x1
        LDA #170
        STA ln_y1
        JSR line_xy
        LDA #220
        STA ln_x0
        LDA #170
        STA ln_y0
        LDA #160
        STA ln_x1
        LDA #170
        STA ln_y1
        JSR line_xy
        LDA #160
        STA ln_x0
        LDA #170
        STA ln_y0
        LDA #160
        STA ln_x1
        LDA #105
        STA ln_y1
        JSR line_xy
        ; perspective lines
        LDA #160
        STA ln_x0
        LDA #105
        STA ln_y0
        LDA #185
        STA ln_x1
        LDA #125
        STA ln_y1
        JSR line_xy
        LDA #220
        STA ln_x0
        LDA #105
        STA ln_y0
        LDA #195
        STA ln_x1
        LDA #125
        STA ln_y1
        JSR line_xy
        LDA #220
        STA ln_x0
        LDA #170
        STA ln_y0
        LDA #195
        STA ln_x1
        LDA #150
        STA ln_y1
        JSR line_xy
        LDA #160
        STA ln_x0
        LDA #170
        STA ln_y0
        LDA #185
        STA ln_x1
        LDA #150
        STA ln_y1
        JSR line_xy
        ; back wall
        LDA #185
        STA ln_x0
        LDA #125
        STA ln_y0
        LDA #195
        STA ln_x1
        LDA #125
        STA ln_y1
        JSR line_xy
        LDA #195
        STA ln_x0
        LDA #125
        STA ln_y0
        LDA #195
        STA ln_x1
        LDA #150
        STA ln_y1
        JSR line_xy
        LDA #195
        STA ln_x0
        LDA #150
        STA ln_y0
        LDA #185
        STA ln_x1
        LDA #150
        STA ln_y1
        JSR line_xy
        LDA #185
        STA ln_x0
        LDA #150
        STA ln_y0
        LDA #185
        STA ln_x1
        LDA #125
        STA ln_y1
        JSR line_xy

        JSR wait_key
        CMP #KEY_ESC
        BNE @ok
        INC quit_flag
@ok:    RTS

; =============================================
; show_help: instructions screen
; =============================================
show_help:
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

        JSR wait_key
        CMP #KEY_ESC
        BNE @ok
        INC quit_flag
@ok:    RTS

; =============================================
; show_win
; =============================================
show_win:
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
        JSR wait_key
        CMP #KEY_ESC
        BNE @ok
        INC quit_flag
@ok:    RTS

; =============================================
; show_lose
; =============================================
show_lose:
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
        JSR wait_key
        CMP #KEY_ESC
        BNE @ok
        INC quit_flag
@ok:    RTS

; =============================================
; render_3d - main scene
; =============================================
render_3d:
        ; Sync to VBlank before the full 3D-scene rebuild burst.
        WAIT_VBLANK
        JSR clear_bitmap

        ; floor & ceiling base lines (horizon at y=96)
        LDA #0
        STA fl_x0
        LDA #255
        STA fl_x1
        LDA #95
        STA fl_y0
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR hline
        LDA #96
        STA fl_y0
        JSR hline

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
        ; front blocked: draw closing front wall
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
        ; HUD: HP, ATK, level, facing
        JSR draw_hud_3d
        ; mob present at next forward cell? (give a hint via sprite-like icon)
        JSR draw_mob_indicator

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
        LDX rd_depth
        LDA frame_lx,X
        STA fl_x0
        LDA frame_rx,X
        STA fl_x1
        LDA frame_ty,X
        STA fl_y0
        JSR hline
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
        LDA frame_rx,X
        STA fl_x0
        JSR vline
        ; mortar seam: horizontal mid-line spanning the wall
        LDX rd_depth
        LDA frame_lx,X
        CLC
        ADC #1
        STA fl_x0
        LDA frame_rx,X
        SEC
        SBC #1
        STA fl_x1
        LDA frame_ty,X
        CLC
        ADC frame_by,X
        ROR
        STA fl_y0
        JSR hline
        RTS

; =============================================
; draw_hud_3d - small status under the floor area
; =============================================
draw_hud_3d:
        LDA #0
        STA ch_cx
        LDA #22
        STA ch_cy
        LDA #<str_hud_hp
        LDX #>str_hud_hp
        JSR print_str_ax
        LDA #4
        STA ch_cx
        LDA #22
        STA ch_cy
        LDA p_hp
        JSR write_decimal_2d

        LDA #8
        STA ch_cx
        LDA #22
        STA ch_cy
        LDA #<str_hud_atk
        LDX #>str_hud_atk
        JSR print_str_ax
        LDA #13
        STA ch_cx
        LDA #22
        STA ch_cy
        LDA p_atk
        JSR write_decimal_2d

        LDA #16
        STA ch_cx
        LDA #22
        STA ch_cy
        LDA #<str_hud_lvl
        LDX #>str_hud_lvl
        JSR print_str_ax
        LDA #20
        STA ch_cx
        LDA #22
        STA ch_cy
        LDA p_lvl
        JSR write_decimal_2d

        LDA #24
        STA ch_cx
        LDA #22
        STA ch_cy
        LDA p_face
        TAX
        LDA face_chars,X
        STA ch_code
        JSR write_char
        RTS

face_chars:
        .byte 'N', 'E', 'S', 'W'

; =============================================
; draw_mob_indicator: if the next forward cell holds a live mob AND the
; passage to it is open (a mob behind a wall is not visible), draw the
; monster standing in the corridor + a '!' above it. The old code drew
; only a lone '!' floating at the top of the screen, wall or no wall.
; =============================================
draw_mob_indicator:
        ; visible only through an open passage
        LDA p_col
        STA rd_col
        LDA p_row
        STA rd_row
        JSR check_front_wall
        BNE @none
        ; target = (p_col + dx, p_row + dy)
        LDA p_col
        CLC
        ADC rd_dx
        STA tmp
        LDA p_row
        CLC
        ADC rd_dy
        STA tmp2
        LDX #0
@lp:    LDA mob_type,X
        CMP #MOB_DEAD
        BEQ @nx
        LDA mob_col,X
        CMP tmp
        BNE @nx
        LDA mob_row,X
        CMP tmp2
        BNE @nx
        JMP draw_mob_figure     ; mob ahead and visible
@nx:    INX
        CPX #NUM_MOBS
        BNE @lp
@none:  RTS

; =============================================
; draw_mob_figure: monster silhouette standing in the corridor one
; cell ahead — triangle head + eyes on the horizon, '!' above.
; =============================================
draw_mob_figure:
        ; head triangle (112,108)-(143,108)-(127,80)
        LDA #112
        STA ln_x0
        LDA #108
        STA ln_y0
        LDA #143
        STA ln_x1
        LDA #108
        STA ln_y1
        JSR line_xy
        LDA #112
        STA ln_x0
        LDA #108
        STA ln_y0
        LDA #127
        STA ln_x1
        LDA #80
        STA ln_y1
        JSR line_xy
        LDA #143
        STA ln_x0
        LDA #108
        STA ln_y0
        LDA #127
        STA ln_x1
        LDA #80
        STA ln_y1
        JSR line_xy
        ; eyes
        LDA #122
        STA pix_x
        LDA #98
        STA pix_y
        JSR plot_set
        LDA #133
        STA pix_x
        JSR plot_set
        ; '!' warning above the head
        LDA #15
        STA ch_cx
        LDA #8
        STA ch_cy
        LDA #'!'
        STA ch_code
        JMP write_char

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
; Cell size 16x16 px; maze 11x7 -> 176x112; centered with header.
; =============================================
render_map:
        ; Sync to VBlank before the top-down map rebuild burst.
        WAIT_VBLANK
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
        LDA #(40)
        STA fl_x0
        LDA #(40+11*16)         ; 216
        STA fl_x1
        LDA #(24)
        STA fl_y0
        JSR hline
        LDA #(24+7*16)          ; 136
        STA fl_y0
        JSR hline

        LDA #40
        STA fl_x0
        LDA #(24)
        STA fl_y0
        LDA #(24+7*16)
        STA fl_y1
        JSR vline
        LDA #(40+11*16)
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
        ; vertical line at x = 40+(col+1)*16, y 24+row*16 .. 24+(row+1)*16
        LDA rd_col
        CLC
        ADC #1
        ASL
        ASL
        ASL
        ASL                     ; (col+1)*16
        CLC
        ADC #40
        STA fl_x0
        LDA rd_row
        ASL
        ASL
        ASL
        ASL
        CLC
        ADC #24
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
        ; horizontal line at y = 24+(row+1)*16, x 40+col*16 .. +16
        LDA rd_col
        ASL
        ASL
        ASL
        ASL
        CLC
        ADC #40
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
        ADC #24
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
        LDA #6
        STA ch_cx
        LDA #4
        STA ch_cy
        LDA #'S'
        STA ch_code
        JSR write_char

        ; E in bottom-right cell of map: cell col=10, row=6 -> px=40+10*16+4=204, py=24+6*16+4=124
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
@stay:  RTS
@flee_ok:
        ; pop back to previous gameplay state
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
        ; gain XP
        LDA p_xp
        CLC
        ADC #4
        STA p_xp
        CMP #10
        BCC @nolvl
        SEC
        SBC #10
        STA p_xp
        INC p_lvl
        INC p_atk
        LDA p_lvl
        AND #$01
        BNE @nolvl
        INC p_def
@nolvl:
        ; back to previous gameplay state
        LDA prev_state
        STA gstate
        RTS
@mob_alive:
        ; monster's turn
        JSR mob_attacks
        LDA p_hp
        BEQ @die2
        BPL @end
@die2:  LDA #ST_LOSE
        STA gstate
@end:   RTS

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
        ; Sync to VBlank before the combat-scene rebuild burst.
        WAIT_VBLANK
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

        ; Monster HP
        LDA #5
        STA ch_cx
        LDA #6
        STA ch_cy
        LDA #<str_mob_hp
        LDX #>str_mob_hp
        JSR print_str_ax
        LDA #11
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

        ; Monster portrait: simple line drawing per type
        LDX cur_mob
        LDA mob_type,X
        BEQ @gob
        CMP #1
        BEQ @orc
        ; mage (default)
        JSR draw_mage
        RTS
@gob:   JSR draw_goblin
        RTS
@orc:   JSR draw_orc
        RTS

; =============================================
; goblin: small triangle head + ears
; =============================================
draw_goblin:
        ; head triangle
        LDA #110
        STA ln_x0
        LDA #80
        STA ln_y0
        LDA #150
        STA ln_x1
        LDA #80
        STA ln_y1
        JSR line_xy
        LDA #110
        STA ln_x0
        LDA #80
        STA ln_y0
        LDA #100
        STA ln_x1
        LDA #110
        STA ln_y1
        JSR line_xy
        LDA #150
        STA ln_x0
        LDA #80
        STA ln_y0
        LDA #160
        STA ln_x1
        LDA #110
        STA ln_y1
        JSR line_xy
        LDA #100
        STA ln_x0
        LDA #110
        STA ln_y0
        LDA #160
        STA ln_x1
        LDA #110
        STA ln_y1
        JSR line_xy
        ; eyes
        LDA #122
        STA pix_x
        LDA #92
        STA pix_y
        JSR plot_set
        LDA #138
        STA pix_x
        JSR plot_set
        ; mouth
        LDA #120
        STA fl_x0
        LDA #140
        STA fl_x1
        LDA #102
        STA fl_y0
        JSR hline
        RTS

; =============================================
; orc: bigger head with horns
; =============================================
draw_orc:
        ; jaw box
        LDA #100
        STA fl_x0
        LDA #160
        STA fl_x1
        LDA #75
        STA fl_y0
        JSR hline
        LDA #115
        STA fl_y0
        JSR hline
        LDA #100
        STA fl_x0
        LDA #75
        STA fl_y0
        LDA #115
        STA fl_y1
        JSR vline
        LDA #160
        STA fl_x0
        JSR vline
        ; horns
        LDA #100
        STA ln_x0
        LDA #75
        STA ln_y0
        LDA #90
        STA ln_x1
        LDA #60
        STA ln_y1
        JSR line_xy
        LDA #160
        STA ln_x0
        LDA #75
        STA ln_y0
        LDA #170
        STA ln_x1
        LDA #60
        STA ln_y1
        JSR line_xy
        ; eyes
        LDA #115
        STA pix_x
        LDA #88
        STA pix_y
        JSR plot_set
        LDA #145
        STA pix_x
        JSR plot_set
        LDA #116
        STA pix_x
        JSR plot_set
        LDA #146
        STA pix_x
        JSR plot_set
        ; tusks
        LDA #115
        STA ln_x0
        LDA #115
        STA ln_y0
        LDA #110
        STA ln_x1
        LDA #125
        STA ln_y1
        JSR line_xy
        LDA #145
        STA ln_x0
        LDA #115
        STA ln_y0
        LDA #150
        STA ln_x1
        LDA #125
        STA ln_y1
        JSR line_xy
        RTS

; =============================================
; mage: cone (hat) + circle (face)
; =============================================
draw_mage:
        ; hat
        LDA #95
        STA ln_x0
        LDA #95
        STA ln_y0
        LDA #165
        STA ln_x1
        LDA #95
        STA ln_y1
        JSR line_xy
        LDA #95
        STA ln_x0
        LDA #95
        STA ln_y0
        LDA #130
        STA ln_x1
        LDA #45
        STA ln_y1
        JSR line_xy
        LDA #165
        STA ln_x0
        LDA #95
        STA ln_y0
        LDA #130
        STA ln_x1
        LDA #45
        STA ln_y1
        JSR line_xy
        ; face circle (approx: rectangle with cut corners)
        LDA #105
        STA fl_x0
        LDA #155
        STA fl_x1
        LDA #100
        STA fl_y0
        JSR hline
        LDA #130
        STA fl_y0
        JSR hline
        LDA #105
        STA fl_x0
        LDA #100
        STA fl_y0
        LDA #130
        STA fl_y1
        JSR vline
        LDA #155
        STA fl_x0
        JSR vline
        ; eyes
        LDA #115
        STA pix_x
        LDA #110
        STA pix_y
        JSR plot_set
        LDA #145
        STA pix_x
        JSR plot_set
        ; beard hatching
        LDA #115
        STA ln_x0
        LDA #130
        STA ln_y0
        LDA #130
        STA ln_x1
        LDA #145
        STA ln_y1
        JSR line_xy
        LDA #145
        STA ln_x0
        LDA #130
        STA ln_y0
        LDA #130
        STA ln_x1
        LDA #145
        STA ln_y1
        JSR line_xy
        RTS

; =============================================
; DATA TABLES
; =============================================

; row_offset[r] = r * NCOLS  (NCOLS=11)
row_offset:
        .byte 0, 11, 22, 33, 44, 55, 66

; depth frame coordinates (5 entries: depth 0..MAX_DEPTH)
; depth 0 = whole screen, 4 = vanishing
frame_lx:
        .byte   0,  40,  72,  96, 112
frame_rx:
        .byte 255, 215, 183, 159, 143
frame_ty:
        .byte   0,  30,  54,  72,  84
frame_by:
        .byte 191, 161, 137, 119, 107

; Monster names
mob_names_lo:
        .byte <str_mob_gob, <str_mob_orc, <str_mob_mage
mob_names_hi:
        .byte >str_mob_gob, >str_mob_orc, >str_mob_mage

; ---- Strings (null-terminated, ASCII < 128) ----
str_title1:   .byte "MAZE 3D",0
str_title2:   .byte "WIZARDRY-STYLE",0
str_title3:   .byte "DUNGEON CRAWLER",0
str_title4:   .byte "FOR P-LAB TMS9918 + APPLE 1",0
str_press_any:.byte "PRESS ANY KEY...",0

str_help_h1:  .byte "HOW TO PLAY",0
str_help_l1:  .byte "Z   FORWARD       Q   TURN LEFT",0
str_help_l2:  .byte "S   BACKWARD      D   TURN RIGHT",0
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

; END
