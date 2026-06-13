; ============================================================================
; A1_TelemetryDemo — minimal "homing" game showcasing the POM1 telemetry SDK.
;
; Each frame it emits [player, target, won] and parks (lock-step). The external
; harness reads the frame, sends a direction byte (1 = move +1, 2 = move -1)
; and ACKs; the game steps the player and loops. The player converges on the
; target driven entirely by the automated test — no human, no display.
;
;   Build : make            -> ../../../software/Telemetry/A1_TelemetryDemo.bin
;   Run   : POM1 --headless --telemetry-port 6601 --load 0280:<bin> --run 0280
;   Test  : python3 tools/test_telemetry_demo.py
;
; This is the worked example for doc/TELEMETRY_SIDE_CHANNEL.md +
; tools/pom1_telemetry.py.
; ============================================================================

.include "telemetry.inc"

.zeropage
player: .res 1
target: .res 1
won:    .res 1

.code
start:
    TELE_ARM                 ; arm deterministic lock-step
    lda #5
    sta player               ; player starts at 5
    lda #20
    sta target               ; target sits at 20

frame:
    ; won = (player == target)
    lda #0
    sta won
    lda player
    cmp target
    bne emit
    lda #1
    sta won

emit:
    TELE_PUT player          ; frame byte 0
    TELE_PUT target          ; frame byte 1
    TELE_PUT won             ; frame byte 2
    TELE_FRAME               ; flush + park until the harness ACKs

    ; resume here: apply the harness's direction byte for this frame
    lda TELE_IN              ; 1 = +1, 2 = -1, anything else = no move
    cmp #1
    bne not_right
    inc player
    jmp frame
not_right:
    cmp #2
    bne frame
    dec player
    jmp frame
