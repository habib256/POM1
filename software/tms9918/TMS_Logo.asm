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

; --- I/O equates -----------------------------------------------------------
ECHO     = $FFEF
KBD      = $D010
KBDCR    = $D011
VDP_DATA = $CC00
VDP_CTRL = $CC01
WOZ_RST  = $FF1F

LINE_MAX = 60
MAX_VARS = 6
MAX_PROCS     = 8
PROC_SLOT     = 64        ; bytes per procedure slot (4 name + 1 len + 59 body)
PROC_BODY_MAX = 59

; --- Zero page (matches TMS_Maze3D conventions for the line/plot block) ---
.segment "ZEROPAGE"
pix_x:        .res 1     ; $00
pix_y:        .res 1
pix_addr_lo:  .res 1
pix_addr_hi:  .res 1
pix_mask:     .res 1
pix_byte:     .res 1
ln_x0:        .res 1
ln_y0:        .res 1
ln_x1:        .res 1
ln_y1:        .res 1
ln_dx:        .res 1
ln_dy:        .res 1
ln_sx:        .res 1
ln_sy:        .res 1
ln_err:       .res 1     ; signed 16-bit Bresenham error -- low byte
ln_err_hi:    .res 1     ; signed 16-bit Bresenham error -- high byte
tmp:          .res 1
tmp2:         .res 1     ; $10
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
mnem_buf:  .res 4         ; 4 uppercase letters / spaces
n_letters: .res 1
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
; variable table -- MAX_VARS entries x (4 byte name + 2 byte value)
var_table: .res 36
n_vars:    .res 1         ; current number of vars in use (0..MAX_VARS)
n_procs:   .res 1         ; current number of user procedures (0..MAX_PROCS)
cmd_status: .res 1        ; 0 = nothing, 1 = OK, 2 = ERR (per parse_and_exec call)
lfsr_lo:    .res 1        ; 16-bit LFSR seed for RANDOM
lfsr_hi:    .res 1

; --- User-procedure storage ($0F80-$0FFF) -----------------------------------
; MAX_PROCS slots, each 32 bytes:
;   bytes 0-3  : 4-char name (uppercase, padded with space)
;   byte  4    : body length (0..PROC_BODY_MAX)
;   bytes 5-31 : body characters (raw line_buf bytes between TO NAME and END)
.segment "PROCBSS"
proc_table:    .res 512      ; MAX_PROCS (8) x PROC_SLOT (64) = 512
proc_save_buf: .res 60       ; saved line_buf during proc invocation
proc_save_idx: .res 1        ; saved line_idx

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
        ; LFSR seed -- non-zero
        LDA #$AC
        STA lfsr_lo
        LDA #$E1
        STA lfsr_hi
        JSR cmd_home          ; centre + heading 0 + draw turtle
repl:
        JSR new_prompt
        JSR read_line
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
        LDA #'?'
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
        .byte $0D, "APPLE-1 LOGO V1.2 (TMS9918)", $0D
        .byte "FD/BK N  : FORWARD/BACK N PIX", $0D
        .byte "TR/TL N  : TURN RIGHT/LEFT", $0D
        .byte "PU PD    : PEN UP / DOWN", $0D
        .byte "HOME     : CENTRE 128,96 H=0", $0D
        .byte "CS       : CLEAR + HOME", $0D
        .byte "SETXY X Y / SETH N", $0D
        .byte "REPEAT N [ ... ] : LOOP", $0D
        .byte "MAKE NAME N : SET VAR=N", $0D
        .byte ":NAME    : USE VAR IN ARGS", $0D
        .byte "TO NAME ... END : DEF PROC", $0D
        .byte "NAME     : INVOKE PROC", $0D
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
; parse_and_exec: tokenize from line_idx, dispatch each command, advance.
;                 Stops on CR or ']' (REPEAT block end).
; ============================================================================
parse_and_exec:
@loop:  JSR skip_spaces
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
@dispatch:
        JSR mark_ok
        JSR call_handler
        JMP @loop
@try_proc:
        ; mnem_buf still holds the consumed name. Try the user-proc table.
        JSR find_proc
        BCS @err
        JSR mark_ok
        JSR proc_invoke
        JMP @loop
