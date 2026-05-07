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

        ; --- Stress benchmark --------------------------------------------
        LDA     #<msg_stress
        LDX     #>msg_stress
        JSR     print_string
        JSR     stress_benchmark
        JSR     print_stress_result

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
        CMP     #18
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

; T01: slot-table active Mode 0 + sprites burst — Galaga-damiers pattern.
test_T01_slot_active_burst:
        JSR     setup_mode0_active_sprite
        JSR     vram_addr_1000
        LDX     #0
@bw:    TXA
        STA     VDP_DATA
        INX
        BNE     @bw
        JSR     vram_addr_1000_read
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
test_T12_blank_sprite_scan:
        JSR     setup_mode0_no_sprite
        JSR     fill_pat0_FF
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
        LDA     VDP_CTRL
        JSR     tms9918_pad12
        ; Blank display
        LDA     #$80
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     delay_frame    ; wait full frame WITHOUT polling status
        ;                          (preserves bits 5/6/7 latched by per-scanline scan)
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
test_T14_frame_rate:
        LDA     #$C0
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     wait_frame
        LDX     #0
        LDY     #0
@inner: INX
        BNE     @check
        INY
@check: LDA     VDP_CTRL
        BPL     @inner
        STX     val_T14_lo
        STY     val_T14_hi
        ; Result code: we report 'M' (measured) — exact value is in val.
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

        ; Animate ~120 frames doing the R7 split trick
        LDA     #120
        STA     loop_ctr
@frame:
        LDA     #$04
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     wait_frame
        JSR     wait_frame
        LDA     VDP_CTRL
        JSR     tms9918_pad12
        LDX     #0
@poll:  LDA     VDP_CTRL
        AND     #$40
        BNE     @split
        DEX
        BNE     @poll
        JMP     @nf
@split:
        LDA     #$0C
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12
@nf:    DEC     loop_ctr
        BNE     @frame

        LDA     #<q_T15
        LDX     #>q_T15
        JSR     print_string
        JSR     wait_yn
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
        LDA     #180
        STA     loop_ctr
@frame:
        LDA     #$04
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     wait_frame
        JSR     wait_frame
        ; Spin ~5000c to mid-screen
        LDX     #0
        LDY     #5
@d:     DEX
        BNE     @d
        DEY
        BNE     @d
        LDA     #$0A
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12
        DEC     loop_ctr
        BNE     @frame
        LDA     #<q_T17
        LDX     #>q_T17
        JSR     print_string
        JSR     wait_yn
        STA     res_T17
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
        JSR     vram_addr_1500
        LDX     #0
@bw:    TXA
        STA     VDP_DATA
        INX
        BNE     @bw
        JSR     vram_addr_1500_read
        LDX     #0
@vr:    LDA     VDP_DATA
        STX     loop_ctr
        CMP     loop_ctr
        BEQ     @ok
        INC     stress_drops_lo
        BNE     @ok
        INC     stress_drops_hi
@ok:    INX
        BNE     @vr
        INC     stress_frames_lo
        BNE     @nh
        INC     stress_frames_hi
@nh:
        ; Stop after frames reach $0708 (= 1800)
        LDA     stress_frames_hi
        CMP     #$07
        BCC     @frame
        LDA     stress_frames_lo
        CMP     #$08
        BCC     @frame
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
        ; SAT[0] active sentinel
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
        .byte   <res_T16, <res_T17
res_table_hi:
        .byte   >res_T01, >res_T02, >res_T03, >res_T04, >res_T05
        .byte   >res_T06, >res_T07, >res_T08, >res_T09, >res_T10
        .byte   >res_T11, >res_T12, >res_T13, >res_T14, >res_T15
        .byte   >res_T16, >res_T17

val_lo_table_lo:
        .byte   <val_T01_lo, <val_T02_lo, <val_T03_lo, <val_T04_lo, <val_T05_lo
        .byte   <val_T06,    <val_T07,    <val_T08,    <val_T09,    <val_T10
        .byte   <val_T11_b,  <val_T12,    <val_T13,    <val_T14_lo, <zero_byte
        .byte   <zero_byte,  <zero_byte
val_lo_table_hi:
        .byte   >val_T01_lo, >val_T02_lo, >val_T03_lo, >val_T04_lo, >val_T05_lo
        .byte   >val_T06,    >val_T07,    >val_T08,    >val_T09,    >val_T10
        .byte   >val_T11_b,  >val_T12,    >val_T13,    >val_T14_lo, >zero_byte
        .byte   >zero_byte,  >zero_byte
val_hi_table_lo:
        .byte   <val_T01_hi, <val_T02_hi, <val_T03_hi, <val_T04_hi, <val_T05_hi
        .byte   <zero_byte,  <zero_byte,  <zero_byte,  <zero_byte,  <zero_byte
        .byte   <val_T11_a,  <zero_byte,  <zero_byte,  <val_T14_hi, <zero_byte
        .byte   <zero_byte,  <zero_byte
val_hi_table_hi:
        .byte   >val_T01_hi, >val_T02_hi, >val_T03_hi, >val_T04_hi, >val_T05_hi
        .byte   >zero_byte,  >zero_byte,  >zero_byte,  >zero_byte,  >zero_byte
        .byte   >val_T11_a,  >zero_byte,  >zero_byte,  >val_T14_hi, >zero_byte
        .byte   >zero_byte,  >zero_byte

name_table_lo:
        .byte   <name_T01, <name_T02, <name_T03, <name_T04, <name_T05
        .byte   <name_T06, <name_T07, <name_T08, <name_T09, <name_T10
        .byte   <name_T11, <name_T12, <name_T13, <name_T14, <name_T15
        .byte   <name_T16, <name_T17
name_table_hi:
        .byte   >name_T01, >name_T02, >name_T03, >name_T04, >name_T05
        .byte   >name_T06, >name_T07, >name_T08, >name_T09, >name_T10
        .byte   >name_T11, >name_T12, >name_T13, >name_T14, >name_T15
        .byte   >name_T16, >name_T17

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

q_T15: .byte "  SAW 2 COLOR SPLIT? Y/N: ", 0
q_T16: .byte "  SAW GHOST SPRITES? Y/N: ", 0
q_T17: .byte "  SAW 2 COLOR BANDS? Y/N: ", 0

msg_stress: .byte "STRESS 30S SAT BURST...", 0
msg_summary_hdr:  .byte "===== SUMMARY =====", 0
msg_summary_hdr2: .byte "TXX  RES VALU", 0
msg_summary_stress: .byte "STRESS DROPS=", 0
msg_summary_frames: .byte " FRAMES=", 0
msg_done:         .byte "DONE - POWER CYCLE TO RERUN", 0
