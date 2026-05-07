; ============================================================================
; TMS_SilTest.asm  --  TMS9918 Silicon-Strict Validation Suite v2.0
;                      (c) 2026 VERHILLE Arnaud
; ============================================================================
; Comprehensive silicon-divergence test battery for the P-LAB TMS9918
; Graphic Card on a Replica-1 + P-LAB TMS9918 setup. Each test probes
; one of the silicon behaviours catalogued in dev/SILICONBUGS.md (Bug
; N°1 to N°11) and either auto-records a quantitative result (drop count,
; status-bit value, frame counter) or prompts the observer for a Y/N
; answer when the test outcome is visual (raster split, illegal mode
; sprite cloning, mid-frame R7 rainbow).
;
; OUTPUT: native Apple-1 PIA display ($D012 / Wozmon ECHO @ $FFEF).
; Test results print line-by-line as each test completes — even if a
; later test crashes, the operator already has the partial log on screen.
; The TMS9918 is used only for the silicon scenarios under test (sprite
; setups, raster polling, mode switches). All operator interaction
; happens via the standard Apple-1 keyboard/screen.
;
; Workflow:
;   1. Boot — banner + test plan print on Apple-1 display.
;   2. Tests run sequentially (ALL bugs N°1..N°11 + Nouspikel extras).
;   3. Each test prints   "TXX <name>... <RESULT> <16-bit hex value>"
;      as soon as it finishes.
;   4. Visual tests prompt the observer with Y/N — answer is printed
;      and recorded.
;   5. Stress benchmark runs as long as it needs to (default ~30 sec
;      Galaga-class burst — drop counter live).
;   6. Final summary block re-prints every result + stress numbers.
;   7. "DONE" then halt — the screen scrolling holds the trace.
;
; Goal: lockstep silicon ↔ POM1 silicon-strict comparison. Any divergence
; flags a candidate POM1 bug. The recommended workflow is "validate
; software in POM1 strict before deploying to silicon" — this binary is
; the canonical regression fixture for that flow.
;
; Build:
;     cd dev/projects/tms9918_siltest && make
;     # Standalone:  software/tms9918/TMS_SilTest.bin (load $0280, 280R)
;     # CodeTank:    python3 tools/build_codetank_rom.py --rom=tools
;     #              load Codetank_TOOLS.rom, jumper down, 4000R
; ============================================================================

        .import tms9918_pad12  ; silicon-strict pad12-v3 (helper from tms9918_pad.asm)
        ; Fauna sprite patterns reused for the final visual demo.
        .import fauna_dog_pat, fauna_cat_pat, fauna_rabbit_pat
        .import fauna_spider_pat, fauna_snake_pat
.include "apple1.inc"
.include "tms9918.inc"

; ============================================================================
; ZP layout
; ============================================================================
.segment "ZEROPAGE"
str_lo:         .res 1
str_hi:         .res 1
loop_ctr:       .res 1
drop_lo:        .res 1
drop_hi:        .res 1
src_lo:         .res 1          ; source pointer for VRAM uploads (demo)
src_hi:         .res 1
demo_frame_lo:  .res 1
demo_frame_hi:  .res 1
demo_collide:   .res 1          ; latched collision flag during demo

; ============================================================================
; BSS — per-test result + numeric value storage. The summary block at
; end-of-program re-prints all of these so the screen has a coherent grid
; without depending on whether earlier prints scrolled out of view.
; ============================================================================
.segment "BSS"
res_T01: .res 1
res_T02: .res 1
res_T03: .res 1
res_T04: .res 1
res_T05: .res 1
res_T06: .res 1
res_T07: .res 1
res_T08: .res 1
res_T09: .res 1
res_T10: .res 1
res_T11: .res 1
res_T12: .res 1
res_T13: .res 1
res_T14: .res 1
res_T15: .res 1
res_T16: .res 1
res_T17: .res 1
res_T18: .res 1
res_T19: .res 1
res_T20: .res 1
val_T01_lo: .res 1
val_T01_hi: .res 1
val_T02_lo: .res 1
val_T02_hi: .res 1
val_T03_lo: .res 1
val_T03_hi: .res 1
val_T04_lo: .res 1
val_T04_hi: .res 1
val_T05_lo: .res 1
val_T05_hi: .res 1
val_T06: .res 1                 ; 4K read-back ($A5 expected)
val_T07: .res 1                 ; status snapshot
val_T08: .res 1
val_T09: .res 1
val_T10: .res 1
val_T11_a: .res 1               ; 1st status read
val_T11_b: .res 1               ; 2nd status read
val_T12: .res 1
val_T13: .res 1                 ; flip-flop test read-back
val_T18: .res 1                 ; T18 status snapshot ($D0 mid-SAT)
val_T19: .res 1                 ; T19 status snapshot (color-0 in 5S)
val_T20: .res 1                 ; T20 status snapshot (Y wraparound)
val_T14_lo: .res 1
val_T14_hi: .res 1
stress_drops_lo: .res 1
stress_drops_hi: .res 1
stress_frames_lo: .res 1
stress_frames_hi: .res 1

; ============================================================================
.segment "CODE"
; ============================================================================

; ============================================================================
; main: entry at $0280 (Wozmon `280R`) or $4000 (CodeTank lower jumper).
; ============================================================================
main:
        SEI

        ; --- Default all results to '?' so partial completion is visible -
        LDA     #'?'
        STA     res_T01
        STA     res_T02
        STA     res_T03
        STA     res_T04
        STA     res_T05
        STA     res_T06
        STA     res_T07
        STA     res_T08
        STA     res_T09
        STA     res_T10
        STA     res_T11
        STA     res_T12
        STA     res_T13
        STA     res_T14
        STA     res_T15
        STA     res_T16
        STA     res_T17
        STA     res_T18
        STA     res_T19
        STA     res_T20

        ; --- Banner + test plan on Apple-1 native display ----------------
        ; Each line prints immediately so even a later crash leaves a
        ; visible record of what was supposed to happen.
        JSR     do_cr
        LDA     #<banner1
        LDX     #>banner1
        JSR     print_string
        JSR     do_cr
        LDA     #<banner2
        LDX     #>banner2
        JSR     print_string
        JSR     do_cr
        JSR     do_cr

        ; --- Run all tests ------------------------------------------------
        LDA     #1
        JSR     print_test_header
        JSR     test_T01_slot_active_burst
        LDA     #1
        JSR     print_test_result

        LDA     #2
        JSR     print_test_header
        JSR     test_T02_vblank_free
        LDA     #2
        JSR     print_test_result

        LDA     #3
        JSR     print_test_header
        JSR     test_T03_blank_free
        LDA     #3
        JSR     print_test_result

        LDA     #4
        JSR     print_test_header
        JSR     test_T04_text_tight_burst
        LDA     #4
        JSR     print_test_result

        LDA     #5
        JSR     print_test_header
        JSR     test_T05_multicolor_tight_burst
        LDA     #5
        JSR     print_test_result

        LDA     #6
        JSR     print_test_header
        JSR     test_T06_R1_4K_16K
        LDA     #6
        JSR     print_test_result

        LDA     #7
        JSR     print_test_header
        JSR     test_T07_overscan_collision
        LDA     #7
        JSR     print_test_result

        LDA     #8
        JSR     print_test_header
        JSR     test_T08_status_bits_0_4
        LDA     #8
        JSR     print_test_result

        LDA     #9
        JSR     print_test_header
        JSR     test_T09_color0_collision
        LDA     #9
        JSR     print_test_result

        LDA     #10
        JSR     print_test_header
        JSR     test_T10_5S_first_occurrence
        LDA     #10
        JSR     print_test_result

        LDA     #11
        JSR     print_test_header
        JSR     test_T11_status_sticky
        LDA     #11
        JSR     print_test_result

        LDA     #12
        JSR     print_test_header
        JSR     test_T12_blank_sprite_scan
        LDA     #12
        JSR     print_test_result

        LDA     #13
        JSR     print_test_header
        JSR     test_T13_flipflop_reset
        LDA     #13
        JSR     print_test_result

        LDA     #14
        JSR     print_test_header
        JSR     test_T14_frame_rate
        LDA     #14
        JSR     print_test_result

        ; --- Interactive tests --------------------------------------------
        LDA     #15
        JSR     print_test_header
        JSR     test_T15_raster_split
        LDA     #15
        JSR     print_test_result

        LDA     #16
        JSR     print_test_header
        JSR     test_T16_illegal_mode_clone
        LDA     #16
        JSR     print_test_result

        LDA     #17
        JSR     print_test_header
        JSR     test_T17_mid_frame_rainbow
        LDA     #17
        JSR     print_test_result

        ; --- Auto silicon-edge-case tests --------------------------------
        LDA     #18
        JSR     print_test_header
        JSR     test_T18_terminator_stops
        LDA     #18
        JSR     print_test_result

        LDA     #19
        JSR     print_test_header
        JSR     test_T19_color0_in_5S
        LDA     #19
        JSR     print_test_result

        LDA     #20
        JSR     print_test_header
        JSR     test_T20_y_wraparound
        LDA     #20
        JSR     print_test_result

        ; --- Stress benchmark --------------------------------------------
        LDA     #<msg_stress
        LDX     #>msg_stress
        JSR     print_string
        JSR     stress_benchmark
        JSR     print_stress_result

        ; --- Visual demo (5 phases × ~5 sec) -----------------------------
        LDA     #<msg_demo
        LDX     #>msg_demo
        JSR     print_string
        JSR     final_demo

        ; --- Final summary block (re-prints all results) ------------------
        JSR     do_cr
        LDA     #<msg_summary_hdr
        LDX     #>msg_summary_hdr
        JSR     print_string
        JSR     do_cr
        LDA     #<msg_summary_hdr2
        LDX     #>msg_summary_hdr2
        JSR     print_string
        JSR     do_cr

        LDA     #1