@err:   LDA #2
        STA cmd_status
        LDA #'?'
        ORA #$80
        JSR ECHO
        LDA #$0D
        ORA #$80
        JSR ECHO
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

; print_status: outermost-level OK / silent-on-err helper.
;   Prints "OK<CR>" if cmd_status==1, otherwise silent.
print_status:
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
; find_mnem: read up to 4 letters at line_buf,line_idx (uppercase) into
;            mnem_buf, pad with space, look up in mnem_tab.
;   Match  -> A = flag byte, mptr_lo/hi = handler addr, line_idx += letters,
;             C = 0.
;   Miss   -> C = 1, line_idx unchanged.
; mnem_tab entry: 4 bytes name (UPPER, padded space), 1 flag, 2 handler.
;                 terminator $FF.
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
        AND #$DF              ; uppercase
@upcheck:
        CMP #'A'
        BCC @done_rd
        CMP #'Z'+1
        BCS @done_rd
        ; letter: store first 4 only, but keep consuming additional
        CPY #4
        BCS @past4
        STA mnem_buf,Y
        INY
@past4: INX
        JMP @rd
@done_rd:
        STY n_letters
        STX tmp               ; save consume-end X for later advance
        ; pad rest with space (only matters if n_letters < 4)
        LDA #' '
@padl:  CPY #4
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
parse_dec_arg:
        JSR skip_spaces
        LDX line_idx
        LDA line_buf,X
        AND #$7F
        CMP #':'
        BEQ @var_ref
        CMP #'R'
        BNE @dec_far
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
        LDY #4
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
        LDA #2
        STA cmd_status
        LDA #0
        STA arg_lo
        STA arg_hi
        LDA #'?'
        ORA #$80
        JSR ECHO
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
@nc:    ; run the line. parse_and_exec clobbers mptr_lo:hi via find_mnem,
        ; so save/restore the demo script pointer around the call.
        LDA mptr_lo
        PHA
        LDA mptr_hi
        PHA
        LDA #0
        STA line_idx
        STA cmd_status
        JSR parse_and_exec
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
        .byte "PRINT ", $22, "SQUARE", $0D
        .byte "REPEAT 4 [FD 60 TR 90]", $0D
        .byte "WAIT 2", $0D
        .byte "CS", $0D
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
        .byte "PRINT ", $22, "ROSACE", $0D
        .byte "REPEAT 10 [REPEAT 5 [FD 30 TR 144] TR 36]", $0D
        .byte "WAIT 3", $0D
        .byte "CS", $0D
        .byte "PRINT ", $22, "RAYS", $0D
        .byte "REPEAT 24 [FD 70 BK 70 TR 15]", $0D
        .byte "WAIT 2", $0D
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
        JSR parse_dec_arg     ; -> arg_lo:arg_hi
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
        ; found: update value at offset 4-5 of slot
        LDY #4
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
        ; append new entry: write 4-byte name then 2-byte value
        LDY #0
@cn:    LDA mnem_buf,Y
        STA (mptr_lo),Y
        INY
        CPY #4
        BNE @cn
        LDA arg_lo
        STA (mptr_lo),Y
        INY
        LDA arg_hi
        STA (mptr_lo),Y
        INC n_vars
        RTS
@bad:
@full:  LDA #2
        STA cmd_status
        LDA #'?'
        ORA #$80
        JSR ECHO
        LDA #$0D
        ORA #$80
        JSR ECHO
        RTS

; read_var_name: read up to 4 letters at line_buf,line_idx into mnem_buf,
;   uppercased, padded with space. Advances line_idx past consumed letters.
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
        BCC @done
        CMP #'Z'+1
        BCS @done
        CPY #4
        BCS @past4
        STA mnem_buf,Y
        INY
@past4: INX
        JMP @rd
@done:  STX line_idx
        LDA #' '
@padl:  CPY #4
        BEQ @ret
        STA mnem_buf,Y
        INY
        JMP @padl
@ret:   RTS

