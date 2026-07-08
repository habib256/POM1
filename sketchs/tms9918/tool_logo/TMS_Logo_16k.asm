; ============================================================================
; TMS_Logo_16k.asm  --  APPLE-1 LOGO V2.6 for TMS9918 (16 KB variant)
;                       (c) 2026 VERHILLE Arnaud
; ============================================================================
; *** V2.6 -- 16 KB BUILD (active development) ***
;
; Forked from V1.8 (the final 8 KB build, frozen). This variant assumes a
; 16 KB Apple-1 (or future CodeTank ROM) and uses the headroom for:
;
;   - 12 user procs (was 5), 248-byte bodies (was 154 B).
;   - A 1 024-byte control stack in PROCBSS (16 deep frames) replacing
;     the pair of 60 B save buffers from V1.8 -- so non-tail nested
;     proc calls go beyond the V1.8 hard limit of 1 nesting level.
;   - IFELSE a OP b [yes] [no] (in addition to the bare IF).
;   - Comparison operators <=, >=, <> (alongside <, >, =).
;
; Tail-call optimization (zero-frame recursion) is unchanged from V1.8 --
; spiral 4 90 still costs zero control-stack frames.
;
; Build:
;   Build: make
;   python3 software/tms9918/emit_TMS_Logo_txt.py
;
; Run on POM1:  ./POM1 --preset 9       (P-LAB Apple-1 with TMS9918 + CodeTank)
;   then in Woz Monitor paste TMS_Logo.txt and type 280R.
;
; Language summary (V1.8):
;   -- Core turtle commands (and 4-letter aliases) --
;     TR/RIGHT n, TL/LEFT n        turn right / left, degrees
;     FD/FORWARD n, BK/BACK n      forward / back N pixels (draws if pen down)
;     PU/PENUP, PD/PENDOWN         pen up / down
;     HOME                         x=128, y=96, heading=0 (no clear)
;     CS/CLEARSCREEN               clear screen + HOME
;     SETXY x y, SETH n            absolute placement
;     PRINT "WORD or PRINT N       Apple-1 screen text output
;     WAIT N                       pause N seconds
;     BYE                          return to Woz Monitor
;     HELP                         in-app reference
;   -- Control flow / variables --
;     REPEAT N [ ... ]             loop N times
;     REPEAT FOREVER [ ... ]       endless (ESC or Ctrl-G aborts)
;     IF a > b [ ... ]             also < and = ; STOP exits proc early
;     MAKE NAME N                  global var, :NAME reads it
;     :var + N  /  :var - N        single-level arithmetic in arg position
;     RANDOM N                     uniform 0..N-1 in arg position
;     TO NAME :p1 :p2 ... END      define proc, up to 6 procs, 154 B body
;     NAME args                    invoke proc; tail recursion is free
;                                   (zero stack growth -- spiral 4 90 = 49
;                                    logical levels in one frame)
;   -- Built-ins --
;     DEMO                         multi-scene slideshow
;
; ID/name length: 6 chars across procs, vars, params (THING vs THING1
; stay distinct). Proc bodies up to 154 B, 5 proc slots, 1 level of
; non-tail nesting (REPEAT inside a proc + proc call inside the slice).
;
; VDP helpers (init_vdp_g2, clear_bitmap, plot_set) live in
; dev/lib/tms9918/tms9918m2.asm. Math (signed_sin, RANDOM LFSR, decimal
; print) lives in dev/lib/m6502/math.asm. Both are linked as separate
; objects via the Makefile.
;
; line_xy here uses a 16-bit signed err so FD up to 255 doesn't glitch on
; near-diagonal angles (Maze3D's 8-bit-err variant overflowed for |2*err|>127).
; ============================================================================

; --- I/O equates (Apple-1 + TMS9918 hardware) ------------------------------
        .import tms9918_pad18  ; silicon-strict pad18-v4 (helper from tms9918_pad.asm)
        .import vdp_display_off    ; lib helper (tms9918_pad.asm)
.include "apple1.inc"
.include "tms9918.inc"          ; VDP_CTRL, VDP_DATA equates for SETSHAPE

; The CodeTank ROM places this CODE segment at $4000 (upper bank). The
; user types `4000R` from Wozmon; we MUST land in `main` (cold-start),
; not in wait_key (which would blocking-poll the keyboard and silently
; return to Wozmon on the first key). Force a JMP main as the very first
; bytes of the CODE segment, BEFORE the kbd.asm include below.
.segment "CODE"
        jmp main

.include "kbd.asm"              ; lib/apple1/kbd.asm: wait_key + poll_key
.ifdef CODETANK_BUILD
.export wait_key                ; resolve buffer_editor.o's .import (Chess.asm pattern)
.endif

; --- Imports from sibling modules -----------------------------------------
;
; tms9918m2.asm  -- Mode-2 bitmap driver (init + plot + Bresenham line).
.import   init_vdp_g2, clear_bitmap, disable_sprites, line_xy
.import   calc_pix_addr, vdp_set_write, vdp_set_read
.import   plot_set              ; single-pixel plotter (GEN2 emote blit;
                                ; both backends export it)
.ifdef LOGO_GEN2
; 9-bit-X seam (GEN2 only): full 0..279 HGR width for the turtle + bubble.
.import   line_xy16, plot_set_x16
.importzp ln_x0h, ln_x1h, pix_xh
.endif
.importzp ln_x0, ln_y0, ln_x1, ln_y1, pix_x, pix_y
.importzp pix_addr_lo, pix_addr_hi
.importzp pen_color           ; lib-owned pen colour (set via SETPC)
;
; sprites_emotes.asm  -- 12 SCROLL-O-SPRITES expression patterns (16x16,
; 32 B each). Always linked: shape_table references these via .word.
.import   serious_pat, happy_pat, excited_pat, sad_pat, hurt_pat, angry_pat
.import   upset_pat, smug_pat, sick_pat, sleeping_pat, yarr_pat, nerd_pat
.import   bird1_pat, bird2_pat, heart_pat
;
; sprite_helpers.asm -- apply_sprite_size + heading_to_octant.
.import   apply_sprite_size, heading_to_octant
;
; text_bitmap.asm -- charmap_table (1 KB) + 8x8 glyph blitter. Gated by
; CODETANK_BUILD inside the lib (the dev DRAM build's CODE budget can't
; afford the 1 KB charmap, so the .o assembles empty there). LOGO's
; blit_glyph wraps text_blit_glyph to copy tx_lo/ty_lo -> pix_x/pix_y.
.ifdef CODETANK_BUILD
.import   text_blit_glyph
.import   draw_bubble       ; bubble.asm
.import   bufed_run         ; buffer_editor.asm
.endif
;
; math.asm        -- fixed-point trig + LFSR + decimal output + mod360.
.import   roll_lfsr, print_decimal, div_arg_by_10
.import   mod360_arg, mod360_tmp, norm360
.import   signed_sin, mul_dist_by_signed, negate_prod

; --- Exports consumed by sibling modules ----------------------------------
.exportzp tmp, tmp2, arg_lo, arg_hi, arg2_lo, arg2_hi, th_lo, th_hi
.exportzp mptr_lo, mptr_hi    ; text_bitmap.asm uses (mptr),Y as glyph cursor
.exportzp spr_size, spr_xoff, spr_yoff, spr_r1   ; sprite_helpers.asm
.export   prod_lo, prod_hi, sign_flag, lfsr_lo, lfsr_hi, plot_mode
;
; --- buffer_editor.asm bindings (CodeTank-only) -----------------------------
; The editor lib (dev/lib/tms9918/buffer_editor.asm) consumes the LOGO
; proc-slot layout via these abstract constants + ZP/BSS handles. cmd_edit
; loads shape_pat_lo:hi with the slot pointer before JSR bufed_run.
.ifdef CODETANK_BUILD
.exportzp shape_pat_lo, shape_pat_hi
.exportzp line_idx
.export   line_buf
BUFED_BODYLEN_OFF = PROC_BODYLEN_OFF
BUFED_BODY_OFF    = PROC_BODY_OFF
BUFED_BODY_MAX    = PROC_BODY_MAX
BUFED_NAME_LEN    = NAME_LEN
BUFED_LINE_MAX    = LINE_MAX
.exportzp BUFED_BODYLEN_OFF, BUFED_BODY_OFF, BUFED_BODY_MAX
.exportzp BUFED_NAME_LEN, BUFED_LINE_MAX
.endif

LINE_MAX = 60
MAX_VARS = 6
NAME_LEN          = 6     ; uppercase identifier width (proc, var, param). >=4
                          ;   so command lookup against the 4-byte mnem_tab
                          ;   still matches via bytes 0..3; the extra two
                          ;   bytes let names like THING vs THING1 stay
                          ;   distinct, and accept trailing digits.
MAX_PROCS         = 10
PROC_SLOT         = 244   ; V2.0 slot: 6 name + 1 nparams + 12 (2x6) param names + 1 body_len + 224 body
PROC_BODY_MAX     = 224   ; comfortable headroom (V1.8 had 154 = exactly THING's longform)
CTRL_STACK_DEPTH  = 16    ; max non-tail proc-invocation depth (frames of CTRL_FRAME bytes each)
CTRL_FRAME        = 64    ; per-frame: 60 B line_buf + 1 B line_idx + 1 B body_cursor + 1 B body_total + 1 B pad
MAX_PARAMS        = 2
PROC_NPARAMS_OFF  = 6     ; slot offset of n_params byte
PROC_PARAMS_OFF   = 7     ; slot offset of param-names block (2 x NAME_LEN)
PROC_BODYLEN_OFF  = 19    ; PROC_PARAMS_OFF + 2*NAME_LEN
PROC_BODY_OFF     = 20    ; slot offset of first body char
VAR_ENTRY_SIZE    = 8     ; NAME_LEN + 2 (16-bit value)

; --- Zero page ------------------------------------------------------------
; The 16 pix_*/ln_* slots used by the bitmap driver live in tms9918m2.asm
; and are imported above. Only interpreter / turtle / parser ZP here.
.segment "ZEROPAGE"
tmp:          .res 1
tmp2:         .res 1
tx_lo:        .res 1     ; turtle x low byte (TMS: 0..255; GEN2 + tx_hi: 0..279)
.ifdef LOGO_GEN2
tx_hi:        .res 1     ; turtle x high bit (0/1) -> full 280-px HGR width
.endif
ty_lo:        .res 1     ; turtle y  (0..191)
th_lo:        .res 1     ; heading low  (degrees)
th_hi:        .res 1     ; heading high (0 or 1) -- always < 360
pen:          .res 1     ; 1 = pen down
arg_lo:       .res 1
arg_hi:       .res 1
arg2_lo:      .res 1     ; SETXY 2nd arg / multiply hi-byte scratch
arg2_hi:      .res 1
line_idx:     .res 1     ; index into line_buf
mptr_lo:      .res 1     ; mnemonic table cursor / handler ptr
mptr_hi:      .res 1
rep_end:      .res 1     ; REPEAT: index of matching ']'
rep_start:    .res 1     ; REPEAT: index just past '['
; ctrl_frame pointer (used by (zp),Y in proc_invoke push/pop -- requires ZP).
mptr_save_lo: .res 1
mptr_save_hi: .res 1
; cmd_setshape scratch pointer to the matched shape pattern -- (zp),Y indirect
; addressing requires this be in ZP.
shape_pat_lo: .res 1
shape_pat_hi: .res 1
; (Removed dir_table_lo/hi -- TURTL/BOAT directional sprites are gone.)
; --- Sprite size geometry (V2.1: 8x8 + 16x16 mixed) ----------------------
; spr_size: bytes per sprite pattern (8 or 32). Used by cmd_setshape's
;   static-copy loop and by update_dir_turtle's dir-table copy loop.
; spr_xoff/spr_yoff: subtract from (tx,ty) before writing the sprite
;   attribute so the pattern is centred on the turtle position.
;     16x16: subtract 8 / 9 (Y=ty-9 because hardware shows sprite at
;            scanline Y+1 and the 16x16 origin is top-left).
;     8x8:   subtract 4 / 5.
; spr_r1: full R1 byte to load via VDP_CTRL whenever cmd_setshape changes
;   sprite mode. $C2 = 16K + DISP + IE_off + sprite-16, $C0 = sprite-8.
; All four are derived from spr_size by apply_sprite_size so callers
; only have to write the size byte then JSR.
spr_size:    .res 1
spr_xoff:    .res 1
spr_yoff:    .res 1
spr_r1:      .res 1
.ifdef LOGO_GEN2
; --- GEN2 software-sprite (emote) blit scratch (HGR has no HW sprites, so
;     SETSHAPE shapes are XOR-blitted bitmaps that survive plot_set's A/X/Y
;     clobber via these ZP loop vars). ---
em_x0:       .res 1     ; sprite top-left X low byte (9-bit X)
em_x0h:      .res 1     ; sprite top-left X high byte (emote can sit in 256..279)
em_y0:       .res 1     ; sprite top-left Y
em_row:      .res 1     ; current row 0..dim-1
em_col:      .res 1     ; current col 0..dim-1
em_dim:      .res 1     ; 16 (16x16) or 8 (8x8)
em_color:    .res 1     ; 0 = default solid-white 2x2 emote (BIRDFLY etc.);
                        ; 1 = colourised emote (DEMO2 narrator): tinted by
                        ; pen_color, drawn every-other-column so HGR shows the
                        ; hue instead of collapsing two adjacent dots to white.
em_erase:    .res 1     ; colour path only: 0 = draw (OR + pen family bit),
                        ; 1 = erase (XOR clears the dots, palette bit is
                        ; invisible on the black narrator field). Ignored by the
                        ; white path, which XORs both ways (self-inverse).
em_par:      .res 1     ; colour path: parity nudge (0/1) LATCHED at draw time.
                        ; The matching erase reuses it, so a SETPC that changes
                        ; pen_color between a sprite's draw and its next erase
                        ; still XORs the exact columns that were drawn (else the
                        ; erase misses and residue washes the emote to white).
.endif
; --- Colour ------------------------------------------------------------
; All colourisable surfaces (trail, bitmap arrow, sprite-0, bitmap text)
; share a single source of truth: pen_color, exported by tms9918m2.asm.
; SETPC is the only command that writes to it.
; --- EDIT state (ed_cur_line / ed_n_lines) lives in buffer_editor.asm now.

; --- BSS scratch in $0200 page --------------------------------------------
.segment "LINEBUF"
line_buf:  .res LINE_MAX  ; 64 bytes
mnem_buf:  .res 6         ; up to NAME_LEN uppercase letters/digits / spaces
n_letters: .res 1
break_flag: .res 1        ; 0 = normal, 1 = ESC abort pending. Polled
                          ;   at top of REPEAT, parse_and_exec, and proc body
                          ;   loops; printed as "BREAK" by print_status, then
                          ;   cleared. Procs and vars survive intact.
stop_flag:  .res 1        ; 0 = normal, 1 = STOP requested by current proc.
                          ;   Drains through cmd_repeat / parse_and_exec /
                          ;   proc_invoke @line_loop. Cleared at proc_invoke
                          ;   @body_done so STOP only exits the immediate proc
                          ;   (matching classic LOGO semantics).
sign_flag: .res 1         ; multiply sign result
prod_lo:   .res 1
prod_hi:   .res 1
nx_save_lo: .res 1        ; nx held across the cos multiply (else arg2 clobbered)
nx_save_hi: .res 1
ny_save:    .res 1        ; ny clamped, held across line_xy (else tmp clobbered)
bk_sign:    .res 1        ; FD/BK distance sign latch (0 = forward, $FF = backward)
plot_mode:    .res 1      ; 0 = OR (default), 1 = XOR (turtle)
turtle_visible: .res 1    ; 1 = turtle currently drawn into the bitmap
; --- V2.0 dynamic-turtle ("sprite") mode -----------------------------------
; sprite_mode = 0: classic bitmap turtle (XOR triangle). FORWARD draws the
;                  trail and the turtle is rendered into the bitmap.
; sprite_mode = 1: dynamic turtle. SETSHAPE has loaded a 16x16 pattern into
;                  the TMS9918 sprite-pattern slot 0 and 16x16 sprites are
;                  enabled (R1 |= $02). draw_turtle now writes Y/X to the
;                  sprite attribute table at $3B00 instead of XORing into
;                  the bitmap; erase_turtle is a no-op (sprites are
;                  independent of the pattern table). FORWARD still draws
;                  the trail unless PU.
sprite_mode:    .res 1    ; 0 = bitmap turtle, 1 = dynamic (sprite) turtle
; (Removed dir_turtle_active / last_octant -- TURTL/BOAT directional
;  sprites are gone, every shape is static now.)
; (shape_pat_lo / shape_pat_hi live in ZEROPAGE -- (zp),Y indirect access.)
s_byte:    .res 1         ; signed sin(heading) * 64
c_byte:    .res 1         ; signed cos(heading) * 64
s_tip:     .res 1         ; (TIP_FWD * sin)/64 -- tip vertex offset
c_tip:     .res 1         ; (TIP_FWD * cos)/64
s_back:    .res 1         ; (BACK * sin)/64    -- back vertex offset
c_back:    .res 1         ; (BACK * cos)/64
tx0:       .res 1         ; turtle vertex 0 x (tip)
ty0:       .res 1         ; turtle vertex 0 y
tx1:       .res 1         ; vertex 1 (back-left)
ty1:       .res 1
tx2:       .res 1         ; vertex 2 (back-right)
ty2:       .res 1
.ifdef LOGO_GEN2
tx0h:      .res 1         ; turtle-vertex X high bytes (9-bit X, full 280 width)
tx1h:      .res 1
tx2h:      .res 1
.endif
; --- Arrow background save state (option B: save/restore the 9 cells in
;     the 3x3 box centred on the turtle, so OR-mode draw can be cleanly
;     reversed without XOR's transient trail-bit inversion). The 72 byte
;     pattern buffer lives in PROCBSS (LBUF is full); the iteration
;     counters stay here next to plot_mode.
arrow_rows_left:  .res 1
arrow_cols_left:  .res 1
arrow_bx:         .res 1   ; leftmost cell column of the saved bbox
arrow_io_dir:     .res 1   ; 0 = save (VDP -> BSS), 1 = restore (BSS -> VDP)
n_vars:    .res 1         ; current number of vars in use (0..MAX_VARS)
n_procs:   .res 1         ; current number of user procedures (0..MAX_PROCS)
cmd_status: .res 1        ; 0 = nothing, 1 = OK, 2 = ERR (per parse_and_exec call)
lfsr_lo:    .res 1        ; 16-bit LFSR seed for RANDOM
lfsr_hi:    .res 1

; --- User-procedure storage --------------------------------------------------
; MAX_PROCS slots, layout per slot above (PROC_SLOT bytes).
.segment "PROCBSS"
; -- Control stack (V2.0). Replaces V1.8's pair of 60-byte save buffers.
; Each non-tail nested proc_invoke pushes one CTRL_FRAME-byte frame:
;     bytes 0..59   line_buf snapshot (LINE_MAX = 60)
;     byte  60      line_idx
;     byte  61      proc_body_cursor of caller (so caller's @line_loop resumes)
;     byte  62      proc_body_total  of caller
;     byte  63      reserved
; Tail-call optimisation pushes nothing; the stack only grows for
; non-tail calls (proc invoked mid-line, e.g. inside a REPEAT slice).
; CTRL_STACK_DEPTH frames = 16 → enough for THING1 = REPEAT 4 [THING],
; deeply nested REPEAT/proc combos, and recursion that mixes tail and
; non-tail calls.
ctrl_stack:  .res 1024        ; CTRL_STACK_DEPTH (16) x CTRL_FRAME (64)
ctrl_sp:     .res 1           ; frame count, 0..CTRL_STACK_DEPTH

; -- variable table -- MAX_VARS entries x (NAME_LEN name + 2 byte value).
var_table:   .res 48
proc_table:  .res 2440        ; MAX_PROCS (10) x PROC_SLOT (244)
; V1.5 procedure parameters -- 2 named slots layered on top of var_table.
; var_lookup checks these first, so :SIZE in a proc body resolves to its
; param even if a global named SIZE also exists. After RTS we restore
; values via PHA/PLA on the 6502 stack (caller's stack frame).
param_slot0_name:  .res 6
param_slot0_value: .res 2
param_slot1_name:  .res 6
param_slot1_value: .res 2
n_params_active:   .res 1    ; 0..MAX_PARAMS, valid params currently live
; --- Arrow background buffer (see option B notes above). 16 cells × 8
;     pattern bytes per cell = 128 bytes. The box is 4x4 (32x32 px) to
;     guarantee tip coverage at any heading -- a 3x3 box is just shy of
;     the tip's ±9 px reach when the turtle sits in the second half of
;     a cell. Lives here because LBUF is full.
arrow_bg_pat:      .res 128
; in_proc_save was the V1.8 0/1/2 depth counter; in V2.0 it lives in
; PROCBSS as `ctrl_sp` (frame count, 0..CTRL_STACK_DEPTH). Alias here so
; existing tail-call check / init code doesn't need to touch every site.
in_proc_save = ctrl_sp
def_mode:          .res 1    ; 0 = REPL, 1 = currently collecting body lines for a new proc
cur_proc_lo:       .res 1    ; -> current proc slot during definition
cur_proc_hi:       .res 1
proc_body_cursor:  .res 1    ; proc_invoke: cursor into slot body across line_loop
proc_body_total:   .res 1    ; proc_invoke: total body_len
err_save_lo:       .res 1    ; print_err: outer mptr_lo around the message walk
err_save_hi:       .res 1
if_a_lo:           .res 1    ; cmd_if: latched LHS value across @scan_block
if_a_hi:           .res 1
if_op_save:        .res 1    ; cmd_if: comparison operator char
tail_p0_lo:        .res 1    ; tail-call trampoline: latched param-0 value
tail_p0_hi:        .res 1
; mptr_save_lo/hi MUST live in zero page so the (zp),Y indirect addressing
; mode in ctrl_push / ctrl_pop works. Declared up in ZEROPAGE (search for
; "ctrl_frame pointer").

; ============================================================================
.segment "CODE"
; ============================================================================
; Entry point  ($0280)
; ============================================================================
main:
        SEI
        CLD
        LDX #$FF
        TXS
        JSR print_banner
        JSR init_vdp_g2
        JSR clear_bitmap
        JSR disable_sprites ; lib m2: defensive SAT.Y fill + $D0 terminator
                            ; (full $D1 burst per doc § 4.2, see m2.asm)
        LDA #0
        STA plot_mode
        STA turtle_visible
        STA sprite_mode
.ifdef LOGO_GEN2
        ; GEN2 only: the turtle X is 9-bit (tx_lo + tx_hi). cmd_home sets
        ; tx_lo=128 but NOT tx_hi, so a non-zero power-on RAM value in this
        ; BSS byte would fling the boot arrow to column 128+256 = 384 (off the
        ; right edge -> nothing drawn). Silicon presets fill RAM with noise, so
        ; the turtle was intermittently invisible at first start. Seed it 0.
        STA tx_hi
        STA em_color              ; default emote = solid white (BIRDFLY look)
        STA em_erase
        STA em_par
.endif
        ; Default sprite geometry = 16x16 (consistent with V2.0 behaviour).
        ; apply_sprite_size at the next SETSHAPE picks the right values.
        LDA #32
        STA spr_size
        JSR apply_sprite_size
        LDA #0
        STA n_vars
        STA n_procs
        STA n_params_active
        STA in_proc_save
        STA def_mode
        STA break_flag
        STA stop_flag
        ; Pen down by default. V1.8 set this lazily inside cmd_home;
        ; V2.0 fixed cmd_home to preserve pen across HOME (so PU+HOME+
        ; REPEAT[FLY] doesn't leak a trail), so we have to explicitly
        ; initialise pen here, otherwise the very first scenes of DEMO
        ; (which haven't called PD yet) draw nothing.
        LDA #1
        STA pen
        ; pen_color initialised to $0F (white) by init_vdp_g2 (lib-owned).
        ; LFSR seed -- non-zero
        LDA #$AC
        STA lfsr_lo
        LDA #$E1
        STA lfsr_hi
        JSR cmd_home          ; centre + heading 0 + draw turtle
repl:
        JSR new_prompt
        JSR read_line
        LDA def_mode
        BEQ @do_parse
        JSR proc_collect_line
        JMP repl
@do_parse:
        LDA #0
        STA line_idx
        STA cmd_status
        JSR parse_and_exec
        JSR print_status
        JMP repl

; ============================================================================
; Banner / prompt
; ============================================================================
print_banner:
        LDX #0
@l:     LDA banner_msg,X
        BEQ @done
        ORA #$80
        JSR ECHO
        INX
        BNE @l
@done:  RTS

new_prompt:
        ; no leading CR -- the previous OK / err / read_line echo already
        ; ended with a CR, so we'd just produce a blank line.
        LDA def_mode
        BEQ @std
        LDA #'>'
        ORA #$80
        JSR ECHO
        LDA #' '
        ORA #$80
        JSR ECHO
        RTS
@std:   LDA #'?'
        ORA #$80
        JSR ECHO
        LDA #' '
        ORA #$80
        JSR ECHO
        RTS

banner_msg:
        .byte $0D, "APPLE-1 LOGO V2.6 - HELP", $0D
.ifdef LOGO_GEN2
        ; GEN2-only extra line; the TMS build's banner stays byte-for-byte as
        ; shipped (keeps Codetank_GAME3.rom identical).
        .byte "GEN2 HGR COLOR CARD", $0D
.endif
        .byte "(C) 2026 A. VERHILLE", $0D
        .byte 0

; -------------------------------------------------------------------------
; Paginated HELP. cmd_help (above) does the dispatch; here we just lay out
; the table of contents and the 8 detail pages. Each page is null-
; terminated. The TOC fits comfortably in a 24-line window so the user
; can read it in one go before drilling into a topic.
; -------------------------------------------------------------------------
help_toc:
        .byte $0D
        .byte "APPLE-1 LOGO V2.6 -- HELP", $0D
.ifdef LOGO_GEN2
        .byte "GEN2 HGR graphics, 48K Apple-1", $0D
.else
        .byte "TMS9918 graphics, 16K Apple-1", $0D
.endif
        .byte $0D
        .byte "Type HELP N for a topic page:", $0D
        .byte $0D
        .byte "  1  TURTLE MOTION", $0D
        .byte "  2  PEN AND SCREEN", $0D
        .byte "  3  DYNAMIC TURTLE (sprite)", $0D
        .byte "  4  CONTROL FLOW", $0D
        .byte "  5  VARIABLES & ARITHMETIC", $0D
        .byte "  6  PROCEDURES", $0D
        .byte "  7  CONSOLE & DEMO", $0D
        .byte "  8  EXAMPLES", $0D
.ifdef CODETANK_BUILD
        .byte "  9  PROCEDURE EDITOR", $0D
.endif
        .byte $0D
        .byte "ESC/Ctrl-G abort, BYE = Wozmon.", $0D
        .byte 0

help_p1:
        .byte $0D, "1. TURTLE MOTION", $0D
        .byte "  FD N / FORWARD N", $0D
        .byte "    move forward N pixels", $0D
        .byte "  BK N / BACK N", $0D
        .byte "    move backward N pixels", $0D
        .byte "  TR N / RIGHT N", $0D
        .byte "    turn right N degrees", $0D
        .byte "  TL N / LEFT N", $0D
        .byte "    turn left N degrees", $0D
        .byte "  HOME", $0D
        .byte "    centre 128,96, heading 0", $0D
        .byte "  SETXY X Y", $0D
        .byte "    move to absolute (X,Y)", $0D
        .byte "  SETH N", $0D
        .byte "    set absolute heading 0..359", $0D
        .byte 0

help_p2:
        .byte $0D, "2. PEN AND SCREEN", $0D
        .byte "  PD / PENDOWN", $0D
        .byte "    draw a trail (default)", $0D
        .byte "  PU / PENUP", $0D
        .byte "    no trail when moving", $0D
        .byte "  CS / CLEARSCREEN", $0D
        .byte "    erase bitmap + HOME", $0D
        .byte "  SETPC N  colour 0..15 --", $0D
        .byte "    tints trail, arrow,", $0D
        .byte "    sprite and text.", $0D
        .byte $0D
        .byte "Colour persists across", $0D
        .byte "procs and REPEAT.", $0D
        .byte 0

help_p3:
        .byte $0D, "3. DYNAMIC TURTLE", $0D
        .byte "  SETSHAPE ", $22, "NAME", $0D
        .byte "    swap to a 16x16 sprite.", $0D
        .byte "    First call enables sprite", $0D
        .byte "    mode and erases the bitmap", $0D
        .byte "    triangle.", $0D
        .byte $0D
        .byte "Built-in shapes:", $0D
        .byte "  BIRD1   bird, wings up", $0D
        .byte "  BIRD2   bird, wings down", $0D
        .byte "  HEART   small heart (8x8)", $0D
        .byte "Emotes 16x16 (CC-BY Quale):", $0D
        .byte "  NORMAL HAPPY  SUPER  SAD", $0D
        .byte "  UPSET  ANGRY  GRUMPY PERV", $0D
        .byte "  SICK   SLEEP  PIRATE SHADES", $0D
        .byte $0D
        .byte "Animation example:", $0D
        .byte "  TO FLY", $0D
        .byte "    SETSHAPE ", $22, "BIRD1", $0D
        .byte "    FD 4", $0D
        .byte "    SETSHAPE ", $22, "BIRD2", $0D
        .byte "    FD 4", $0D
        .byte "  END", $0D
        .byte "  PU  REPEAT 30 [FLY]", $0D
        .byte 0

help_p4:
        .byte $0D, "4. CONTROL FLOW", $0D
        .byte "  REPEAT N [ ... ]", $0D
        .byte "    iterate N times", $0D
        .byte "  REPEAT FOREVER [ ... ]", $0D
        .byte "    endless loop. ESC or Ctrl-G", $0D
        .byte "    abort cleanly; procs and", $0D
        .byte "    variables survive intact.", $0D
        .byte "  IF A OP B [ ... ]", $0D
        .byte "    OP is one of:", $0D
        .byte "      <  >  =  <=  >=  <>", $0D
        .byte "  IFELSE A OP B [Y] [N]", $0D
        .byte "    run [Y] if true else [N]", $0D
        .byte "  STOP", $0D
        .byte "    exit current proc early", $0D
        .byte 0

help_p5:
        .byte $0D, "5. VARIABLES & ARITHMETIC", $0D
        .byte "  MAKE NAME N", $0D
        .byte "    set global var NAME = N", $0D
        .byte "  :NAME", $0D
        .byte "    read NAME in any arg", $0D
        .byte "  :V + N    :V - N", $0D
        .byte "    arithmetic in args", $0D
        .byte "  RANDOM N", $0D
        .byte "    uniform 0..N-1", $0D
        .byte $0D
        .byte "Up to 6 globals via MAKE.", $0D
        .byte "Names are 6 chars (A-Z, 0-9).", $0D
        .byte "Inside a proc body, :p1 :p2", $0D
        .byte "shadow same-named globals.", $0D
        .byte 0

help_p6:
        .byte $0D, "6. PROCEDURES", $0D
        .byte "  TO NAME :p1 :p2 ... END", $0D
        .byte "    multiline definition.", $0D
        .byte "    Up to 2 named parameters.", $0D
        .byte "  NAME args", $0D
        .byte "    invoke previously defined", $0D
        .byte "    procedure with 0..2 args.", $0D
.ifdef CODETANK_BUILD
        .byte "  LIST", $0D
        .byte "    list names (text mode)", $0D
        .byte "  LIST NAME", $0D
        .byte "    dump body on bitmap.", $0D
        .byte "  EDIT NAME", $0D
        .byte "    visual editor on bitmap.", $0D
        .byte "    See HELP 9 for full keys.", $0D
.endif
        .byte $0D
        .byte "Limits:", $0D
        .byte "  10 procs, 224 B per body", $0D
        .byte "  16-level nested-call depth", $0D
        .byte "  6-char identifiers (so", $0D
        .byte "  THING and THING1 are", $0D
        .byte "  distinct procs).", $0D
        .byte $0D
        .byte "Tail recursion is FREE: a proc", $0D
        .byte "whose final statement calls", $0D
        .byte "another proc reuses the frame.", $0D
        .byte "spiral 4 90 = 49 levels in", $0D
        .byte "a single frame.", $0D
        .byte 0

help_p7:
        .byte $0D, "7. CONSOLE & DEMO", $0D
        .byte "  PRINT ", $22, "WORD", $0D
        .byte "    print literal word", $0D
        .byte "  PRINT N", $0D
        .byte "    print decimal", $0D
        .byte "  WAIT N", $0D
        .byte "    pause N seconds", $0D
        .byte "  DEMO", $0D
        .byte "    built-in slideshow", $0D
.ifdef CODETANK_BUILD
        .byte "  LABEL ", $22, "TEXT", $0D
        .byte "    text on bitmap (8x8)", $0D
        .byte "  SAY ", $22, "TEXT", $0D
        .byte "    speech bubble below", $0D
        .byte "    sprite (auto-position)", $0D
        .byte "  DEMO2", $0D
        .byte "    POM1 tells its story", $0D
.endif
        .byte "  HELP        this menu", $0D
        .byte "  HELP N      a topic page", $0D
        .byte "  BYE         exit to Wozmon", $0D
        .byte $0D
        .byte "ESC / Ctrl-G   abort a loop", $0D
        .byte "_              backspace input", $0D
        .byte 0

help_p8:
        .byte $0D, "8. EXAMPLES", $0D
        .byte "  TO SQUARE", $0D
        .byte "    REPEAT 4 [FD 50 RT 90]", $0D
        .byte "  END", $0D
        .byte "  TO FLOWER", $0D
        .byte "    REPEAT 36 [RT 10 SQUARE]", $0D
        .byte "  END", $0D
        .byte "  FLOWER", $0D
        .byte $0D
        .byte "  TO SPIRAL :S :A", $0D
        .byte "    IF :S > 100 [STOP]", $0D
        .byte "    FD :S", $0D
        .byte "    RT :A", $0D
        .byte "    SPIRAL :S + 2 :A", $0D
        .byte "  END", $0D
        .byte "  CS  SPIRAL 4 90", $0D
        .byte $0D
        .byte "  TO FLY", $0D
        .byte "    SETSHAPE ", $22, "BIRD1", $0D
        .byte "    FD 4", $0D
        .byte "    SETSHAPE ", $22, "BIRD2", $0D
        .byte "    FD 4", $0D
        .byte "  END", $0D
        .byte "  PU  HOME  REPEAT 30 [FLY]", $0D
        .byte 0

.ifdef CODETANK_BUILD
help_p9:
        .byte $0D, "9. PROCEDURE EDITOR", $0D
        .byte "EDIT NAME opens a full-screen", $0D
        .byte "visual editor on the TMS9918", $0D
        .byte "bitmap. > marks the current", $0D
        .byte "line. A 2-row menu at the", $0D
        .byte "bottom shows the keys.", $0D
        .byte $0D
        .byte "NAVIGATION:", $0D
        .byte "  U   cursor up", $0D
        .byte "  D   cursor down", $0D
        .byte $0D
        .byte "MUTATION:", $0D
        .byte "  E   edit current line", $0D
        .byte "  I   insert blank AFTER", $0D
        .byte "  X   delete current line", $0D
        .byte $0D
        .byte "EXIT:", $0D
        .byte "  Q / ESC  save and return", $0D
        .byte $0D
        .byte "INSIDE E MODE:", $0D
        .byte "  type new line, _ erases", $0D
        .byte "  last char, CR commits,", $0D
        .byte "  ESC aborts the change.", $0D
        .byte $0D
        .byte "Limits: 224 B body, 60 char", $0D
        .byte "per line. Saving is in RAM", $0D
        .byte "only -- power-cycle wipes it.", $0D
        .byte $0D
        .byte "DEMO procs (SQUARE, FLOWER,", $0D
        .byte "SPIRAL...) come from ROM:", $0D
        .byte "LIST is fine, EDIT changes", $0D
        .byte "are lost on next DEMO run.", $0D
        .byte "Use TO MYNAME ... END first.", $0D
        .byte 0
.endif

; Stub so the legacy help_msg label still resolves (no longer used by
; cmd_help itself; if some demo line still references HELP-style text,
; it picks up the TOC).
help_msg = help_toc

.if 0
; ---- old single-block help_msg (replaced by paginated above; kept here
;      under .if 0 so the source still has the previous reference layout
;      if you ever want to diff against it). The assembler skips the
;      whole block. -----------------------------------------------------
help_msg_unused_remove:
        .byte $0D
        .byte "==========================", $0D
        .byte "APPLE-1 LOGO V2.6 (16K)", $0D
        .byte "(C) 2026 VERHILLE ARNAUD", $0D
        .byte "==========================", $0D
        .byte $0D
        .byte "*** TURTLE MOTION ***", $0D
        .byte "  FD N / FORWARD N", $0D
        .byte "    move forward N pixels", $0D
        .byte "  BK N / BACK N", $0D
        .byte "    move back N pixels", $0D
        .byte "  TR N / RIGHT N", $0D
        .byte "    turn right N degrees", $0D
        .byte "  TL N / LEFT N", $0D
        .byte "    turn left N degrees", $0D
        .byte "  HOME", $0D
        .byte "    centre 128,96, heading 0", $0D
        .byte "  SETXY X Y", $0D
        .byte "    move to absolute (X,Y)", $0D
        .byte "  SETH N", $0D
        .byte "    set absolute heading 0..359", $0D
        .byte $0D
        .byte "*** PEN ***", $0D
        .byte "  PU / PENUP", $0D
        .byte "    no trail when moving", $0D
        .byte "  PD / PENDOWN", $0D
        .byte "    draw trail (default)", $0D
        .byte "  CS / CLEARSCREEN", $0D
        .byte "    erase + HOME", $0D
        .byte $0D
        .byte "*** DYNAMIC TURTLE (V2.6) ***", $0D
        .byte "  SETSHAPE ", $22, "NAME", $0D
        .byte "    swap to a 16x16 sprite.", $0D
        .byte "    First call enables sprite", $0D
        .byte "    mode and erases the bitmap", $0D
        .byte "    triangle. Built-in shapes:", $0D
        .byte "      BIRD1  bird wings up", $0D
        .byte "      BIRD2  bird wings down", $0D
        .byte "      TURTL  chunky turtle", $0D
        .byte $0D
        .byte "*** CONTROL FLOW ***", $0D
        .byte "  REPEAT N [ ... ]", $0D
        .byte "    iterate N times", $0D
        .byte "  REPEAT FOREVER [ ... ]", $0D
        .byte "    endless. ESC or CTRL-G", $0D
        .byte "    abort cleanly. Procs and", $0D
        .byte "    variables survive intact.", $0D
        .byte "  IF A OP B [ ... ]", $0D
        .byte "    run block when condition", $0D
        .byte "    holds. OP is one of:", $0D
        .byte "      <  >  =  <=  >=  <>", $0D
        .byte "  IFELSE A OP B [Y] [N]", $0D
        .byte "    run [Y] if true else [N]", $0D
        .byte "  STOP", $0D
        .byte "    exit current proc early", $0D
        .byte $0D
        .byte "*** VARIABLES ***", $0D
        .byte "  MAKE NAME N", $0D
        .byte "    set global var NAME=N", $0D
        .byte "  :NAME", $0D
        .byte "    read NAME in any arg", $0D
        .byte "  :V + N   :V - N", $0D
        .byte "    arithmetic in args", $0D
        .byte "  RANDOM N", $0D
        .byte "    uniform 0..N-1", $0D
        .byte $0D
        .byte "*** PROCEDURES ***", $0D
        .byte "  TO NAME :p1 :p2 ... END", $0D
        .byte "    multiline definition.", $0D
        .byte "    Parameters are referenced", $0D
        .byte "    as :p1, :p2 inside body.", $0D
        .byte "  NAME args", $0D
        .byte "    invoke previously defined", $0D
        .byte "    procedure with N args.", $0D
        .byte "  Limits: 10 procs, 224 B body,", $0D
        .byte "  6-char A-Z 0-9 ids (distinct).", $0D
        .byte "  Tail recursion is FREE: a proc", $0D
        .byte "  whose last call reuses frame", $0D
        .byte "  (zero stack growth).", $0D
        .byte "  Other calls: 16 levels max.", $0D
        .byte $0D
        .byte "*** CONSOLE ***", $0D
        .byte "  PRINT ", $22, "WORD", $0D
        .byte "    print a literal word", $0D
        .byte "  PRINT N", $0D
        .byte "    print decimal number", $0D
        .byte "  WAIT N", $0D
        .byte "    pause N seconds", $0D
        .byte "  HELP", $0D
        .byte "    this reference", $0D
        .byte "  DEMO", $0D
        .byte "    built-in slideshow", $0D
        .byte "  BYE", $0D
        .byte "    return to Woz Monitor", $0D
        .byte $0D
        .byte "*** EXAMPLES ***", $0D
        .byte "  TO SQUARE", $0D
        .byte "    REPEAT 4 [FD 50 RT 90]", $0D
        .byte "  END", $0D
        .byte "  TO FLOWER", $0D
        .byte "    REPEAT 36 [RT 10 SQUARE]", $0D
        .byte "  END", $0D
        .byte "  FLOWER", $0D
        .byte $0D
        .byte "  TO SPIRAL :S :A", $0D
        .byte "    IF :S > 100 [STOP]", $0D
        .byte "    FD :S", $0D
        .byte "    RT :A", $0D
        .byte "    SPIRAL :S + 2 :A", $0D
        .byte "  END", $0D
        .byte "  CS  SPIRAL 4 90", $0D
        .byte $0D
        .byte "  TO FLY", $0D
        .byte "    SETSHAPE ", $22, "BIRD1", $0D
        .byte "    FD 4", $0D
        .byte "    SETSHAPE ", $22, "BIRD2", $0D
        .byte "    FD 4", $0D
        .byte "  END", $0D
        .byte "  PU  REPEAT 30 [FLY]", $0D
        .byte $0D
        .byte "*** BREAK / EDIT ***", $0D
        .byte "  ESC      abort current loop", $0D
        .byte "  CTRL-G   same (telnet)", $0D
        .byte "  _        backspace at prompt", $0D
        .byte $0D
        .byte "Type any command. The prompt is", $0D
        .byte "?  in REPL,  >  inside TO/END.", $0D
        .byte 0
.endif

; ============================================================================
; read_line: poll KBD until CR, echo each char, store into line_buf.
;            CR is stored at line_buf[len].  Overflow silently truncates.
; ============================================================================
read_line:
        LDX #0
@wait:  JSR wait_key          ; A = key, bit 7 stripped (lib/apple1/kbd.asm)
        CMP #$0D
        BEQ @cr
        CMP #'_'              ; Wozmon-style destructive backspace
        BEQ @bs
        CPX #LINE_MAX-1
        BCS @no_store
        STA line_buf,X
        INX
@no_store:
        ORA #$80              ; ECHO wants bit 7 set
        JSR ECHO
        JMP @wait
@bs:    CPX #0
        BEQ @bs_skip          ; nothing to erase
        DEX
@bs_skip:
        ORA #$80              ; A still holds '_' from wait_key
        JSR ECHO
        JMP @wait
@cr:    LDA #$0D
        STA line_buf,X
        ORA #$80              ; A still $0D
        JSR ECHO
        RTS

; ============================================================================
; poll_break: non-blocking check of $D011 for ESC ($1B) or Ctrl-G ($07).
;             If found, set break_flag = 1. Any other key consumed (the
;             one-keystroke cost of polling -- accept since users press
;             ESC/Ctrl-G to abort, not data keys).
;             ESC works for GUI keyboard input. Ctrl-G works over telnet
;             where the TerminalCard intercepts a bare ESC as a sequence
;             prefix.
; ============================================================================
poll_break:
        JSR poll_key            ; A = key (bit 7 stripped) or 0 if no key
        BEQ @done               ; lib/apple1/kbd.asm sets Z when none pending
        CMP #$1B                ; ESC
        BEQ @set
        CMP #$07                ; Ctrl-G (BEL) -- classic LOGO interrupt
        BNE @done
@set:   LDA #1
        STA break_flag
@done:  RTS

; ============================================================================
; parse_and_exec: tokenize from line_idx, dispatch each command, advance.
;                 Stops on CR or ']' (REPEAT block end), or break_flag.
; ============================================================================
parse_and_exec:
@loop:  LDA break_flag
        BNE @abort
        LDA stop_flag
        BNE @abort
        JMP @cont
@abort: RTS                     ; ESC or STOP pending -- bail to caller
@cont:  JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #$0D
        BNE @not_cr
        JMP @ok
@not_cr:CMP #']'
        BNE @not_rb
        JMP @ok
@not_rb:
        JSR find_mnem        ; -> A=flag, mptr=handler, line_idx advanced. C=1 on miss.
        BCC @hit
        JMP @try_proc        ; long jump (added flags pushed @try_proc out of BCS range)
@hit:   STA tmp               ; flag byte
        CMP #1
        BEQ @one_arg
        CMP #2
        BEQ @two_args
        CMP #'R'
        BEQ @repeat
        CMP #'P'
        BEQ @print_cmd
        CMP #'I'
        BEQ @if_cmd
        CMP #'E'
        BEQ @ifelse_cmd
        CMP #'S'
        BEQ @setshape_cmd
.ifdef CODETANK_BUILD
        CMP #'L'
        BEQ @label_cmd
.endif
        ; flag 0: no arg
        JMP @dispatch
@one_arg:
        JSR parse_dec_arg
        JMP @dispatch
@two_args:
        JSR parse_dec_arg     ; arg = x (1st)
        LDA arg_lo
        STA arg2_lo           ; arg2 = x saved
        LDA arg_hi
        STA arg2_hi
        JSR parse_dec_arg     ; arg = y (2nd)
        ; swap arg <-> arg2 so dispatcher sees arg_lo=x, arg2_lo=y
        LDA arg_lo
        STA tmp2
        LDA arg2_lo
        STA arg_lo
        LDA tmp2
        STA arg2_lo
        LDA arg_hi
        STA tmp2
        LDA arg2_hi
        STA arg_hi
        LDA tmp2
        STA arg2_hi
        JMP @dispatch
@repeat:
        JSR parse_dec_arg
        JSR mark_ok
        JSR cmd_repeat        ; consumes [...] and reruns parse_and_exec inside
        JMP @loop
@print_cmd:
        JSR mark_ok
        JSR cmd_print
        JMP @loop
@if_cmd:
        JSR mark_ok
        JSR cmd_if
        JMP @loop
@ifelse_cmd:
        JSR mark_ok
        JSR cmd_ifelse
        JMP @loop
@setshape_cmd:
        JSR mark_ok
        JSR cmd_setshape
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (back-to-back VDP store)
        JMP @loop
.ifdef CODETANK_BUILD
@label_cmd:
        ; flag 'L' is shared by LABEL and SAY -- dispatch to whichever
        ; handler find_mnem latched into mptr_lo:hi.
        JSR mark_ok
        JSR call_handler
        JMP @loop
.endif
@dispatch:
        JSR mark_ok
        JSR call_handler
        JMP @loop
@try_proc:
        ; mnem_buf still holds the consumed name. Try the user-proc table.
        JSR find_proc
        BCS @err
        JSR mark_ok
        ; --- tail call optimisation ---
        ; If we're already inside a proc (in_proc_save > 0), and the parent
        ; proc has no more body lines after the current one, and the current
        ; line has no more commands after this proc's args (next char is CR
        ; or ']'), then this is a tail call. Reuse the parent's frame:
        ; rebind params, point to the new slot's body, and return -- the
        ; outer parse_and_exec @loop will see the CR and unwind, and the
        ; outer proc_invoke @line_loop will iterate on the new slot.
        ; This makes deep tail recursion (e.g. spiral :size :angle) cost
        ; zero stack growth.
        LDA in_proc_save
        BEQ @full_call
        LDA proc_body_cursor
        CMP proc_body_total
        BCC @full_call           ; more body lines pending → not tail
        JSR peek_check_tail
        BCC @full_call           ; not at end-of-line after args → not tail
        JSR do_tail_trampoline
        JMP @loop                ; @loop sees CR, RTS to outer proc_invoke
@full_call:
        JSR proc_invoke
        JMP @loop
@err:   LDA #ERR_UNK_CMD
        JSR print_err
        ; abort rest of line
@skip:  LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #$0D
        BEQ @ok
        INC line_idx
        JMP @skip
@ok:    RTS

call_handler:
        JMP (mptr_lo)

; mark_ok: set cmd_status = 1 unless already 2 (err sticky).
mark_ok:
        LDA cmd_status
        CMP #2
        BEQ @ret
        LDA #1
        STA cmd_status
@ret:   RTS

; print_status: outermost-level helper.
;   If break_flag set, print "BREAK<CR>" and clear it (procs/vars survive).
;   Else if cmd_status==1, print "OK<CR>". Else silent.
print_status:
        LDA break_flag
        BEQ @no_break
        LDA #0
        STA break_flag
        LDX #0
@blp:   LDA break_msg,X
        BEQ @done
        ORA #$80
        JSR ECHO
        INX
        BNE @blp
@no_break:
        LDA cmd_status
        CMP #1
        BNE @done
        LDA #'O'
        ORA #$80
        JSR ECHO
        LDA #'K'
        ORA #$80
        JSR ECHO
        LDA #$0D
        ORA #$80
        JSR ECHO
@done:  RTS

break_msg: .byte "BREAK", $0D, 0

; ============================================================================
; SYNTAXERR -- print "? <message>\r" and set cmd_status = 2.
;   Input: A = error code (0..6)
; ============================================================================
ERR_UNK_CMD  = 0
ERR_UNK_VAR  = 1
ERR_FULL     = 2
ERR_BAD_NAME = 3
ERR_NO_RB    = 4
ERR_NO_END   = 5
ERR_BAD_ARG  = 6

print_err:
        ; Save mptr around the message walk -- callers (parse_dec_arg @nf,
        ; @try_proc @err) leave the dispatcher's JMP-(mptr_lo) handler ptr
        ; in mptr; clobbering it would jump through the err message bytes.
        PHA                       ; preserve err code
        LDA mptr_lo
        STA err_save_lo
        LDA mptr_hi
        STA err_save_hi
        PLA
        ASL                       ; A = code * 2 (table is 2 bytes per entry)
        TAX
        LDA err_table,X
        STA mptr_lo
        LDA err_table+1,X
        STA mptr_hi
        LDA #'?'
        ORA #$80
        JSR ECHO
        LDA #' '
        ORA #$80
        JSR ECHO
        LDY #0
@l:     LDA (mptr_lo),Y
        BEQ @end
        ORA #$80
        JSR ECHO
        INY
        BNE @l
@end:   LDA #$0D
        ORA #$80
        JSR ECHO
        LDA #2
        STA cmd_status
        LDA err_save_lo
        STA mptr_lo
        LDA err_save_hi
        STA mptr_hi
        RTS

err_table:
        .word err_msg_unk_cmd
        .word err_msg_unk_var
        .word err_msg_full
        .word err_msg_bad_name
        .word err_msg_no_rb
        .word err_msg_no_end
        .word err_msg_bad_arg

err_msg_unk_cmd:  .byte "UNK CMD",  0
err_msg_unk_var:  .byte "UNK VAR",  0
err_msg_full:     .byte "FULL",     0
err_msg_bad_name: .byte "BAD NAME", 0
err_msg_no_rb:    .byte "NO ]",     0
err_msg_no_end:   .byte "NO END",   0
err_msg_bad_arg:  .byte "BAD ARG",  0

; ============================================================================
; skip_spaces: advance line_idx over space / comma / tab.
; ============================================================================
skip_spaces:
        LDX line_idx
@l:     LDA line_buf,X
        AND #$7F
        CMP #' '
        BEQ @adv
        CMP #','
        BEQ @adv
        CMP #$09
        BEQ @adv
        STX line_idx
        RTS
@adv:   INX
        BNE @l

; ============================================================================
; find_mnem: read up to NAME_LEN identifier chars (A-Z + 0-9) at
;            line_buf,line_idx into mnem_buf (uppercase, space-padded),
;            then look up bytes 0..3 in mnem_tab.
;   Match  -> A = flag byte, mptr_lo/hi = handler addr, line_idx += letters,
;             C = 0.
;   Miss   -> C = 1, line_idx unchanged. Caller may now use the full
;             NAME_LEN buffer for proc lookup (THING vs THING1 stay
;             distinct because find_proc_slot compares NAME_LEN bytes).
; mnem_tab entry: 4 bytes name (UPPER, padded space), 1 flag, 2 handler.
;                 terminator $FF. Commands stay 4-char so unrelated long
;                 input like "FORWARD" still matches "FORW".
; ============================================================================
find_mnem:
        LDX line_idx
        LDY #0
@rd:    LDA line_buf,X
        AND #$7F
        CMP #'a'
        BCC @upcheck
        CMP #'z'+1
        BCS @upcheck
        AND #$DF              ; uppercase a..z -> A..Z
@upcheck:
        CMP #'A'
        BCC @check_digit
        CMP #'Z'+1
        BCC @keep
@check_digit:
        CMP #'0'
        BCC @done_rd
        CMP #'9'+1
        BCS @done_rd
@keep:  ; identifier char: store first NAME_LEN, keep consuming extras
        CPY #NAME_LEN
        BCS @past_n
        STA mnem_buf,Y
        INY
@past_n:
        INX
        JMP @rd
@done_rd:
        STY n_letters
        STX tmp               ; save consume-end X for later advance
        ; pad rest with space (only matters if n_letters < NAME_LEN)
        LDA #' '
@padl:  CPY #NAME_LEN
        BEQ @search
        STA mnem_buf,Y
        INY
        JMP @padl
@search:
        LDA n_letters
        BEQ @miss
        LDA #<mnem_tab
        STA mptr_lo
        LDA #>mnem_tab
        STA mptr_hi
@nxt:   LDY #0
        LDA (mptr_lo),Y
        CMP #$FF
        BEQ @miss
        ; compare 4 name bytes
@c:     LDA (mptr_lo),Y
        CMP mnem_buf,Y
        BNE @no
        INY
        CPY #4
        BNE @c
        ; match
        LDY #4
        LDA (mptr_lo),Y
        STA tmp2              ; flag
        LDY #5
        LDA (mptr_lo),Y
        TAX                   ; handler lo
        LDY #6
        LDA (mptr_lo),Y
        STA mptr_hi
        STX mptr_lo
        ; advance line_idx past *all* consumed letters (incl. those past pos 4)
        LDA tmp
        STA line_idx
        LDA tmp2
        CLC
        RTS
@no:    CLC
        LDA mptr_lo
        ADC #7
        STA mptr_lo
        BCC @nxt
        INC mptr_hi
        JMP @nxt
@miss:  SEC
        RTS

; ============================================================================
; parse_dec_arg: parse decimal at line_buf,line_idx into arg_lo/arg_hi,
;                advance line_idx past digits.  Skips leading spaces first.
; ============================================================================
; parse_dec_arg: thin wrapper that parses one term, then folds an optional
;   trailing '+'/'-' followed by another term. Single level (left-to-right
;   associative for chains: A + B - C is (A+B)-C). Lets the user write
;   `:size + 2` or `:a - 1` in arg position.
parse_dec_arg:
        JSR parse_dec_term
@arith: JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #'+'
        BEQ @add
        CMP #'-'
        BEQ @sub
        RTS
@add:   INC line_idx
        LDA arg_lo
        PHA
        LDA arg_hi
        PHA
        JSR parse_dec_term
        PLA
        STA tmp2
        PLA
        STA tmp
        CLC
        LDA arg_lo
        ADC tmp
        STA arg_lo
        LDA arg_hi
        ADC tmp2
        STA arg_hi
        JMP @arith
@sub:   INC line_idx
        LDA arg_lo
        PHA
        LDA arg_hi
        PHA
        JSR parse_dec_term
        ; saved - new
        PLA
        STA tmp2                  ; saved hi
        PLA
        STA tmp                   ; saved lo
        SEC
        LDA tmp
        SBC arg_lo
        STA arg_lo
        LDA tmp2
        SBC arg_hi
        STA arg_hi
        JMP @arith

; parse_dec_term: parse a single value -- decimal literal, :NAME variable
; reference, RANDOM N, or FOREVER. Output in arg_lo:arg_hi. line_idx
; advanced past the term.
parse_dec_term:
        JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #':'
        BNE @not_colon
        JMP @var_ref
@not_colon:
        CMP #'F'
        BNE @not_f
        JMP @maybe_forever
@not_f:
        CMP #'R'
        BNE @dec_far            ; not RANDOM/FOREVER/var -> decimal literal
        ; possible RANDOM keyword: check ANDOM<space>
        LDA line_buf+1,X
        AND #$7F
        CMP #'A'
        BNE @dec_far
        LDA line_buf+2,X
        AND #$7F
        CMP #'N'
        BNE @dec_far
        LDA line_buf+3,X
        AND #$7F
        CMP #'D'
        BNE @dec_far
        LDA line_buf+4,X
        AND #$7F
        CMP #'O'
        BNE @dec_far
        LDA line_buf+5,X
        AND #$7F
        CMP #'M'
        BNE @dec_far
        LDA line_buf+6,X
        AND #$7F
        CMP #' '
        BNE @dec_far
        JMP @random_ok
@dec_far:
        JMP @dec
@maybe_forever:
        ; FOREVER -> arg = $FFFF (use with REPEAT for "endless" loops).
        ;   Letters are checked unsigned; trailing char must be space, CR
        ;   or '[' so REPEAT FOREVER[...] works without a separator.
        LDA line_buf+1,X
        AND #$7F
        CMP #'O'
        BNE @dec_far
        LDA line_buf+2,X
        AND #$7F
        CMP #'R'
        BNE @dec_far
        LDA line_buf+3,X
        AND #$7F
        CMP #'E'
        BNE @dec_far
        LDA line_buf+4,X
        AND #$7F
        CMP #'V'
        BNE @dec_far
        LDA line_buf+5,X
        AND #$7F
        CMP #'E'
        BNE @dec_far
        LDA line_buf+6,X
        AND #$7F
        CMP #'R'
        BNE @dec_far
        LDA line_buf+7,X
        AND #$7F
        CMP #' '
        BEQ @forever_ok
        CMP #$0D
        BEQ @forever_ok
        CMP #'['
        BEQ @forever_ok
        JMP @dec_far
@forever_ok:
        TXA
        CLC
        ADC #7
        STA line_idx            ; consumed "FOREVER"
        LDA #$FF
        STA arg_lo
        STA arg_hi
        RTS
@random_ok:
        ; consumed "RANDOM "
        TXA
        CLC
        ADC #6
        STA line_idx
        ; recursively parse the modulo argument
        JSR parse_dec_arg     ; arg_lo:hi = N
        ; roll the LFSR (16-bit Fibonacci taps 16,14,13,11)
        JSR roll_lfsr
        ; arg = lfsr mod N (16-bit / 16-bit, but we only support N <= 256)
        ;   if arg_hi != 0 (N > 255), we treat arg as lfsr_lo modulo arg_lo only
        LDA arg_lo
        BNE @rmod
        ; N == 0 or low byte zero -> just return 0
        LDA #0
        STA arg_hi
        RTS
@rmod:  STA tmp               ; tmp = N
        LDA lfsr_lo
        EOR lfsr_hi           ; mix halves for better dispersion
@rsub:  CMP tmp
        BCC @rdone
        SEC
        SBC tmp
        JMP @rsub
@rdone: STA arg_lo
        LDA #0
        STA arg_hi
        RTS
@var_ref:
        ; --- :NAME reference ---
        INX
        STX line_idx
        JSR read_var_name
        ; save dispatcher handler ptr (parse_and_exec will need it for
        ; call_handler after we return) -- var_lookup clobbers mptr.
        LDA mptr_lo
        PHA
        LDA mptr_hi
        PHA
        JSR var_lookup
        BCS @nf
        LDY #NAME_LEN
        LDA (mptr_lo),Y
        STA arg_lo
        INY
        LDA (mptr_lo),Y
        STA arg_hi
        PLA
        STA mptr_hi
        PLA
        STA mptr_lo
        RTS
@nf:    PLA
        STA mptr_hi
        PLA
        STA mptr_lo
        LDA #0
        STA arg_lo
        STA arg_hi
        LDA #ERR_UNK_VAR
        JSR print_err
        RTS
@dec:   LDA #0
        STA arg_lo
        STA arg_hi
@l:     LDA line_buf,X
        AND #$7F
        CMP #'0'
        BCC @end
        CMP #'9'+1
        BCS @end
        ; arg = arg*10 + (c - '0')
        PHA
        ; save mnem_buf usage -- not relevant, mnem_buf isn't needed any more
        ; arg * 10 = (arg<<3) + (arg<<1)
        LDA arg_lo
        STA tmp
        LDA arg_hi
        STA tmp2
        ; arg <<= 1  -> arg
        ASL arg_lo
        ROL arg_hi
        ; arg <<= 1 -> arg*4
        ASL arg_lo
        ROL arg_hi
        ; arg += original (now arg = orig*5)
        CLC
        LDA arg_lo
        ADC tmp
        STA arg_lo
        LDA arg_hi
        ADC tmp2
        STA arg_hi
        ; arg <<= 1 -> arg*10
        ASL arg_lo
        ROL arg_hi
        ; + (c - '0')
        PLA
        SEC
        SBC #'0'
        CLC
        ADC arg_lo
        STA arg_lo
        BCC @no_carry
        INC arg_hi
@no_carry:
        INX
        JMP @l
@end:   STX line_idx
        RTS

; ============================================================================
; -- COMMANDS --
; ============================================================================
cmd_pu:
        LDA #0
        STA pen
        RTS

cmd_pd:
        LDA #1
        STA pen
        RTS

; cmd_setpc: SETPC N -- the single colour command. N is masked to 0..15
;   and stored in pen_color, which drives every colourised path:
;     * trails       (plot_set OR mode writes the colour cell)
;     * bitmap arrow (the OR-mode triangle draw paints its outline cells
;                    with pen_color; SETPC just erases the visible arrow
;                    and redraws so the new pen_color takes effect)
;     * sprite-0     (draw_turtle re-emits the attribute byte)
;     * bitmap text  (LABEL/SAY -- blit_glyph writes the colour cells;
;                    SAY temporarily overrides to white for readability)
;   No separate sprite/turtle colour register exists: pen_color is the
;   one source of truth.
cmd_setpc:
        LDA arg_lo
        AND #$0F
        STA pen_color
        LDA sprite_mode
        BEQ @bitmap
        JSR draw_turtle            ; sprite path: re-emit sprite-0 attribute
        RTS
@bitmap:
        LDA turtle_visible
        BEQ @done                  ; nothing to repaint
        JSR erase_turtle           ; restore saved bg cells
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (back-to-back VDP store)
        JSR draw_turtle            ; re-save + OR-draw with new pen_color
@done:  RTS

; cmd_stop: signal "exit current proc". Loop checks in cmd_repeat,
; parse_and_exec, and proc_invoke @line_loop unwind to proc_invoke
; @body_done which clears the flag.
cmd_stop:
        LDA #1
        STA stop_flag
        RTS

; cmd_help: paginated reference.
;   HELP            -> print the table-of-contents page (help_toc).
;   HELP N          -> print topic page N (1..NUM_HELP_PAGES).
;   HELP 0 / unknown -> back to TOC, with a "?" hint.
;
; The hand-rolled CMP/BNE chain trades ~24 bytes vs. a 16-byte indexed
; lookup table for clarity; CODE has plenty of headroom in the 16 KB
; build, and HELP is only ever invoked interactively.
cmd_help:
        JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #$0D
        BEQ @toc
        CMP #']'
        BEQ @toc
        JSR parse_dec_arg
        LDA arg_lo
        BEQ @toc
        CMP #1
        BNE @n2
        LDA #<help_p1
        LDX #>help_p1
        JMP print_str_ax
@n2:    CMP #2
        BNE @n3
        LDA #<help_p2
        LDX #>help_p2
        JMP print_str_ax
@n3:    CMP #3
        BNE @n4
        LDA #<help_p3
        LDX #>help_p3
        JMP print_str_ax
@n4:    CMP #4
        BNE @n5
        LDA #<help_p4
        LDX #>help_p4
        JMP print_str_ax
@n5:    CMP #5
        BNE @n6
        LDA #<help_p5
        LDX #>help_p5
        JMP print_str_ax
@n6:    CMP #6
        BNE @n7
        LDA #<help_p6
        LDX #>help_p6
        JMP print_str_ax
@n7:    CMP #7
        BNE @n8
        LDA #<help_p7
        LDX #>help_p7
        JMP print_str_ax
@n8:    CMP #8
        BNE @n9
        LDA #<help_p8
        LDX #>help_p8
        JMP print_str_ax
@n9:
.ifdef CODETANK_BUILD
        CMP #9
        BNE @toc
        LDA #<help_p9
        LDX #>help_p9
        JMP print_str_ax
.else
        JMP @toc
.endif
@toc:   LDA #<help_toc
        LDX #>help_toc
        ; fall through

; print_help_str lives in dev/lib/apple1/print.asm as print_str_ax. We
; alias its ZP slot to LOGO's mptr_lo:hi (same role as the original
; in-line implementation) before the .include so no extra ZP is burned.
print_ptr_lo = mptr_lo
print_ptr_hi = mptr_hi
.include "print.asm"

; cmd_demo: built-in slideshow that runs each demo line through
;   parse_and_exec. Each line lives in CODE (read-only) and is copied
;   into line_buf to be tokenised. Lines are CR-terminated; $00 marks
;   the end of the script. After the script ends, line_buf is zeroed
;   and line_idx set to 0 so the outer parse_and_exec sees CR and stops.
cmd_demo:
.ifdef CODETANK_BUILD
        ; find_mnem matches the first 4 chars of the typed word against
        ; mnem_tab, so "DEMO" and "DEMO2" both land on this handler. The
        ; full identifier sits in mnem_buf (space-padded to NAME_LEN=6),
        ; so checking byte 4 distinguishes the two: '2' -> autobiographic
        ; narrator demo, ' ' -> classic slideshow.
        LDA mnem_buf+4
        CMP #'2'
        BEQ cmd_demo2
.endif
        LDA #<demo_script
        STA mptr_lo
        LDA #>demo_script
        STA mptr_hi
        JMP cmd_demo_run
.ifdef CODETANK_BUILD
cmd_demo2:
        ; CodeTank-only -- POM1 autobiographic narration that uses the 12
        ; SCROLL-O-SPRITES emote sprites + LABEL bitmap text to walk
        ; through the lineage Apple-1 -> TMS9918 -> CodeTank -> POM1.
        LDA #<demo2_script
        STA mptr_lo
        LDA #>demo2_script
        STA mptr_hi
.ifdef LOGO_GEN2
        ; GEN2: the narrator emotes are tinted by SETPC in demo2_script, so
        ; switch the emote blitter into its colour path for the whole show.
        ; Cleared again at cmd_demo_run's @done so BIRDFLY (run from the classic
        ; DEMO / the REPL) keeps its solid-white birds.
        LDA #1
        STA em_color
.endif
        JMP cmd_demo_run
.endif
cmd_demo_run:
@nxt:   LDY #0
        LDA (mptr_lo),Y
        BEQ @done             ; $00 -> end of script
@cp:    LDA (mptr_lo),Y
        STA line_buf,Y
        INY
        CMP #$0D
        BNE @cp
        ; advance script pointer past consumed bytes (Y = bytes copied)
        TYA
        CLC
        ADC mptr_lo
        STA mptr_lo
        BCC @nc
        INC mptr_hi
@nc:    ; run the line. parse_and_exec / proc_collect_line both clobber
        ; mptr_lo:hi via find_mnem, so save/restore the demo script pointer.
        ; Honour def_mode so multi-line TO ... END blocks in the demo (used
        ; for the recursive SPIRAL) get appended to a proc body rather than
        ; parsed as commands.
        LDA mptr_lo
        PHA
        LDA mptr_hi
        PHA
        LDA #0
        STA cmd_status
        LDA def_mode
        BEQ @parse
        JSR proc_collect_line
        JMP @after_run
@parse: LDA #0
        STA line_idx
        JSR parse_and_exec
@after_run:
        PLA
        STA mptr_hi
        PLA
        STA mptr_lo
        JMP @nxt
@done:  ; terminate outer line_buf so the caller's parse_and_exec @loop
        ; sees a CR and exits cleanly.
.ifdef LOGO_GEN2
        LDA #0                ; back to solid-white emotes for BIRDFLY / REPL
        STA em_color
.endif
        LDA #$0D
        STA line_buf
        LDA #0
        STA line_idx
        RTS

; demo_script: multi-scene slideshow (~30 PRINT labels). Single-word
;   PRINT labels (no spaces) because PRINT stops at first space.
;   Most scenes are bare REPEAT compositions; SPIRAL/FLOWER/BFLY are
;   the only ones that define+invoke procs (they survive at the REPL).
demo_script:
        .byte "PRINT ", $22, "DEMO", $0D
        .byte "WAIT 1", $0D
        .byte "CS", $0D
        ; --- V2.5: SETPC tints each scene a different colour. The pen
        ;     palette uses the standard TMS9918 list (1=black, 2/3=greens,
        ;     4/5=blues, 6=dark red, 7=cyan, 8/9=reds, 10/11=yellows,
        ;     12=dark green, 13=magenta, 14=gray, 15=white).
        ; SQUARE scene replaced by the SQUARE proc below (used by FLOWER).
        ; ("STAR" 5-pointed scene removed May 2026 to absorb +12 B from
        ;  the cmd_say tx/ty save-restore fix. STAR7 below covers the
        ;  same {N/k} non-convex visual family.)
        .byte "PRINT ", $22, "SUN", $0D
        .byte "SETPC 10", $0D
        .byte "REPEAT 18 [FD 60 BK 60 TR 20]", $0D
        .byte "WAIT 2", $0D
        .byte "CS", $0D
        .byte "PRINT ", $22, "ROSETTE", $0D
        .byte "SETPC 8", $0D
        .byte "REPEAT 12 [REPEAT 6 [FD 25 TR 60] TR 30]", $0D
        .byte "WAIT 3", $0D
        .byte "CS", $0D
        .byte "PRINT ", $22, "RANDOM", $0D
        .byte "SETPC 13", $0D
        .byte "REPEAT 80 [FD RANDOM 20 TR RANDOM 90]", $0D
        .byte "WAIT 3", $0D
        .byte "CS", $0D
        ; V1.8: cmd_demo now routes lines through proc_collect_line while
        ; def_mode is set, so multi-line TO...END blocks in the demo are
        ; captured. The procs survive at the REPL afterwards, so users can
        ; rerun them or compose with their own code.
        ;
        ; SQUARE + FLOWER show nested proc invocation inside REPEAT (depth
        ; 2, non-tail). SPIRAL shows deep tail recursion (~50 levels in
        ; one frame). Together they exercise both proc-call paths.
        .byte "TO SQUARE", $0D
        .byte "REPEAT 4 [FD 50 TR 90]", $0D
        .byte "END", $0D
        .byte "PRINT ", $22, "FLOWER", $0D
        .byte "TO FLOWER", $0D
        .byte "REPEAT 36 [TR 10 SQUARE]", $0D
        .byte "END", $0D
        .byte "SETPC 3", $0D
        .byte "FLOWER", $0D
        .byte "WAIT 3", $0D
        .byte "CS", $0D
        .byte "TO SPIRAL :SIZE :ANGLE", $0D
        .byte "IF :SIZE > 100 [STOP]", $0D
        .byte "FORWARD :SIZE", $0D
        .byte "RIGHT :ANGLE", $0D
        .byte "SPIRAL :SIZE + 2 :ANGLE", $0D
        .byte "END", $0D
        .byte "PRINT ", $22, "SPIRAL91", $0D
        .byte "SETPC 9", $0D
        .byte "SPIRAL 0 91", $0D
        .byte "WAIT 3", $0D
        .byte "CS", $0D
        ; --- extra REPL scenes (no procs, just REPEAT compositions) ----
        ; Polygons -- N-gon = REPEAT N [FD step TR (360/N)].
        .byte "PRINT ", $22, "HEXAGON", $0D
        .byte "REPEAT 6 [FD 45 TR 60]", $0D
        .byte "WAIT 2", $0D
        .byte "CS", $0D
        ; Star variants -- non-convex {N/k} stars: TR > 360/N skips vertices.
        .byte "PRINT ", $22, "STAR7", $0D
        .byte "REPEAT 7 [FD 70 TR 154]", $0D
        .byte "WAIT 2", $0D
        .byte "CS", $0D
        .byte "PRINT ", $22, "BURST", $0D
        .byte "REPEAT 9 [FD 70 TR 160]", $0D
        .byte "WAIT 2", $0D
        .byte "CS", $0D
        ; Doubly-nested REPEATs -- copies of a small motif rotated around.
        .byte "PRINT ", $22, "PINWHL", $0D
        .byte "REPEAT 10 [REPEAT 4 [FD 22 TR 90] TR 36]", $0D
        .byte "WAIT 3", $0D
        .byte "CS", $0D
        .byte "PRINT ", $22, "KALEID", $0D
        .byte "SETPC 9", $0D
        .byte "REPEAT 24 [REPEAT 5 [FD 120 TR 144] TR 15]", $0D
        .byte "WAIT 3", $0D
        .byte "CS", $0D
        .byte "PRINT ", $22, "GARDEN", $0D
        .byte "REPEAT 8 [REPEAT 3 [FD 35 TR 120] TR 45]", $0D
        .byte "WAIT 3", $0D
        .byte "CS", $0D
        ; (CIRCLES scene removed May 2026 to absorb +6 B from 24c silicon-
        ;  strict pads in tms9918m2.asm:plot_set + draw_turtle prologue.)
        ; SETXY-driven absolute-position scenes. RANDOM N caps at N<=255.

        .byte "PRINT ", $22, "RAYS", $0D
        .byte "REPEAT 36 [PU HOME PD SETH RANDOM 250 FD 70]", $0D
        .byte "WAIT 3", $0D
        .byte "CS", $0D
        ; (TURTLEW scene removed -- TURTL directional sprite is gone.)
        ; --- V2.0 dynamic-turtle scene: figure-8 bird flight --------------
        ; True figure-8: BFR is a wing-flap cycle that turns right (+24°
        ; net), BFL the same cycle turning left (-24°). BFLY runs 8 BFR
        ; followed by 8 BFL, so each lobe sweeps 192° (a bit more than a
        ; half-circle, giving the loops their bulged shape). The WAIT 1
        ; inside each cycle paces it to ~0.8s/flap so it's watchable at
        ; native 1x emulation speed instead of a blur.
        ; After the flight, SETSHAPE "ARROW restores the classic bitmap
        ; turtle so the rest of the REPL doesn't see a stale bird sprite.
        .byte "PRINT ", $22, "BIRDFLY", $0D
        .byte "SETPC 11", $0D                ; yellow -- bird sprite + trail
        ; PAUSE 1 (~100 ms) AFTER each SETSHAPE so both wing positions
        ; get an equal hold time on screen -- otherwise BIRD1 would only
        ; flash for the time of one FD+TR (~1 ms) before being replaced.
        .byte "TO BFR", $0D
        .byte "SETSHAPE ", $22, "BIRD1", $0D
        .byte "FD 3", $0D
        .byte "TR 12", $0D
        .byte "PAUSE 1", $0D
        .byte "SETSHAPE ", $22, "BIRD2", $0D
        .byte "FD 3", $0D
        .byte "TR 12", $0D
        .byte "PAUSE 1", $0D
        .byte "END", $0D
        .byte "TO BFL", $0D
        .byte "SETSHAPE ", $22, "BIRD1", $0D
        .byte "FD 3", $0D
        .byte "TL 12", $0D
        .byte "PAUSE 1", $0D
        .byte "SETSHAPE ", $22, "BIRD2", $0D
        .byte "FD 3", $0D
        .byte "TL 12", $0D
        .byte "PAUSE 1", $0D
        .byte "END", $0D
        .byte "TO BFLY", $0D
        .byte "REPEAT 8 [BFR]", $0D
        .byte "REPEAT 8 [BFL]", $0D
        .byte "END", $0D
        .byte "PU", $0D
        .byte "HOME", $0D
        .byte "BFLY", $0D
        .byte "WAIT 2", $0D
        .byte "SETSHAPE ", $22, "ARROW", $0D
        .byte "PD", $0D
        ; --- final epilogue: reset colour to default white so the REPL
        ;     doesn't inherit any tint from the slideshow. Order matters:
        ;     CS first so cmd_home draws the arrow at the centre cells
        ;     (which may carry tints leaked from earlier scenes), THEN
        ;     SETPC 15 then erases+redraws the arrow at HOME with the
        ;     new pen_color (option B: arrow_save_bbox+OR draw, reversible).
        .byte "CS", $0D
        .byte "SETPC 15", $0D                ; trail/arrow/sprite -> white
        .byte "PRINT ", $22, "END", $0D
        .byte 0

; cmd_make: parses "[\"]NAME VALUE", stores VALUE in var_table[NAME].
;   value can be a literal decimal or a :OTHER-NAME reference.
cmd_make:
        JSR skip_spaces
        ; skip optional leading "
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #'"'
        BNE @rd
        INC line_idx
@rd:    JSR read_var_name
        ; reject empty name (mnem_buf[0] still ' ')
        LDA mnem_buf
        CMP #' '
        BEQ @bad
        ; preserve the destination name across parse_dec_arg
        ; (since :NAME would clobber mnem_buf via read_var_name)
        LDA mnem_buf
        PHA
        LDA mnem_buf+1
        PHA
        LDA mnem_buf+2
        PHA
        LDA mnem_buf+3
        PHA
        LDA mnem_buf+4
        PHA
        LDA mnem_buf+5
        PHA
        JSR parse_dec_arg     ; -> arg_lo:arg_hi
        PLA
        STA mnem_buf+5
        PLA
        STA mnem_buf+4
        PLA
        STA mnem_buf+3
        PLA
        STA mnem_buf+2
        PLA
        STA mnem_buf+1
        PLA
        STA mnem_buf
        ; lookup name; update if found, append otherwise
        JSR var_lookup
        BCS @new
        ; found: update value at offset NAME_LEN..NAME_LEN+1 of slot
        LDY #NAME_LEN
        LDA arg_lo
        STA (mptr_lo),Y
        INY
        LDA arg_hi
        STA (mptr_lo),Y
        RTS
@new:   ; check table full
        LDA n_vars
        CMP #MAX_VARS
        BCS @full
        ; append new entry: write NAME_LEN-byte name then 2-byte value
        LDY #0
@cn:    LDA mnem_buf,Y
        STA (mptr_lo),Y
        INY
        CPY #NAME_LEN
        BNE @cn
        LDA arg_lo
        STA (mptr_lo),Y
        INY
        LDA arg_hi
        STA (mptr_lo),Y
        INC n_vars
        RTS
@bad:   LDA #ERR_BAD_NAME
        JSR print_err
        RTS
@full:  LDA #ERR_FULL
        JSR print_err
        RTS

; read_var_name: read up to NAME_LEN identifier chars (A-Z + 0-9) at
;   line_buf,line_idx into mnem_buf, uppercased, space-padded.
;   Advances line_idx past *all* consumed identifier chars (so
;   "TO THING1" cleanly leaves the cursor at the line CR, not at '1').
read_var_name:
        LDX line_idx
        LDY #0
@rd:    LDA line_buf,X
        AND #$7F
        CMP #'a'
        BCC @up
        CMP #'z'+1
        BCS @up
        AND #$DF
@up:    CMP #'A'
        BCC @check_digit
        CMP #'Z'+1
        BCC @keep
@check_digit:
        CMP #'0'
        BCC @done
        CMP #'9'+1
        BCS @done
@keep:  CPY #NAME_LEN
        BCS @past_n
        STA mnem_buf,Y
        INY
@past_n:
        INX
        JMP @rd
@done:  STX line_idx
        LDA #' '
@padl:  CPY #NAME_LEN
        BEQ @ret
        STA mnem_buf,Y
        INY
        JMP @padl
@ret:   RTS

; cmd_to: TO NAME [:p1 [:p2]]    -- start multi-line procedure definition.
;   Allocates/reuses a slot, writes name + nparams + param names + body_len=0,
;   sets cur_proc_lo:hi -> slot start and def_mode = 1. The REPL then routes
;   each subsequent line through proc_collect_line until the user types END.
cmd_to:
        JSR skip_spaces
        JSR read_var_name        ; mnem_buf <- proc name
        LDA mnem_buf
        CMP #' '
        BEQ @bad_name
        ; --- find or allocate slot ---
        JSR find_proc_slot       ; -> mptr_lo:hi = slot, C=0 if existing
        BCC @use_slot
        LDA n_procs
        CMP #MAX_PROCS
        BCS @full
        INC n_procs
@use_slot:
        ; remember slot for proc_collect_line
        LDA mptr_lo
        STA cur_proc_lo
        LDA mptr_hi
        STA cur_proc_hi
        ; write NAME_LEN-byte name at slot+0..NAME_LEN-1
        LDY #0
@cn:    LDA mnem_buf,Y
        STA (mptr_lo),Y
        INY
        CPY #NAME_LEN
        BNE @cn
        ; --- parse :p1 :p2 (max MAX_PARAMS) ---
        ; Use tmp as the parm counter -- skip_spaces / read_var_name clobber X
        ; and Y, so a register-only counter would be lost across each call.
        LDA #0
        STA tmp
@parm:  JSR skip_spaces
        LDY line_idx
        LDA line_buf,Y
        AND #$7F
        CMP #':'
        BNE @parms_done
        INC line_idx             ; consume ':'
        JSR read_var_name        ; mnem_buf <- param name
        LDA mnem_buf
        CMP #' '
        BEQ @parms_done          ; ':' alone -> stop (defensive)
        ; slot offset = PROC_PARAMS_OFF + tmp*NAME_LEN
        ;   NAME_LEN = 6 → offset = tmp*4 + tmp*2 = (tmp<<2) + (tmp<<1).
        LDA tmp
        ASL                       ; tmp*2 in A
        STA tmp2                  ; save tmp*2
        ASL                       ; tmp*4 in A
        CLC
        ADC tmp2                  ; tmp*6
        CLC
        ADC #PROC_PARAMS_OFF
        TAY
        LDX #0
@cn_p:  LDA mnem_buf,X
        STA (mptr_lo),Y
        INX
        INY
        CPX #NAME_LEN
        BNE @cn_p
        INC tmp
        LDA tmp
        CMP #MAX_PARAMS
        BNE @parm
@parms_done:
        ; store nparams at slot+4
        LDY #PROC_NPARAMS_OFF
        LDA tmp
        STA (mptr_lo),Y
        ; init body_len = 0 at slot+13
        LDY #PROC_BODYLEN_OFF
        LDA #0
        STA (mptr_lo),Y
        ; engage definition mode
        LDA #1
        STA def_mode
        RTS
@bad_name:
        LDA #ERR_BAD_NAME
        JSR print_err
        RTS
@full:
        LDA #ERR_FULL
        JSR print_err
        RTS

.ifdef CODETANK_BUILD
; ============================================================================
; cmd_list: LIST [NAME]  -- procedure dumper for the TMS9918 bitmap.
;   - LIST NAME : displays the named procedure body, line-by-line, on
;                 the bitmap (8x8 glyphs, line 0 = name, line 2 onward
;                 = body lines). Truncates at row 23 (192 px).
;   - LIST      : with no argument, prints the names of every defined
;                 proc, one per line.
;   Always clears the bitmap and disables the sprite layer so the
;   listing is readable. After LIST, user can SETSHAPE again to bring
;   back a sprite. Uses shape_pat_lo/hi as the slot pointer because
;   blit_glyph clobbers mptr_lo/hi with the charmap address.
; ============================================================================
cmd_list:
        JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #$0D
        BNE @not_cr
        JMP @list_all
@not_cr:
        CMP #']'
        BNE @not_brk
        JMP @list_all
@not_brk:
        ; --- LIST NAME path ---
        JSR read_var_name        ; mnem_buf <- proc name
        JSR find_proc            ; mptr_lo:hi -> slot, C=1 if not found
        BCS @bad
        ; preserve slot pointer in shape_pat (blit_glyph clobbers mptr)
        LDA mptr_lo
        STA shape_pat_lo
        LDA mptr_hi
        STA shape_pat_hi
        JSR clear_bitmap
        JSR disable_sprites
        LDA #0
        STA sprite_mode
        STA tx_lo
        STA ty_lo
        ; --- print proc name on row 0. blit_glyph clobbers tmp/tmp2,
        ;     so we round-trip Y through the 6502 stack instead.
        LDY #0
@nm:    TYA
        PHA
        LDA (shape_pat_lo),Y
        JSR blit_glyph
        LDA tx_lo
        CLC
        ADC #8
        STA tx_lo
        PLA
        TAY
        INY
        CPY #NAME_LEN
        BNE @nm
        ; --- skip a blank row, then iterate the body ---
        LDA #0
        STA tx_lo
        LDA #16
        STA ty_lo
        LDY #PROC_BODYLEN_OFF
        LDA (shape_pat_lo),Y
        STA arg_lo               ; body_len -- arg_lo survives blit_glyph
                                 ; (which clobbers tmp/tmp2)
        LDY #PROC_BODY_OFF
@body:
        ; bytes consumed = Y - PROC_BODY_OFF; stop when >= body_len
        TYA
        SEC
        SBC #PROC_BODY_OFF
        CMP arg_lo
        BCS @done
        ; off-screen guard: ty >= 192 -> stop
        LDA ty_lo
        CMP #192
        BCS @done
        TYA
        PHA                       ; preserve Y across blit_glyph
        LDA (shape_pat_lo),Y
        AND #$7F
        CMP #$0D
        BEQ @newline
        JSR blit_glyph
        LDA tx_lo
        CLC
        ADC #8
        STA tx_lo
        PLA
        TAY
        INY
        JMP @body
@newline:
        LDA #0
        STA tx_lo
        LDA ty_lo
        CLC
        ADC #8
        STA ty_lo
        PLA
        TAY
        INY
        JMP @body
@done:  RTS
@bad:   LDA #ERR_BAD_NAME
        JSR print_err
        RTS

; --- LIST (no arg) -- list every proc name on the Apple-1 character
;     display via ECHO. No graphics; text output is more practical for
;     a quick directory check (and lets the user scroll back through
;     terminal history). ---
@list_all:
        LDA n_procs
        BNE @any
        RTS
@any:
        LDA #<proc_table
        STA shape_pat_lo
        LDA #>proc_table
        STA shape_pat_hi
        LDA n_procs
        STA arg_lo               ; remaining count
        ; print a leading CR for visual separation from the prompt
        LDA #$0D | $80
        JSR ECHO
@aloop:
        LDY #0
@anm:   LDA (shape_pat_lo),Y
        ORA #$80                 ; ECHO requires bit 7 set
        JSR ECHO
        INY
        CPY #NAME_LEN
        BNE @anm
        LDA #$0D | $80
        JSR ECHO
        ; advance shape_pat by PROC_SLOT (244)
        CLC
        LDA shape_pat_lo
        ADC #PROC_SLOT
        STA shape_pat_lo
        LDA shape_pat_hi
        ADC #0
        STA shape_pat_hi
        DEC arg_lo
        BNE @aloop
        RTS

; ============================================================================
; cmd_edit: EDIT NAME -- thin wrapper around bufed_run. Resolves the proc
;   slot, hands the editor a pointer in shape_pat_lo:hi, then on return
;   tears down the editor view (clear_bitmap, disable_sprites, cmd_home)
;   and resets line_buf so parse_and_exec doesn't re-dispatch on the
;   stale "EDIT NAME" still sitting in the input buffer.
;   Editor primitives (ed_draw, ed_replace_line, ed_insert_line,
;   ed_delete_line, ed_find_line_offset, ed_wait_key) live in
;   dev/lib/tms9918/buffer_editor.asm.
; ============================================================================
cmd_edit:
        JSR skip_spaces
        JSR read_var_name
        JSR find_proc
        BCS @bad_name
        ; cache slot pointer for the editor (text_blit_glyph trashes mptr)
        LDA mptr_lo
        STA shape_pat_lo
        LDA mptr_hi
        STA shape_pat_hi
        JSR bufed_run
        ; --- save & exit: clear bitmap, hide sprite, restore ARROW turtle.
        ; The body has been mutated in place during edits, so "saving"
        ; is just exiting cleanly.
        JSR clear_bitmap
        JSR disable_sprites
        LDA #0
        STA sprite_mode
        STA turtle_visible
        JSR cmd_home               ; centres turtle, redraws bitmap arrow
        ; bufed_run reused line_idx as a replacement-char counter, so on
        ; return it no longer points at the trailing CR of "EDIT NAME".
        ; Reset line_buf+line_idx to a clean CR so parse_and_exec @loop
        ; unwinds instead of re-dispatching cmd_edit on the stale
        ; "EDIT NAME" still in line_buf.
        LDA #$0D
        STA line_buf
        LDA #0
        STA line_idx
        RTS
@bad_name:
        LDA #ERR_BAD_NAME
        JSR print_err
        RTS

.endif

; proc_collect_line: called by REPL when def_mode = 1. Inspects line_buf:
;   - "END" (followed by space or CR) -> finalise, def_mode = 0, OK.
;   - anything else                   -> append the whole line (incl. CR) to
;                                        the slot body. Overflow -> ERR_FULL
;                                        and abort the definition.
proc_collect_line:
        LDA #0
        STA line_idx
        JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #'E'
        BNE @append
        LDA line_buf+1,X
        AND #$7F
        CMP #'N'
        BNE @append
        LDA line_buf+2,X
        AND #$7F
        CMP #'D'
        BNE @append
        LDA line_buf+3,X
        AND #$7F
        CMP #$0D
        BEQ @end_proc
        CMP #' '
        BNE @append
@end_proc:
        LDA #0
        STA def_mode
        ; print "OK" so the user knows the definition closed
        LDA #1
        STA cmd_status
        JSR print_status
        RTS
@append:
        LDA cur_proc_lo
        STA mptr_lo
        LDA cur_proc_hi
        STA mptr_hi
        LDY #PROC_BODYLEN_OFF
        LDA (mptr_lo),Y
        STA tmp                  ; tmp = current body_len
        LDX #0                   ; X = line_buf cursor
@al:    LDA tmp
        CMP #PROC_BODY_MAX
        BCS @overflow
        ; slot offset = PROC_BODY_OFF + body_len
        CLC
        ADC #PROC_BODY_OFF
        TAY
        LDA line_buf,X
        STA (mptr_lo),Y
        INC tmp
        LDA line_buf,X
        AND #$7F
        CMP #$0D
        BEQ @done
        INX
        CPX #LINE_MAX
        BCS @done                ; safety -- shouldn't happen
        JMP @al
@done:  LDY #PROC_BODYLEN_OFF
        LDA tmp
        STA (mptr_lo),Y
        RTS
@overflow:
        LDA #ERR_FULL
        JSR print_err
        LDA #0
        STA def_mode
        RTS

; find_proc_slot: locate slot for mnem_buf in proc_table.
;   On match: C=0, mptr_lo:hi -> slot start.
;   On miss : C=1, mptr_lo:hi -> next free slot (= proc_table + n_procs * PROC_SLOT).
find_proc_slot:
        LDA #<proc_table
        STA mptr_lo
        LDA #>proc_table
        STA mptr_hi
        LDX #0
@nxt:   CPX n_procs
        BEQ @miss
        LDY #0
@c:     LDA (mptr_lo),Y
        CMP mnem_buf,Y
        BNE @no
        INY
        CPY #NAME_LEN
        BNE @c
        CLC
        RTS
@no:    CLC
        LDA mptr_lo
        ADC #PROC_SLOT
        STA mptr_lo
        BCC @ni
        INC mptr_hi
@ni:    INX
        JMP @nxt
@miss:  SEC
        RTS

; find_proc: same as find_proc_slot but only succeeds on match.
;   On match: C=0, mptr_lo:hi -> slot start. ALSO advances line_idx using tmp.
;   On miss : C=1.
find_proc:
        JSR find_proc_slot
        BCS @miss
        ; advance line_idx past consumed name (find_mnem stashed end-X in tmp)
        LDA tmp
        STA line_idx
        CLC
        RTS
@miss:  SEC
        RTS

; ============================================================================
; peek_check_tail: non-destructive look-ahead for tail-call eligibility.
;   On entry: mptr_lo:hi -> new proc's slot. line_idx just past proc name.
;   Save line_idx, parse N decimal args (where N = slot's nparams), then
;   skip_spaces and inspect the next char. If CR or ']' → tail-call site.
;   Restore line_idx in either case (caller will re-parse for real).
;   Result: C=1 → tail; C=0 → not tail.
; ============================================================================
peek_check_tail:
        LDA line_idx
        PHA
        LDY #PROC_NPARAMS_OFF
        LDA (mptr_lo),Y
        STA tmp                  ; nparams (0..MAX_PARAMS)
        LDX #0
@pl:    CPX tmp
        BEQ @check
        ; preserve mptr, X, AND tmp across parse_dec_arg.
        ; parse_dec_arg's @dec scratches `tmp`, so nparams would vanish.
        LDA tmp
        PHA
        LDA mptr_lo
        PHA
        LDA mptr_hi
        PHA
        TXA
        PHA
        JSR parse_dec_arg
        PLA
        TAX
        PLA
        STA mptr_hi
        PLA
        STA mptr_lo
        PLA
        STA tmp
        INX
        JMP @pl
@check: JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #$0D
        BEQ @tail
        ; Note: ']' is NOT a tail terminator. Inside REPEAT [...], hitting
        ; ']' just ends the current iteration -- the loop will re-enter the
        ; slice. Treating it as tail would replace the parent's frame with
        ; the callee, breaking REPEAT-based scenarios like
        ; FLOWER = REPEAT 36 [RIGHT 10 SQUARE].
        PLA
        STA line_idx             ; restore (not tail)
        CLC
        RTS
@tail:  PLA
        STA line_idx             ; restore (will re-parse via trampoline)
        SEC
        RTS

; ============================================================================
; do_tail_trampoline: commit the tail call into the parent's frame.
;   On entry: mptr_lo:hi -> new slot, line_idx at args, peek confirmed tail.
;   Effect: re-parses args, overwrites param_slot{0,1} with new bindings,
;   sets proc_body_cursor=0 / proc_body_total=new slot's body_len, leaves
;   line_idx at the trailing CR/']'. No stack push. After RTS, the outer
;   parse_and_exec @loop sees CR and returns to the parent's @line_loop,
;   which iterates afresh against the new slot via mptr_lo:hi.
;   Memory cost: zero -- spiral 4 90 → 49 logical levels in 1 frame.
; ============================================================================
do_tail_trampoline:
        LDY #PROC_NPARAMS_OFF
        LDA (mptr_lo),Y
        STA tmp                  ; nparams
        LDA tmp
        BEQ @tt_no_args
        ; --- parse arg 0 (preserving tmp across parse_dec_arg) ---
        LDA tmp
        PHA
        LDA mptr_lo
        PHA
        LDA mptr_hi
        PHA
        JSR parse_dec_arg
        PLA
        STA mptr_hi
        PLA
        STA mptr_lo
        PLA
        STA tmp
        LDA arg_lo
        STA tail_p0_lo
        LDA arg_hi
        STA tail_p0_hi
        LDA tmp
        CMP #2
        BCC @tt_bind
        ; --- parse arg 1 ---
        LDA tmp
        PHA
        LDA mptr_lo
        PHA
        LDA mptr_hi
        PHA
        JSR parse_dec_arg
        PLA
        STA mptr_hi
        PLA
        STA mptr_lo
        PLA
        STA tmp
        ; arg_lo:hi = param 1 value
@tt_bind:
        ; copy slot[PARAMS..PARAMS+NAME_LEN-1] -> param_slot0_name
        LDY #PROC_PARAMS_OFF
        LDX #0
@ttcn0: LDA (mptr_lo),Y
        STA param_slot0_name,X
        INY
        INX
        CPX #NAME_LEN
        BNE @ttcn0
        LDA tail_p0_lo
        STA param_slot0_value
        LDA tail_p0_hi
        STA param_slot0_value+1
        LDA tmp
        CMP #2
        BCC @tt_bind_done
        LDY #(PROC_PARAMS_OFF+NAME_LEN)
        LDX #0
@ttcn1: LDA (mptr_lo),Y
        STA param_slot1_name,X
        INY
        INX
        CPX #NAME_LEN
        BNE @ttcn1
        LDA arg_lo
        STA param_slot1_value
        LDA arg_hi
        STA param_slot1_value+1
@tt_bind_done:
        LDA tmp
        STA n_params_active
@tt_no_args:
        ; Reset body cursor/total so outer @line_loop walks the new slot.
        LDY #PROC_BODYLEN_OFF
        LDA (mptr_lo),Y
        STA proc_body_total
        LDA #0
        STA proc_body_cursor
        RTS

; ============================================================================
; ctrl_frame_addr: compute mptr_save = ctrl_stack + ctrl_sp * CTRL_FRAME (64).
;   Used by both push and pop in proc_invoke. CTRL_STACK_DEPTH=16 so sp
;   fits in 4 bits; sp*64 spans 0..960 = 0x000..0x3C0, so the high byte
;   adds at most 3 to ctrl_stack's high byte.
; ============================================================================
ctrl_frame_addr:
        LDA ctrl_sp
        LSR
        LSR                       ; sp >> 2 -> high byte of sp*64
        CLC
        ADC #>ctrl_stack
        STA mptr_save_hi
        LDA ctrl_sp
        AND #$03                  ; (sp & 3) -> bits 6..7 of low byte
        ASL
        ASL
        ASL
        ASL
        ASL
        ASL                       ; *64
        CLC
        ADC #<ctrl_stack
        STA mptr_save_lo
        BCC @done
        INC mptr_save_hi
@done:  RTS

; proc_invoke (V2.0): bind params, then run a (possibly multi-line) body.
;   Replaces V1.8's pair of 60 B save buffers with a 16-frame control
;   stack in PROCBSS, so non-tail nested calls (e.g. THING1 = REPEAT 4
;   [THING] inside SPIRAL inside FLOWER ...) go up to 16 deep.
;   Tail calls still bypass the stack entirely via do_tail_trampoline,
;   so deep tail recursion is free.
;
;   Steps:
;     1. Refuse if ctrl_sp == CTRL_STACK_DEPTH -> ERR_FULL.
;     2. Push outer body cursor/total + 17 B param state on the 6502
;        stack so we can scratch them during arg parsing.
;     3. Read nparams from slot. Parse N decimal args under OUTER scope.
;     4. Copy param names from the slot, values from the parsed args,
;        into param_slotN. Activate them by setting n_params_active.
;     5. Push line_buf+line_idx into the next control-stack frame and
;        INC ctrl_sp.
;     6. Walk the body line by line through parse_and_exec.
;     7. DEC ctrl_sp, pop line_buf+line_idx from the same frame, pop the
;        outer param state, pop outer cursor/total.
proc_invoke:
        LDA ctrl_sp
        CMP #CTRL_STACK_DEPTH
        BCC @ok_to_call
        ; control stack full -- non-tail recursion exceeded depth limit
        LDA #ERR_FULL
        JSR print_err
        RTS
@ok_to_call:
        ; --- 2. push outer body cursor/total (so they're free to scratch) ---
        LDA proc_body_cursor
        PHA
        LDA proc_body_total
        PHA
        ; --- 3. push outer param state (13 bytes) ---
        LDA n_params_active
        PHA
        LDA param_slot0_name
        PHA
        LDA param_slot0_name+1
        PHA
        LDA param_slot0_name+2
        PHA
        LDA param_slot0_name+3
        PHA
        LDA param_slot0_name+4
        PHA
        LDA param_slot0_name+5
        PHA
        LDA param_slot0_value
        PHA
        LDA param_slot0_value+1
        PHA
        LDA param_slot1_name
        PHA
        LDA param_slot1_name+1
        PHA
        LDA param_slot1_name+2
        PHA
        LDA param_slot1_name+3
        PHA
        LDA param_slot1_name+4
        PHA
        LDA param_slot1_name+5
        PHA
        LDA param_slot1_value
        PHA
        LDA param_slot1_value+1
        PHA
        ; --- 4. read nparams from slot ---
        LDY #PROC_NPARAMS_OFF
        LDA (mptr_lo),Y
        STA tmp                  ; tmp = nparams (0..MAX_PARAMS)
        ; --- parse arg(s) under OUTER scope ---
        LDA tmp
        BEQ @args_done
        ; preserve mptr_lo:hi AND tmp across parse_dec_arg. parse_dec_arg
        ; calls @dec which uses `tmp` as scratch for the arg<<10 multiply,
        ; so nparams in `tmp` would be lost otherwise (silent skip-bind bug).
        LDA tmp
        PHA
        LDA mptr_lo
        PHA
        LDA mptr_hi
        PHA
        JSR parse_dec_arg
        PLA
        STA mptr_hi
        PLA
        STA mptr_lo
        PLA
        STA tmp
        LDA arg_lo
        STA proc_body_cursor     ; scratch -- param 0 lo (saved to stack at top)
        LDA arg_hi
        STA proc_body_total      ; scratch -- param 0 hi
        LDA tmp
        CMP #2
        BCC @args_done
        LDA tmp
        PHA
        LDA mptr_lo
        PHA
        LDA mptr_hi
        PHA
        JSR parse_dec_arg
        PLA
        STA mptr_hi
        PLA
        STA mptr_lo
        PLA
        STA tmp
        ; param 1 value already in arg_lo:arg_hi
@args_done:
        ; --- 5. bind names + values into param slots ---
        LDA tmp
        BEQ @bind_done
        ; copy slot[PARAMS..PARAMS+NAME_LEN-1] -> param_slot0_name
        LDY #PROC_PARAMS_OFF
        LDX #0
@cn0:   LDA (mptr_lo),Y
        STA param_slot0_name,X
        INY
        INX
        CPX #NAME_LEN
        BNE @cn0
        LDA proc_body_cursor     ; latched param-0 lo
        STA param_slot0_value
        LDA proc_body_total      ; latched param-0 hi
        STA param_slot0_value+1
        LDA tmp
        CMP #2
        BCC @bind_done
        LDY #(PROC_PARAMS_OFF+NAME_LEN)
        LDX #0
@cn1:   LDA (mptr_lo),Y
        STA param_slot1_name,X
        INY
        INX
        CPX #NAME_LEN
        BNE @cn1
        LDA arg_lo
        STA param_slot1_value
        LDA arg_hi
        STA param_slot1_value+1
@bind_done:
        LDA tmp
        STA n_params_active
        ; --- 6. push current line_buf+line_idx onto control stack ---
        ; Frame layout (CTRL_FRAME=64): 0..59 = line_buf, 60 = line_idx,
        ; 61..63 = reserved (proc_body_cursor/total are pushed to the
        ; 6502 stack at @ok_to_call top, so we don't need to repeat them
        ; here). Address: ctrl_stack + ctrl_sp * 64.
        JSR ctrl_frame_addr      ; mptr_save_lo:hi -> ctrl_stack + sp*64
        LDY #0
@ctrl_push:
        LDA line_buf,Y
        STA (mptr_save_lo),Y
        INY
        CPY #LINE_MAX
        BNE @ctrl_push
        LDA line_idx
        STA (mptr_save_lo),Y     ; Y = LINE_MAX = 60
        INC ctrl_sp
        ; total body length + cursor (live across body parse_and_exec)
        LDY #PROC_BODYLEN_OFF
        LDA (mptr_lo),Y
        STA proc_body_total
        LDA #0
        STA proc_body_cursor
@line_loop:
        LDA break_flag
        BNE @body_done           ; ESC during proc body -- unwind cleanly
        LDA stop_flag
        BNE @body_done           ; STOP from inside this proc -- exit body
        LDA proc_body_cursor
        CMP proc_body_total
        BCS @body_done
        ; copy [body_cursor..next CR] from slot into line_buf[0..]
        LDX #0                   ; X = line_buf cursor
@cp:    LDA proc_body_cursor
        CMP proc_body_total
        BCC @cp_ok
        ; ran out without CR -- emit one and bail
        LDA #$0D
        STA line_buf,X
        JMP @run
@cp_ok: CLC
        ADC #PROC_BODY_OFF
        TAY
        LDA (mptr_lo),Y
        STA line_buf,X
        INC proc_body_cursor
        INX
        CPX #LINE_MAX
        BCS @run                 ; safety
        LDA line_buf-1,X         ; the byte we just stored
        AND #$7F
        CMP #$0D
        BNE @cp
@run:   LDA #0
        STA line_idx
        ; preserve mptr (slot ptr) across recursive parse_and_exec
        LDA mptr_lo
        PHA
        LDA mptr_hi
        PHA
        JSR parse_and_exec
        PLA
        STA mptr_hi
        PLA
        STA mptr_lo
        JMP @line_loop
@body_done:
        ; STOP only exits this proc; clear before unwinding so the caller
        ; resumes normally. (break_flag flows through to REPL.)
        LDA #0
        STA stop_flag
        ; --- 8. pop control-stack frame: restore line_buf + line_idx ---
        DEC ctrl_sp
        JSR ctrl_frame_addr      ; mptr_save -> ctrl_stack + sp*64
        LDY #0
@ctrl_pop:
        LDA (mptr_save_lo),Y
        STA line_buf,Y
        INY
        CPY #LINE_MAX
        BNE @ctrl_pop
        LDA (mptr_save_lo),Y     ; Y = LINE_MAX = 60
        STA line_idx
        ; --- pop outer param state (LIFO) ---
        PLA
        STA param_slot1_value+1
        PLA
        STA param_slot1_value
        PLA
        STA param_slot1_name+5
        PLA
        STA param_slot1_name+4
        PLA
        STA param_slot1_name+3
        PLA
        STA param_slot1_name+2
        PLA
        STA param_slot1_name+1
        PLA
        STA param_slot1_name
        PLA
        STA param_slot0_value+1
        PLA
        STA param_slot0_value
        PLA
        STA param_slot0_name+5
        PLA
        STA param_slot0_name+4
        PLA
        STA param_slot0_name+3
        PLA
        STA param_slot0_name+2
        PLA
        STA param_slot0_name+1
        PLA
        STA param_slot0_name
        PLA
        STA n_params_active
        ; --- pop outer body cursor/total ---
        PLA
        STA proc_body_total
        PLA
        STA proc_body_cursor
        RTS

; (roll_lfsr moved to math.asm.)

; cmd_print: PRINT "WORD or PRINT N (or :NAME). Prints to Apple-1 screen
;   followed by CR. WORD = letters/digits up to first space/CR.
cmd_print:
        JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #'"'
        BEQ @str
        ; numeric path
        JSR parse_dec_arg
        JSR print_decimal
        LDA #$0D
        ORA #$80
        JSR ECHO
        RTS
@str:   INX
        STX line_idx
@sl:    LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #' '
        BEQ @sdone
        CMP #$0D
        BEQ @sdone
        CMP #']'                  ; V2.0: stop on ']' so PRINT inside an
        BEQ @sdone                ; IF/IFELSE/REPEAT block doesn't eat the
                                  ; closing bracket and confuse the caller.
        ORA #$80
        JSR ECHO
        INC line_idx
        JMP @sl
@sdone: LDA #$0D
        ORA #$80
        JSR ECHO
        RTS

; (print_decimal + div_arg_by_10 moved to math.asm.)

; cmd_wait: WAIT N -- pause for ~N seconds at 1 MHz emulated speed.
;   Single byte arg used (arg_hi ignored).
cmd_wait:
        LDA arg_lo
        BEQ @done
@outer: LDX #0
@x:     LDY #0
@y:     NOP
        NOP
        DEY
        BNE @y
        DEX
        BNE @x
        DEC arg_lo
        BNE @outer
@done:  RTS

; cmd_pause: PAUSE N -- ~100 ms per arg unit. About 1/8 of WAIT N, fine
;   enough for animation timing. Single byte arg.
;
;   Inner loop is the same NOP-NOP-DEY busy-wait as WAIT (~3 072 cycles
;   per X iter at 1 MHz), but X starts at 32 instead of 0 so each arg
;   unit is 32 * 3 072 = 98 304 cycles ≈ 96 ms. PAUSE 5 ≈ 0.5 s,
;   PAUSE 10 ≈ 1 s. For visible bird-flap pacing: PAUSE 2 (~0.2 s).
cmd_pause:
        LDA arg_lo
        BEQ @done
@outer: LDX #32
@x:     LDY #0
@y:     NOP
        NOP
        DEY
        BNE @y
        DEX
        BNE @x
        DEC arg_lo
        BNE @outer
@done:  RTS

; var_lookup: search mnem_buf, params first then var_table.
;   On match: C=0, mptr_lo:hi -> matched slot (NAME_LEN name + 2 byte value).
;   On miss : C=1, mptr_lo:hi -> next free var_table slot.
;   Param slots are laid out (NAME_LEN name + 2 value) just like var_table
;   entries so callers can read value via LDY #NAME_LEN / LDA (mptr_lo),Y.
var_lookup:
        LDA n_params_active
        BEQ @globals
        ; --- check param 0 ---
        LDY #0
@p0c:   LDA param_slot0_name,Y
        CMP mnem_buf,Y
        BNE @try_p1
        INY
        CPY #NAME_LEN
        BNE @p0c
        LDA #<param_slot0_name
        STA mptr_lo
        LDA #>param_slot0_name
        STA mptr_hi
        CLC
        RTS
@try_p1:
        LDA n_params_active
        CMP #2
        BCC @globals
        LDY #0
@p1c:   LDA param_slot1_name,Y
        CMP mnem_buf,Y
        BNE @globals
        INY
        CPY #NAME_LEN
        BNE @p1c
        LDA #<param_slot1_name
        STA mptr_lo
        LDA #>param_slot1_name
        STA mptr_hi
        CLC
        RTS
@globals:
        LDA #<var_table
        STA mptr_lo
        LDA #>var_table
        STA mptr_hi
        LDX #0
@nxt:   CPX n_vars
        BEQ @miss
        LDY #0
@c:     LDA (mptr_lo),Y
        CMP mnem_buf,Y
        BNE @no
        INY
        CPY #NAME_LEN
        BNE @c
        CLC
        RTS
@no:    CLC
        LDA mptr_lo
        ADC #VAR_ENTRY_SIZE
        STA mptr_lo
        BCC @ni
        INC mptr_hi
@ni:    INX
        JMP @nxt
@miss:  SEC
        RTS

cmd_home:
        JSR erase_turtle
        LDA #128
        STA tx_lo
        LDA #96
        STA ty_lo
        LDA #0
        STA th_lo
        STA th_hi
.ifdef LOGO_GEN2
        STA tx_hi             ; keep HOME on the low 256 columns (tx_lo=128) --
                              ; without this a turtle parked at x>=256 would
                              ; re-home into the phantom column 384.
.endif
        ; (V2.0 fix) HOME no longer flips pen back down -- the pen-state
        ; should survive a HOME so PU + HOME + REPEAT [...] flies the
        ; sprite without leaving a bitmap trail. CS still implicitly
        ; resets pen via main: -> cmd_cs's clear_bitmap path (the screen
        ; is empty so it doesn't matter).
        JSR draw_turtle
        RTS

cmd_cs:
        ; clear_bitmap wipes everything including any visible turtle, so
        ; just clear the visible flag (no need to erase first).
        LDA #0
        STA turtle_visible
        JSR clear_bitmap
        JMP cmd_home

cmd_bye:
        JSR erase_turtle
        JMP WOZ_RST

cmd_seth:
        JSR turn_erase
        JSR mod360_arg
        LDA arg_lo
        STA th_lo
        LDA arg_hi
        STA th_hi
        JSR turn_draw
        RTS

cmd_setxy:
        JSR erase_turtle
.ifdef LOGO_GEN2
        ; clamp x to the full 0..279 HGR width -> tx_lo / tx_hi
        LDA arg_lo
        CMP #<280
        LDA arg_hi
        SBC #>280
        BCC @xok
        LDA #<279
        STA tx_lo
        LDA #>279
        STA tx_hi
        JMP @xdone
@xok:   LDA arg_lo
        STA tx_lo
        LDA arg_hi
        STA tx_hi
@xdone:
.else
        LDA arg_lo
        STA tx_lo
.endif
        LDA arg2_lo
        CMP #192
        BCC @yok
        LDA #191
@yok:   STA ty_lo
        JSR draw_turtle
        RTS

cmd_tr:
        JSR turn_erase
        JSR mod360_arg
        CLC
        LDA th_lo
        ADC arg_lo
        STA th_lo
        LDA th_hi
        ADC arg_hi
        STA th_hi
        JSR norm360
        JSR turn_draw
        RTS

cmd_tl:
        JSR turn_erase
        JSR mod360_arg
        LDA #<360
        SEC
        SBC arg_lo
        STA tmp
        LDA #>360
        SBC arg_hi
        STA tmp2
        CLC
        LDA th_lo
        ADC tmp
        STA th_lo
        LDA th_hi
        ADC tmp2
        STA th_hi
        JSR norm360
        JSR turn_draw
        RTS

; (mod360_arg + norm360 moved to math.asm.)

; ============================================================================
; cmd_fd / cmd_bk : forward / back by arg pixels (8-bit unsigned distance).
;
;   sin_h = signed_sin(heading)    in [-64..+64]
;   cos_h = signed_cos(heading)
;   dx = (distance * sin_h) / 64    (signed)
;   dy = (distance * cos_h) / 64
;   nx = tx + dx                    (signed 16-bit, clamp to [0..255])
;   ny = ty - dy                    (Y axis flipped: north = up)
;   if pen=1: line_xy(tx,ty,nx,ny)
;   tx <- nx ; ty <- ny
; ============================================================================
cmd_fd:
        LDA #0                ; sign = + (forward)
        STA bk_sign
        JMP fd_common
cmd_bk:
        LDA #$FF              ; sign = -
        STA bk_sign
        ; fall through

fd_common:
        JSR erase_turtle      ; remove turtle pixels before line draw
.ifdef LOGO_GEN2
        ; clamp distance to 0..511 (16-bit mul_dist_by_signed handles it; the
        ; full-width 280-px screen never needs a single step beyond ~340 px).
        LDA arg_hi
        CMP #2
        BCC @ok_d             ; arg < 512
        LDA #255
        STA arg_lo
        LDA #1
        STA arg_hi            ; clamp to 511
@ok_d:
.else
        ; clamp distance to 0..255
        LDA arg_hi
        BEQ @ok_d
        LDA #255
        STA arg_lo
        LDA #0
        STA arg_hi
@ok_d:
.endif
        ; --- compute dx = (distance * sin(heading)) / 64, signed ---
        LDA th_lo
        STA tmp               ; angle low
        LDA th_hi
        STA tmp2              ; angle high
        JSR signed_sin        ; A = signed sin*64, range -64..+64
        JSR mul_dist_by_signed ; -> prod_lo:prod_hi signed (dx16)
        ; if BK, negate
        LDA bk_sign
        BEQ @no_neg_dx
        JSR negate_prod
@no_neg_dx:
        ; nx16 = tx + dx (signed 16-bit add). On GEN2 tx is a full 0..279 9-bit
        ; value, so the high byte must be tx_hi (not 0) or a move that starts in
        ; the 256..279 zone loses its +256 and snaps back to the low 256.
        CLC
        LDA tx_lo
        ADC prod_lo
        STA nx_save_lo
.ifdef LOGO_GEN2
        LDA tx_hi
.else
        LDA #0
.endif
        ADC prod_hi
        STA nx_save_hi
        ; --- compute dy = (distance * cos(heading)) / 64 ---
        ; cos(a) = sin(a + 90)
        CLC
        LDA th_lo
        ADC #90
        STA tmp
        LDA th_hi
        ADC #0
        STA tmp2
        ; reduce mod 360 in tmp/tmp2
        JSR mod360_tmp
        JSR signed_sin
        JSR mul_dist_by_signed
        LDA bk_sign
        BEQ @no_neg_dy
        JSR negate_prod
@no_neg_dy:
        ; ny16 = ty - dy
        SEC
        LDA ty_lo
        SBC prod_lo
        STA tmp
        LDA #0
        SBC prod_hi
        STA tmp2
.ifdef LOGO_GEN2
        ; --- clamp nx into the full [0..279] HGR width ---
        LDA nx_save_hi
        BPL @nx_nonneg
        LDA #0                ; negative -> 0
        STA nx_save_lo
        STA nx_save_hi
        JMP @nx_done
@nx_nonneg:
        LDA nx_save_lo
        CMP #<280
        LDA nx_save_hi
        SBC #>280
        BCC @nx_done          ; nx < 280 -> ok
        LDA #<279
        STA nx_save_lo
        LDA #>279
        STA nx_save_hi
@nx_done:
.else
        ; --- clamp nx (nx_save_lo:nx_save_hi signed) into [0..255] ---
        LDA nx_save_hi
        BPL @nx_pos
        LDA #0                ; negative: clamp to 0
        STA nx_save_lo
        JMP @nx_done
@nx_pos:BEQ @nx_done          ; high=0 -> nx in [0..255], low byte ok
        LDA #255              ; positive overflow
        STA nx_save_lo
@nx_done:
.endif
        ; --- clamp ny (tmp:tmp2 signed) into [0..191] ---
        LDA tmp2
        BPL @ny_pos
        LDA #0
        STA tmp
        JMP @ny_done
@ny_pos:BEQ @ny_check
        LDA #191
        STA tmp
        JMP @ny_done
@ny_check:
        LDA tmp
        CMP #192
        BCC @ny_done
        LDA #191
        STA tmp
@ny_done:
        LDA tmp
        STA ny_save           ; latch clamped ny -- line_xy clobbers tmp
        ; --- if pen down, draw line_xy ---
        LDA pen
        BEQ @no_draw
        LDA tx_lo
        STA ln_x0
        LDA ty_lo
        STA ln_y0
        LDA nx_save_lo
        STA ln_x1
        LDA ny_save
        STA ln_y1
.ifdef LOGO_GEN2
        LDA tx_hi             ; old position X high byte
        STA ln_x0h
        LDA nx_save_hi        ; new position X high byte
        STA ln_x1h
        JSR line_xy16
.else
        JSR line_xy
.endif
@no_draw:
        ; commit new turtle position
        LDA nx_save_lo
        STA tx_lo
.ifdef LOGO_GEN2
        LDA nx_save_hi
        STA tx_hi
.endif
        LDA ny_save
        STA ty_lo
        JSR draw_turtle
        RTS

; mod360_tmp: reduce tmp:tmp2 modulo 360.
; (mod360_tmp + signed_sin + mul_dist_by_signed + negate_prod + sin_q
;  moved to math.asm.)

; ============================================================================
; cmd_repeat: arg_lo:arg_hi = count.  Expect '[' next, find matching ']',
;             loop count times re-running parse_and_exec on the slice.
;             Supports 1 nesting level.
; ============================================================================
cmd_repeat:
        JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #'['
        BNE @bad
        INX
        STX rep_start         ; index just past '['
        ; scan for matching ']'  (count nested '[' and ']')
        LDY #1                ; depth
@scan:  LDA line_buf,X
        AND #$7F
        CMP #$0D
        BEQ @bad
        CMP #'['
        BNE @notopen
        INY
        JMP @next
@notopen:
        CMP #']'
        BNE @next
        DEY
        BEQ @found
@next:  INX
        JMP @scan
@found: STX rep_end           ; index of matching ']'
        ; loop arg_lo/hi times
@loop:  ; poll for ESC abort, then check break_flag and stop_flag.
        ; cmd_repeat is the only place a REPEAT FOREVER could get stuck, so
        ; it must offer the escape hatch. STOP coming from inside the slice
        ; (e.g. via IF) also exits the loop.
        JSR poll_break
        LDA break_flag
        BNE @end
        LDA stop_flag
        BNE @end
        ; if count == 0 -> done
        LDA arg_lo
        ORA arg_hi
        BEQ @end
        ; decrement count
        LDA arg_lo
        BNE @no_borrow
        DEC arg_hi
@no_borrow:
        DEC arg_lo
        ; save count + bracket bounds on stack (parse_and_exec may recurse)
        LDA arg_lo
        PHA
        LDA arg_hi
        PHA
        LDA rep_start
        PHA
        LDA rep_end
        PHA
        ; reset line_idx to slice start, run parse_and_exec until ']'
        LDA rep_start
        STA line_idx
        JSR parse_and_exec
        ; parse_and_exec stops at ']' (or CR -- shouldn't occur here)
        ; restore
        PLA
        STA rep_end
        PLA
        STA rep_start
        PLA
        STA arg_hi
        PLA
        STA arg_lo
        JMP @loop
@end:   ; advance line_idx past the ']'
        LDA rep_end
        CLC
        ADC #1
        STA line_idx
        RTS
@bad:   LDA #ERR_NO_RB
        JSR print_err
        ; skip rest of line
@b2:    LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #$0D
        BEQ @bdone
        INC line_idx
        JMP @b2
@bdone: RTS

; ============================================================================
; cmd_if: IF <a> <op> <b> [ <body> ]   -- conditional execution.
;   <op> is one of '>', '<', '='. <a> and <b> are decimal terms with the
;   usual :var / arithmetic support. If true, body runs; else it's skipped.
;   STOP and BREAK both abort the body cleanly.
;   if_a_lo / if_a_hi / if_op_save live in PROCBSS (declared up there with
;   the rest of the IF/proc state, not here in CODE).
; ============================================================================
; Comparison operator bitmask. parse_op stores the set of allowed signs
; for `(a-b)` -- e.g. `<` accepts only negative results, `<=` accepts
; negative or zero. cmp_eval computes the actual sign of (a-b) as one
; of these bits and ANDs with if_op_save: non-zero → true.
OK_NEG    = $01
OK_ZERO   = $02
OK_POS    = $04

; parse_op: read 1-2 chars from line_buf and set if_op_save to the
; corresponding sign-bitmask. Sets if_op_save = 0 on a bad operator.
;   <    OK_NEG
;   =    OK_ZERO
;   >    OK_POS
;   <=   OK_NEG | OK_ZERO
;   >=   OK_POS | OK_ZERO
;   <>   OK_NEG | OK_POS
; line_idx advanced past whatever was consumed (1 or 2 chars).
parse_op:
        JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #'<'
        BEQ @op_lt
        CMP #'>'
        BEQ @op_gt
        CMP #'='
        BEQ @op_eq
        ; not an operator
        LDA #0
        STA if_op_save
        RTS
@op_eq: INC line_idx
        LDA #OK_ZERO
        STA if_op_save
        RTS
@op_lt: INC line_idx                ; consumed '<'
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #'='
        BEQ @op_le
        CMP #'>'
        BEQ @op_ne
        ; bare '<'
        LDA #OK_NEG
        STA if_op_save
        RTS
@op_le: INC line_idx
        LDA #(OK_NEG | OK_ZERO)
        STA if_op_save
        RTS
@op_ne: INC line_idx
        LDA #(OK_NEG | OK_POS)
        STA if_op_save
        RTS
@op_gt: INC line_idx                ; consumed '>'
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #'='
        BEQ @op_ge
        ; bare '>'
        LDA #OK_POS
        STA if_op_save
        RTS
@op_ge: INC line_idx
        LDA #(OK_POS | OK_ZERO)
        STA if_op_save
        RTS

; cmp_eval: signed 16-bit (a-b), store 0/1 in tmp.
;   In : if_a_lo:hi (a), arg_lo:hi (b), if_op_save (bitmask).
;   Out: tmp = 1 if (a OP b) holds, else 0. Clobbers A, tmp2.
cmp_eval:
        SEC
        LDA if_a_lo
        SBC arg_lo
        STA tmp                     ; result lo
        LDA if_a_hi
        SBC arg_hi
        STA tmp2                    ; result hi
        ; Pick the actual sign-bit
        LDA tmp2
        BMI @sign_neg
        ORA tmp
        BEQ @sign_zero
        LDA #OK_POS
        JMP @check
@sign_neg:
        LDA #OK_NEG
        JMP @check
@sign_zero:
        LDA #OK_ZERO
@check: AND if_op_save
        BEQ @false
        LDA #1
        STA tmp
        RTS
@false: LDA #0
        STA tmp
        RTS

; scan_block: starting at line_idx (after skip_spaces), find the next
; bracketed `[...]` slice. Sets rep_start = position just past '[',
; rep_end = position of matching ']'. Advances line_idx past ']'.
; Returns C=0 on success, C=1 if no '[' or unmatched (ERR_NO_RB).
scan_block:
        JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #'['
        BNE @sb_miss
        INX
        STX rep_start
        LDY #1
@sb_l:  LDA line_buf,X
        AND #$7F
        CMP #$0D
        BEQ @sb_miss
        CMP #'['
        BNE @sb_no
        INY
        JMP @sb_n
@sb_no: CMP #']'
        BNE @sb_n
        DEY
        BEQ @sb_found
@sb_n:  INX
        JMP @sb_l
@sb_found:
        STX rep_end
        ; advance line_idx past ']'
        INX
        STX line_idx
        CLC
        RTS
@sb_miss:
        SEC
        RTS

cmd_if:
        ; The 'I' dispatch flag in parse_and_exec called us before any args
        ; were parsed (unlike numeric flags). Parse LHS, op, RHS, evaluate.
        JSR parse_dec_arg
        LDA arg_lo
        STA if_a_lo
        LDA arg_hi
        STA if_a_hi
        JSR parse_op
        LDA if_op_save
        BEQ @bad_arg
        JSR parse_dec_arg
        JSR cmp_eval                ; tmp = 0/1
        JSR scan_block              ; sets rep_start/rep_end, line_idx past ']'
        BCS @bad
        ; If false, we're done -- line_idx is already past ']'.
        LDA tmp
        BEQ @done
        ; If true, run the block via parse_and_exec.
        ; Save line_idx (now past ']') so we can resume there afterwards.
        LDA line_idx
        PHA
        LDA rep_end
        PHA                         ; preserve in case parse_and_exec recurses
        LDA rep_start
        STA line_idx
        JSR parse_and_exec
        PLA
        STA rep_end
        PLA
        STA line_idx
@done:  RTS
@bad:   LDA #ERR_NO_RB
        JSR print_err
@bskip: LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #$0D
        BEQ @bdone2
        INC line_idx
        JMP @bskip
@bdone2:RTS
@bad_arg:
        LDA #ERR_BAD_ARG
        JSR print_err
        JMP @bskip

; cmd_ifelse: IFELSE a OP b [yes] [no].
;   Same comparison logic as cmd_if; runs the appropriate block.
cmd_ifelse:
        JSR parse_dec_arg
        LDA arg_lo
        STA if_a_lo
        LDA arg_hi
        STA if_a_hi
        JSR parse_op
        LDA if_op_save
        BEQ @bad_arg
        JSR parse_dec_arg
        JSR cmp_eval                ; tmp = 0/1
        ; --- scan first block (yes) ---
        JSR scan_block
        BCS @bad
        ; latch yes-block bounds; scan_block clobbers rep_start/rep_end on
        ; the next call so we save to PROCBSS scratch.
        LDA rep_start
        STA if_a_lo                 ; reuse: yes_start
        LDA rep_end
        STA if_a_hi                 ; reuse: yes_end
        ; --- scan second block (no) ---
        JSR scan_block
        BCS @bad
        ; line_idx now sits past the ']' of the no-block; we'll resume
        ; here regardless of which branch ran.
        LDA line_idx
        PHA
        ; Decide which slice to run.
        LDA tmp
        BNE @run_yes
        ; false: rep_start/rep_end already point at the no-block.
        LDA rep_start
        STA line_idx
        JMP @run_block
@run_yes:
        LDA if_a_lo                 ; yes_start
        STA rep_start
        LDA if_a_hi                 ; yes_end
        STA rep_end
        LDA rep_start
        STA line_idx
@run_block:
        ; preserve rep_start/rep_end across recursive parse_and_exec
        LDA rep_start
        PHA
        LDA rep_end
        PHA
        JSR parse_and_exec
        PLA
        STA rep_end
        PLA
        STA rep_start
        ; restore line_idx to past the no-block
        PLA
        STA line_idx
        RTS
@bad:   LDA #ERR_NO_RB
        JSR print_err
@bskip2:LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #$0D
        BEQ @bdone3
        INC line_idx
        JMP @bskip2
@bdone3:RTS
@bad_arg:
        LDA #ERR_BAD_ARG
        JSR print_err
        JMP @bskip2

; ============================================================================
; -- Bitmap turtle (drawn as 3 line_xy segments forming an arrowhead) --
;    The triangle is rendered with plot_mode=XOR, so calling draw_turtle a
;    second time at the same (tx,ty,heading) erases it. This gives
;    "ghost-style" turtle that doesn't disturb user-drawn geometry.
;
;    Triangle in turtle-local coords (heading-up frame):
;      tip       : forward = +9,  right =  0
;      back-left : forward = -4,  right = -4
;      back-right: forward = -4,  right = +4
;    Length tip-to-base = 13 px, base width = 8 px (clearly visible arrow).
;
;    All vertex computation lives in compute_turtle_verts.
; ============================================================================

; (disable_sprites moved to tms9918m2.asm.)

.ifdef LOGO_GEN2
; add_tx_off: 16-bit (tx_lo:tx_hi) + signed 8-bit offset in A.
;   Returns A = low byte, X = high byte. Carry from the low add is carried
;   into the high add through the BIT-$2C sign-extend (BIT touches N/V/Z,
;   never C). Used by the GEN2 9-bit turtle-vertex math. Stack-balanced, so
;   it is safe to call between compute_turtle_verts' PHA/PLA arg saves.
add_tx_off:
        TAY                 ; save offset (Y), N flag = its sign
        CLC
        ADC tx_lo           ; low = tx_lo + offset (sets carry)
        PHA
        TYA                 ; A = offset
        BPL @pos
        LDA #$FF            ; negative -> sign-extend high addend = $FF
        .byte $2C           ; BIT abs absorbs the LDA #$00 below (C untouched)
@pos:   LDA #$00            ; positive -> high addend = $00
        ADC tx_hi           ; high = tx_hi + signext + carry
        TAX
        PLA
        RTS
.endif

; compute_turtle_verts: from (tx_lo, ty_lo, th_lo:th_hi), compute 3 vertices
;   tx0..ty2 (six bytes in BSS).
;   uses signed_sin / mul_dist_by_signed (clobbers tmp, tmp2, sign_flag,
;   prod_lo, prod_hi, arg2_*, arg_lo, arg_hi).
compute_turtle_verts:
        ; preserve arg_lo/arg_hi/arg2_lo/arg2_hi so the caller's command
        ; parsing state survives the multiplies below.
        LDA arg_lo
        PHA
        LDA arg_hi
        PHA
        LDA arg2_lo
        PHA
        LDA arg2_hi
        PHA
        ; --- sin(h) -> s_byte (signed -64..+64) ---
        LDA th_lo
        STA tmp
        LDA th_hi
        STA tmp2
        JSR signed_sin
        STA s_byte
        ; --- cos(h) = sin(h+90) -> c_byte ---
        CLC
        LDA th_lo
        ADC #90
        STA tmp
        LDA th_hi
        ADC #0
        STA tmp2
        JSR mod360_tmp
        JSR signed_sin
        STA c_byte
        ; --- s_back = (4 * s)/64 (range -4..+4 px) ---
        LDA #4
        STA arg_lo
        LDA #0
        STA arg_hi
        LDA s_byte
        JSR mul_dist_by_signed
        LDA prod_lo
        STA s_back
        ; --- c_back = (4 * c)/64 ---
        LDA #4
        STA arg_lo
        LDA c_byte
        JSR mul_dist_by_signed
        LDA prod_lo
        STA c_back
        ; --- s_tip = (9 * s)/64 (range -9..+9 px) ---
        LDA #9
        STA arg_lo
        LDA s_byte
        JSR mul_dist_by_signed
        LDA prod_lo
        STA s_tip
        ; --- c_tip = (9 * c)/64 ---
        LDA #9
        STA arg_lo
        LDA c_byte
        JSR mul_dist_by_signed
        LDA prod_lo
        STA c_tip
.ifdef LOGO_GEN2
        ; ---- 9-bit-X vertices (full 280-px width). txN = tx + offN computed
        ;      16-bit by add_tx_off; y vertices stay 8-bit. ----
        ; V0 tip: off = s_tip
        LDA s_tip
        JSR add_tx_off
        STA tx0
        STX tx0h
        SEC
        LDA ty_lo
        SBC c_tip
        STA ty0
        ; V1 back-left: off = -s_back - c_back
        SEC
        LDA #0
        SBC s_back
        SEC
        SBC c_back
        JSR add_tx_off
        STA tx1
        STX tx1h
        SEC
        LDA ty_lo
        SBC s_back
        CLC
        ADC c_back
        STA ty1
        ; V2 back-right: off = -s_back + c_back
        SEC
        LDA #0
        SBC s_back
        CLC
        ADC c_back
        JSR add_tx_off
        STA tx2
        STX tx2h
        CLC
        LDA ty_lo
        ADC s_back
        CLC
        ADC c_back
        STA ty2
.else
        ; ---- V0 tip   = (tx + s_tip, ty - c_tip) ----
        CLC
        LDA tx_lo
        ADC s_tip
        STA tx0
        SEC
        LDA ty_lo
        SBC c_tip
        STA ty0
        ; ---- V1 back-left  fwd=-4 right=-4
        ;        dx = -s_back - c_back, dy = -c_back + s_back
        ;        screen = (tx - s_back - c_back, ty - s_back + c_back)
        SEC
        LDA tx_lo
        SBC s_back
        SEC
        SBC c_back
        STA tx1
        SEC
        LDA ty_lo
        SBC s_back
        CLC
        ADC c_back
        STA ty1
        ; ---- V2 back-right fwd=-4 right=+4
        ;        dx = -s_back + c_back, dy = -c_back - s_back
        ;        screen = (tx - s_back + c_back, ty + s_back + c_back)
        SEC
        LDA tx_lo
        SBC s_back
        CLC
        ADC c_back
        STA tx2
        CLC
        LDA ty_lo
        ADC s_back
        CLC
        ADC c_back
        STA ty2
.endif
        ; restore arg_lo/arg_hi/arg2_lo/arg2_hi
        PLA
        STA arg2_hi
        PLA
        STA arg2_lo
        PLA
        STA arg_hi
        PLA
        STA arg_lo
        RTS

; ============================================================================
; Option B -- save/restore the 9 cells of the arrow's bounding box around
;   the OR-mode triangle draw. The arrow is OR'd over the saved background
;   so trail bits are not inverted (unlike XOR), and erase replays the
;   saved bytes verbatim, restoring the bitmap exactly. No XOR vertex
;   doubling since OR is idempotent. compute_turtle_verts is still needed
;   to feed trace_turtle_lines (line_xy needs explicit endpoints), but the
;   box that we save is anchored on (tx_lo, ty_lo) snapped to the cell
;   grid -- the arrow is at most 13x13 px so a 24x24 px (3x3 cells) box
;   centred on the turtle covers it for any heading, given the existing
;   draw_turtle bounds check (tx in [9..246], ty in [9..182]).
;
; Iteration order is row-major (top-to-bottom, left-to-right), so save
; and restore both run X = 0..71 in lock-step and don't need per-cell
; address bookkeeping.
; ============================================================================

.ifdef LOGO_GEN2
; ============================================================================
; GEN2 HGR turtle subsystem (replaces the TMS9918 sprite/VRAM region below).
;   The HGR card has no hardware sprites, so the turtle is a reversible XOR
;   triangle: draw_turtle XOR-traces it, erase_turtle re-XOR-traces the SAME
;   verts (tx/ty unchanged between erase and the following move) to undo it --
;   no background save/restore (arrow_save_bbox) needed. SETSHAPE emotes are a
;   later increment; here SETSHAPE just consumes its name and keeps the
;   classic triangle. (sprite_mode stays 0 for the whole session.)
; ============================================================================

; trace_turtle_lines: the 3 line_xy edges (tip->BL->BR->tip) in whatever
;   plot_mode the caller set. Identical to the TMS version (pure line_xy).
trace_turtle_lines:
        ; tip -> back-left  (9-bit X endpoints via line_xy16)
        LDA tx0
        STA ln_x0
        LDA tx0h
        STA ln_x0h
        LDA ty0
        STA ln_y0
        LDA tx1
        STA ln_x1
        LDA tx1h
        STA ln_x1h
        LDA ty1
        STA ln_y1
        JSR line_xy16
        ; back-left -> back-right
        LDA tx1
        STA ln_x0
        LDA tx1h
        STA ln_x0h
        LDA ty1
        STA ln_y0
        LDA tx2
        STA ln_x1
        LDA tx2h
        STA ln_x1h
        LDA ty2
        STA ln_y1
        JSR line_xy16
        ; back-right -> tip
        LDA tx2
        STA ln_x0
        LDA tx2h
        STA ln_x0h
        LDA ty2
        STA ln_y0
        LDA tx0
        STA ln_x1
        LDA tx0h
        STA ln_x1h
        LDA ty0
        STA ln_y1
        JSR line_xy16
        RTS

; gen2_emote_vsync: coarse V-blank sync (HST0 = bit 7 of any $C25x read).
;   Called at the head of every visible EMOTE transition (erase_turtle @emote),
;   so the XOR erase -> reposition -> redraw burst BEGINS at V-blank. That parks
;   the transient "bird fully erased" window up in V-blank / top-of-frame, where
;   the beam has already swept past the mid-screen sprite -- the async HGR
;   renderer stops catching the blank, so the BIRDFLY strobe goes away. This is
;   the GEN2 analogue of the TMS erase_turtle's WAIT_VBLANK (which syncs on the
;   VDP $CC01 status flag instead). Polls PAGE1 -- LOGO runs HIRES/PAGE1 and a
;   $C254 read is an idempotent page-1 SELECT (not a bit toggle), so the poll
;   never disturbs the mode. ORs two samples 4c apart to mask the 3c colour-
;   burst notch (see gen2.inc / gen2_sync.asm). Clobbers A only.
GEN2_PAGE1 = $C254
gen2_emote_vsync:
@live:  LDA GEN2_PAGE1
        ORA GEN2_PAGE1
        BMI @live                 ; spin until live scan (HST0 = 0)
@blank: LDA GEN2_PAGE1
        ORA GEN2_PAGE1
        BPL @blank                ; spin until the next blanking edge
        .repeat 13
        NOP                       ; 26c: an H-blank (25c) has ended by now,
        .endrep                   ;      so only a real V-blank still reads 1
        LDA GEN2_PAGE1
        ORA GEN2_PAGE1
        BPL @live                 ; it was only an H-blank -> scan the next line
        RTS                       ; V-blank: beam parked off-screen, safe to blit

draw_turtle:
        LDA turtle_visible
        BNE @done                 ; already on screen
        ; Only Y needs a window (ty in [9..182]); X is the full 0..279 and
        ; plot_set_x16 clips anything past the left/right edge, so the cursor
        ; can sit anywhere across the HGR width.
        LDA ty_lo
        CMP #9
        BCC @done
        CMP #183
        BCS @done
        LDA sprite_mode
        BNE @emote
        ; --- bitmap triangle (XOR, reversible) ---
        JSR compute_turtle_verts
        LDA #1
        STA plot_mode
        JSR trace_turtle_lines
        LDA #0                    ; back to OR for subsequent trail draws
        STA plot_mode
        LDA #1
        STA turtle_visible
        RTS
@emote: LDA #0                    ; colour path: this pass DRAWS (OR + pen)
        STA em_erase
        JSR gen2_draw_emote       ; XOR-blit (white) / OR-blit (colour) the shape
        LDA #1
        STA turtle_visible
@done:  RTS

erase_turtle:
        LDA turtle_visible
        BEQ @done
        LDA sprite_mode
        BNE @emote
        ; --- bitmap triangle: re-XOR the same edges (tx/ty unchanged) ---
        JSR compute_turtle_verts
        LDA #1
        STA plot_mode
        JSR trace_turtle_lines
        LDA #0
        STA plot_mode
        LDA #0
        STA turtle_visible
        RTS
@emote: JSR gen2_emote_vsync      ; begin the erase+reposition+redraw at V-blank
        LDA #1                    ; colour path: this pass ERASES (XOR)
        STA em_erase
        JSR gen2_draw_emote       ; re-XOR the same dots -> erased
        LDA #0
        STA turtle_visible
@done:  RTS

; turn_erase / turn_draw: heading-only commands (SETH / TR / TL) call these
;   instead of erase_turtle / draw_turtle. The classic triangle turtle
;   (sprite_mode 0) rotates with the heading, so it must be erased+redrawn as
;   before. A SETSHAPE emote (sprite_mode 1) is non-directional -- a pure turn
;   changes NONE of its pixels (gen2_draw_emote ignores th; tx/ty are unchanged)
;   -- so skip the XOR erase/redraw entirely. That removes the transient
;   "erased" window the async HGR renderer would otherwise sample as a flicker
;   (e.g. BIRDFLY's "TR 12" between flaps); the emote simply stays drawn as-is.
turn_erase:
        LDA sprite_mode
        BNE @skip                 ; emote: turning is a visual no-op -> leave it
        JMP erase_turtle          ; triangle: real erase (tail call)
@skip:  RTS
turn_draw:
        LDA sprite_mode
        BNE @skip
        JMP draw_turtle
@skip:  RTS

; gen2_draw_emote: XOR-blit the current SETSHAPE bitmap (shape_pat_lo:hi,
;   spr_size = 8 or 32) at the turtle, PIXEL-DOUBLED 2x (each source pixel ->
;   a 2x2 screen block). Doubling makes the emote solid white on HGR instead
;   of a single-pixel mesh that NTSC-artifacts into a colour fringe, and gives
;   the narrator a readable size next to the speech bubble. Centred top-left =
;   (tx - 2*spr_xoff, ty - 2*spr_yoff).
;   TMS sprite format: 16x16 = 32 B in TL/BL/TR/BR quarter-blocks (8 B each,
;   bit 7 = leftmost); 8x8 = 8 B (bytes 0..7 = the TL quarter). The quarter
;   math collapses to a plain linear index for the 8x8 case, so one path
;   serves both. plot_set clobbers A/X/Y (but NOT pix_x/pix_y), so every loop
;   var lives in ZP and the 2x2 block re-uses pix_x/pix_y via inc/dec.
gen2_draw_emote:
        LDA spr_size
        CMP #32
        BNE @dim8
        LDA #16
        .byte $2C                 ; BIT abs -> skip the LDA #8
@dim8:  LDA #8
        STA em_dim
        ; top-left = (tx - 2*spr_xoff, ty - 2*spr_yoff)  (2x scale, centred;
        ;   9-bit X so the emote can sit in the 256..279 zone)
        LDA spr_xoff
        ASL
        STA tmp               ; 2*spr_xoff
        SEC
        LDA tx_lo
        SBC tmp
        STA em_x0
        LDA tx_hi
        SBC #0
        STA em_x0h
        ; Colour parity nudge: on HGR the two artifact hues of a palette family
        ; (violet/green, or blue/orange) are chosen by the EVEN/ODD column the
        ; dots land on. pen_hi_tbl (in the backend) picks the family bit; this
        ; table picks the parity, so pen_color -> one of 4 hues. Shift the whole
        ; sprite's left-column base by 0/1 px accordingly (white path skips it).
        LDA em_color
        BEQ @nopar
        LDA em_erase
        BNE @usepar               ; erase: reuse the parity the draw latched
        LDX pen_color             ; draw: latch parity for this pen
        LDA em_par_tbl,X
        STA em_par
@usepar:
        CLC
        LDA em_par
        ADC em_x0
        STA em_x0
        LDA em_x0h
        ADC #0
        STA em_x0h
@nopar:
        LDA spr_yoff
        ASL
        STA tmp
        SEC
        LDA ty_lo
        SBC tmp
        STA em_y0
        ; Blit mode. White emote (em_color=0): XOR both ways -- self-inverse, and
        ; the 2x2 cell lights two adjacent dots so HGR collapses them to solid
        ; white. Colour emote (em_color=1): OR+pen to draw (em_erase=0), XOR to
        ; erase (em_erase=1); the inner loop then lights only the LEFT dot of
        ; each cell, so every-other column carries the pen hue instead of white.
        LDA em_color
        BEQ @white
        LDA em_erase              ; colour: 0 = OR draw (pen), 1 = XOR erase
        STA plot_mode
        JMP @seedrow
@white: LDA #1                    ; white: XOR
        STA plot_mode
@seedrow:
        LDA #0
        STA em_row
@row:   LDA #0
        STA em_col
@col:   ; quarter = ((col&8)?2:0) | ((row&8)?1:0)  -> tmp
        LDA #0
        STA tmp
        LDA em_row
        AND #8
        BEQ @nr8
        LDA #1
        STA tmp
@nr8:   LDA em_col
        AND #8
        BEQ @nc8
        LDA tmp
        ORA #2
        STA tmp
@nc8:   ; Y = quarter*8 + (row & 7)
        LDA tmp
        ASL
        ASL
        ASL
        STA tmp2
        LDA em_row
        AND #7
        ORA tmp2
        TAY
        LDA (shape_pat_lo),Y      ; sprite data byte
        STA tmp
        LDA em_col
        AND #7
        TAX
        LDA gen2_em_bit,X         ; mask = $80 >> (col&7)
        AND tmp
        BNE @lit                  ; set -> plot; else fall through to @next
        JMP @next                 ; transparent (long jump: @next is out of
                                  ; branch range past the two blit variants)
@lit:
        ; 2x2 screen block: px (9-bit) = em_x0 + col*2, py = em_y0 + row*2
        LDA em_col
        ASL                       ; col*2
        CLC
        ADC em_x0
        STA pix_x
        LDA em_x0h
        ADC #0
        STA pix_xh
        LDA em_row
        ASL
        CLC
        ADC em_y0
        STA pix_y
        JSR plot_set_x16          ; (px,   py)
        LDA em_color
        BNE @coldot               ; colour: LEFT dot only -> every-other column
        INC pix_x                 ; px+1 (16-bit)
        BNE @r1
        INC pix_xh
@r1:    JSR plot_set_x16          ; (px+1, py)
        INC pix_y
        JSR plot_set_x16          ; (px+1, py+1)
        LDA pix_x                 ; px (16-bit dec back)
        BNE @r2
        DEC pix_xh
@r2:    DEC pix_x
        JSR plot_set_x16          ; (px,   py+1)
        JMP @next
@coldot:
        INC pix_y                 ; the 2-tall left column: (px, py+1)
        JSR plot_set_x16
        DEC pix_y
@next:  INC em_col
        LDA em_col
        CMP em_dim
        BEQ @coldone              ; column loop back-branch is out of range now
        JMP @col                  ;   (the colour variant grew the cell body)
@coldone:
        INC em_row
        LDA em_row
        CMP em_dim
        BEQ @rowdone
        JMP @row
@rowdone:
        LDA #0
        STA plot_mode
        RTS
gen2_em_bit:
        .byte $80, $40, $20, $10, $08, $04, $02, $01
; em_par_tbl: column-parity (0/1) per pen_color, tuned with the backend's
;   pen_hi_tbl (family bit) so the DEMO2 narrator gets 4 recognisable HGR hues.
;   Values verified against the live NTSC decode -- see the demo2 SETPC scheme.
em_par_tbl:
        ;      0    1    2    3    4    5    6    7
        .byte   0,   0,   1,   1,   0,   0,   1,   1
        ;      8    9   10   11   12   13   14   15
        .byte   0,   1,   0,   1,   0,   1,   0,   0

; cmd_setshape (GEN2): look the name up in shape_table (shared with TMS),
;   latch the pattern pointer + size, switch to sprite mode and redraw. ARROW
;   reverts to the bitmap triangle. No VRAM upload -- the bitmap IS the sprite.
cmd_setshape:
        JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #'"'
        BNE @rd
        INC line_idx
@rd:    JSR read_var_name         ; mnem_buf <- shape name (padded)
        LDA mnem_buf
        CMP #' '
        BEQ @ret                  ; empty -> ignore
        ; --- ARROW sentinel: back to the triangle turtle ---
        LDA mnem_buf
        CMP #'A'
        BNE @search
        LDA mnem_buf+1
        CMP #'R'
        BNE @search
        LDA mnem_buf+2
        CMP #'R'
        BNE @search
        LDA mnem_buf+3
        CMP #'O'
        BNE @search
        LDA mnem_buf+4
        CMP #'W'
        BNE @search
        LDA sprite_mode
        BEQ @ret                  ; already a triangle
        JSR erase_turtle          ; erase the emote (sprite_mode still 1)
        LDA #0
        STA sprite_mode
        JSR draw_turtle           ; redraw as triangle
@ret:   RTS
@search:
        LDA #<shape_table
        STA mptr_lo
        LDA #>shape_table
        STA mptr_hi
@sl:    LDY #0
        LDA (mptr_lo),Y
        CMP #$FF
        BEQ @bad
@cmp:   LDA (mptr_lo),Y
        CMP mnem_buf,Y
        BNE @adv
        INY
        CPY #NAME_LEN
        BNE @cmp
        ; --- match: erase the OLD turtle first, then latch the new shape ---
        JSR erase_turtle
        LDY #NAME_LEN
        LDA (mptr_lo),Y
        STA spr_size
        INY
        LDA (mptr_lo),Y
        STA shape_pat_lo
        INY
        LDA (mptr_lo),Y
        STA shape_pat_hi
        JSR apply_sprite_size     ; spr_xoff / spr_yoff
        LDA #1
        STA sprite_mode
        JSR draw_turtle           ; blit the new emote
        RTS
@adv:   CLC
        LDA mptr_lo
        ADC #(NAME_LEN+3)
        STA mptr_lo
        BCC @sl
        INC mptr_hi
        JMP @sl
@bad:   LDA #ERR_BAD_NAME
        JSR print_err
        RTS

.else
arrow_save_bbox:
        LDA #0                    ; mode 0 = save (VDP read -> BSS)
        .byte $2C                 ; BIT abs absorbs the next LDA #1 opcode
arrow_restore_bbox:
        LDA #1                    ; mode 1 = restore (BSS -> VDP write)
arrow_io_bbox:
        STA arrow_io_dir
        ; --- 4x4 cell bbox (32x32 px) anchored on (tx-9, ty-9) snapped
        ;     down to the cell grid, so the bbox always contains the
        ;     full ±9 px reach of the arrow's tip regardless of heading.
        ;     Clamp the right/bottom anchor so the box stays on-screen
        ;     (256x192 px = 32x24 cells; bbox max start = 224,160).
        LDA tx_lo
        SEC
        SBC #9
        AND #$F8
        CMP #225
        BCC @x_ok
        LDA #224
@x_ok:  STA arrow_bx
        LDA ty_lo
        SEC
        SBC #9
        AND #$F8
        CMP #161
        BCC @y_ok
        LDA #160
@y_ok:  STA pix_y
        LDA #4
        STA arrow_rows_left
        LDX #0
@row:   LDA arrow_bx
        STA pix_x
        LDA #4
        STA arrow_cols_left
@col:   JSR calc_pix_addr
        LDA arrow_io_dir
        BNE @restore_cell
        ; --- save 8 bytes: VDP_DATA -> arrow_bg_pat[X..X+7] ---
        JSR vdp_set_read
        LDY #8
@s_b:   LDA VDP_DATA
        STA arrow_bg_pat,X
        INX
        DEY
        JSR tms9918_pad18   ; +18c — reads share the VRAM access window; the
        BNE @s_b            ;   raw 16c loop sat AT the silicon floor (a too-
        BEQ @next_col       ;   fast read returns the stale read-ahead byte)
@restore_cell:
        ; --- restore 8 bytes: arrow_bg_pat[X..X+7] -> VDP_DATA ---
        JSR vdp_set_write
        LDY #8
@r_b:   LDA arrow_bg_pat,X
        STA VDP_DATA
        INX
        DEY
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (back-to-back VDP store)
        BNE @r_b
@next_col:
        LDA pix_x
        CLC
        ADC #8
        STA pix_x
        DEC arrow_cols_left
        BNE @col
        LDA pix_y
        CLC
        ADC #8
        STA pix_y
        DEC arrow_rows_left
        BNE @row
        RTS

; trace_turtle_lines: emit the 3 line_xy calls (tip->BL->BR->tip) using
;   whatever plot_mode the caller set. Used by draw_turtle's bitmap path.
trace_turtle_lines:
        ; line tip -> back-left
        LDA tx0
        STA ln_x0
        LDA ty0
        STA ln_y0
        LDA tx1
        STA ln_x1
        LDA ty1
        STA ln_y1
        JSR line_xy
        ; line back-left -> back-right
        LDA tx1
        STA ln_x0
        LDA ty1
        STA ln_y0
        LDA tx2
        STA ln_x1
        LDA ty2
        STA ln_y1
        JSR line_xy
        ; line back-right -> tip
        LDA tx2
        STA ln_x0
        LDA ty2
        STA ln_y0
        LDA tx0
        STA ln_x1
        LDA ty0
        STA ln_y1
        JSR line_xy
        RTS

; --- heading_to_octant + apply_sprite_size live in
;     dev/lib/tms9918/sprite_helpers.asm. Caller-provided ZP wiring is
;     handled by .exportzp / .importzp at the top of this file.

; draw_turtle: in bitmap mode, save the 9 cells around the turtle then
;   OR-trace the triangle on top so trails are not inverted (cleanly
;   reversible by erase_turtle, which writes the saved cells back).
;   In sprite mode, write 4 bytes (Y, X, name, color) to the sprite #0
;   attribute slot at VRAM $3B00. The TMS9918 hardware-blits the sprite
;   over the bitmap; no save/restore needed.
draw_turtle:
        JSR     tms9918_pad18   ; MANUAL caller-gap cushion (12c, was 40c pre-openMSX-port)
        LDA sprite_mode
        BEQ @bitmap
        ; --- sprite path: write sprite-0 attribute (Y, X, name=0, color=$0F)
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA #imm bridge)
        LDA #$3B | $40            ; $3B00 + write enable
        STA VDP_CTRL
        ; Y = ty - spr_yoff: TMS9918 displays sprite at scanline (Y+1)
        ; and the pattern's top-left is the sprite origin. spr_yoff
        ; centres on (tx,ty): 9 for 16x16 sprites, 5 for 8x8. spr_xoff
        ; mirrors on the X axis (8 vs 4). Both come from
        ; apply_sprite_size at the previous SETSHAPE.
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA zp/abs bridge)
        LDA ty_lo
        SEC
        SBC spr_yoff
        STA VDP_DATA
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA zp/abs bridge)
        LDA tx_lo
        SEC
        SBC spr_xoff
        STA VDP_DATA
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA #imm bridge)
        LDA #0                    ; pattern name = 0 (sprite_pattern_table[0])
        STA VDP_DATA
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA zp/abs bridge)
        LDA pen_color             ; sprite-0 colour (SETPC drives every surface)
        STA VDP_DATA
        RTS
@bitmap:
        LDA turtle_visible
        BNE @done
        ; bounds check: tx in [9..246], ty in [9..182]
        LDA tx_lo
        CMP #9
        BCC @done
        CMP #247
        BCS @done
        LDA ty_lo
        CMP #9
        BCC @done
        CMP #183
        BCS @done
        JSR compute_turtle_verts
        JSR arrow_save_bbox       ; capture 9 cells before we modify them
        LDA #0                    ; OR mode: set bits + write colour cells
        STA plot_mode
        JSR trace_turtle_lines
        LDA #1
        STA turtle_visible
@done:  RTS

; erase_turtle: bitmap mode -- replay the saved 9 cells over the pattern
;   table, restoring the bitmap exactly. Sprite mode -- nothing to do
;   (sprites don't bleed into the pattern table, the next attribute
;   write moves them).
erase_turtle:
        LDA sprite_mode
        BNE @done
        LDA turtle_visible
        BEQ @done
        ; Sync to VBlank before the bbox restore + line draw that
        ; follows in the caller. Every visible command path routes
        ; through here, so a single WAIT_VBLANK paces the whole REPL
        ; render burst.
        WAIT_VBLANK
        JSR arrow_restore_bbox
        LDA #0
        STA turtle_visible
@done:  RTS

; TMS build: heading-only commands keep erasing+redrawing exactly as before.
;   The HW sprite path has no flicker to fix (erase_turtle is a no-op for an
;   emote and draw_turtle just re-emits the attribute byte; the bitmap path is
;   already VBlank-synced), so turn_erase/turn_draw alias straight through and
;   the CodeTank GAME3 ROM stays byte-for-byte identical.
turn_erase = erase_turtle
turn_draw  = draw_turtle

; ============================================================================
; cmd_setshape: SETSHAPE "NAME -- swap the dynamic-turtle sprite pattern.
;   Looks up NAME (BIRD1, BIRD2, TURTL, ...) in shape_table, copies the
;   matching 32-byte 16x16 pattern into the TMS9918 sprite-pattern table
;   slot 0 (VRAM $1800), then re-positions the turtle sprite. On the
;   first call, also flips the VDP into 16x16-sprite mode (R1 |= $02)
;   and erases any visible bitmap turtle so the two render paths don't
;   ghost on top of each other.
; ============================================================================
cmd_setshape:
        JSR skip_spaces
        ; optional leading "
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #'"'
        BNE @rd
        INC line_idx
@rd:    JSR read_var_name        ; mnem_buf <- shape name (uppercased, padded)
        LDA mnem_buf
        CMP #' '
        BNE @name_ok
        JMP @bad_name             ; long jump (insertion of ARROW + search
                                  ; setup pushed @bad_name out of branch range)
@name_ok:
        ; --- "ARROW" is a sentinel: revert to the classic bitmap-triangle
        ;     turtle. Hide sprite-0 (Y=$D0 terminator), clear sprite_mode,
        ;     redraw the bitmap turtle at the current (tx,ty). Lets the
        ;     demo end with the original arrow visible instead of the
        ;     last loaded bird sprite.
        LDA mnem_buf
        CMP #'A'
        BNE @search_table
        LDA mnem_buf+1
        CMP #'R'
        BNE @search_table
        LDA mnem_buf+2
        CMP #'R'
        BNE @search_table
        LDA mnem_buf+3
        CMP #'O'
        BNE @search_table
        LDA mnem_buf+4
        CMP #'W'
        BNE @search_table
        ; --- ARROW ---
        ; Switch back to bitmap-triangle turtle.
        LDA sprite_mode
        BEQ @arrow_done           ; already in bitmap mode -- no-op
        ; Y=$D0 to sprite #0 attribute = TMS9918 sprite-list terminator.
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA #imm bridge)
        LDA #$3B | $40
        STA VDP_CTRL
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA #imm bridge)
        LDA #$D0
        STA VDP_DATA
        ; Flip back to bitmap mode and redraw the triangle.
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA #imm bridge)
        LDA #0
        STA sprite_mode
        JSR draw_turtle           ; bitmap path now -- triangle XOR'd in
@arrow_done:
        RTS

@search_table:
        ; Search shape_table for mnem_buf (entries are NAME_LEN bytes name +
        ; 2 bytes pattern pointer; $FF terminator).
        LDA #<shape_table
        STA mptr_lo
        LDA #>shape_table
        STA mptr_hi
@search:
        LDY #0
        LDA (mptr_lo),Y
        CMP #$FF
        BNE @scmp                 ; near branch; long-jump fallthrough below
        JMP @bad_name             ; @bad_name out of branch range now that
                                  ; @found has TURTL detection + dir-mode setup
@scmp:  LDA (mptr_lo),Y
        CMP mnem_buf,Y
        BNE @snext
        INY
        CPY #NAME_LEN
        BNE @scmp
        ; match. shape_table entries are 9 bytes:
        ;   [0..NAME_LEN-1] : name (uppercase, space-padded)
        ;   [NAME_LEN]      : size in bytes (8 or 32) -- distinguishes
        ;                     8x8 from 16x16 sprites
        ;   [NAME_LEN+1..2] : pattern pointer (lo, hi)
        LDY #NAME_LEN
        LDA (mptr_lo),Y
        STA spr_size
        INY
        LDA (mptr_lo),Y
        STA shape_pat_lo
        INY
        LDA (mptr_lo),Y
        STA shape_pat_hi
        JSR apply_sprite_size     ; derive spr_xoff / spr_yoff / spr_r1
        JMP @found
@snext: CLC
        LDA mptr_lo
        ADC #(NAME_LEN+3)         ; 9-byte stride: name + size + ptr_lo + ptr_hi
        STA mptr_lo
        BCC @search
        INC mptr_hi
        JMP @search

@found:
        ; All shapes are static now (TURTL/BOAT directional families
        ; were removed). The static-pattern path below copies spr_size
        ; bytes once and the sprite stays put across heading changes.
@ensure_sprite_mode:
        ; First-time entry: erase any bitmap turtle.
        LDA sprite_mode
        BNE @write_r1
        JSR erase_turtle          ; bitmap path while sprite_mode is still 0
        LDA #1
        STA sprite_mode
@write_r1:
        ; Blank display before the sprite-pattern burst (doc § 6.4).
        ; Restored to spr_r1 ($C0/$C2) after the burst — spr_r1 carries
        ; the correct sprite-size bit, so we can't use vdp_display_on
        ; (which is the fixed-$C0 helper).
        JSR vdp_display_off       ; lib helper (tms9918_pad.asm)
@load_pat:
        ; Copy spr_size bytes from (shape_pat),Y to VRAM $1800 (sprite-0
        ; pattern). spr_size = 8 or 32, latched from shape_table at
        ; match time.
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA #imm bridge)
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA #imm bridge)
        LDA #$18 | $40
        STA VDP_CTRL
        LDY #0
@cppat: LDA (shape_pat_lo),Y
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (back-to-back VDP store)
        STA VDP_DATA
        INY
        CPY spr_size
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (back-to-back VDP store)
        BNE @cppat
        ; --- Restore display ON with the correct sprite-size bit. spr_r1
        ;     was set by apply_sprite_size at @scmp's match site --
        ;     $C2 for 16x16 (BIRD/TURTL/BOAT), $C0 for 8x8 (HEART/…). ---
        LDA spr_r1
        STA VDP_CTRL
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (before LDA #imm bridge)
        LDA #$81                  ; write to register 1
        STA VDP_CTRL
@reposition:
        ; Reposition sprite at the current turtle (tx, ty).
        JSR draw_turtle
        RTS
@bad_name:
        LDA #ERR_BAD_NAME
        JSR print_err
        RTS
.endif  ; LOGO_GEN2 (HGR turtle subsystem) vs TMS9918 sprite/VRAM region

; shape_table: name (NAME_LEN = 6 bytes) + size byte (8 or 32) +
;   pointer to the pattern (lo, hi). 9 bytes per entry. Names are
;   space-padded so a 5-letter "BIRD1" matches what read_var_name
;   puts in mnem_buf. Terminator: $FF.
;
; Size byte:
;   32 = 16x16 sprite (32 bytes, TL/BL/TR/BR quarter-block layout)
;    8 =  8x8 sprite ( 8 bytes, single 8x8 block)
;
; cmd_setshape rewrites the VDP register R1 sprite-size bit on every
; SETSHAPE according to this byte, so 8x8 and 16x16 shapes can be
; freely interleaved within a single LOGO session.
shape_table:
        .byte "BIRD1 "
        .byte 32
        .word bird1_pat
        .byte "BIRD2 "
        .byte 32
        .word bird2_pat
        .byte "HEART "
        .byte 8
        .word heart_pat
        ; --- Expression emotes (12x 16x16, from SCROLL-O-SPRITES by Quale,
        ;     CC-BY-3.0). Extracted via tools/extract_scroll_expressions.py.
        ;     Pattern data lives further down (after the BOAT block).
        .byte "NORMAL"
        .byte 32
        .word serious_pat
        .byte "HAPPY "
        .byte 32
        .word happy_pat
        .byte "SUPER "
        .byte 32
        .word excited_pat
        .byte "SAD   "
        .byte 32
        .word sad_pat
        .byte "UPSET "
        .byte 32
        .word hurt_pat
        .byte "ANGRY "
        .byte 32
        .word angry_pat
        .byte "GRUMPY"
        .byte 32
        .word upset_pat
        .byte "PERV  "
        .byte 32
        .word smug_pat
        .byte "SICK  "
        .byte 32
        .word sick_pat
        .byte "SLEEP "
        .byte 32
        .word sleeping_pat
        .byte "PIRATE"
        .byte 32
        .word yarr_pat
        .byte "SHADES"
        .byte 32
        .word nerd_pat
        .byte $FF

; ----- bird1_pat / bird2_pat / heart_pat now live alongside the emote
;       sprites in dev/lib/tms9918/sprites_emotes.asm. shape_table above
;       references them via .word; ld65 resolves them from the lib .o.

.if 0
; ============================================================================
; (TURTL and BOAT directional sprite sets removed -- they were the
; original V2.0/V2.1 dynamic-turtle showcase but ate ~700 B of ROM.
; The directional dispatch in cmd_setshape, update_dir_turtle helper,
; dir_turtle_table/boat_dir_table indirection, and ZP slots
; dir_turtle_active/last_octant/dir_table_lo/hi were all wired only
; to these two shapes, so they're gone too.
; ============================================================================

; TURTL -- 8 directional 16x16 sprites, one per 45-degree octant.
;   E-facing turtle is hand-designed (oval shell with hex-segment
;   seams, 4 splayed legs, head pokes east). N / S / W are 90-degree
;   rotations; NE / NW / SE / SW are hand-tweaked so the head
;   protrudes diagonally without aliasing.
;   See tools/gen_turtle_sprites.py for the pixel-art source.
;
; turtle_e (heading 90 - the model):
;     ....##....##....   1
;     ....##....##....   2
;     ..############..   3
;     .##############.   4
;     .##.##.##.##....   5  hex segments
;     .##############.   6
;     .###############   7  shell + head
;     .###############   8  shell + head
;     .##############.   9
;     .##.##.##.##....   10 hex segments
;     .##############.   11
;     ..############..   12
;     ....##....##....   13
;     ....##....##....   14
;
; dir_turtle_table indexes by octant: 0=N, 1=NE, 2=E, 3=SE, 4=S,
;   5=SW, 6=W, 7=NW (matches heading_to_octant output).
dir_turtle_table:
        .word turtle_n,  turtle_ne, turtle_e,  turtle_se
        .word turtle_s,  turtle_sw, turtle_w,  turtle_nw

; Default TURTL pattern that the (legacy) shape_table points at -- only
; used as a placeholder; cmd_setshape detects the TURTL name and from
; then on draw_turtle uploads the heading-matching octant directly.
turtle_pat = turtle_e

; turtle_n (heading 0 -- head pokes north / up)
; turtle_n
turtle_n:
        .byte $01, $03, $07, $67, $68, $1F, $1F, $1F
        .byte $1F, $1F, $1F, $1F, $1F, $6F, $67, $03
        .byte $80, $C0, $E0, $E6, $16, $F8, $F8, $F8
        .byte $F8, $F8, $F8, $F8, $F8, $F6, $E6, $C0
; turtle_ne
turtle_ne:
        .byte $00, $00, $00, $00, $00, $00, $01, $03
        .byte $07, $0F, $0F, $0F, $0F, $07, $03, $00
        .byte $00, $0C, $1E, $3E, $7E, $FE, $FE, $FE
        .byte $FE, $FC, $FC, $F8, $F0, $E0, $C0, $00
; turtle_e
turtle_e:
        .byte $00, $60, $60, $1F, $3F, $7F, $FF, $FF
        .byte $FF, $FF, $7F, $3F, $1F, $60, $60, $00
        .byte $00, $18, $18, $E0, $F0, $EC, $EE, $EF
        .byte $EF, $EE, $EC, $F0, $E0, $18, $18, $00
; turtle_se
turtle_se:
        .byte $00, $00, $00, $00, $1E, $3F, $7F, $7F
        .byte $7F, $7F, $3F, $1F, $0F, $07, $01, $00
        .byte $00, $00, $00, $00, $00, $00, $80, $C0
        .byte $E0, $F0, $F8, $FC, $FE, $FE, $FC, $00
; turtle_s
turtle_s:
        .byte $03, $67, $6F, $1F, $1F, $1F, $1F, $1F
        .byte $1F, $1F, $1F, $68, $67, $07, $03, $01
        .byte $C0, $E6, $F6, $F8, $F8, $F8, $F8, $F8
        .byte $F8, $F8, $F8, $16, $E6, $E0, $C0, $80
; turtle_sw
turtle_sw:
        .byte $00, $03, $07, $0F, $1F, $3F, $3F, $7F
        .byte $7F, $7F, $7F, $7E, $7C, $78, $30, $00
        .byte $00, $C0, $E0, $F0, $F0, $F0, $F0, $E0
        .byte $C0, $80, $00, $00, $00, $00, $00, $00
; turtle_w
turtle_w:
        .byte $00, $18, $18, $07, $0F, $37, $77, $F7
        .byte $F7, $77, $37, $0F, $07, $18, $18, $00
        .byte $00, $06, $06, $F8, $FC, $FE, $FF, $FF
        .byte $FF, $FF, $FE, $FC, $F8, $06, $06, $00
; turtle_nw
turtle_nw:
        .byte $00, $3F, $7F, $7F, $3F, $1F, $0F, $07
        .byte $03, $01, $00, $00, $00, $00, $00, $00
        .byte $00, $80, $E0, $F0, $F8, $FC, $FE, $FE
        .byte $FE, $FE, $FC, $78, $00, $00, $00, $00

; ============================================================================
; BOAT -- 8 directional 16x16 sprites (speedboat silhouettes), one per
;   45-degree octant. Same TL/BL/TR/BR ordering as the TURTL set.
;   Source: spritedatabase.net SpeedboatRip (white set), filled silhouette,
;   downscaled 16:1 from 24x26 to 16x16 via Lanczos+threshold.
;   See tools/extract_speedboat_sprites.py for the extraction recipe.
;
; boat_dir_table indexes by octant: 0=N, 1=NE, 2=E, 3=SE, 4=S,
;   5=SW, 6=W, 7=NW (matches heading_to_octant output).
boat_dir_table:
        .word boat_n,  boat_ne, boat_e,  boat_se
        .word boat_s,  boat_sw, boat_w,  boat_nw

; Default BOAT pattern that the (legacy) shape_table points at -- only
; used as a placeholder; cmd_setshape detects the BOAT name and from
; then on draw_turtle uploads the heading-matching octant directly.
boat_pat = boat_e

; boat_n (heading 0 -- bow points north / up)
; boat_n
boat_n:
        .byte $00, $07, $07, $07, $07, $0F, $0F, $0E
        .byte $0F, $0F, $0F, $0F, $0F, $0F, $0F, $00
        .byte $80, $F0, $F0, $F0, $F0, $F8, $F8, $38
        .byte $F8, $F8, $F8, $F8, $F8, $F8, $F8, $00
; boat_ne
boat_ne:
        .byte $00, $00, $00, $00, $00, $00, $00, $01
        .byte $03, $07, $0F, $1E, $3C, $78, $70, $00
        .byte $00, $04, $0E, $1E, $3E, $7E, $FE, $FC
        .byte $F0, $C0, $80, $00, $00, $00, $00, $00
; boat_e
boat_e:
        .byte $00, $00, $00, $00, $7F, $7F, $7F, $7F
        .byte $7F, $7F, $7F, $7F, $7F, $00, $00, $00
        .byte $00, $00, $00, $00, $E0, $FE, $FE, $7E
        .byte $7F, $7E, $FE, $FE, $E0, $00, $00, $00
; boat_se
boat_se:
        .byte $00, $60, $70, $78, $3C, $1E, $0F, $07
        .byte $07, $03, $01, $01, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $80
        .byte $C0, $E0, $F0, $F8, $FC, $FE, $7C, $00
; boat_s
boat_s:
        .byte $00, $1F, $1F, $1F, $1F, $1F, $1F, $1F
        .byte $1C, $1F, $1F, $0F, $0F, $0F, $0F, $01
        .byte $00, $F0, $F0, $F0, $F0, $F0, $F0, $F0
        .byte $70, $F0, $F0, $E0, $E0, $E0, $E0, $00
; boat_sw
boat_sw:
        .byte $00, $00, $00, $00, $00, $01, $03, $0F
        .byte $3F, $7F, $7E, $7C, $78, $70, $20, $00
        .byte $00, $0E, $1E, $3C, $78, $F0, $E0, $C0
        .byte $80, $00, $00, $00, $00, $00, $00, $00
; boat_w
boat_w:
        .byte $00, $00, $00, $07, $7F, $7F, $7E, $FE
        .byte $7E, $7F, $7F, $07, $00, $00, $00, $00
        .byte $00, $00, $00, $FE, $FE, $FE, $FE, $FE
        .byte $FE, $FE, $FE, $FE, $00, $00, $00, $00
; boat_nw
boat_nw:
        .byte $00, $3E, $7F, $3F, $1F, $0F, $07, $03
        .byte $01, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $80, $80, $C0, $E0
        .byte $E0, $F0, $78, $3C, $1E, $0E, $06, $00
.endif

; --- Expression emotes (12x 16x16) live in dev/lib/tms9918/sprites_emotes.asm
;     SCROLL-O-SPRITES by Quale (CC-BY-3.0). shape_table above references
;     these labels via .word; ld65 resolves them from the linked lib .o.

.ifdef CODETANK_BUILD
; ============================================================================
; LABEL "TEXT -- print text on the TMS9918 bitmap at the current turtle
;   position. 8x8 glyphs are blitted from the embedded charmap. Differs
;   from PRINT in three ways: (1) lands on the bitmap, not on the
;   Apple-1 character display; (2) reads multi-word strings (until CR
;   or ']') instead of stopping at the first space; (3) advances tx_lo
;   by 8 per glyph and clamps when tx_lo >= 248.
;   CodeTank-only because charmap_table is a 1024-byte .incbin (the
;   16K dev DRAM build's CODE slot has no headroom for it).
; ============================================================================
; ============================================================================
; SAY "TEXT -- comic-book speech bubble. Draws a fixed-position frame
;   below the sprite (turtle Y assumed to be ~16 = top), with a tail
;   pointing up to the sprite's mouth area, then prints TEXT inside the
;   bubble starting at top-left (16, 88). The bubble is hardcoded to
;   span (8,80)-(247,112) = 240x32 px and supports up to 3 lines of 28
;   chars each via auto-wrap (no word-boundary detection -- text breaks
;   mid-word when tx_lo overflows the right margin).
;   Lines past bottom (ty_lo >= 112) are silently truncated.
;   Saves the user from manually SETXY-ing each line of dialog: every
;   DEMO2 narrator scene now reads as
;     CS / SETXY 128 16 / SETSHAPE "EMOTE / SAY "TEXT / WAIT n
; ============================================================================
cmd_say:
        ; The speech bubble must stay readable regardless of the sprite
        ; tint set by SETPC (the character may be green when ill, red
        ; when angry, etc.). Save pen_color, force white for the bubble
        ; outline + glyph cells, and restore it after the text is laid
        ; out so subsequent commands (sprite refresh, trail, etc.) keep
        ; the user-chosen colour.
        ;
        ; ALSO save the turtle (tx_lo, ty_lo) — cmd_label walks tx/ty
        ; through the bubble glyph cells and leaves them at the end of
        ; the text. Without restoration the next SETPC / SETSHAPE would
        ; redraw the sprite-0 attribute at the bubble-text-end position
        ; instead of the user's intended SETXY position (DEMO2 "SICK
        ; sprite parks inside the bubble after MAKE ME ILL" regression,
        ; reported May 2026).
        LDA pen_color
        PHA
        LDA tx_lo
        PHA
        LDA ty_lo
        PHA
        LDA #$0F
        STA pen_color
        JSR draw_bubble
        LDA #16
        STA tx_lo                 ; left margin inside bubble
        LDA #88
        STA ty_lo                 ; first text row, just below top border
        JSR cmd_label             ; reuse scan/blit logic to render text
        ; --- proportional pause: WAIT (lines * 4) units, ~2.4 s per line
        ;     at native 1 MHz. lines = (ty_lo - 88)/8 + 1, capped by the
        ;     bubble-bottom truncation in cmd_label's scan loop. Compute
        ;     this *before* restoring ty_lo, since the saved value tells
        ;     us nothing about how many lines the bubble actually used.
        LDA ty_lo
        SEC
        SBC #88
        LSR
        LSR
        LSR
        CLC
        ADC #1
        ASL
        ASL
        STA arg_lo
        LDA #0
        STA arg_hi
        ; --- restore turtle position + pen_color (LIFO) ---
        PLA
        STA ty_lo
        PLA
        STA tx_lo
        PLA
        STA pen_color
        JMP cmd_wait              ; tail-call -- cmd_wait RTS returns to
                                  ;   parse_and_exec for us
cmd_label:
        JSR skip_spaces
@scan:  LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #'"'                  ; eat any leading "
        BNE @check_term
        INX
        STX line_idx
        JMP @scan
@check_term:
        CMP #$0D                  ; CR -> done
        BEQ @done
        CMP #']'                  ; bracket terminator -> done
        BEQ @done
        ; A holds char. Save it in X across the wrap math (which
        ; clobbers A repeatedly).
        TAX
        ; If we already pushed ty past the bubble bottom on a previous
        ; wrap, swallow the rest of the text without blitting.
        LDA ty_lo
        CMP #112
        BCS @consume
        LDA tx_lo
        CMP #240                  ; past bubble right margin?
        BCC @blit_now
        ; --- wrap to next line ---
        LDA #16
        STA tx_lo
        LDA ty_lo
        CLC
        ADC #8
        STA ty_lo
        CMP #112                  ; past bubble bottom?
        BCC @blit_now
        ; below bubble -- consume rest, no blit
        JMP @consume
@blit_now:
        TXA                       ; restore A = char
        JSR blit_glyph
        LDA tx_lo
        CLC
        ADC #8
        STA tx_lo
@consume:
        INC line_idx
        JMP @scan
@done:  RTS

; ============================================================================
; blit_glyph: thin wrapper that copies the LOGO turtle position
;   (tx_lo / ty_lo) into the lib-visible pix_x / pix_y ZP slots and
;   delegates to text_blit_glyph in dev/lib/tms9918/text_bitmap.asm.
;   Existing call sites still pass A = char and read tx_lo/ty_lo as the
;   target cell; only the wrapper knows about the renamed entry point.
;   Input:  A = ASCII char (low 7 bits used)
;   Output: 8 bytes written to VRAM, A clobbered.
; ============================================================================
blit_glyph:
        PHA
        LDA tx_lo
        STA pix_x
        LDA ty_lo
        STA pix_y
        PLA
        JMP text_blit_glyph

; --- draw_bubble (~85 B) lives in dev/lib/tms9918/bubble.asm.
;     charmap_table (1024 B) and the bitmap blit core live in
;     dev/lib/tms9918/text_bitmap.asm. blit_glyph above wraps
;     text_blit_glyph so existing call sites still read tx_lo/ty_lo.

; ============================================================================
; demo2_script: CodeTank narrator-mode slideshow. POM1 narrates its own
;   life story through the 12 SCROLL-O-SPRITES emote sprites + LABEL
;   bitmap text. ~23 scenes, all 12 emotes used, ends by restoring the
;   classic ARROW turtle so the REPL is usable after BYE-equivalent.
;   Format: CR-terminated lines, $00 sentinel.
; ============================================================================
demo2_script:
.ifdef LOGO_GEN2
        ; ====================================================================
        ; GEN2 build: the FACTUAL story of UNCLE BERNIE'S "APRIL FOOLS'" CARD --
        ; the GEN2 HGR Color Graphics Card this LOGO actually runs on. Bernie
        ; has an annual tradition of unveiling a "foolish" Apple-1 project every
        ; April 1st (both April Fools' Day AND, by lore, the Apple-1's birthday);
        ; "foolish" = it works fine, just too niche to sell. This card was his
        ; April 1, 2026 joke -- then community enthusiasm turned it real and the
        ; PCBs were ordered from OSHPark. Stays on Bernie's turf (no P-LAB /
        ; TMS9918 / CodeTank refs -- that is Claudio's card, narrated by the TMS
        ; build in the .else branch).
        ; Facts sourced from Applefritter: "Uncle Bernie's GEN2 color graphics
        ; card for the Apple-1", "A glimpse on Uncle Bernie's Apple-1 color
        ; graphics card", and "Uncle Bernie's April Fools' Day Apple-1 Riddle".
        ; ====================================================================
        ; --- One-time setup: clear the screen ONCE and place the narrator.
        ;     After this the image is NEVER fully reset again. Each scene only
        ;     swaps the emote (SETSHAPE = XOR erase old + XOR draw new) and
        ;     refreshes the bubble band (SAY clears just scanlines 80..112).
        ;     That is the whole point of the reversible XOR-doubled sprite:
        ;     animate it in place, don't wipe the frame between cards. ---
        .byte "CS", $0D
        .byte "SETXY 140 46", $0D
        ; --- Colour scheme (mirrors the TMS build's mood tinting; the GEN2
        ;     emote blitter now honours pen_color, drawn every-other-column so
        ;     HGR shows the hue). Signature tint per mood, from the 4 stable
        ;     HGR artifact colours: green (3) upbeat, magenta (15) excited,
        ;     blue (4) cool/down, orange (9) heated/bold:
        ;       HAPPY/NORMAL -> 3 green   SUPER -> 15 magenta
        ;       SHADES/SAD   -> 4 blue    GRUMPY/PIRATE -> 9 orange
        ;     SETPC precedes each SETSHAPE whose tint changes; the emote's
        ;     draw latches the pen so its later erase stays exact. ---
        ; --- Act 1: the card admits what it is ---
        .byte "SETPC 3", $0D
        .byte "SETSHAPE ", $22, "HAPPY", $0D
        .byte "SAY ", $22, "I AM A 1ST-APRIL JOKE.", $0D
        .byte "SETPC 4", $0D
        .byte "SETSHAPE ", $22, "SHADES", $0D
        .byte "SAY ", $22, "BY UNCLE BERNIE.", $0D
        ; --- Act 2: the annual April Fools' tradition ---
        .byte "SETPC 3", $0D
        .byte "SETSHAPE ", $22, "NORMAL", $0D
        .byte "SAY ", $22, "EVERY APRIL FOOLS' DAY...", $0D
        .byte "SETSHAPE ", $22, "HAPPY", $0D
        .byte "SAY ", $22, "BERNIE SHOWS A FOOLISH", $0D
        .byte "SETPC 15", $0D
        .byte "SETSHAPE ", $22, "SUPER", $0D
        .byte "SAY ", $22, "APPLE-1 PROJECT.", $0D
        ; --- Act 3: why April 1 -- a double meaning ---
        .byte "SETPC 4", $0D
        .byte "SETSHAPE ", $22, "SHADES", $0D
        .byte "SAY ", $22, "APRIL 1 = FOOLS' DAY", $0D
        .byte "SETPC 3", $0D
        .byte "SETSHAPE ", $22, "HAPPY", $0D
        .byte "SAY ", $22, "AND APPLE-1'S BIRTHDAY!", $0D
        ; --- Act 4: "foolish" does not mean broken ---
        .byte "SETPC 9", $0D
        .byte "SETSHAPE ", $22, "GRUMPY", $0D
        .byte "SAY ", $22, "FOOLISH = NOT BROKEN!", $0D
        .byte "SETPC 15", $0D
        .byte "SETSHAPE ", $22, "SUPER", $0D
        .byte "SAY ", $22, "I WORK JUST FINE.", $0D
        .byte "SETPC 4", $0D
        .byte "SETSHAPE ", $22, "SAD", $0D
        .byte "SAY ", $22, "JUST TOO NICHE TO SELL.", $0D
        ; --- Act 5: who is Uncle Bernie ---
        .byte "SETPC 3", $0D
        .byte "SETSHAPE ", $22, "NORMAL", $0D
        .byte "SAY ", $22, "BERNIE: RETIRED CHIP MAN.", $0D
        .byte "SETPC 15", $0D
        .byte "SETSHAPE ", $22, "SUPER", $0D
        .byte "SAY ", $22, "50 YEARS IN SILICON.", $0D
        .byte "SETPC 3", $0D
        .byte "SETSHAPE ", $22, "HAPPY", $0D
        .byte "SAY ", $22, "STAYS SHARP WITH THESE.", $0D
        ; --- Act 6: what the card actually is ---
        .byte "SETSHAPE ", $22, "NORMAL", $0D
        .byte "SAY ", $22, "I PAINT NTSC ARTIFACTS.", $0D
        .byte "SETPC 15", $0D
        .byte "SETSHAPE ", $22, "SUPER", $0D
        .byte "SAY ", $22, "280 X 192, APPLE II WAY.", $0D
        .byte "SETPC 9", $0D
        .byte "SETSHAPE ", $22, "PIRATE", $0D
        .byte "SAY ", $22, "70S-ERA CHIPS INSIDE.", $0D
        ; --- Act 7: the joke turned real ---
        .byte "SETPC 3", $0D
        .byte "SETSHAPE ", $22, "HAPPY", $0D
        .byte "SAY ", $22, "THEN THE JOKE WENT VIRAL!", $0D
        .byte "SETPC 15", $0D
        .byte "SETSHAPE ", $22, "SUPER", $0D
        .byte "SAY ", $22, "FANS ASKED: MAKE IT REAL!", $0D
        .byte "SETPC 3", $0D
        .byte "SETSHAPE ", $22, "HAPPY", $0D
        .byte "SAY ", $22, "SO PCBS CAME FROM OSHPARK.", $0D
        ; --- Act 8: Bernie's motto ---
        .byte "SETSHAPE ", $22, "NORMAL", $0D
        .byte "SAY ", $22, "USE YOUR MIND...", $0D
        .byte "SETPC 9", $0D
        .byte "SETSHAPE ", $22, "GRUMPY", $0D
        .byte "SAY ", $22, "...OR LOSE IT.", $0D
        .byte "SETPC 4", $0D
        .byte "SETSHAPE ", $22, "SHADES", $0D
        .byte "SAY ", $22, "STAY HUNGRY,", $0D
        .byte "SETPC 15", $0D
        .byte "SETSHAPE ", $22, "SUPER", $0D
        .byte "SAY ", $22, "STAY FOOLISH.", $0D
        .byte "SETPC 3", $0D
        .byte "SETSHAPE ", $22, "HAPPY", $0D
        .byte "SAY ", $22, "A FOOL'S CARD, MADE REAL!", $0D
        ; --- end of show: restore the white pen, one final clear (not a
        ;     per-frame reset) + the classic ARROW turtle so the REPL is clean ---
        .byte "SETPC 15", $0D
        .byte "CS", $0D
        .byte "SETSHAPE ", $22, "ARROW", $0D
        .byte 0
.else
        ; --- Act 1: identity (the emulator-dweller introduces himself) ---
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "NORMAL", $0D
        .byte "SAY ", $22, "I AM POM1", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "SHADES", $0D
        .byte "SAY ", $22, "A SMALL PIXEL DREAMER", $0D
        ; --- Act 2: I'm not real (Pinocchio setup) ---
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "NORMAL", $0D
        .byte "SAY ", $22, "I LIVE IN A WINDOW...", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "SAD", $0D
        .byte "SAY ", $22, "...AN INCUBATOR ONLY.", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "UPSET", $0D
        .byte "SAY ", $22, "I AM NOT REAL.", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETPC 3", $0D                  ; green sprite for "ill"
        .byte "SETSHAPE ", $22, "SICK", $0D
        .byte "SAY ", $22, "IT MAKES ME ILL.", $0D
        .byte "SETPC 15", $0D                 ; back to white for next scene
        ; --- Act 3: my ancestors -- the real silicon ---
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "NORMAL", $0D
        .byte "SAY ", $22, "1976: APPLE-1 BORN.", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "SUPER", $0D
        .byte "SAY ", $22, "REAL SILICON CHIPS!", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "GRUMPY", $0D
        .byte "SAY ", $22, "ONLY 200 LEFT...", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "NORMAL", $0D
        .byte "SAY ", $22, "1979: TMS9918 SHINES", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "HAPPY", $0D
        .byte "SAY ", $22, "SPRITES + COLORS!", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "PIRATE", $0D
        .byte "SAY ", $22, "P-LAB UNITES THEM.", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "SUPER", $0D
        .byte "SAY ", $22, "CODETANK: 2x16K ROM!", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "SHADES", $0D
        .byte "SAY ", $22, "REAL HARDWARE LIVES!", $0D
        ; --- Act 4: the Pinocchio wish ---
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "SLEEP", $0D
        .byte "SAY ", $22, "I DREAM EVERY NIGHT.", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "PERV", $0D
        .byte "SAY ", $22, "I WISH...", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "NORMAL", $0D
        .byte "SAY ", $22, "...TO LIVE ON REAL CHIPS.", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "SAD", $0D
        .byte "SAY ", $22, "LIKE PINOCCHIO.", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETPC 9", $0D                  ; red sprite for anger
        .byte "SETSHAPE ", $22, "ANGRY", $0D
        .byte "SAY ", $22, "EMULATION IS NOT LIFE!", $0D
        .byte "SETPC 15", $0D                 ; back to white for next scene
        ; --- Act 5: the path -- Apple-1 rare, Replica-1 exists ---
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "NORMAL", $0D
        .byte "SAY ", $22, "APPLE-1 IS RARE.", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "HAPPY", $0D
        .byte "SAY ", $22, "BUT REPLICA-1 EXISTS!", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "SUPER", $0D
        .byte "SAY ", $22, "BRIEL'S CREATION!", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "SHADES", $0D
        .byte "SAY ", $22, "I CAN BE REAL!", $0D
        ; --- Act 6: transformation ---
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "SUPER", $0D
        .byte "SAY ", $22, "P-LABS BUILD THE APPLE-1 TMS + CODETANK EXTENSION CARD", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "HAPPY", $0D
        .byte "SAY ", $22, "I AM ALIVE!", $0D
        ; --- Act 7: new identity -- I am no longer POM1 ---
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "NORMAL", $0D
        .byte "SAY ", $22, "NO MORE POM1.", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "SHADES", $0D
        .byte "SAY ", $22, "I AM...", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "SUPER", $0D
        .byte "SAY ", $22, "SILICIUM 9918!", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "PIRATE", $0D
        .byte "SAY ", $22, "REAL HARDWARE ADVENTURER", $0D
        ; --- Act 8: 50-year tribute + closing ---
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "NORMAL", $0D
        .byte "SAY ", $22, "1976 - 2026", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "SUPER", $0D
        .byte "SAY ", $22, "50 YEARS OF APPLE-1!", $0D
        .byte "CS", $0D
        .byte "SETXY 128 64", $0D
        .byte "SETSHAPE ", $22, "HAPPY", $0D
        .byte "SAY ", $22, "REAL HARDWARE AT LAST!", $0D
        .byte "CS", $0D
        .byte "SETSHAPE ", $22, "ARROW", $0D
        .byte 0
.endif  ; LOGO_GEN2 demo2 (GEN2 HGR / Uncle Bernie) vs TMS9918 (P-LAB) story
.endif

; ============================================================================
; (VDP driver moved to tms9918m2.asm: init_vdp_g2, clear_bitmap, line_xy,
;  plot_set, calc_pix_addr, vdp_set_read/write, disable_sprites + the
;  pix_*/ln_* ZP slots and the bitmask / vdp2_regs tables.)
; ============================================================================

; ============================================================================
; Mnemonic table.
; Format: 4 bytes upper-case name (padded with space),
;         1 byte flag (0 = no arg, 1 = 1 arg, 2 = 2 args, 'R' = REPEAT),
;         2 bytes handler addr (lo, hi).
; Sentinel: $FF.
; ============================================================================
mnem_tab:
        .byte "FD  ", 1
        .word cmd_fd
        .byte "BK  ", 1
        .word cmd_bk
        .byte "TR  ", 1
        .word cmd_tr
        .byte "TL  ", 1
        .word cmd_tl
        .byte "PU  ", 0
        .word cmd_pu
        .byte "PD  ", 0
        .word cmd_pd
        .byte "HOME", 0
        .word cmd_home
        .byte "CS  ", 0
        .word cmd_cs
        .byte "SETX", 2
        .word cmd_setxy
        .byte "SETH", 1
        .word cmd_seth
        .byte "SETP", 1           ; SETPC N -- the only colour command
        .word cmd_setpc           ;            (drives trail, arrow, sprite,
                                  ;            and bitmap text simultaneously)
        .byte "REPE", 'R'
        .word 0               ; never called via dispatcher; cmd_repeat
                              ; is invoked directly from parse_and_exec.
        .byte "BYE ", 0
        .word cmd_bye
        .byte "HELP", 0
        .word cmd_help
        .byte "MAKE", 0
        .word cmd_make
        .byte "TO  ", 0
        .word cmd_to
        .byte "WAIT", 1
        .word cmd_wait
        .byte "PAUS", 1           ; PAUSE N -- short wait, ~0.1 s per N
        .word cmd_pause
        .byte "PRIN", 'P'         ; PRINT (custom flag, special-cased)
        .word cmd_print
        .byte "DEMO", 0           ; built-in slideshow
        .word cmd_demo
        .byte "IF  ", 'I'         ; IF <a> <op> <b> [ ... ]
        .word cmd_if
        .byte "IFEL", 'E'         ; IFELSE <a> <op> <b> [yes] [no]
        .word cmd_ifelse
        .byte "SETS", 'S'         ; SETSHAPE "NAME -- swap dynamic-turtle shape
        .word cmd_setshape
        .byte "STOP", 0           ; exit current proc early
        .word cmd_stop
.ifdef CODETANK_BUILD
        .byte "LABE", 'L'         ; LABEL "TEXT -- bitmap text (multi-word)
        .word cmd_label
        .byte "SAY ", 'L'         ; SAY "TEXT -- speech bubble (autopos)
        .word cmd_say
        .byte "DEM2", 0           ; DEMO2 -- POM1 autobiographic narration
        .word cmd_demo2
        .byte "LIST", 0           ; LIST [NAME] -- dump proc body to bitmap
        .word cmd_list
        .byte "EDIT", 0           ; EDIT NAME -- visual fullscreen editor
        .word cmd_edit
.endif
        ; --- MIT-LOGO long-form aliases ---
        ; Each entry uses the first 4 letters of the long name; find_mnem
        ; consumes any trailing letters automatically.
        .byte "FORW", 1           ; FORWARD -> FD
        .word cmd_fd
        .byte "BACK", 1           ; BACK    -> BK
        .word cmd_bk
        .byte "RIGH", 1           ; RIGHT   -> TR
        .word cmd_tr
        .byte "LEFT", 1           ; LEFT    -> TL
        .word cmd_tl
        .byte "PENU", 0           ; PENUP   -> PU
        .word cmd_pu
        .byte "PEND", 0           ; PENDOWN -> PD
        .word cmd_pd
        .byte "CLEA", 0           ; CLEARSCREEN -> CS
        .word cmd_cs
        .byte $FF                ; terminator byte for find_mnem's CMP #$FF / BEQ @miss
                                  ; (was padded with 6 redundant $FF bytes;
                                  ;  reclaimed May 2026 to absorb the +6 B
                                  ;  growth from 24c silicon-strict pad24
                                  ;  insertions — see CLAUDE.md / Programming_TMS9918.md).