@sloop:
        STA     loop_ctr
        JSR     print_test_summary_row
        LDA     loop_ctr
        CLC
        ADC     #1
        CMP     #21                 ; 20 tests + 1 (1..20)
        BCC     @sloop

        ; Final stress numbers
        LDA     #<msg_summary_stress
        LDX     #>msg_summary_stress
        JSR     print_string
        LDA     stress_drops_hi
        JSR     print_hex
        LDA     stress_drops_lo
        JSR     print_hex
        LDA     #<msg_summary_frames
        LDX     #>msg_summary_frames
        JSR     print_string
        LDA     stress_frames_hi
        JSR     print_hex
        LDA     stress_frames_lo
        JSR     print_hex
        JSR     do_cr
        JSR     do_cr
        LDA     #<msg_done
        LDX     #>msg_done
        JSR     print_string
        JSR     do_cr

@halt:  JMP     @halt

; ============================================================================
; print_test_header — print "T<NN> " prefix for the running test, with
; the test name. Called BEFORE the test runs so that an early crash
; still shows what was being attempted. A = test number (1..17).
; ============================================================================
print_test_header:
        STA     loop_ctr
        LDA     #'T'
        JSR     print_char
        LDA     loop_ctr
        JSR     print_dec2
        LDA     #' '
        JSR     print_char
        ; Print the test name from the name_table.
        LDX     loop_ctr
        DEX                     ; 0-based
        LDA     name_table_lo,X
        STA     str_lo
        LDA     name_table_hi,X
        STA     str_hi
        LDA     str_lo
        LDX     str_hi
        JSR     print_string
        ; Print "..." marker so observer sees "running" until result lands.
        LDA     #'.'
        JSR     print_char
        LDA     #'.'
        JSR     print_char
        LDA     #'.'
        JSR     print_char
        LDA     #' '
        JSR     print_char
        RTS

; ============================================================================
; print_test_result — print "<RESULT> <hi-hex><lo-hex>" + CR for the test
; whose number is in A (1..17).
; ============================================================================
print_test_result:
        STA     loop_ctr
        ; Result char (table_lo points to res_TNN slot; deref it)
        LDX     loop_ctr
        DEX
        LDA     res_table_lo,X
        STA     str_lo
        LDA     res_table_hi,X
        STA     str_hi
        LDY     #0
        LDA     (str_lo),Y
        JSR     print_char
        LDA     #' '
        JSR     print_char
        ; Numeric value: hi byte then lo byte
        LDX     loop_ctr
        DEX
        LDA     val_hi_table_lo,X
        STA     str_lo
        LDA     val_hi_table_hi,X
        STA     str_hi
        LDY     #0
        LDA     (str_lo),Y
        JSR     print_hex
        LDX     loop_ctr
        DEX
        LDA     val_lo_table_lo,X
        STA     str_lo
        LDA     val_lo_table_hi,X
        STA     str_hi
        LDY     #0
        LDA     (str_lo),Y
        JSR     print_hex
        JMP     do_cr

; ============================================================================
; print_test_summary_row — single-row "T<NN> <result> <hex>" for the
; final summary block. Doesn't print the test name. A in loop_ctr.
; ============================================================================
print_test_summary_row:
        LDA     #'T'
        JSR     print_char
        LDA     loop_ctr
        JSR     print_dec2
        LDA     #' '
        JSR     print_char
        LDX     loop_ctr
        DEX
        LDA     res_table_lo,X
        STA     str_lo
        LDA     res_table_hi,X
        STA     str_hi
        LDY     #0
        LDA     (str_lo),Y
        JSR     print_char
        LDA     #' '
        JSR     print_char
        LDX     loop_ctr
        DEX
        LDA     val_hi_table_lo,X
        STA     str_lo
        LDA     val_hi_table_hi,X
        STA     str_hi
        LDY     #0
        LDA     (str_lo),Y
        JSR     print_hex
        LDX     loop_ctr
        DEX
        LDA     val_lo_table_lo,X
        STA     str_lo
        LDA     val_lo_table_hi,X
        STA     str_hi
        LDY     #0
        LDA     (str_lo),Y
        JSR     print_hex
        JMP     do_cr

; ============================================================================
; print_stress_result — "STRESS DROPS=NNNN FRAMES=NNNN" + CR
; ============================================================================
print_stress_result:
        LDA     #' '
        JSR     print_char
        LDA     stress_drops_hi
        JSR     print_hex
        LDA     stress_drops_lo
        JSR     print_hex
        LDA     #' '
        JSR     print_char
        LDA     #'F'
        JSR     print_char
        LDA     #'='
        JSR     print_char
        LDA     stress_frames_hi
        JSR     print_hex
        LDA     stress_frames_lo
        JSR     print_hex
        JMP     do_cr

; ============================================================================
; --- Per-test implementations ----------------------------------------------
; All test logic kept in the silicon-strict-clean form (every back-to-
; back VDP store gapped via JSR tms9918_pad12).
; ============================================================================