; cmd_to: TO NAME ... END   --- single-line user procedure definition.
;   Reads NAME, then captures everything between NAME and the END keyword
;   into a fixed-size proc_table slot. Single-line bodies only.
cmd_to:
        JSR skip_spaces
        JSR read_var_name     ; mnem_buf <- name
        LDA mnem_buf
        CMP #' '
        BNE @ok_name
        JMP @bad
@ok_name:
        ; preserve dest name across the body scan (which calls read_var_name)
        LDA mnem_buf
        PHA
        LDA mnem_buf+1
        PHA
        LDA mnem_buf+2
        PHA
        LDA mnem_buf+3
        PHA
        ; skip space(s) after the name; body starts here
        JSR skip_spaces
        LDA line_idx
        STA arg_lo            ; arg_lo = body_start (line_buf index)
        ; --- scan forward for "END" surrounded by space/CR ---
        ; nx_save_lo = current scan X. We look for E,N,D as 3 consecutive
        ; uppercase letters preceded by space and followed by space/CR.
        LDA line_idx
        STA nx_save_lo
@scan:  LDX nx_save_lo
        LDA line_buf,X
        AND #$7F
        CMP #$0D
        BNE @not_eol
        JMP @bad_pop          ; no END before EOL
@not_eol:
        ; check for E
        CMP #'E'
        BNE @next
        ; need 'N' next
        INX
        LDA line_buf,X
        AND #$7F
        CMP #'N'
        BNE @next
        INX
        LDA line_buf,X
        AND #$7F
        CMP #'D'
        BNE @next
        ; check terminator (space or CR)
        INX
        LDA line_buf,X
        AND #$7F
        CMP #' '
        BEQ @found
        CMP #$0D
        BEQ @found
@next:  INC nx_save_lo
        JMP @scan
@found: ; nx_save_lo currently points at 'E'. body_end = nx_save_lo (exclusive).
        ; advance line_idx past END (3 chars beyond start of E)
        ; arg_hi = body_end
        LDA nx_save_lo
        STA arg_hi
        CLC
        ADC #3
        STA line_idx          ; line_idx now after "END"
        ; restore dest name (mnem_buf was clobbered by scan? no, scan didn't
        ; touch it -- but read_var_name might have, on second thought no)
        PLA
        STA mnem_buf+3
        PLA
        STA mnem_buf+2
        PLA
        STA mnem_buf+1
        PLA
        STA mnem_buf
        ; --- compute body_len = arg_hi - arg_lo  (clamp to PROC_BODY_MAX) ---
        SEC
        LDA arg_hi
        SBC arg_lo
        ; trim trailing space(s)
        TAX                   ; X = body_len candidate
@trim:  CPX #0
        BEQ @len_ok
        DEX
        LDA arg_lo
        STX tmp
        CLC
        ADC tmp
        TAY
        LDA line_buf,Y
        AND #$7F
        CMP #' '
        BEQ @trim_more
        ; not space -> stop. body_len = X+1
        INX
        JMP @len_ok
@trim_more:
        JMP @trim
@len_ok:
        ; X = trimmed body_len
        CPX #PROC_BODY_MAX+1
        BCC @len_fits
        LDX #PROC_BODY_MAX
@len_fits:
        STX tmp               ; tmp = body_len
        ; --- look up name; update or append ---
        JSR find_proc_slot    ; -> mptr_lo:hi = slot, C=0 if existing, C=1 if new
        BCC @write
        ; new: check space
        LDA n_procs
        CMP #MAX_PROCS
        BCS @bad
        INC n_procs
@write: ; write name (4 bytes) at slot+0..3
        LDY #0
@cn:    LDA mnem_buf,Y
        STA (mptr_lo),Y
        INY
        CPY #4
        BNE @cn
        ; write body length at slot+4 (Y=4 here)
        LDA tmp
        STA (mptr_lo),Y
        ; advance mptr_lo:hi by 5 -> points at body start
        CLC
        LDA mptr_lo
        ADC #5
        STA mptr_lo
        BCC @aiok
        INC mptr_hi
@aiok:  ; copy body: for Y in 0..tmp-1: body[Y] = line_buf[arg_lo + Y]
        LDY #0
