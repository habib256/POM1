; ============================================================================
; TMS_Logo_16k.asm  --  APPLE-1 LOGO V2.0 for TMS9918 (16 KB variant)
; ============================================================================
; *** V2.0 -- 16 KB BUILD (active development) ***
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
;   ca65 -I software/tms9918 -o build/TMS_Logo.o software/tms9918/TMS_Logo.asm
;   ld65 -C software/tms9918/apple1_logo.cfg -o build/TMS_Logo.bin build/TMS_Logo.o
;   python3 software/tms9918/emit_TMS_Logo_txt.py
;
; Run on POM1:  ./POM1 --preset 8       (P-LAB Apple-1 with TMS9918 + CodeTank)
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
.include "apple1.inc"
.include "tms9918.inc"          ; VDP_CTRL, VDP_DATA equates for SETSHAPE

; --- Imports from sibling modules -----------------------------------------
;
; tms9918m2.asm  -- Mode-2 bitmap driver (init + plot + Bresenham line).
.import   init_vdp_g2, clear_bitmap, disable_sprites, line_xy
.importzp ln_x0, ln_y0, ln_x1, ln_y1
;
; math.asm        -- fixed-point trig + LFSR + decimal output + mod360.
.import   roll_lfsr, print_decimal, div_arg_by_10
.import   mod360_arg, mod360_tmp, norm360
.import   signed_sin, mul_dist_by_signed, negate_prod

; --- Exports consumed by sibling modules ----------------------------------
.exportzp tmp, tmp2, arg_lo, arg_hi, arg2_lo, arg2_hi, th_lo, th_hi
.export   prod_lo, prod_hi, sign_flag, lfsr_lo, lfsr_hi, plot_mode

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
tx_lo:        .res 1     ; turtle x  (0..255, integer pixels)
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
        JSR disable_sprites
        LDA #0
        STA plot_mode
        STA turtle_visible
        STA sprite_mode
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
        .byte $0D, "APPLE-1 LOGO FOR TMS9918 - TYPE HELP", $0D
        .byte 0

; -------------------------------------------------------------------------
; Paginated HELP. cmd_help (above) does the dispatch; here we just lay out
; the table of contents and the 8 detail pages. Each page is null-
; terminated. The TOC fits comfortably in a 24-line window so the user
; can read it in one go before drilling into a topic.
; -------------------------------------------------------------------------
help_toc:
        .byte $0D
        .byte "APPLE-1 LOGO V2.0 -- HELP", $0D
        .byte "TMS9918 graphics, 16K Apple-1", $0D
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
        .byte $0D
        .byte "ESC or Ctrl-G aborts a loop.", $0D
        .byte "BYE returns to Woz Monitor.", $0D
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
        .byte "    erase the bitmap + HOME", $0D
        .byte $0D
        .byte "Pen state persists across", $0D
        .byte "procs and REPEAT loops.", $0D
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
        .byte "  TURTL   chunky turtle", $0D
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
        .byte "APPLE-1 LOGO V2.0 (16K)", $0D
        .byte "TMS9918 GRAPHICS, BY ARNAUD", $0D
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
        .byte "*** DYNAMIC TURTLE (V2.0) ***", $0D
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
        .byte "  identifiers 6 chars A-Z 0-9", $0D
        .byte "  (THING vs THING1 distinct).", $0D
        .byte "  Tail recursion is FREE: a proc", $0D
        .byte "  whose final statement calls", $0D
        .byte "  itself reuses the frame and", $0D
        .byte "  costs zero stack growth.", $0D
        .byte "  Other nested calls go up to", $0D
        .byte "  16 levels via the control", $0D
        .byte "  stack in PROCBSS.", $0D
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
        .byte "  CTRL-G   same (telnet-friendly)", $0D
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
@wait:  LDA KBDCR
        BPL @wait
        LDA KBD
        STA tmp               ; raw (bit 7 set) for echo
        AND #$7F
        CMP #$0D
        BEQ @cr
        CMP #'_'              ; Wozmon-style destructive backspace
        BEQ @bs
        CPX #LINE_MAX-1
        BCS @no_store
        STA line_buf,X
        INX
@no_store:
        LDA tmp
        JSR ECHO
        JMP @wait