; T01: slot-table active Mode 0+sprites burst — true Galaga-damiers pattern.
;
; Sequence:
;   1. Pre-fill VRAM $1000-$10FF with $00 in VBlank (silicon-safe).
;   2. Tight 4c-gap burst of $FF: 8× STA VDP_DATA back-to-back per
;      iteration, 32 iterations = 256 writes. Gap between consecutive
;      STAs = 4c (just the STA opcode itself), well below the silicon
;      worst-case Gfx12 = 7.5c → silicon WILL drop.
;   3. Verify: count $00 bytes (= dropped writes — they didn't land).
test_T01_slot_active_burst:
        JSR     setup_mode0_active_sprite

        ; --- Pre-fill $1000-$10FF with $00 (in VBlank, padded burst) ----
        JSR     wait_frame
        JSR     vram_addr_1000
        LDX     #0
        LDA     #$00
@pre:   STA     VDP_DATA
        JSR     tms9918_pad12
        INX
        BNE     @pre

        ; --- Tight burst at 4c gap, value = $FF -------------------------
        JSR     vram_addr_1000
        LDA     #$FF
        LDX     #32
@bw:    STA     VDP_DATA            ; 4c
        STA     VDP_DATA            ; 4c (gap=4c → silicon drops)
        STA     VDP_DATA
        STA     VDP_DATA
        STA     VDP_DATA
        STA     VDP_DATA
        STA     VDP_DATA
        STA     VDP_DATA            ; 8 STAs = 32c
        DEX                         ; 2c
        BNE     @bw                 ; 3c — gap inter-iter = 5c

        ; --- Verify: count $00 bytes (= drops) --------------------------
        JSR     vram_addr_1000_read
        LDA     #0
        STA     drop_lo
        STA     drop_hi
        LDX     #0
@vr:    LDA     VDP_DATA
        BNE     @ok                 ; got $FF (or other non-$00) → not dropped
        INC     drop_lo
        BNE     @ok
        INC     drop_hi
@ok:    INX
        BNE     @vr

        LDA     drop_lo
        STA     val_T01_lo
        LDA     drop_hi
        STA     val_T01_hi
        ORA     drop_lo
        BNE     @drops
        LDA     #'P'
        STA     res_T01
        RTS
@drops: LDA     #'D'
        STA     res_T01
        RTS

; T02: VBlank free-bandwidth burst.
test_T02_vblank_free:
        JSR     setup_mode0_active_sprite
        JSR     wait_frame      ; sync to VBlank entry
        JSR     vram_addr_1100
        LDX     #0
@bw:    TXA
        STA     VDP_DATA
        INX
        BNE     @bw
        JSR     vram_addr_1100_read
        LDA     #0
        STA     drop_lo
        STA     drop_hi
        LDX     #0
@vr:    LDA     VDP_DATA
        STX     loop_ctr
        CMP     loop_ctr
        BEQ     @ok
        INC     drop_lo
        BNE     @ok
        INC     drop_hi
@ok:    INX
        BNE     @vr
        LDA     drop_lo
        STA     val_T02_lo
        LDA     drop_hi
        STA     val_T02_hi
        ORA     drop_lo
        BNE     @bad
        LDA     #'P'
        STA     res_T02
        RTS
@bad:   LDA     #'F'
        STA     res_T02
        RTS

; T03: display-blanked free-bandwidth.
test_T03_blank_free:
        LDA     #$80            ; R1 = blanked, 16K
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12

        JSR     vram_addr_1200
        LDX     #0
@bw:    TXA
        STA     VDP_DATA
        INX
        BNE     @bw
        JSR     vram_addr_1200_read
        LDA     #0
        STA     drop_lo
        STA     drop_hi
        LDX     #0
@vr:    LDA     VDP_DATA
        STX     loop_ctr
        CMP     loop_ctr
        BEQ     @ok
        INC     drop_lo
        BNE     @ok
        INC     drop_hi
@ok:    INX
        BNE     @vr
        LDA     drop_lo
        STA     val_T03_lo
        LDA     drop_hi
        STA     val_T03_hi
        ORA     drop_lo
        BNE     @bad
        LDA     #'P'
        STA     res_T03
        RTS
@bad:   LDA     #'F'
        STA     res_T03
        RTS

; T04: text-mode tight burst.
test_T04_text_tight_burst:
        LDA     #$D0            ; R1 = display on, M1=1 (text), 16K
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     vram_addr_1300
        LDX     #0
@bw:    TXA
        STA     VDP_DATA
        INX
        BNE     @bw
        JSR     vram_addr_1300_read
        LDA     #0
        STA     drop_lo
        STA     drop_hi
        LDX     #0
@vr:    LDA     VDP_DATA
        STX     loop_ctr
        CMP     loop_ctr
        BEQ     @ok
        INC     drop_lo
        BNE     @ok
        INC     drop_hi
@ok:    INX
        BNE     @vr
        LDA     drop_lo
        STA     val_T04_lo
        LDA     drop_hi
        STA     val_T04_hi
        ORA     drop_lo
        BNE     @drops
        LDA     #'P'
        STA     res_T04
        RTS
@drops: LDA     #'D'
        STA     res_T04
        RTS

; T05: multicolor tight burst.
test_T05_multicolor_tight_burst:
        LDA     #$C8            ; R1 = display on, M2=1 (multicolor), 16K
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     vram_addr_1400
        LDX     #0
@bw:    TXA
        STA     VDP_DATA
        INX
        BNE     @bw
        JSR     vram_addr_1400_read
        LDA     #0
        STA     drop_lo
        STA     drop_hi
        LDX     #0
@vr:    LDA     VDP_DATA
        STX     loop_ctr
        CMP     loop_ctr
        BEQ     @ok
        INC     drop_lo
        BNE     @ok
        INC     drop_hi
@ok:    INX
        BNE     @vr
        LDA     drop_lo
        STA     val_T05_lo
        LDA     drop_hi
        STA     val_T05_hi
        ORA     drop_lo
        BNE     @drops
        LDA     #'P'
        STA     res_T05
        RTS
@drops: LDA     #'D'
        STA     res_T05
        RTS

; T06: R1 bit 7 (4K vs 16K) addressing.
test_T06_R1_4K_16K:
        LDA     #$40            ; R1 = display on, R1.7=0 (4K mode)
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12
        ; Write $A5 at $1000 → 4K-mode silicon writes at $0000 (high
        ; bits truncated).
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$50
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$A5
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Read $0000 — should be $A5 if 4K honoured.
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     VDP_DATA
        STA     val_T06
        CMP     #$A5
        BNE     @no
        LDA     #'Y'
        STA     res_T06
        RTS
@no:    LDA     #'N'
        STA     res_T06
        RTS

; T07: overscan collision.
test_T07_overscan_collision:
        JSR     setup_mode0_no_sprite
        JSR     fill_pat0_FF
        JSR     vram_addr_sat0
        ; SAT[0..1] = early-clock + colour 15, X=10 → real X=-22
        LDX     #2
@s:     LDA     #49
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #10
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$8F
        STA     VDP_DATA
        JSR     tms9918_pad12
        DEX
        BNE     @s
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     delay_frame    ; wait full frame WITHOUT polling status
        ;                          (preserves bits 5/6/7 latched by per-scanline scan)
        LDA     VDP_CTRL
        STA     val_T07
        AND     #$20
        BEQ     @no
        LDA     #'Y'
        STA     res_T07
        RTS
@no:    LDA     #'N'
        STA     res_T07
        RTS

; T08: status bits 0..4 = last sprite scanned.
test_T08_status_bits_0_4:
        JSR     setup_mode0_no_sprite
        JSR     vram_addr_sat0
        LDX     #0
@s:     LDA     #49
        STA     VDP_DATA
        JSR     tms9918_pad12
        TXA
        ASL
        ASL
        ASL
        ASL
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12
        INX
        CPX     #4
        BNE     @s
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     delay_frame    ; wait full frame WITHOUT polling status
        ;                          (preserves bits 5/6/7 latched by per-scanline scan)
        LDA     VDP_CTRL
        STA     val_T08
        TAX
        AND     #$40
        BNE     @over
        TXA
        AND     #$1F
        CMP     #4
        BNE     @wrong
        LDA     #'Y'
        STA     res_T08
        RTS
@over:  LDA     #'F'
        STA     res_T08
        RTS
@wrong: LDA     #'N'
        STA     res_T08
        RTS

; T09: color-0 (transparent) sprite collision.
test_T09_color0_collision:
        JSR     setup_mode0_no_sprite
        JSR     fill_pat0_FF
        JSR     vram_addr_sat0
        LDX     #2
@s:     LDA     #49
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #80
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$00            ; transparent (colour 0)
        STA     VDP_DATA
        JSR     tms9918_pad12
        DEX
        BNE     @s
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     delay_frame    ; wait full frame WITHOUT polling status
        ;                          (preserves bits 5/6/7 latched by per-scanline scan)
        LDA     VDP_CTRL
        STA     val_T09
        AND     #$20
        BEQ     @no
        LDA     #'Y'
        STA     res_T09
        RTS
@no:    LDA     #'N'
        STA     res_T09
        RTS

; T10: 5S latch first-occurrence.
test_T10_5S_first_occurrence:
        JSR     setup_mode0_no_sprite
        JSR     vram_addr_sat0
        LDA     #0
        STA     loop_ctr
@l:
        LDX     loop_ctr
        CPX     #5
        BCS     @grp2
        LDA     #49
        JMP     @yd
@grp2:  LDA     #99
@yd:    STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     loop_ctr
        CMP     #5
        BCC     @keep
        SBC     #5
@keep:  ASL
        ASL
        ASL
        ASL
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12
        INC     loop_ctr
        LDA     loop_ctr
        CMP     #10
        BNE     @l
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     delay_frame    ; wait full frame WITHOUT polling status
        ;                          (preserves bits 5/6/7 latched by per-scanline scan)
        LDA     VDP_CTRL
        STA     val_T10
        TAX
        AND     #$40
        BEQ     @no
        TXA
        AND     #$1F
        CMP     #4
        BNE     @wrong
        LDA     #'Y'
        STA     res_T10
        RTS
@no:    LDA     #'N'
        STA     res_T10
        RTS
@wrong: LDA     #'?'
        STA     res_T10
        RTS

; T11: status sticky-until-readControl.
test_T11_status_sticky:
        JSR     setup_mode0_no_sprite
        JSR     fill_pat0_FF
        JSR     vram_addr_sat0
        LDX     #2
@s:     LDA     #49
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #80
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12
        DEX
        BNE     @s
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     delay_frame    ; wait full frame WITHOUT polling status
        ;                          (preserves bits 5/6/7 latched by per-scanline scan)
        LDA     VDP_CTRL
        STA     val_T11_a       ; first read (should have bit 5 set)
        AND     #$20
        BEQ     @no
        LDA     VDP_CTRL
        STA     val_T11_b       ; second read (should have bit 5 clear)
        AND     #$20
        BNE     @no
        LDA     #'Y'
        STA     res_T11
        RTS
@no:    LDA     #'N'
        STA     res_T11
        RTS

; T12: sprite engine in display blank — KEY OPEN QUESTION.
;
; Setup: BLANK display FIRST (R1.6=0 before any SAT writes), then write
; the SAT entries with collision-causing sprites. With display blanked
; the WHOLE TIME, no per-scanline scan can fire — the only way bit 5
; ends up set is if silicon scans during blank.
;
; Previous implementation set up SAT first (display ON) then blanked,
; which gave the per-scanline scan ~60 cycles to latch bit 5 with the
; collision sprites in place — false positive. This ordering eliminates
; that race.
test_T12_blank_sprite_scan:
        ; --- Configure registers with display ALREADY blanked ---------
        ; R1 = $80 — 16K, display OFF, no sprites mag, no IRQ
        LDA     #$80
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12
        ; R0 = 0 (Mode 0)
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$80
        STA     VDP_CTRL
        JSR     tms9918_pad12
        ; R5 = $36 (SAT @ $1B00)
        LDA     #$36
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$85
        STA     VDP_CTRL
        JSR     tms9918_pad12
        ; R6 = 0 (sprite pattern @ $0000)
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$86
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; Pattern 0 = solid $FF (collision-friendly).
        JSR     fill_pat0_FF

        ; SAT setup — display still blanked, no scan can fire here.
        JSR     vram_addr_sat0
        LDX     #2
@s:     LDA     #79
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #100
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12
        DEX
        BNE     @s
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12

        ; Clear stale flags. Display still blanked.
        LDA     VDP_CTRL
        JSR     tms9918_pad12

        ; Wait one frame, display blanked the WHOLE time.
        JSR     delay_frame

        ; Read status. bit 5 set ⇒ silicon DOES scan during blank.
        LDA     VDP_CTRL
        STA     val_T12
        AND     #$20
        BEQ     @no
        LDA     #'Y'
        STA     res_T12
        RTS
@no:    LDA     #'N'
        STA     res_T12
        RTS

; T13: flip-flop reset on readControl.
test_T13_flipflop_reset:
        JSR     setup_mode0_no_sprite
        ; Step 1: orphan 1st-byte
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        ; Step 2: status read — silicon resets the flip-flop
        LDA     VDP_CTRL
        JSR     tms9918_pad12
        ; Step 3: clean 2-byte sequence to set write addr = $1000
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$50
        STA     VDP_CTRL
        JSR     tms9918_pad12
        ; Step 4: write $A5
        LDA     #$A5
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Step 5: read back. After set-read-addr, the chip prefetched
        ; vram[$1000] into the read-ahead buffer. The first LDA VDP_DATA
        ; returns that prefetched byte (= $A5 if step 3-4 worked,
        ; bypassing the abandoned step 1 thanks to the flip-flop reset
        ; in step 2). Reading TWICE returns vram[$1001] which is junk —
        ; SINGLE read is correct.
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$10
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     VDP_DATA
        STA     val_T13
        CMP     #$A5
        BEQ     @yes
        LDA     #'N'
        STA     res_T13
        RTS
@yes:   LDA     #'Y'
        STA     res_T13
        RTS

; T14: frame-rate cycle measurement (silicon ≈ 17062 c/frame).
;
; Trick: a single wait_frame can exit at any point in the frame because
; F may be sticky-set from a previous test. Two consecutive wait_frames
; deterministically pin the second exit to the next active→VBlank
; transition (= frameCycle ~12500). From there, the inner counter
; measures exactly one frame to the next F-rise. Each iter is 12 cycles
; (INX + BNE + LDA + BPL); 17062 / 12 ≈ 1422 ≈ $058E.
test_T14_frame_rate:
        LDA     #$C0
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     wait_frame              ; clear sticky F
        JSR     wait_frame              ; deterministic sync to next F-rise
        LDX     #0
        LDY     #0
@inner: INX
        BNE     @check
        INY
@check: LDA     VDP_CTRL
        BPL     @inner
        STX     val_T14_lo
        STY     val_T14_hi
        LDA     #'M'
        STA     res_T14
        RTS

; T15: raster-split 5S — INTERACTIVE. Visual on TMS9918, prompt+answer
;      via Apple-1 keyboard.
test_T15_raster_split:
        JSR     setup_mode0_no_sprite
        JSR     fill_pat0_FF
        JSR     vram_addr_sat0
        LDX     #0
@s:     LDA     #94
        STA     VDP_DATA
        JSR     tms9918_pad12
        TXA
        ASL
        ASL
        ASL
        ASL
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12
        INX
        CPX     #5
        BNE     @s
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12

        LDA     #<q_T15
        LDX     #>q_T15
        JSR     print_string

        ; --- Initial deterministic sync (once before the loop) ---------
        ; wait_frame ×2: first clears any sticky F, second pins us to
        ; the next F-rise (= VBlank entry of frame M).
        JSR     wait_frame
        JSR     wait_frame

        ; --- Per-iter animation: complete in ONE frame, then sync ----
        ; wait_frame at END of iter = exactly 1 frame per iter (vs 2
        ; in the wait_frame-at-start version which produced 30 Hz blink).
        ; Each frame gets the split → no flicker.
        ;
        ; Bright contrasting colours: $05 = light blue (top), $0D =
        ; magenta (bottom) — easy for the observer to see the split.
@frame:
        ; At VBlank entry of frame M. R7 set here applies to lines 0+ of M+1.
        LDA     #$05
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; Clear any stale bit 6 from a previous frame.
        LDA     VDP_CTRL
        JSR     tms9918_pad12

        ; Spin-poll bit 6 (16-bit ctr ~71000c — well over a frame).
        ; The 5S latches at line 95 of M+1, ~10750c after start of poll.
        LDX     #0
        LDY     #20
@poll:  LDA     VDP_CTRL
        AND     #$40
        BNE     @split
        DEX
        BNE     @poll
        DEY
        BNE     @poll
        JMP     @sync                   ; timeout — no split this frame

@split:
        ; 5S armed → raster at line 95 of M+1. Switch to magenta.
        LDA     #$0D
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12

@sync:
        ; Wait for next F-rise (= VBlank entry of M+1). Per-scanline
        ; render captures R7=$0D for lines 95..191. Iter time = 1 frame.
        JSR     wait_frame

        ; Keyboard poll between frames — bands persist while operator
        ; decides their answer.
        LDA     KBDCR
        BPL     @frame
        LDA     KBD
        AND     #$7F
        CMP     #'Y'
        BEQ     @y
        CMP     #'y'
        BEQ     @y
        CMP     #'N'
        BEQ     @n
        CMP     #'n'
        BEQ     @n
        JMP     @frame

@y:     LDA     #'Y'
        JSR     print_char
        JSR     do_cr
        LDA     #'Y'
        STA     res_T15
        RTS
@n:     LDA     #'N'
        JSR     print_char
        JSR     do_cr
        LDA     #'N'
        STA     res_T15
        RTS

; T16: illegal-mode sprite cloning — INTERACTIVE.
test_T16_illegal_mode_clone:
        ; R0 = $02 (M3=1)
        LDA     #$02
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$80
        STA     VDP_CTRL
        JSR     tms9918_pad12
        ; R1 = $D8 (display on, M1+M2 = illegal)
        LDA     #$D8
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     fill_pat0_FF
        JSR     vram_addr_sat0
        LDX     #0
@s:     LDA     #69
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #80
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12
        INX
        CPX     #4
        BNE     @s
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDX     #120
@h:     JSR     wait_frame
        DEX
        BNE     @h
        LDA     #<q_T16
        LDX     #>q_T16
        JSR     print_string
        JSR     wait_yn
        STA     res_T16
        RTS

; T17: mid-frame R7 rainbow — INTERACTIVE.
test_T17_mid_frame_rainbow:
        JSR     setup_mode0_no_sprite

        LDA     #<q_T17
        LDX     #>q_T17
        JSR     print_string

        ; --- Initial deterministic sync (once before the loop) ---------
        JSR     wait_frame              ; clear sticky F
        JSR     wait_frame              ; sync to next F-rise

        ; --- Per-iter animation in ONE frame --------------------------
        ; wait_frame at END of iter (not at start) gives exactly 1 frame
        ; per iter → animation happens every frame → no blink.
        ;
        ; Bright colours: $05 = light blue (top), $0B = light yellow
        ; (bottom).
@frame:
        ; At VBlank entry of frame M. R7=$05 applies to M+1 lines 0+.
        LDA     #$05
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; Delay through VBlank + into line ~96 of M+1 (~11520c).
        LDX     #0
        LDY     #9
@d1:    DEX
        BNE     @d1
        DEY
        BNE     @d1

        ; Mid-frame R7=$0B (light yellow) applies to lines 96..191.
        LDA     #$0B
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; Wait for next F-rise (= VBlank entry of M+1). The remaining
        ; ~6300c of the active display + render capture lines 96..191
        ; with R7=$0B before we land at the next iter's @frame top.
        JSR     wait_frame

        ; Keyboard poll between frames.
        LDA     KBDCR
        BPL     @frame
        LDA     KBD
        AND     #$7F
        CMP     #'Y'
        BEQ     @y
        CMP     #'y'
        BEQ     @y
        CMP     #'N'
        BEQ     @n
        CMP     #'n'
        BEQ     @n
        JMP     @frame

@y:     LDA     #'Y'
        JSR     print_char
        JSR     do_cr
        LDA     #'Y'
        STA     res_T17
        RTS
@n:     LDA     #'N'
        JSR     print_char
        JSR     do_cr
        LDA     #'N'
        STA     res_T17
        RTS

; ============================================================================
; T18 — $D0 mid-SAT stops scan.
;
; SAT layout: 4 visible sprites at Y=50 (slots 0..3), $D0 terminator at
; SAT[4], 5 MORE sprites at Y=50 (slots 5..9). Silicon stops scanning at
; the first $D0, so only 4 sprites are seen by the scanline scan — bit 6
; (5S overflow) MUST stay 0. Confirms terminator semantics.
;
; PASS ('Y') = bit 6 = 0 (no 5S latched), bits 0..4 = 4 (terminator slot).
; FAIL ('N') = bit 6 = 1 (silicon scanned past $D0).
; ============================================================================
test_T18_terminator_stops:
        JSR     setup_mode0_no_sprite
        JSR     fill_pat0_FF
        JSR     vram_addr_sat0
        ; Slots 0..3: Y=49, X=i*16, name=0, color=$0F
        LDX     #0
@s1:    LDA     #49
        STA     VDP_DATA
        JSR     tms9918_pad12
        TXA
        ASL
        ASL
        ASL
        ASL
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12
        INX
        CPX     #4
        BNE     @s1
        ; SAT[4] = $D0 (terminator)
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Plus a 4-byte filler (X, name, color) — silicon ignores these
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        STA     VDP_DATA
        JSR     tms9918_pad12
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Slots 5..9 — silicon should ignore (past first $D0)
        LDX     #0
@s2:    LDA     #49
        STA     VDP_DATA
        JSR     tms9918_pad12
        TXA
        ASL
        ASL
        ASL
        ASL
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12
        INX
        CPX     #5
        BNE     @s2
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     delay_frame
        LDA     VDP_CTRL
        STA     val_T18
        TAX
        AND     #$40
        BNE     @over
        TXA
        AND     #$1F
        CMP     #4
        BNE     @wrong
        LDA     #'Y'
        STA     res_T18
        RTS
@over:  LDA     #'N'
        STA     res_T18
        RTS
@wrong: LDA     #'?'
        STA     res_T18
        RTS

; ============================================================================
; T19 — Color-0 sprite counts in 5S overflow detection.
;
; 5 sprites at Y=50, slots 0..3 with color=$0F (visible), slot 4 with
; color=$00 (transparent — invisible but still scanned). Silicon counts
; ALL 5 sprites toward the 5S overflow → bit 6 latches with bits 0..4 = 4.
; Confirms Nouspikel's "scan counts color-0 sprites".
;
; PASS ('Y') = bit 6 set + bits 0..4 = 4.
; ============================================================================
test_T19_color0_in_5S:
        JSR     setup_mode0_no_sprite
        JSR     fill_pat0_FF
        JSR     vram_addr_sat0
        LDX     #0
@s:     LDA     #49
        STA     VDP_DATA
        JSR     tms9918_pad12
        TXA
        ASL
        ASL
        ASL
        ASL
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; color: $0F for slots 0..3, $00 for slot 4
        TXA
        CMP     #4
        BEQ     @c0
        LDA     #$0F
        JMP     @cd
@c0:    LDA     #$00
@cd:    STA     VDP_DATA
        JSR     tms9918_pad12
        INX
        CPX     #5
        BNE     @s
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     delay_frame
        LDA     VDP_CTRL
        STA     val_T19
        TAX
        AND     #$40
        BEQ     @no
        TXA
        AND     #$1F
        CMP     #4
        BNE     @wrong
        LDA     #'Y'
        STA     res_T19
        RTS
@no:    LDA     #'N'
        STA     res_T19
        RTS
@wrong: LDA     #'?'
        STA     res_T19
        RTS

; ============================================================================
; T20 — Sprite Y wraparound (yRaw > $D0 means line < 1).
;
; Y in SAT byte 0: yRaw < $D0 ⇒ line = yRaw + 1; yRaw > $D0 ⇒ line =
; yRaw - 256 + 1 (negative-ish). Silicon: Y=$FF ⇒ line 0; Y=$00 ⇒ line 1.
; A single 8-pixel-tall opaque sprite at Y=$FF spans lines 0..7. A
; second sprite at Y=$00 spans lines 1..8. Both at same X with $FF
; pattern → collision on lines 1..7 (overlap of 7 lines).
;
; PASS ('Y') = bit 5 (collision) set after one frame.
; ============================================================================
test_T20_y_wraparound:
        JSR     setup_mode0_no_sprite
        JSR     fill_pat0_FF
        JSR     vram_addr_sat0
        ; SAT[0] = (Y=$FF, X=80, name=0, color=$0F)
        LDA     #$FF
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #80
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; SAT[1] = (Y=$00, X=80, name=0, color=$0F)
        LDA     #$00
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #80
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Terminator (note: Y=$D0 specifically — $FF and $00 are NOT $D0)
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     delay_frame
        LDA     VDP_CTRL
        STA     val_T20
        AND     #$20
        BEQ     @no
        LDA     #'Y'
        STA     res_T20
        RTS
@no:    LDA     #'N'
        STA     res_T20
        RTS

; ============================================================================
; stress_benchmark — ~30 sec sprite-multiplexing burst stress.
; Runs ~1800 frames of Galaga-class no-pad SAT writes; tracks 16-bit
; drop count + 16-bit frame count.
; ============================================================================
stress_benchmark:
        JSR     setup_mode0_active_sprite
        LDA     #0
        STA     stress_drops_lo
        STA     stress_drops_hi
        STA     stress_frames_lo
        STA     stress_frames_hi
@frame:
        JSR     wait_frame
        ; Pre-fill with $00 (silicon-safe padded burst in VBlank).
        JSR     vram_addr_1500
        LDX     #0
        LDA     #$00
@pre:   STA     VDP_DATA
        JSR     tms9918_pad12
        INX
        BNE     @pre

        ; Tight 4c burst of $FF in active display (after VBlank ends).
        JSR     vram_addr_1500
        LDA     #$FF
        LDX     #32
@bw:    STA     VDP_DATA
        STA     VDP_DATA
        STA     VDP_DATA
        STA     VDP_DATA
        STA     VDP_DATA
        STA     VDP_DATA
        STA     VDP_DATA
        STA     VDP_DATA
        DEX
        BNE     @bw

        ; Verify: count $00 bytes = drops.
        JSR     vram_addr_1500_read
        LDX     #0
@vr:    LDA     VDP_DATA
        BNE     @ok
        INC     stress_drops_lo
        BNE     @ok
        INC     stress_drops_hi
@ok:    INX
        BNE     @vr

        INC     stress_frames_lo
        BNE     @nh
        INC     stress_frames_hi
@nh:
        LDA     stress_frames_hi
        CMP     #$07
        BCC     @frame
        LDA     stress_frames_lo
        CMP     #$08
        BCC     @frame
        RTS

; ============================================================================
; final_demo — animated visual sanity-check using real fauna sprite
; patterns from dev/lib/tms9918/sprites_fauna.asm. Five short phases, each
; testing a different silicon-relevant scenario:
;
;   Phase 1 (~5s) — 4 sprites bouncing horizontally (silicon-clean motion).
;   Phase 2 (~5s) — 5 sprites at same Y to trigger the 4-per-line / 5S
;                   sprite-multiplexing flicker (visible drop-out).
;   Phase 3 (~5s) — 2 sprites converging head-on; on collision (bit 5),
;                   backdrop flashes red — confirms collision latch
;                   feeding game logic.
;   Phase 4 (~5s) — Rainbow background (mid-frame R7 split) with 4 sprites
;                   bouncing across the colour boundary.
;   Phase 5 (~5s) — Final tableau: 4 sprites at fixed positions, backdrop
;                   green, no animation. Stable end frame.
;
; Operator runs the same binary on POM1 strict and on silicon, watches
; the TMS9918 monitor side-by-side, and notes any visual divergence.
; ============================================================================
final_demo:
        ; Configure 16x16 sprites (R1.1=1), display ON, 16K, no IRQ.
        ; R0 = 0 (Mode 0). R5 = $36 (SAT @ $1B00). R6 = 0 (sprite pat @ $0000).
        ; R7 = $04 (dark blue backdrop).
        LDA     #$C2
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$80
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$36
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$85
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$86
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$04
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; Upload 5 fauna patterns (5 × 32 bytes = 160 bytes) at VRAM $0000.
        ; In 16x16 mode, sprite name=N×4 reads patterns at offset N×32.
        ;   name=0  → dog
        ;   name=4  → cat
        ;   name=8  → rabbit
        ;   name=12 → spider
        ;   name=16 → snake
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$40
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #<fauna_dog_pat
        STA     src_lo
        LDA     #>fauna_dog_pat
        STA     src_hi
        JSR     copy_pat_32
        LDA     #<fauna_cat_pat
        STA     src_lo
        LDA     #>fauna_cat_pat
        STA     src_hi
        JSR     copy_pat_32
        LDA     #<fauna_rabbit_pat
        STA     src_lo
        LDA     #>fauna_rabbit_pat
        STA     src_hi
        JSR     copy_pat_32
        LDA     #<fauna_spider_pat
        STA     src_lo
        LDA     #>fauna_spider_pat
        STA     src_hi
        JSR     copy_pat_32
        LDA     #<fauna_snake_pat
        STA     src_lo
        LDA     #>fauna_snake_pat
        STA     src_hi
        JSR     copy_pat_32

        ; --- Run the 6 phases in sequence -------------------------------
        JSR     demo_phase1_bouncing
        JSR     demo_phase2_5S_flicker
        JSR     demo_phase3_collision
        JSR     demo_phase4_rainbow
        JSR     demo_phase6_32x32_composite
        JSR     demo_phase5_tableau
        RTS

; --- copy_pat_32: stream 32 bytes from (src_lo,src_hi) into VDP_DATA ---
copy_pat_32:
        LDY     #0
@l:     LDA     (src_lo),Y
        STA     VDP_DATA
        JSR     tms9918_pad12
        INY
        CPY     #32
        BNE     @l
        RTS

; --- demo_vram_addr_sat: set VRAM write addr to SAT @ $1B00 ----------
demo_vram_addr_sat:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$5B
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

; --- write_sat_entry: A=Y, X=X, Y=name, then color $0F ---------------
; (Awkward 6502 register usage — caller passes via tmp slots.)
; Helper not used; SAT entries written inline in each phase.

; ============================================================================
; demo_phase1_bouncing — 4 sprites bouncing horizontally for ~5 sec.
; Each sprite has a different Y and moves with a different phase.
; Used to confirm clean per-frame sprite rendering on both targets.
; ============================================================================
demo_phase1_bouncing:
        LDA     #0
        STA     demo_frame_lo
        STA     demo_frame_hi
@frame:
        JSR     demo_vram_addr_sat
        ; Sprite 0 — dog, name=0, Y=30, X = frame_lo & $7F
        LDA     #29
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     demo_frame_lo
        AND     #$7F
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F                    ; white
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Sprite 1 — cat, name=4, Y=70, X = ($FF - frame_lo) & $7F + 64
        LDA     #69
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     demo_frame_lo
        EOR     #$FF
        AND     #$7F
        CLC
        ADC     #64
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #4
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$08                    ; medium red
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Sprite 2 — rabbit, name=8, Y=110, X via different offset
        LDA     #109
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     demo_frame_lo
        CLC
        ADC     #$40
        AND     #$7F
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #8
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0B                    ; light yellow
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Sprite 3 — spider, name=12, Y=150
        LDA     #149
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     demo_frame_lo
        EOR     #$FF
        CLC
        ADC     #$20
        AND     #$7F
        CLC
        ADC     #16
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #12
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0E                    ; grey
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Terminator
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12

        JSR     wait_frame
        INC     demo_frame_lo
        BNE     @nh
        INC     demo_frame_hi
@nh:    LDA     demo_frame_hi
        BNE     @done
        LDA     demo_frame_lo
        CMP     #200                    ; ~3.3 sec at 60 Hz
        BCS     @done
        JMP     @frame
@done:  RTS

; ============================================================================
; demo_phase2_5S_flicker — 5 sprites at same Y (50). Silicon: only the
; first 4 by SAT order render per scanline → 5th drops/flickers. The
; sprite engine arms 5S each frame and authentic silicon shows visible
; flicker on the 5th sprite.
; ============================================================================
demo_phase2_5S_flicker:
        LDA     #0
        STA     demo_frame_lo
        STA     demo_frame_hi
@frame:
        JSR     demo_vram_addr_sat
        ; Five sprites at Y=80, X spread evenly across screen
        LDX     #0
@s:     LDA     #79
        STA     VDP_DATA
        JSR     tms9918_pad12
        TXA
        ASL                             ; X*2
        ASL                             ; X*4
        ASL                             ; X*8
        ASL                             ; X*16 = X positions 0..64
        CLC
        ADC     #32                     ; offset start to leave left margin
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Sprite name in 16x16 mode must be a multiple of 4 (each
        ; sprite consumes 4 patterns = 32 bytes). 5 patterns uploaded
        ; at $0000-$009F → valid names 0, 4, 8, 12, 16. Using X*16
        ; (= 0, 16, 32, 48, 64) reaches uninitialised VRAM and renders
        ; random "ghost" shapes — which is what we DON'T want here.
        ; Use X*4 instead.
        TXA
        ASL                             ; X*2
        ASL                             ; X*4 — 0, 4, 8, 12, 16
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Color: rotate 4 colours
        TXA
        AND     #$03
        CLC
        ADC     #6                      ; 6,7,8,9 → red, cyan, red-light
        STA     VDP_DATA
        JSR     tms9918_pad12
        INX
        CPX     #5
        BNE     @s
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12

        JSR     wait_frame
        INC     demo_frame_lo
        BNE     @nh2
        INC     demo_frame_hi
@nh2:   LDA     demo_frame_hi
        BNE     @done2
        LDA     demo_frame_lo
        CMP     #200
        BCS     @done2
        JMP     @frame
@done2: RTS

; ============================================================================
; demo_phase3_collision — 2 sprites converge head-on. When the per-
; scanline scan latches collision (bit 5), backdrop flashes red.
; ============================================================================
demo_phase3_collision:
        LDA     #0
        STA     demo_frame_lo
        STA     demo_frame_hi
        STA     demo_collide
@frame:
        ; Read status — clears bit 5 sticky from previous frame.
        LDA     VDP_CTRL
        JSR     tms9918_pad12
        AND     #$20
        BEQ     @no_coll
        ; Collision — store flag for next frame
        LDA     #$01
        STA     demo_collide
@no_coll:

        ; Backdrop: red if collision was latched, blue otherwise.
        LDA     demo_collide
        BEQ     @bg_blue
        LDA     #$06
        JMP     @bg_set
@bg_blue:
        LDA     #$04
@bg_set:
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; Reset collide latch for next iter (only flash 1 frame at a time)
        LDA     #0
        STA     demo_collide

        ; SAT: dog sprite moving right (X = frame_lo % 200)
        ;      cat sprite moving left  (X = 200 - frame_lo % 200)
        JSR     demo_vram_addr_sat
        LDA     #79
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     demo_frame_lo
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0                      ; dog
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Cat
        LDA     #79
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #200
        SEC
        SBC     demo_frame_lo
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #4                      ; cat
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0A
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12

        JSR     wait_frame
        INC     demo_frame_lo
        LDA     demo_frame_lo
        CMP     #200
        BCS     @done3
        JMP     @frame
@done3: RTS

; ============================================================================
; demo_phase4_rainbow — mid-frame R7 split (top blue, bottom green) with
; 3 sprites bouncing across the boundary. Silicon: per-scanline render
; produces visible split with sprite passing through. POM1 strict
; reproduces this via the per-scanline render pipeline.
; ============================================================================
demo_phase4_rainbow:
        LDA     #0
        STA     demo_frame_lo
@frame:
        ; Initial sync — but to keep timing stable, do wait_frame ×2 on
        ; first iter only. Subsequent iters use exact CPU-cycle timing.
        ; Set R7 = $05 (light blue) at iter top
        LDA     #$05
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     wait_frame              ; sync to VBlank entry

        ; Delay ~10000c → reach line ~96 mid-screen
        LDX     #0
        LDY     #8
@d:     DEX
        BNE     @d
        DEY
        BNE     @d

        ; Mid-frame R7 = $03 (light green)
        LDA     #$03
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; SAT: 3 sprites bouncing across the colour boundary (Y range 40-150)
        JSR     demo_vram_addr_sat
        ; Sprite 0 — rabbit, Y bouncing
        LDA     demo_frame_lo
        AND     #$3F
        CLC
        ADC     #40
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #80
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #8                      ; rabbit
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Sprite 1 — snake, opposite phase
        LDA     demo_frame_lo
        EOR     #$FF
        AND     #$3F
        CLC
        ADC     #40
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #150
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #16                     ; snake
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0D                    ; magenta
        STA     VDP_DATA
        JSR     tms9918_pad12
        ; Sprite 2 — spider, mid-screen drift
        LDA     demo_frame_lo
        AND     #$1F
        CLC
        ADC     #80
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #120
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #12                     ; spider
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0B                    ; light yellow
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12

        INC     demo_frame_lo
        LDA     demo_frame_lo
        CMP     #150
        BCS     @done4
        JMP     @frame
@done4: RTS

; ============================================================================
; demo_phase6_32x32_composite — 4×16x16 sprites positioned in a 2×2 grid
; to compose a single 32×32 visual sprite, the technique used by TMS_Rogue
; for its boss sprites and big creatures. Tests:
;   - 4 sprites at the same scanline range (no 5S overflow if R1.6=1
;     and bit 6 stays clear).
;   - All 4 slots consumed → no other sprite can render on those lines.
;   - 16x16 mode pattern banking (name=0,4,8,12 each pointing to its
;     own 32-byte pattern bank) producing a coherent 32×32 image.
;
; Animation: the 4-sprite composite slides horizontally across the screen
; for ~5 seconds, demonstrating the technique on POM1 strict and on
; silicon side-by-side. Visible coherence of the 32×32 image confirms:
;   - sprite priority/order (slot 0 NW corner, slot 3 SE corner)
;   - 4-sprites-per-scanline limit not exceeded
;   - per-scanline render produces stable composite image
;
; If silicon shows tearing or sprite drop-out where POM1 shows clean
; coherence, one of the silicon-faithful behaviours diverges.
; ============================================================================
demo_phase6_32x32_composite:
        ; Backdrop = $04 dark blue for contrast.
        LDA     #$04
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12

        LDA     #0
        STA     demo_frame_lo
@frame:
        ; Compute base X for the composite (slides left→right, wraps).
        ; X_base = demo_frame_lo & $7F + 16 (= 16..143)
        LDA     demo_frame_lo
        AND     #$7F
        CLC
        ADC     #16
        STA     loop_ctr            ; X_base parked here

        JSR     demo_vram_addr_sat

        ; --- Slot 0 — NW (top-left) — name = 0 (dog) -------------------
        LDA     #69                 ; Y=70 raw 69 (silicon adds +1)
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     loop_ctr            ; X = X_base
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0                  ; name = 0 (dog → top-left quadrant)
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F                ; white
        STA     VDP_DATA
        JSR     tms9918_pad12

        ; --- Slot 1 — NE (top-right) — X = X_base + 16, Y = same ------
        LDA     #69
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     loop_ctr
        CLC
        ADC     #16
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #4                  ; name = 4 (cat)
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12

        ; --- Slot 2 — SW (bottom-left) — Y = Y_base + 16 --------------
        LDA     #85                 ; Y_base+16 raw
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     loop_ctr
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #8                  ; name = 8 (rabbit)
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12

        ; --- Slot 3 — SE (bottom-right) -------------------------------
        LDA     #85
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     loop_ctr
        CLC
        ADC     #16
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #12                 ; name = 12 (spider)
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12

        ; Terminator
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12

        JSR     wait_frame
        INC     demo_frame_lo
        LDA     demo_frame_lo
        CMP     #200
        BCS     @done6
        JMP     @frame
@done6: RTS

; ============================================================================
; demo_phase5_tableau — final still frame. 5 fauna sprites in a row with
; green backdrop. Holds for ~5 sec.
; ============================================================================
demo_phase5_tableau:
        ; Backdrop dark green
        LDA     #$0C
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; Place 5 sprites in a horizontal row at Y=80
        JSR     demo_vram_addr_sat
        LDX     #0
@s:     LDA     #79
        STA     VDP_DATA
        JSR     tms9918_pad12
        TXA
        CLC
        ADC     #2
        ASL
        ASL
        ASL
        ASL                             ; X position = (i+2)*16 = 32, 48, 64, 80, 96
        STA     VDP_DATA
        JSR     tms9918_pad12
        TXA
        ASL
        ASL                             ; name = i*4 = 0, 4, 8, 12, 16
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F                    ; white
        STA     VDP_DATA
        JSR     tms9918_pad12
        INX
        CPX     #5
        BNE     @s
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12

        ; Hold ~300 frames (~5 sec)
        LDA     #0
        STA     demo_frame_lo
@hold:  JSR     wait_frame
        INC     demo_frame_lo
        LDA     demo_frame_lo
        CMP     #200
        BCC     @hold
        RTS

; ============================================================================
; --- VDP setup helpers ----------------------------------------------------
; ============================================================================
setup_mode0_active_sprite:
        LDA     #$C0
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$80
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$36
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$85
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$86
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; Make the sentinel sprite visible by populating sprite-pattern 0
        ; with a recognisable "X" shape rather than all-zero (transparent).
        JSR     fill_pat0_X

        ; SAT[0] active sentinel — visible white X at top-left.
        ; SAT[1] = $D0 terminator.
        JSR     vram_addr_sat0
        LDA     #9
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$0F
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12
        RTS

; --- fill_pat0_X — write a recognisable "X" shape (8 bytes) at sprite
; pattern 0 ($0000-$0007). Visible 8x8 cross — sprites with name=0 appear
; as a small white X on the TMS9918 display, so an observer can confirm
; sprite-rendering activity during the test battery.
fill_pat0_X:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$40
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDX     #0
@l:     LDA     pat_X_data,X
        STA     VDP_DATA
        JSR     tms9918_pad12
        INX
        CPX     #8
        BNE     @l
        RTS

pat_X_data:
        .byte   $81, $42, $24, $18, $18, $24, $42, $81

setup_mode0_no_sprite:
        LDA     #$C0
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$80
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$36
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$85
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$86
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

fill_pat0_FF:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$40
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDX     #8
        LDA     #$FF
@f:     STA     VDP_DATA
        JSR     tms9918_pad12
        DEX
        BNE     @f
        RTS

; --- VRAM addr helpers -----------------------------------------------------
vram_addr_sat0:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$5B
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

vram_addr_1000:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$50
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

vram_addr_1000_read:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$10
        STA     VDP_CTRL
        JSR     tms9918_pad12
        ; Don't consume the read-ahead prefetch — caller's first
        ; LDA VDP_DATA returns vram[$1000] as expected.
        RTS

vram_addr_1100:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$51
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

vram_addr_1100_read:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$11
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

vram_addr_1200:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$52
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

vram_addr_1200_read:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$12
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

vram_addr_1300:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$53
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

vram_addr_1300_read:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$13
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

vram_addr_1400:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$54
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

vram_addr_1400_read:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$14
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

vram_addr_1500:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$55
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

vram_addr_1500_read:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$15
        STA     VDP_CTRL
        JSR     tms9918_pad12
        RTS

; wait_frame — sync to next F-flag rise via polling. Each iteration's
; LDA VDP_CTRL CLEARS bits 5/6/7 → must NOT be used after a SAT setup
; if we want to inspect collision/5S latched during the frame.
; Reserved for synchronization-only paths (e.g. animation frame pacing).
wait_frame:
@spin:  LDA     VDP_CTRL
        BPL     @spin
        RTS

; delay_frame — pure CPU-cycle wait, ~17000 cycles ≈ one full NTSC
; frame. Crucially does NOT touch VDP_CTRL, so any collision/5S bits
; latched by the per-scanline scan during the frame remain set when
; the caller subsequently reads status. The right choice for any test
; that needs to capture mid-frame chip state.
delay_frame:
        LDX     #0
        LDY     #$0E            ; 14 outer × 256 inner × ~5c ≈ 17920c
@d:     DEX
        BNE     @d
        DEY
        BNE     @d
        RTS

; ============================================================================
; --- Apple-1 native I/O helpers (use $D012 + Wozmon ECHO) -----------------
; ============================================================================

; print_char — output A as a character on the Apple-1 PIA display. Sets
; bit 7 (per Apple-1 convention) and uses Wozmon's ECHO (waits for the
; display to be ready). Doesn't clobber X/Y.
print_char:
        ORA     #$80
        JSR     ECHO
        RTS

