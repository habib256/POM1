; ============================================================================
; t12_sid_player.s -- micro-test: sid/sid_player.asm song walk + row timing
; ============================================================================
; GUARDS: the data-driven SID sequencer's control flow -- what a tune depends on
;   and what silently rots. SID registers are write-only (not in RAM), so the
;   actual $C800 writes are eyeballed in-app; this pins the table machine:
;     - a row is HELD for its `frames` duration (frame countdown), not advanced
;       every tick,
;     - sid_play_tick reads note/ctrl/frames from the CURRENT 3-byte row (row 0
;       -> ctrl $10 / frames 3),
;     - $FE (gate off) and $FF (tie) rows are walked like any other,
;     - the frames=0 terminator returns the player to idle after exactly the
;       right number of ticks (sum of durations + one per row + 1).
;   Song: A4/tri(3) C5/pulse(2) OFF(2) TIE(1) -> 3+2+2+1 + 4 rows + 1 = 13 ticks.
;
; POM1-LIB-MICRO-TEST
; LIBS: sid/sid_player.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 8000
; EXPECT: 0F00 A5 0D 10 03
; ============================================================================

.include "apple1.inc"       ; APPLE1_PREAMBLE
.include "sid.inc"

.import   sid_play_init, sid_play_start, sid_play_tick, sid_play_active
.importzp sid_ctrl, sid_frames

MB = $0F00

.segment "BSS"
count_n:  .res 1            ; tick counter (X/Y clobbered by sid_play_tick)

.segment "CODE"
main:
        APPLE1_PREAMBLE

        JSR     sid_play_init           ; volume + default ADSR (harmless here)

        LDX     #<song
        LDY     #>song
        JSR     sid_play_start

        ; Tick 1 loads row 0 -> pin what it read out of the table.
        JSR     sid_play_tick
        LDA     sid_ctrl
        STA     MB+2                    ; expect $10 (SID_TRI, row 0 ctrl)
        LDA     sid_frames
        STA     MB+3                    ; expect 3 (row 0 duration)

        ; Tick until idle; count ticks (counter in memory -- tick clobbers X/Y).
        LDA     #1
        STA     count_n
@count:
        JSR     sid_play_active
        BEQ     @done                   ; A = ptr hi; 0 => idle
        JSR     sid_play_tick
        INC     count_n
        JMP     @count
@done:
        LDA     count_n
        STA     MB+1                    ; expect 13 ($0D)

        LDA     #$A5
        STA     MB
spin:   JMP     spin

song:
        .byte   57, SID_TRI,   3        ; A4, triangle, 3 frames
        .byte   60, SID_PULSE, 2        ; C5, pulse, 2 frames
        .byte   $FE, 0,        2        ; gate off (rest), 2 frames
        .byte   $FF, 0,        1        ; tie (sustain), 1 frame
        .byte   0,   0,        0        ; terminator