@cb:    CPY tmp
        BEQ @done
        TYA
        CLC
        ADC arg_lo
        TAX
        LDA line_buf,X
        STA (mptr_lo),Y
        INY
        JMP @cb
@done:  RTS
@bad_pop:
        PLA
        PLA
        PLA
        PLA
@bad:   LDA #2
        STA cmd_status
        LDA #'?'
        ORA #$80
        JSR ECHO
        LDA #$0D
        ORA #$80
        JSR ECHO
        RTS

; find_proc_slot: locate slot for mnem_buf in proc_table.
;   On match: C=0, mptr_lo:hi -> slot start.
;   On miss : C=1, mptr_lo:hi -> next free slot (= proc_table + n_procs * 32).
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
        CPY #4
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

; proc_invoke: save the caller's line_buf + line_idx, copy the proc body
;   into line_buf, recursively parse_and_exec, then restore line_buf
;   and line_idx so the caller's outer parse keeps working.
;   This lets `REPEAT N [PROC ...]` re-read its body each iteration.
;   Note: only ONE level of proc nesting is supported (no proc-calls-proc).
proc_invoke:
        ; --- save line_buf to proc_save_buf ---
        LDX #0
@sv:    LDA line_buf,X
        STA proc_save_buf,X
        INX
        CPX #LINE_MAX
        BNE @sv
        LDA line_idx
        STA proc_save_idx
        ; --- copy proc body to line_buf ---
        LDY #4
        LDA (mptr_lo),Y       ; A = body length
        STA tmp               ; tmp = len
        LDX #0
@cp:    CPX tmp
        BEQ @term
        TXA
        CLC
        ADC #5                ; offset 5+X in slot
        TAY
        LDA (mptr_lo),Y
        STA line_buf,X
        INX
        JMP @cp
@term:  LDA #$0D
        STA line_buf,X
        LDA #0
        STA line_idx
        ; --- run the body ---
        JSR parse_and_exec
        ; --- restore line_buf + line_idx ---
        LDX #0
@rs:    LDA proc_save_buf,X
        STA line_buf,X
        INX
        CPX #LINE_MAX
        BNE @rs
        LDA proc_save_idx
        STA line_idx
        RTS

; roll_lfsr: advance the 16-bit Galois LFSR (taps $B400 -> bits 16,14,13,11).
;   Standard right-shift Galois variant: shift the 16-bit value right by 1;
;   if the bit shifted out (= old bit 0 of lfsr_lo) was 1, XOR taps into hi.
roll_lfsr:
        LSR lfsr_hi          ; bit 0 of lfsr_hi -> C
        ROR lfsr_lo          ; lfsr_lo: C -> bit 7, bit 0 -> C (the shifted-out bit)
        BCC @done            ; if shifted-out bit was 0, no XOR
        LDA lfsr_hi
        EOR #$B4
        STA lfsr_hi
@done:  RTS

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

; print_decimal: print arg_lo:hi as unsigned decimal (1..5 digits, no padding).
print_decimal:
        ; If value is zero, print "0" and return.
        LDA arg_lo
        ORA arg_hi
        BNE @nz
        LDA #'0'
        ORA #$80
        JSR ECHO
        RTS
@nz:    ; Extract digits via repeated div by 10, push, then pop to print.
        LDY #0                ; digit count (X is clobbered by div_arg_by_10)
@dig:   JSR div_arg_by_10     ; arg /= 10, A = remainder
        PHA
        INY
        LDA arg_lo
        ORA arg_hi
        BNE @dig
@out:   PLA
        ORA #'0'
        ORA #$80
        JSR ECHO
        DEY
        BNE @out
        RTS

; div_arg_by_10: arg_lo:hi /= 10 (unsigned), returns remainder in A.
div_arg_by_10:
        LDA #0
        STA tmp               ; remainder
        LDX #16
@d:     ASL arg_lo
        ROL arg_hi
        ROL tmp
        LDA tmp
        CMP #10
        BCC @ns
        SBC #10
        STA tmp
        INC arg_lo            ; bit-0 of quotient
@ns:    DEX
        BNE @d
        LDA tmp
        RTS

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