; do_cr — print CR ($0D | $80 = $8D).
do_cr:
        LDA     #$0D
        JMP     print_char

; print_string — A:X = pointer to NUL-terminated string. NUL ends.
print_string:
        STA     str_lo
        STX     str_hi
        LDY     #0
@l:     LDA     (str_lo),Y
        BEQ     @done
        JSR     print_char
        INY
        BNE     @l
@done:
        RTS

; print_hex — A → 2 ASCII hex digits.
print_hex:
        PHA
        LSR
        LSR
        LSR
        LSR
        JSR     @nibble
        PLA
        AND     #$0F
@nibble:
        AND     #$0F
        CMP     #$0A
        BCC     @dec
        CLC
        ADC     #$07
@dec:   CLC
        ADC     #'0'
        JMP     print_char

; print_dec2 — A → 2 ASCII decimal digits (00..99). Wraps at 100.
print_dec2:
        LDX     #'0'-1
@t:     INX
        SEC
        SBC     #10
        BCS     @t
        ADC     #10
        PHA
        TXA
        JSR     print_char
        PLA
        CLC
        ADC     #'0'
        JMP     print_char

; wait_key_any — block until Apple-1 KBD ready; returns key in A (high
; bit cleared).
wait_key_any:
@spin:  LDA     KBDCR
        BPL     @spin
        LDA     KBD
        AND     #$7F
        RTS

