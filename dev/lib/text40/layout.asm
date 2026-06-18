; ============================================================================
; layout.asm -- QWERTY/AZERTY keyboard layout selector for WASD-style games
; ============================================================================
; Models the WASD-layout dispatch inlined in 6+ projects (the apple1 / gen2 /
; tms9918 Sokoban variants, tms9918 Snake / Galaga, Connect 4 — same ~17-line
; pattern). STATUS: not yet adopted — those projects still carry inline copies;
; a future migration will replace each with `.include "layout.asm"`. Two entry
; points:
;
;   prompt_wasd_layout -- print the canned " 1=QWERTY  2=AZERTY" prompt
;                         then run select_wasd_layout. Use when the
;                         project doesn't already have its own banner.
;                         Clobbers A. X, Y preserved.
;
;   select_wasd_layout -- wait for '1' or '2', echo nothing, fill
;                         key_up_code (W or Z) and key_left_code (A or Q).
;                         Use when the caller has already printed a
;                         custom prompt (e.g. game-specific banner).
;                         Clobbers A. X, Y preserved.
;
; Convention: down='S' and right='D' are universal across QWERTY and
; AZERTY (matches every Apple-1 game in this tree). Only the up/left
; pair changes — that's what gets stored in ZP for the game's main
; input loop to compare against (see games_sokoban/Sokoban.asm:140).
;
; ZP usage: key_up_code, key_left_code (1 byte each).
;   - Default: this module reserves a fresh pair via .ifndef.
;   - With zp.inc (lib/apple1/): pre-declare both above zp.inc to fold
;     them into your project's main scratch.
;   - Tight ZP: alias to existing slots before the include:
;         key_up_code   = my_up
;         key_left_code = my_lf
;         .include "layout.asm"
;
; Caller responsibility:
;   - print_str_ax (lib/apple1/print.asm) and wait_key (lib/apple1/kbd.asm)
;     must be in scope. KBDCR/KBD/ECHO from apple1.inc.
; ============================================================================

.ifndef key_up_code
.segment "ZEROPAGE"
key_up_code:    .res 1
key_left_code:  .res 1
.endif

.segment "CODE"

; ----------------------------------------------------------------------------
; prompt_wasd_layout -- full sequence: print canned prompt, dispatch.
; ----------------------------------------------------------------------------
prompt_wasd_layout:
        LDA     #<text40_str_layout
        LDX     #>text40_str_layout
        JSR     print_str_ax
        ; fall through to select_wasd_layout

; ----------------------------------------------------------------------------
; select_wasd_layout -- wait for '1' (QWERTY) or '2' (AZERTY), set
;                       key_up_code (W/Z) and key_left_code (A/Q).
; ----------------------------------------------------------------------------
select_wasd_layout:
@w:     JSR     wait_key
        CMP     #'1'
        BEQ     @qwerty
        CMP     #'2'
        BNE     @w
        ; AZERTY
        LDA     #'Z'
        STA     key_up_code
        LDA     #'Q'
        STA     key_left_code
        RTS
@qwerty:
        LDA     #'W'
        STA     key_up_code
        LDA     #'A'
        STA     key_left_code
        RTS

; ----------------------------------------------------------------------------
; Default prompt string. NUL-terminated; print_str_ax ORs $80 per byte.
; ----------------------------------------------------------------------------
text40_str_layout:
        .byte   $0D, " 1=QWERTY  2=AZERTY", $0D, 0