; var_lookup: search mnem_buf in var_table.
;   On match: C=0, mptr_lo:hi -> matched slot.
;   On miss : C=1, mptr_lo:hi -> next free slot (one past last entry).
var_lookup:
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
        CPY #4
        BNE @c
        CLC
        RTS
@no:    CLC
        LDA mptr_lo
        ADC #6
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

; ----- helpers ----
; mod360_arg: clamp arg_lo/hi into [0..359] by repeated subtraction.
mod360_arg:
@l:     LDA arg_hi
        CMP #>360
        BCC @done
        BNE @sub
        LDA arg_lo
        CMP #<360
        BCC @done
@sub:   SEC
        LDA arg_lo
        SBC #<360
        STA arg_lo
        LDA arg_hi
        SBC #>360
        STA arg_hi
        JMP @l
@done:  RTS

; norm360: reduce th_lo/hi mod 360 (after addition that may have grown).
norm360:
@l:     LDA th_hi
        CMP #>360
        BCC @done
        BNE @sub
        LDA th_lo
        CMP #<360
        BCC @done
@sub:   SEC
        LDA th_lo
        SBC #<360
        STA th_lo
        LDA th_hi
        SBC #>360
        STA th_hi
        JMP @l
@done:  RTS

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
mod360_tmp:
@l:     LDA tmp2
        CMP #>360
        BCC @done
        BNE @sub
        LDA tmp
        CMP #<360
        BCC @done
@sub:   SEC
        LDA tmp
        SBC #<360
        STA tmp
        LDA tmp2
        SBC #>360
        STA tmp2
        JMP @l
@done:  RTS

; ============================================================================
; signed_sin: input  tmp:tmp2 = angle in [0..359]
;             output A = signed_sin*64  in [-64..+64]
;
;   if a <=  90:  +sin_q[a]
;   if a <= 180:  +sin_q[180 - a]
;   if a <= 270:  -sin_q[a - 180]
;   else       :  -sin_q[360 - a]
; ============================================================================
signed_sin:
        LDA tmp2
        BNE @hi               ; angle >= 256 -> in (256..359] => last quadrant
        LDA tmp
        CMP #91
        BCC @q1               ; 0..90
        CMP #181
        BCC @q2               ; 91..180
        ; 181..255 -> q3
@q3:    SEC
        LDA tmp
        SBC #180
        TAX
        LDA sin_q,X
        EOR #$FF
        CLC
        ADC #1
        RTS
@q1:    LDX tmp
        LDA sin_q,X
        RTS
@q2:    SEC
        LDA #180
        SBC tmp
        TAX
        LDA sin_q,X
        RTS
@hi:    ; tmp2 = 1, tmp in 0..103 -> angle 256..359
        ; q3 covers 181..270 (i.e. tmp2=1, tmp 0..14)
        ; q4 covers 271..359 (i.e. tmp2=1, tmp 15..103)
        LDA tmp
        CMP #15               ; 256+15 = 271
        BCC @hi_q3
        ; q4: -sin_q[360 - angle] = -sin_q[360 - (256+tmp)] = -sin_q[104 - tmp]
@hi_q4: SEC
        LDA #104
        SBC tmp
        TAX
        LDA sin_q,X
        EOR #$FF
        CLC
        ADC #1
        RTS
@hi_q3: ; 256..270: -sin_q[angle - 180] = -sin_q[(256+tmp) - 180] = -sin_q[76+tmp]
        CLC
        LDA #76
        ADC tmp
        TAX
        LDA sin_q,X
        EOR #$FF
        CLC
        ADC #1
        RTS

; ============================================================================
; mul_dist_by_signed: multiply arg_lo (unsigned 0..255) by A (signed -64..+64),
;                     divide by 64, sign-extend, store result in prod_lo/prod_hi.
;
;   abs_v = |A|
;   uprod = arg_lo * abs_v             (16-bit unsigned, max 255*64=16320)
;   result = uprod / 64                (range 0..255)
;   apply sign -> prod_lo/prod_hi signed.
; ============================================================================
mul_dist_by_signed:
        ; record sign
        STA tmp               ; signed value
        LDA #0
        STA sign_flag
        LDA tmp
        BPL @abs_done
        LDA tmp
        EOR #$FF
        CLC
        ADC #1
        STA tmp               ; tmp = abs(value)
        LDA #1
        STA sign_flag