; wait_yn — accept Y/N (case insensitive). Echoes the answer. Returns
; uppercase letter in A.
wait_yn:
        JSR     wait_key_any
        CMP     #'Y'
        BEQ     @y
        CMP     #'y'
        BEQ     @y
        CMP     #'N'
        BEQ     @n
        CMP     #'n'
        BEQ     @n
        JMP     wait_yn
@y:     LDA     #'Y'
        JSR     print_char
        JSR     do_cr
        LDA     #'Y'
        RTS
@n:     LDA     #'N'
        JSR     print_char
        JSR     do_cr
        LDA     #'N'
        RTS

; ============================================================================
; --- Lookup tables --------------------------------------------------------
; ============================================================================
res_table_lo:
        .byte   <res_T01, <res_T02, <res_T03, <res_T04, <res_T05
        .byte   <res_T06, <res_T07, <res_T08, <res_T09, <res_T10
        .byte   <res_T11, <res_T12, <res_T13, <res_T14, <res_T15
        .byte   <res_T16, <res_T17, <res_T18, <res_T19, <res_T20
res_table_hi:
        .byte   >res_T01, >res_T02, >res_T03, >res_T04, >res_T05
        .byte   >res_T06, >res_T07, >res_T08, >res_T09, >res_T10
        .byte   >res_T11, >res_T12, >res_T13, >res_T14, >res_T15
        .byte   >res_T16, >res_T17, >res_T18, >res_T19, >res_T20