@bs:    CPX #0
        BEQ @bs_skip          ; nothing to erase
        DEX
@bs_skip:
        LDA tmp               ; echo "_" so display shows the backspace
        JSR ECHO
        JMP @wait
@cr:    LDA #$0D
        STA line_buf,X
        LDA tmp
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
        LDA KBDCR
        BPL @done
        LDA KBD                 ; read clears the strobe
        AND #$7F
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
        JMP @loop
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
        JMP print_help_str
@n2:    CMP #2
        BNE @n3
        LDA #<help_p2
        LDX #>help_p2
        JMP print_help_str
@n3:    CMP #3
        BNE @n4
        LDA #<help_p3
        LDX #>help_p3
        JMP print_help_str
@n4:    CMP #4
        BNE @n5
        LDA #<help_p4
        LDX #>help_p4
        JMP print_help_str
@n5:    CMP #5
        BNE @n6
        LDA #<help_p5
        LDX #>help_p5
        JMP print_help_str
@n6:    CMP #6
        BNE @n7
        LDA #<help_p6
        LDX #>help_p6
        JMP print_help_str
@n7:    CMP #7
        BNE @n8
        LDA #<help_p7
        LDX #>help_p7
        JMP print_help_str
@n8:    CMP #8
        BNE @toc
        LDA #<help_p8
        LDX #>help_p8
        JMP print_help_str
@toc:   LDA #<help_toc
        LDX #>help_toc
        ; fall through

print_help_str:
        STA mptr_lo
        STX mptr_hi
@l:     LDY #0
        LDA (mptr_lo),Y
        BEQ @done
        ORA #$80
        JSR ECHO
        INC mptr_lo
        BNE @l
        INC mptr_hi
        JMP @l
@done:  RTS

; cmd_demo: built-in slideshow that runs each demo line through
;   parse_and_exec. Each line lives in CODE (read-only) and is copied
;   into line_buf to be tokenised. Lines are CR-terminated; $00 marks
;   the end of the script. After the script ends, line_buf is zeroed
;   and line_idx set to 0 so the outer parse_and_exec sees CR and stops.
cmd_demo:
        LDA #<demo_script
        STA mptr_lo
        LDA #>demo_script
        STA mptr_hi
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
        LDA #$0D
        STA line_buf
        LDA #0
        STA line_idx
        RTS

; demo_script: 8-program slideshow. Single-word PRINT labels (no spaces)
;   because PRINT stops at first space.
demo_script:
        .byte "PRINT ", $22, "DEMO", $0D
        .byte "WAIT 1", $0D
        .byte "CS", $0D
        ; SQUARE scene replaced by the SQUARE proc below (used by FLOWER).
        .byte "PRINT ", $22, "STAR", $0D
        .byte "REPEAT 5 [FD 80 TR 144]", $0D
        .byte "WAIT 2", $0D
        .byte "CS", $0D
        .byte "PRINT ", $22, "CIRCLE", $0D
        .byte "REPEAT 36 [FD 5 TR 10]", $0D
        .byte "WAIT 2", $0D
        .byte "CS", $0D
        .byte "PRINT ", $22, "SUN", $0D
        .byte "REPEAT 18 [FD 60 BK 60 TR 20]", $0D
        .byte "WAIT 2", $0D
        .byte "CS", $0D
        .byte "PRINT ", $22, "ROSETTE", $0D
        .byte "REPEAT 12 [REPEAT 6 [FD 25 TR 60] TR 30]", $0D
        .byte "WAIT 3", $0D
        .byte "CS", $0D
        .byte "PRINT ", $22, "RANDOM", $0D
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
        .byte "FLOWER", $0D
        .byte "WAIT 3", $0D
        .byte "CS", $0D
        .byte "TO SPIRAL :SIZE :ANGLE", $0D
        .byte "IF :SIZE > 100 [STOP]", $0D
        .byte "FORWARD :SIZE", $0D
        .byte "RIGHT :ANGLE", $0D
        .byte "SPIRAL :SIZE + 2 :ANGLE", $0D
        .byte "END", $0D
        .byte "PRINT ", $22, "SPIRAL90", $0D
        .byte "SPIRAL 0 90", $0D
        .byte "WAIT 3", $0D
        .byte "CS", $0D
        .byte "PRINT ", $22, "SPIRAL91", $0D
        .byte "SPIRAL 0 91", $0D
        .byte "WAIT 3", $0D
        .byte "CS", $0D
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
        .byte "CS", $0D
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
        JSR erase_turtle
        JSR mod360_arg
        LDA arg_lo
        STA th_lo
        LDA arg_hi
        STA th_hi
        JSR draw_turtle
        RTS

