; ============================================================================
; t16_a1io_rtc.s — micro-test: a1io/a1io.asm broadcast-poll reader
; ============================================================================
; GUARDS: `a1io_read_reg` is the single primitive the whole A1-IO/RTC register
;   set hangs off. It does not address a register — it SPINS on the card's
;   broadcast cycle until the index it wants appears on PORTA, then latches
;   PORTB. Two things can silently break and neither shows on screen:
;
;     * the strobe/index decode — `LDA VIA_IRA / BPL / AND #$1F / CMP` reads
;       bit 7 as "a new register is on the bus" and the low five bits as its
;       index. An off-by-one in the mask, or a card that stops setting the
;       strobe, turns every read into an infinite spin or into the WRONG
;       register's value, which still looks like a plausible clock.
;     * the value latch — PORTB must be read AFTER the index matched, not
;       before; reading it a cycle early returns the previous register.
;
;   Reading six registers whose values are known EXACTLY is what separates
;   those failures from a working read. `--rtc-freeze` is what makes them
;   known: a clock that ticks cannot be asserted, and this driver would
;   otherwise be re-run against a different second every time.
;
;   Frozen instant: 2026-09-02 14:37:51.
;     hours 14 = $0E   minutes 37 = $25   seconds 51 = $33
;     day    2 = $02   month    9 = $09   year (−2000) 26 = $1A
;   The registers are BINARY, not BCD — a card that started emitting BCD
;   would land $14/$37/$51 here and every byte would move.
;
;   Reads run twice, from two different starting indices, so a reader that
;   happened to work only when the broadcast cycle is at phase 0 fails: the
;   second pass starts mid-cycle, on YEAR, and must return the same six bytes.
;
; POM1-LIB-MICRO-TEST
; INCLUDES: a1io/a1io.asm
; CFG: micro.cfg
; PRESET: 1
; ENABLE: a1io
; ARGS: --rtc-freeze "2026-09-02 14:37:51"
; LOAD: 0300
; RUN: 0300
; STEPS: 400000
; EXPECT: 0F00 A5 0E 25 33 02 09 1A 1A 0E 25 33 02 09
; ============================================================================

.include "apple1.inc"
; a1io.asm is a MODEL A lib — textual .include, no .export (dev/lib/README.md).
; It declares its own `a1io_target` ZP slot unless the caller aliases one first;
; taking the default is what a real project does.
.include "a1io.asm"

MB = $0F00

; ENTRY, not CODE. micro.cfg places ENTRY first in MAIN precisely for a driver
; that textually .include's a lib: the lib's own code lands in CODE, so a `main`
; in CODE would sit BEHIND it and `--run 0300` would enter the library instead.
; This is the companion rule to the INCLUDES: key — model A needs both.
.segment "ENTRY"
main:
        APPLE1_PREAMBLE

        ; --- pass 1: the six clock registers, in index order ----------------
        LDX     #A1IO_REG_HOURS
        JSR     a1io_read_reg
        STA     MB+1                    ; $0E — 14h
        LDX     #A1IO_REG_MINUTES
        JSR     a1io_read_reg
        STA     MB+2                    ; $25 — 37
        LDX     #A1IO_REG_SECONDS
        JSR     a1io_read_reg
        STA     MB+3                    ; $33 — 51
        LDX     #A1IO_REG_DAY
        JSR     a1io_read_reg
        STA     MB+4                    ; $02
        LDX     #A1IO_REG_MONTH
        JSR     a1io_read_reg
        STA     MB+5                    ; $09
        LDX     #A1IO_REG_YEAR
        JSR     a1io_read_reg
        STA     MB+6                    ; $1A — 2026

        ; --- pass 2: same six, starting mid-cycle ---------------------------
        ; A reader that only lands correctly when the broadcast happens to be
        ; at phase 0 passes pass 1 and fails here.
        LDX     #A1IO_REG_YEAR
        JSR     a1io_read_reg
        STA     MB+7                    ; $1A
        LDX     #A1IO_REG_HOURS
        JSR     a1io_read_reg
        STA     MB+8                    ; $0E
        LDX     #A1IO_REG_MINUTES
        JSR     a1io_read_reg
        STA     MB+9                    ; $25
        LDX     #A1IO_REG_SECONDS
        JSR     a1io_read_reg
        STA     MB+10                   ; $33
        LDX     #A1IO_REG_DAY
        JSR     a1io_read_reg
        STA     MB+11                   ; $02
        LDX     #A1IO_REG_MONTH
        JSR     a1io_read_reg
        STA     MB+12                   ; $09

        ; Magic LAST: the harness reads the mailbox out of a snapshot, so a
        ; driver that died halfway must not look like one that finished.
        LDA     #$A5
        STA     MB
@spin:  JMP     @spin
