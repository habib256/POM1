; ============================================================================
; LibSmoke.asm -- end-to-end validation of the unified ZP convention
; ============================================================================
; Exercises every lib in dev/lib/{apple1,m6502}/ to prove that:
;   1. zp.inc properly declares + .exportzp's the shared baseline slots
;   2. The textual-include path (print, kbd, delay, print_num, multiply,
;      prng8, prng16) finds those slots without collision.
;   3. The cross-object .importzp path works at link — math.o imports
;      tmp/tmp2/arg_*/th_* from this TU's exports and resolves cleanly.
;   4. JMP WOZMON ($FF1A) returns to the monitor with a visible prompt.
;
; If zp.inc forgets a .exportzp, ld65 fails the link with
;   "Unresolved external symbol: tmp"
; — direct bug-reveal for the most common failure mode.
;
; Build: see Makefile (multi-object link with math.o).
; Run:   POM1 stock preset (0), File > Load Memory → LibSmoke.txt → 280R.
; ============================================================================

.include "apple1.inc"
.include "zp.inc"

; --- Math.asm extra ZP slots (caller-declared, exported for math.o link) ---
.segment "ZEROPAGE"
arg_lo:    .res 1
arg_hi:    .res 1
arg2_lo:   .res 1
arg2_hi:   .res 1
th_lo:     .res 1
th_hi:     .res 1
.exportzp arg_lo, arg_hi, arg2_lo, arg2_hi, th_lo, th_hi

; --- Math.asm BSS slots (caller-declared, exported for math.o link) ---
.segment "BSS"
prod_lo:    .res 1
prod_hi:    .res 1
sign_flag:  .res 1
lfsr_lo:    .res 1
lfsr_hi:    .res 1
.export   prod_lo, prod_hi, sign_flag, lfsr_lo, lfsr_hi

; --- Imports from math.o (the cross-object validation) -----------------------
.import   print_decimal

.segment "CODE"

; ============================================================================
; main -- runs each smoke step in sequence, then returns to Wozmon.
; ============================================================================
main:
        LDA     #<msg_banner
        LDX     #>msg_banner
        JSR     print_str_ax

        ; --- Step 1: wait_key + key echo ------------------------------------
        LDA     #<msg_key
        LDX     #>msg_key
        JSR     print_str_ax
        JSR     wait_key
        ORA     #$80
        JSR     ECHO
        LDA     #$8D
        JSR     ECHO

        ; --- Step 2: print_byte_dec(42) -------------------------------------
        LDA     #<msg_dec
        LDX     #>msg_dec
        JSR     print_str_ax
        LDA     #42
        JSR     print_byte_dec
        LDA     #$8D
        JSR     ECHO

        ; --- Step 3: umul8(6, 7) = 42 (multiply.asm textual include) --------
        LDA     #<msg_mul
        LDX     #>msg_mul
        JSR     print_str_ax
        LDA     #6
        LDX     #7
        JSR     umul8                   ; A = product low, X = product high
        JSR     print_byte_dec
        LDA     #$8D
        JSR     ECHO

        ; --- Step 4: prng8 sample (uses prng_lo/hi from zp.inc) -------------
        LDA     #<msg_prng8
        LDX     #>msg_prng8
        JSR     print_str_ax
        LDA     #$5B
        STA     prng_lo
        LDA     #$A2
        STA     prng_hi
        JSR     random
        JSR     print_byte_dec
        LDA     #$8D
        JSR     ECHO

        ; --- Step 5: prng16 sample (same prng_lo/hi slots, unified) ---------
        LDA     #<msg_prng16
        LDX     #>msg_prng16
        JSR     print_str_ax
        LDA     #$11
        STA     prng_lo
        LDA     #$22
        STA     prng_hi
        JSR     prng16
        JSR     print_byte_dec
        LDA     #$8D
        JSR     ECHO

        ; --- Step 6: print_decimal(1234) — CROSS-OBJECT via math.o ----------
        ; This is the canary for .exportzp resolution. If zp.inc skipped a
        ; .exportzp, ld65 has already failed at link and we never get here.
        LDA     #<msg_decimal
        LDX     #>msg_decimal
        JSR     print_str_ax
        LDA     #<1234
        STA     arg_lo
        LDA     #>1234
        STA     arg_hi
        JSR     print_decimal
        LDA     #$8D
        JSR     ECHO

        ; --- Step 7: text40 menu_select (1..3) ------------------------------
        LDA     #<msg_menu
        LDX     #>msg_menu
        JSR     print_str_ax
        LDA     #'1'
        LDX     #'3'
        JSR     menu_select             ; A = chosen digit, echoed by lib
        LDA     #$8D
        JSR     ECHO

        ; --- Step 8: text40 repeat_char_ax — print 30 dashes ----------------
        LDA     #<msg_bar
        LDX     #>msg_bar
        JSR     print_str_ax
        LDA     #'-'
        LDX     #30
        JSR     repeat_char_ax
        LDA     #$8D
        JSR     ECHO

        ; --- Step 9: delay_ms_a — 4 x 250 ms ≈ 1 s pause --------------------
        LDA     #<msg_delay
        LDX     #>msg_delay
        JSR     print_str_ax
        LDA     #250
        JSR     delay_ms_a
        LDA     #250
        JSR     delay_ms_a
        LDA     #250
        JSR     delay_ms_a
        LDA     #250
        JSR     delay_ms_a
        LDA     #<msg_done
        LDX     #>msg_done
        JSR     print_str_ax

        ; --- Step 10: clean return to Wozmon (validates WOZMON = $FF1A) ------
        ; Plain RTS would pop garbage from the stack (Wozmon's R command jumps
        ; via JMP (XAML), no return address pushed). JMP WOZMON gives the
        ; user a visible "\" prompt + CR — the "I'm back" signal.
        JMP     WOZMON

; ============================================================================
; Strings (NUL-terminated, print_str_ax ORs $80 per byte before ECHO).
; ============================================================================
msg_banner:
        .byte   $0D, "LIB SMOKE — UNIFIED ZP CONVENTION", $0D, 0
msg_key:
        .byte   "PRESS ANY KEY: ", 0
msg_dec:
        .byte   "PRINT_BYTE_DEC(42) = ", 0
msg_mul:
        .byte   "UMUL8(6,7) = ", 0
msg_prng8:
        .byte   "PRNG8 SAMPLE = ", 0
msg_prng16:
        .byte   "PRNG16 SAMPLE = ", 0
msg_decimal:
        .byte   "PRINT_DECIMAL(1234) = ", 0
msg_menu:
        .byte   "MENU PICK [1-3]: ", 0
msg_bar:
        .byte   "BAR: ", 0
msg_delay:
        .byte   "DELAY 1S... ", 0
msg_done:
        .byte   "DONE", $0D, 0

; ============================================================================
; Library textual includes — order matters: print.asm first because every
; later step uses print_str_ax. multiply / prng* live in lib/m6502, found
; via the -I path in the Makefile.
; ============================================================================
.include "print.asm"
.include "kbd.asm"
.include "delay.asm"
.include "print_num.asm"
.include "multiply.asm"
.include "prng8.asm"
.include "prng16.asm"
.include "menu.asm"
.include "repeat.asm"