val_lo_table_lo:
        .byte   <val_T01_lo, <val_T02_lo, <val_T03_lo, <val_T04_lo, <val_T05_lo
        .byte   <val_T06,    <val_T07,    <val_T08,    <val_T09,    <val_T10
        .byte   <val_T11_b,  <val_T12,    <val_T13,    <val_T14_lo, <zero_byte
        .byte   <zero_byte,  <zero_byte,  <val_T18,    <val_T19,    <val_T20
val_lo_table_hi:
        .byte   >val_T01_lo, >val_T02_lo, >val_T03_lo, >val_T04_lo, >val_T05_lo
        .byte   >val_T06,    >val_T07,    >val_T08,    >val_T09,    >val_T10
        .byte   >val_T11_b,  >val_T12,    >val_T13,    >val_T14_lo, >zero_byte
        .byte   >zero_byte,  >zero_byte,  >val_T18,    >val_T19,    >val_T20
val_hi_table_lo:
        .byte   <val_T01_hi, <val_T02_hi, <val_T03_hi, <val_T04_hi, <val_T05_hi
        .byte   <zero_byte,  <zero_byte,  <zero_byte,  <zero_byte,  <zero_byte
        .byte   <val_T11_a,  <zero_byte,  <zero_byte,  <val_T14_hi, <zero_byte
        .byte   <zero_byte,  <zero_byte,  <zero_byte,  <zero_byte,  <zero_byte