@abs_done:
        ; 8x8 unsigned multiply: arg_lo * tmp -> 16-bit in arg2_hi:arg2_lo (scratch)
        LDA #0
        STA arg2_hi
        STA arg2_lo
        LDX #8
@m:     LSR tmp               ; bit -> C
        BCC @noadd
        CLC
        LDA arg2_hi
        ADC arg_lo
        STA arg2_hi
@noadd: ROR arg2_hi
        ROR arg2_lo
        DEX
        BNE @m
        ; uprod is in arg2_hi:arg2_lo. Divide by 64 = shift right by 6 across
        ; the 16-bit value. Result fits in 8 bits since uprod <= 16320.
        LDX #6
@shr:   LSR arg2_hi
        ROR arg2_lo
        DEX
        BNE @shr
        LDA arg2_lo
        STA prod_lo
        LDA #0
        STA prod_hi
        ; apply sign
        LDA sign_flag
        BEQ @done
        ; negate prod
        SEC
        LDA #0
        SBC prod_lo
        STA prod_lo
        LDA #0
        SBC prod_hi
        STA prod_hi
@done:  RTS

negate_prod:
        SEC
        LDA #0
        SBC prod_lo
        STA prod_lo
        LDA #0
        SBC prod_hi
        STA prod_hi
        RTS

; ============================================================================
; sine table: sin_q[i] = round(sin(i degrees) * 64), for i in 0..104.
;             We need 0..90 in principle, but signed_sin's q3 high-half path
;             reaches up to index 104 (76+28? 76+14=90 actually). Indices
;             beyond 90 are zero-valued so the upper bits of the cosine
;             machinery never overshoot.
; ============================================================================
sin_q:
        ; 0  1  2  3  4  5  6  7  8  9
        .byte  0, 1, 2, 3, 4, 6, 7, 8, 9,10
        .byte 11,12,13,14,16,17,18,19,20,21
        .byte 22,23,24,25,26,27,28,29,30,31
        .byte 32,33,34,35,36,37,38,39,40,41
        .byte 42,42,43,44,45,45,46,47,48,48
        .byte 49,50,50,51,52,52,53,54,54,55
        .byte 55,56,56,57,57,58,58,59,59,60
        .byte 60,60,61,61,61,62,62,62,63,63
        .byte 63,63,63,64,64,64,64,64,64,64
        .byte 64,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        ; padding (indices 91..104) stays at 0; signed_sin's q3-high path
        ; never reads here for valid headings <360, but keep a margin.

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
@loop:  ; if count == 0 -> done
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
@bad:   LDA #2
        STA cmd_status
        LDA #'?'
        ORA #$80
        JSR ECHO
        LDA #$0D
        ORA #$80
        JSR ECHO
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

disable_sprites:
        ; write Y=$D0 to sprite #0 attribute -> chip stops scanning sprites.
        LDA #$00
        STA VDP_CTRL
        LDA #$3B | $40
        STA VDP_CTRL
        LDA #$D0
        STA VDP_DATA
        RTS

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
; -- VDP helpers copied verbatim from TMS_Maze3D.asm --
; ============================================================================

; init_vdp_g2: 8 registers + linear name table + colour table = $F1.
init_vdp_g2:
        LDX #0
@rg:    LDA vdp2_regs,X
        STA VDP_CTRL
        TXA
        ORA #$80
        STA VDP_CTRL
        INX
        CPX #8
        BNE @rg
        ; --- write linear name table at $3800 ---
        LDA #$00
        STA VDP_CTRL
        LDA #$78
        STA VDP_CTRL
        LDX #3
@th:    LDY #0
@nm:    TYA
        STA VDP_DATA
        INY
        BNE @nm
        DEX
        BNE @th
        ; --- color table: $F1 everywhere ($2000-$37FF, 6144 bytes) ---
        LDA #$00
        STA VDP_CTRL
        LDA #$60
        STA VDP_CTRL
        LDX #24
        LDY #0