cmd_setxy:
        JSR erase_turtle
        LDA arg_lo
        STA tx_lo
        LDA arg2_lo
        CMP #192
        BCC @yok
        LDA #191
@yok:   STA ty_lo
        JSR draw_turtle
        RTS

cmd_tr:
        JSR erase_turtle
        JSR mod360_arg
        CLC
        LDA th_lo
        ADC arg_lo
        STA th_lo
        LDA th_hi
        ADC arg_hi
        STA th_hi
        JSR norm360
        JSR draw_turtle
        RTS

cmd_tl:
        JSR erase_turtle
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
        JSR draw_turtle
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
        ; clamp distance to 0..255
        LDA arg_hi
        BEQ @ok_d
        LDA #255
        STA arg_lo
        LDA #0
        STA arg_hi
@ok_d:  ; --- compute dx = (distance * sin(heading)) / 64, signed ---
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
        ; nx16 = tx + dx (signed 16-bit add: tx is 0..255 unsigned)
        CLC
        LDA tx_lo
        ADC prod_lo
        STA nx_save_lo
        LDA #0
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
        JSR line_xy
@no_draw:
        ; commit new turtle position
        LDA nx_save_lo
        STA tx_lo
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

; xor_turtle: set XOR plot mode, draw 3 lines tip<->BL<->BR<->tip,
;             restore plot mode. Caller must have called compute_turtle_verts.
xor_turtle:
        LDA #1
        STA plot_mode
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
        LDA #0
        STA plot_mode
        RTS

; draw_turtle: in bitmap mode, XOR-draw the triangle and flag visible.
;   In sprite mode, write 4 bytes (Y, X, name, color) to the sprite #0
;   attribute slot at VRAM $3B00. The TMS9918 hardware-blits the sprite
;   over the bitmap; turtle_visible stays untouched (no XOR bookkeeping).
draw_turtle:
        LDA sprite_mode
        BEQ @bitmap
        ; --- sprite path: write sprite-0 attribute (Y, X, name=0, color=$0F)
        LDA #$00
        STA VDP_CTRL
        LDA #$3B | $40            ; $3B00 + write enable
        STA VDP_CTRL
        ; Y = ty - 9: TMS9918 displays sprite at scanline (Y+1), and the
        ; 16x16 pattern's top-left is the sprite origin -- shift up by 8
        ; to centre on (tx,ty). With Y=$D0 special, we never reach it for
        ; ty in 0..192 -- ty-9 lands in 0xF7..0xB7, all valid.
        LDA ty_lo
        SEC
        SBC #9
        STA VDP_DATA
        LDA tx_lo
        SEC
        SBC #8
        STA VDP_DATA
        LDA #0                    ; pattern name = 0 (sprite_pattern_table[0])
        STA VDP_DATA
        LDA #$0F                  ; foreground colour = white (15)
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
        JSR xor_turtle
        LDA #1
        STA turtle_visible
@done:  RTS

; erase_turtle: bitmap mode -- XOR the triangle out at its current pose.
;   Sprite mode -- nothing to do (TMS9918 sprites don't bleed into the
;   pattern table, so moving the sprite is enough; old position vanishes
;   automatically on the next attribute write).
erase_turtle:
        LDA sprite_mode
        BNE @done
        LDA turtle_visible
        BEQ @done
        JSR compute_turtle_verts
        JSR xor_turtle
        LDA #0
        STA turtle_visible
@done:  RTS

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
        LDA sprite_mode
        BEQ @arrow_done           ; already in bitmap mode -- no-op
        ; Y=$D0 to sprite #0 attribute = TMS9918 sprite-list terminator.
        LDA #$00
        STA VDP_CTRL
        LDA #$3B | $40
        STA VDP_CTRL
        LDA #$D0
        STA VDP_DATA
        ; Flip back to bitmap mode and redraw the triangle.
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
        BEQ @bad_name