val_hi_table_hi:
        .byte   >val_T01_hi, >val_T02_hi, >val_T03_hi, >val_T04_hi, >val_T05_hi
        .byte   >zero_byte,  >zero_byte,  >zero_byte,  >zero_byte,  >zero_byte
        .byte   >val_T11_a,  >zero_byte,  >zero_byte,  >val_T14_hi, >zero_byte
        .byte   >zero_byte,  >zero_byte,  >zero_byte,  >zero_byte,  >zero_byte

name_table_lo:
        .byte   <name_T01, <name_T02, <name_T03, <name_T04, <name_T05
        .byte   <name_T06, <name_T07, <name_T08, <name_T09, <name_T10
        .byte   <name_T11, <name_T12, <name_T13, <name_T14, <name_T15
        .byte   <name_T16, <name_T17, <name_T18, <name_T19, <name_T20
name_table_hi:
        .byte   >name_T01, >name_T02, >name_T03, >name_T04, >name_T05
        .byte   >name_T06, >name_T07, >name_T08, >name_T09, >name_T10
        .byte   >name_T11, >name_T12, >name_T13, >name_T14, >name_T15
        .byte   >name_T16, >name_T17, >name_T18, >name_T19, >name_T20

; A constant zero byte in CODE — for the val_hi tables that need a
; "high byte = 0" pointer when reading 8-bit results.
zero_byte:
        .byte   $00

