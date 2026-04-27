; ============================================================================
; TMS_Logo.asm  --  APPLE-1 LOGO for TMS9918 (turtle interpreter)
; ============================================================================
; Build:
;   ca65 -I software/tms9918 -o build/TMS_Logo.o software/tms9918/TMS_Logo.asm
;   ld65 -C software/tms9918/apple1_logo.cfg -o build/TMS_Logo.bin build/TMS_Logo.o
;   python3 software/tms9918/emit_TMS_Logo_txt.py
;
; Run on POM1:  ./POM1 --preset 8       (P-LAB Apple-1 with TMS9918 + CodeTank)
;   then in Woz Monitor paste TMS_Logo.txt and type 280R.
;
; Commands (V1):
;   TR n            turn right  by n degrees   (0..359)
;   TL n            turn left   by n degrees
;   FD n            forward n pixels   (draws if pen down)
;   BK n            back    n pixels
;   PU              pen up
;   PD              pen down
;   HOME            x=128, y=96, heading=0   (no clear)
;   CS              clear screen + HOME
;   SETXY x y       set position (no draw)
;   SETH n          set heading
;   REPEAT n [...]  repeat block n times      (1 nesting level)
;   BYE             return to Woz Monitor
;
; Out of scope V1:
;   long aliases (FORWARD/RIGHT/...), MAKE/TO/END, multi-command lines
;   outside REPEAT, line editing, colour, magnify sprites.
;
; VDP helpers (init_vdp_g2, clear_bitmap, plot_set) are copied verbatim from
; software/tms9918/TMS_Maze3D.asm with the same ZP conventions.
; line_xy here uses a 16-bit signed err so FD up to 255 doesn't glitch on
; near-diagonal angles (Maze3D's 8-bit-err variant overflowed for |2*err|>127).
; ============================================================================

; --- I/O equates (Apple-1 + TMS9918 hardware) ------------------------------
.include "apple1.inc"

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
MAX_PROCS         = 5
PROC_SLOT         = 174   ; V1.8 slot: 6 name + 1 nparams + 12 (2x6) param names + 1 body_len + 154 body
PROC_BODY_MAX     = 154   ; just enough for verbose THING (154 bytes incl. CRs)
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
; variable table -- MAX_VARS entries x (NAME_LEN name + 2 byte value).
; Lives here (rather than in LINEBUF) because the 6-byte names make it too
; large to fit alongside the parser scratch in the 128-byte LBUF window.
var_table: .res 48
proc_table:    .res 870      ; MAX_PROCS (5) x PROC_SLOT (174) = 870
proc_save_buf: .res 60       ; saved line_buf, depth-0 frame
proc_save_idx: .res 1        ; saved line_idx, depth-0 frame
proc_save_buf2: .res 60      ; saved line_buf, depth-1 frame (nested call)
proc_save_idx2: .res 1       ; saved line_idx, depth-1 frame
; V1.5 procedure parameters -- 2 named slots layered on top of var_table.
; var_lookup checks these first, so :SIZE in a proc body resolves to its
; param even if a global named SIZE also exists. After RTS we restore
; values via PHA/PLA on the 6502 stack (caller's stack frame).
param_slot0_name:  .res 6
param_slot0_value: .res 2
param_slot1_name:  .res 6
param_slot1_value: .res 2
n_params_active:   .res 1    ; 0..MAX_PARAMS, valid params currently live
in_proc_save:      .res 1    ; 1 if proc_save_buf currently holds an outer line_buf
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
        STA n_vars
        STA n_procs
        STA n_params_active
        STA in_proc_save
        STA def_mode
        STA break_flag
        STA stop_flag
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

help_msg:
        .byte $0D, "APPLE-1 LOGO V1.7 (TMS9918)", $0D
        .byte "FD/BK N  : FORWARD/BACK N PIX", $0D
        .byte "TR/TL N  : TURN RIGHT/LEFT", $0D
        .byte "PU PD    : PEN UP / DOWN", $0D
        .byte "HOME     : CENTRE 128,96 H=0", $0D
        .byte "CS       : CLEAR + HOME", $0D
        .byte "SETXY X Y / SETH N", $0D
        .byte "REPEAT N [ ... ]   : LOOP N TIMES", $0D
        .byte "REPEAT FOREVER [ ] : ENDLESS", $0D
        .byte "  ESC OR CTRL-G ABORT LOOP", $0D
        .byte "  PROCS AND VARS SURVIVE", $0D
        .byte "IF A > B [ ... ]   : ALSO < =", $0D
        .byte "STOP : EXIT CURRENT PROC", $0D
        .byte "ARG MAY BE :V + N OR :V - N", $0D
        .byte "TAIL RECURSION OK (E.G. SPIRAL)", $0D
        .byte "MAKE NAME N : SET VAR=N", $0D
        .byte ":NAME    : USE VAR IN ARGS", $0D
        .byte "TO N :A :B ... END : DEF PROC", $0D
        .byte "  NAMES 6 CHARS A-Z 0-9", $0D
        .byte "  6 PROCS, BODY 160 B EACH", $0D
        .byte "  PROCS CAN CALL 1 OTHER PROC", $0D
        .byte "NAME [A B]  : INVOKE PROC", $0D
        .byte "RANDOM N : RANDOM 0..N-1 IN ARG", $0D
        .byte "WAIT N   : PAUSE ~N SECONDS", $0D
        .byte "PRINT ", $22, "WORD / PRINT N", $0D
        .byte "_        : BACKSPACE INPUT", $0D
        .byte "ALIASES  : FORWARD BACK RIGHT LEFT", $0D
        .byte "  PENUP PENDOWN CLEARSCREEN", $0D
        .byte "DEMO     : RUN BUILT-IN SLIDESHOW", $0D
        .byte "HELP     : THIS LIST", $0D
        .byte "BYE      : EXIT TO MONITOR", $0D
        .byte 0

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
        BCS @try_proc
        STA tmp               ; flag byte
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

cmd_help:
        LDA #<help_msg
        STA mptr_lo
        LDA #>help_msg
        STA mptr_hi
        LDY #0
@l:     LDA (mptr_lo),Y
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

; proc_invoke (V1.6): bind params, then run a (possibly multi-line) body.
;   Supports 1 level of nested invocation (caller -> proc1 -> proc2). Tracks
;   depth in `in_proc_save` (0/1/2). Each depth has its own line_buf save
;   slot (proc_save_buf / proc_save_buf2); proc_body_cursor/total are pushed
;   on the 6502 stack so the outer body walker can resume.
;
;   Steps:
;     1. Refuse if depth already 2 -> ERR_FULL.
;     2. Push outer body cursor/total so we can scratch them during arg
;        parsing without losing the outer walker's state.
;     3. Push outer param state (n_params_active + 2 x (name+value)).
;     4. Read nparams from slot. Parse N decimal args from the OUTER line
;        (so :OUTER_VAR still resolves under the OUTER param state).
;     5. Copy param names from the slot, values from the parsed args, into
;        param_slotN. Activate them by setting n_params_active = nparams.
;     6. Save the outer line_buf to proc_save_buf (depth 0) or
;        proc_save_buf2 (depth 1), then INC in_proc_save.
;     7. Walk the body one CR-terminated line at a time through
;        parse_and_exec.
;     8. DEC in_proc_save, restore line_buf+line_idx from the matching
;        depth frame, pop the outer param state, pop outer cursor/total.
proc_invoke:
        LDA in_proc_save
        CMP #2
        BCC @ok_to_call
        ; nested too deep (only 1 level supported)
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
        ; --- 6. save outer line_buf into the depth-N frame ---
        LDA in_proc_save
        BNE @sv_buf2
        LDX #0
@sv_buf1:
        LDA line_buf,X
        STA proc_save_buf,X
        INX
        CPX #LINE_MAX
        BNE @sv_buf1
        LDA line_idx
        STA proc_save_idx
        JMP @after_sv
@sv_buf2:
        LDX #0
@sv_buf2_l:
        LDA line_buf,X
        STA proc_save_buf2,X
        INX
        CPX #LINE_MAX
        BNE @sv_buf2_l
        LDA line_idx
        STA proc_save_idx2
@after_sv:
        INC in_proc_save
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
        ; --- 8. restore outer line_buf + line_idx (depth-N frame) ---
        DEC in_proc_save
        LDA in_proc_save
        BNE @rs_buf2
        LDX #0
@rs_buf1:
        LDA proc_save_buf,X
        STA line_buf,X
        INX
        CPX #LINE_MAX
        BNE @rs_buf1
        LDA proc_save_idx
        STA line_idx
        JMP @after_rs
@rs_buf2:
        LDX #0
@rs_buf2_l:
        LDA proc_save_buf2,X
        STA line_buf,X
        INX
        CPX #LINE_MAX
        BNE @rs_buf2_l
        LDA proc_save_idx2
        STA line_idx
@after_rs:
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
        LDA #1
        STA pen
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
cmd_if:
        ; The 'I' dispatch flag in parse_and_exec called us before any args
        ; were parsed (unlike numeric flags). Parse the LHS, then op, then RHS.
        JSR parse_dec_arg
        LDA arg_lo
        STA if_a_lo
        LDA arg_hi
        STA if_a_hi
        JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        STA if_op_save
        ; valid ops: > < =
        CMP #'>'
        BEQ @op_ok
        CMP #'<'
        BEQ @op_ok
        CMP #'='
        BEQ @op_ok
        JMP @bad_arg
@op_ok: INC line_idx             ; consume op
        JSR parse_dec_arg
        ; --- compare: signed 16-bit subtraction (a - b) -> tmp:tmp2 ---
        SEC
        LDA if_a_lo
        SBC arg_lo
        STA tmp
        LDA if_a_hi
        SBC arg_hi
        STA tmp2
        ; condition flag in A (1 = true, 0 = false)
        LDA if_op_save
        CMP #'>'
        BEQ @cmp_gt
        CMP #'<'
        BEQ @cmp_lt
        ; '=': both bytes zero?
        LDA tmp
        ORA tmp2
        BEQ @set_true
        BNE @set_false
@cmp_gt:; a > b iff (a-b) > 0  → not negative AND not zero
        LDA tmp2
        BMI @set_false
        ORA tmp
        BEQ @set_false
        BNE @set_true
@cmp_lt:; a < b iff (a-b) < 0  → high byte negative
        LDA tmp2
        BMI @set_true
        BPL @set_false
@set_true:
        LDA #1
        STA tmp
        JMP @scan_block
@set_false:
        LDA #0
        STA tmp
@scan_block:
        ; expect '['
        JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #'['
        BNE @bad
        INX
        STX rep_start
        LDY #1
@s_scan:LDA line_buf,X
        AND #$7F
        CMP #$0D
        BEQ @bad
        CMP #'['
        BNE @s_no
        INY
        JMP @s_next
@s_no:  CMP #']'
        BNE @s_next
        DEY
        BEQ @s_found
@s_next:INX
        JMP @s_scan
@s_found:
        STX rep_end
        ; if false, skip past ']' and return
        LDA tmp
        BNE @run_body
        LDA rep_end
        CLC
        ADC #1
        STA line_idx
        RTS
@run_body:
        LDA rep_start
        STA line_idx
        ; save bracket bounds so a nested IF/REPEAT inside the body can
        ; reuse rep_start/rep_end without losing ours
        LDA rep_start
        PHA
        LDA rep_end
        PHA
        JSR parse_and_exec
        PLA
        STA rep_end
        PLA
        STA rep_start
        ; advance past ']'
        LDA rep_end
        CLC
        ADC #1
        STA line_idx
        RTS
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

; draw_turtle: if turtle_visible == 0 AND turtle fits on screen, draw + flag.
;   Skips draw if tx/ty too close to an edge (vertices would wrap and produce
;   garbage Bresenham segments). Margin = 9 (max tip extension).
draw_turtle:
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

; erase_turtle: if turtle_visible == 1, compute verts at CURRENT pose
;   (must match the verts used at last draw) + XOR-draw to remove, clear flag.
erase_turtle:
        LDA turtle_visible
        BEQ @done
        JSR compute_turtle_verts
        JSR xor_turtle
        LDA #0
        STA turtle_visible
@done:  RTS

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
        .byte "PRIN", 'P'         ; PRINT (custom flag, special-cased)
        .word cmd_print
        .byte "DEMO", 0           ; built-in slideshow
        .word cmd_demo
        .byte "IF  ", 'I'         ; IF <a> <op> <b> [ ... ]
        .word cmd_if
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