@scmp:  LDA (mptr_lo),Y
        CMP mnem_buf,Y
        BNE @snext
        INY
        CPY #NAME_LEN
        BNE @scmp
        ; match -- pattern pointer at offset NAME_LEN (lo) / NAME_LEN+1 (hi)
        LDY #NAME_LEN
        LDA (mptr_lo),Y
        STA shape_pat_lo
        INY
        LDA (mptr_lo),Y
        STA shape_pat_hi
        JMP @found
@snext: CLC
        LDA mptr_lo
        ADC #(NAME_LEN+2)
        STA mptr_lo
        BCC @search
        INC mptr_hi
        JMP @search

@found:
        ; First-time entry: erase any bitmap turtle, enable 16x16 sprites.
        LDA sprite_mode
        BNE @load_pat
        JSR erase_turtle          ; bitmap path while sprite_mode is still 0
        ; R1 = $C2 = 16K + DISP + IE off + sprite-16 (M1=0 already in R0)
        LDA #$C2
        STA VDP_CTRL
        LDA #$81                  ; write to register 1
        STA VDP_CTRL
        LDA #1
        STA sprite_mode
@load_pat:
        ; Copy 32 bytes from (shape_pat),Y to VRAM $1800 (sprite pattern 0).
        LDA #$00
        STA VDP_CTRL
        LDA #$18 | $40
        STA VDP_CTRL
        LDY #0
@cppat: LDA (shape_pat_lo),Y
        STA VDP_DATA
        INY
        CPY #32
        BNE @cppat
        ; Reposition sprite at the current turtle (tx, ty).
        JSR draw_turtle
        RTS
@bad_name:
        LDA #ERR_BAD_NAME
        JSR print_err
        RTS

; shape_table: name (NAME_LEN = 6 bytes) + pointer to a 32-byte 16x16
;   pattern. Names are padded with spaces so a 5-letter "BIRD1" matches
;   what read_var_name puts in mnem_buf. Terminator: $FF.
shape_table:
        .byte "BIRD1 "
        .word bird1_pat
        .byte "BIRD2 "
        .word bird2_pat
        .byte "TURTL "
        .word turtle_pat
        .byte $FF

; ----- 16x16 sprite patterns (TL 0..7, BL 8..15, TR 0..7, BR 8..15) -----
; BIRD1 -- bird with wings up (V silhouette).
bird1_pat:
        ; TL (rows 0-7, cols 0-7)
        .byte $00, $80, $C0, $60, $38, $0F, $02, $03
        ; BL (rows 8-15, cols 0-7)
        .byte $03, $02, $00, $00, $00, $00, $00, $00
        ; TR (rows 0-7, cols 8-15)
        .byte $00, $01, $03, $06, $1C, $F0, $40, $C0
        ; BR (rows 8-15, cols 8-15)
        .byte $C0, $40, $00, $00, $00, $00, $00, $00

; BIRD2 -- bird with wings down (^ silhouette). BIRD1 mirrored vertically.
bird2_pat:
        ; TL
        .byte $00, $00, $00, $00, $00, $02, $03, $03
        ; BL
        .byte $02, $0F, $38, $60, $C0, $80, $00, $00
        ; TR
        .byte $00, $00, $00, $00, $00, $40, $C0, $C0
        ; BR
        .byte $40, $F0, $1C, $06, $03, $01, $00, $00

; TURTL -- chunky 16x16 turtle facing right (head pokes east, four legs,
;   diamond shell). Drop-in default for users who want the sprite shape
;   without a bird animation.
turtle_pat:
        ; TL
        .byte $00, $00, $03, $0F, $1F, $3F, $7E, $FC
        ; BL
        .byte $7E, $3F, $1F, $0F, $03, $00, $00, $00
        ; TR
        .byte $00, $30, $E0, $F0, $F8, $FC, $7E, $7F
        ; BR
        .byte $7E, $FC, $F8, $F0, $E0, $30, $00, $00

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
        .byte $FF, $FF, $FF, $FF, $FF, $FF, $FF