@cl:    LDA #$F1
        STA VDP_DATA
        INY
        BNE @cl
        DEX
        BNE @cl
        RTS

vdp2_regs:
        .byte $02, $C0, $0E, $FF, $03, $76, $03, $F1

; clear_bitmap: zero the 6144-byte pattern table at $0000.
clear_bitmap:
        LDA #$00
        STA VDP_CTRL
        LDA #$40
        STA VDP_CTRL
        LDX #24
        LDY #0
@lp:    LDA #$00
        STA VDP_DATA
        INY
        BNE @lp
        DEX
        BNE @lp
        RTS

vdp_set_write:
        LDA pix_addr_lo
        STA VDP_CTRL
        LDA pix_addr_hi
        ORA #$40
        STA VDP_CTRL
        RTS

vdp_set_read:
        LDA pix_addr_lo
        STA VDP_CTRL
        LDA pix_addr_hi
        STA VDP_CTRL
        RTS

calc_pix_addr:
        LDA pix_x
        AND #$F8
        STA tmp
        LDA pix_y
        AND #$07
        ORA tmp
        STA pix_addr_lo
        LDA pix_y
        LSR
        LSR
        LSR
        STA pix_addr_hi
        RTS

plot_set:
        LDA pix_y
        CMP #192
        BCS @done
        JSR calc_pix_addr
        LDA pix_x
        AND #$07
        TAX
        LDA bitmask,X
        STA pix_mask
        JSR vdp_set_read
        LDA VDP_DATA
        LDX plot_mode
        BNE @xor
        ORA pix_mask
        JMP @write
@xor:   EOR pix_mask
@write: STA pix_byte
        JSR vdp_set_write
        LDA pix_byte
        STA VDP_DATA
@done:  RTS

bitmask:
        .byte $80, $40, $20, $10, $08, $04, $02, $01

; line_xy: Bresenham, 16-bit signed err. Inputs ln_x0/y0/x1/y1 (8-bit pixels).
;          dx, dy are 8-bit unsigned magnitudes (always 0..255 on screen).
;          err = dx - dy held in ln_err:ln_err_hi (16-bit signed two's complement).
;          Each iteration tests 2*err vs -dy and dx, both as 16-bit signed.
line_xy:
        ; --- compute dx, sx ---
        SEC
        LDA ln_x1
        SBC ln_x0
        BCS @sxp
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
@dy:    ; --- compute dy, sy ---
        SEC
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
@init:  ; --- err = dx - dy (16-bit signed; dx,dy are unsigned 0..255) ---
        SEC
        LDA ln_dx
        SBC ln_dy
        STA ln_err
        LDA #0
        SBC #0                ; sign-extend the borrow into err_hi
        STA ln_err_hi
        ; --- copy current point to pix_x/y ---
        LDA ln_x0
        STA pix_x
        LDA ln_y0
        STA pix_y

@step:  JSR plot_set
        LDA ln_x0
        CMP ln_x1
        BNE @do
        LDA ln_y0
        CMP ln_y1
        BEQ @end
@do:    ; --- compute 2*err in tmp:tmp2 (preserve ln_err for the per-axis updates) ---
        LDA ln_err
        STA tmp
        LDA ln_err_hi
        STA tmp2
        ASL tmp
        ROL tmp2
        ; --- test 1: step x if 2*err >= -dy  i.e. (2*err + dy) >= 0 ---
        ;   tmp:tmp2 + (dy zero-extended)  -> sign byte in A
        CLC
        LDA tmp
        ADC ln_dy
        LDA tmp2
        ADC #0
        BMI @no_x             ; sum < 0 -> skip x
        ; err -= dy   (16-bit signed: dy is unsigned, so subtract dy_lo with carry)
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

@no_x:  ; --- test 2: step y if 2*err < dx  i.e. (2*err - dx) < 0 ---
        SEC
        LDA tmp
        SBC ln_dx
        LDA tmp2
        SBC #0
        BPL @no_y             ; diff >= 0 -> skip y
        ; err += dx
        CLC
        LDA ln_err
        ADC ln_dx
        STA ln_err
        LDA ln_err_hi
        ADC #0
        STA ln_err_hi
        ; y0 += sy
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
