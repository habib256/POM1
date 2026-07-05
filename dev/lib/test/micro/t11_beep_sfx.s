; ============================================================================
; t11_beep_sfx.s -- micro-test: beep/beep_sfx.asm SFX table walk + step timing
; ============================================================================
; GUARDS: the data-driven 1-bit SFX player's control flow -- the part a game
;   depends on and that silently rots if the step stride or the terminator test
;   is wrong. Pins, for a 4-step SFX (last step a REST, period 0):
;     - sfx_tick reads period/length from the CURRENT step (step 0 -> $08/$04),
;     - each tick advances exactly one 2-byte step,
;     - the length=0 terminator returns the player to idle (sfx_active -> 0)
;       after exactly 5 ticks (4 sound steps + 1 terminator read),
;     - a REST step (period 0) is walked like any other (no infinite loop / no
;       skipped terminator).
;   The audible toggling of $C030 is not asserted here (headless has no ear);
;   this pins the table machine that the beeper editor's export relies on.
;
; POM1-LIB-MICRO-TEST
; LIBS: beep/beep_sfx.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 20000
; EXPECT: 0F00 A5 05 08 04
; ============================================================================

.include "apple1.inc"

.import   sfx_start, sfx_tick, sfx_active
.importzp sfx_per, sfx_len

MB = $0F00

.segment "BSS"
count_n:  .res 1          ; tick counter (X/Y are clobbered by sfx_tick)

.segment "CODE"
main:
        APPLE1_PREAMBLE

        ; Arm a known 4-step SFX (step 2 is a REST: period 0).
        LDX     #<test_sfx
        LDY     #>test_sfx
        JSR     sfx_start

        ; Tick 1 plays step 0 -> pin the values it read out of the table.
        JSR     sfx_tick
        LDA     sfx_per
        STA     MB+2            ; expect $08 (step 0 period)
        LDA     sfx_len
        STA     MB+3            ; expect $04 (step 0 length)

        ; Keep ticking until the player goes idle; count the ticks. The counter
        ; lives in memory because sfx_tick clobbers X and Y.
        LDA     #1              ; one tick already done
        STA     count_n
@count:
        JSR     sfx_active
        BEQ     @done           ; A = ptr hi; 0 => idle
        JSR     sfx_tick
        INC     count_n
        JMP     @count
@done:
        LDA     count_n
        STA     MB+1            ; total ticks to idle -> expect 5

        LDA     #$A5
        STA     MB
spin:   JMP     spin

; 4 sound steps + terminator. Step 2 = REST (period 0).
test_sfx:
        .byte $08, $04          ; step 0
        .byte $10, $04          ; step 1
        .byte $00, $02          ; step 2 (rest)
        .byte $18, $04          ; step 3
        .byte $00, $00          ; terminator