; ============================================================================
; Strings (Apple-1 native — no high-bit set; print_char ORs $80 itself).
; ============================================================================
banner1:
        .byte   "TMS9918 SILICON STRICT VALIDATOR V2.0", 0
banner2:
        .byte   "17 TESTS - SEE DEV/SILICONBUGS.MD", 0

name_T01: .byte "SLOT TBL ACT GFX12+SPR", 0
name_T02: .byte "VBLANK FREE BANDWIDTH", 0
name_T03: .byte "BLANK FREE BANDWIDTH", 0
name_T04: .byte "ACTIVE TEXT TIGHT BURST", 0
name_T05: .byte "ACTIVE MULTICOL TIGHT", 0
name_T06: .byte "R1B7 4K VS 16K MASK", 0
name_T07: .byte "OVERSCAN COLLIDE X<0", 0
name_T08: .byte "STATUS BITS 0..4 LAST", 0
name_T09: .byte "COLOR0 SPRITE COLLIDE", 0
name_T10: .byte "5S LATCH FIRST OCCUR", 0
name_T11: .byte "STATUS STICKY ON READ", 0
name_T12: .byte "SPRITE SCAN IN BLANK", 0
name_T13: .byte "FLIPFLOP RESET ON READ", 0
name_T14: .byte "FRAME RATE NTSC CYCLES", 0
name_T15: .byte "RASTER SPLIT 5S VIS", 0
name_T16: .byte "ILLEGAL MODE CLONE", 0
name_T17: .byte "MID FRAME R7 RAINBOW", 0
name_T18: .byte "$D0 MID-SAT STOPS SCAN", 0
name_T19: .byte "COLOR-0 COUNTS IN 5S", 0
name_T20: .byte "Y=$FF WRAPAROUND COLL", 0

q_T15: .byte "  SAW 2 COLOR SPLIT? Y/N: ", 0
q_T16: .byte "  SAW GHOST SPRITES? Y/N: ", 0
q_T17: .byte "  SAW 2 COLOR BANDS? Y/N: ", 0

msg_stress: .byte "STRESS 30S SAT BURST...", 0
msg_demo:   .byte "VISUAL DEMO 25S - WATCH TMS9918 ", 0
msg_summary_hdr:  .byte "===== SUMMARY =====", 0
msg_summary_hdr2: .byte "TXX  RES VALU", 0
msg_summary_stress: .byte "STRESS DROPS=", 0
msg_summary_frames: .byte " FRAMES=", 0
msg_done:         .byte "DONE - POWER CYCLE TO RERUN", 0
