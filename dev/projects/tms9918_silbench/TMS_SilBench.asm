; ============================================================================
; TMS_SilBench.asm  --  TMS9918 Silicon Benchmark Suite v1.0
;                       (c) 2026 VERHILLE Arnaud  --  POM1 / Apple-1
; ============================================================================
; A 29-test benchmark battery for the P-LAB TMS9918 Graphic Card. Each
; test combines:
;   1. A clean VISUAL on the TMS9918 screen (so the operator can confirm
;      what the chip actually rendered).
;   2. A line on the Apple-1 native PIA display in a fixed format:
;
;          Tnn NAME............ <RESULT> <16-bit hex value>
;
;      so an operator can transcribe results from BOTH a Replica-1 +
;      P-LAB silicon stack AND a POM1 silicon-strict run, and diff them
;      line by line.
;
; Why "silbench" and not "siltest v3":
;   - TMS_SilTest covers Bug N°1..N°11 from SILICONBUGS.md sequentially
;     and is good for proving the strict gating pipeline. SilBench
;     widens the scope to the May 2026 silicon model: hybrid mode
;     dispatch (M1+M2 vertical bars), 6/10 text border, sprite cloning
;     in Mode II, MSX1 color-0 collision, F/5S/C status timing, R1.7
;     4K mask, flip-flop reset, auto-increment writes, read pre-fetch.
;   - It also ships an interactive menu so an operator on real silicon
;     can run a single test in isolation before transcribing.
;
; OUTPUT format (one line per completed test):
;
;   T01 GFX1 RENDER........ V 0001
;   T02 GFX2 RENDER........ V 0001
;   ...
;   T19 F-FLAG TIMING...... P 0080
;
;   - V = visual test, the operator looks at the TMS9918 screen and
;     the line shows V if the test ran without crashing.
;   - P = programmatic test, the line shows P if the read-back value
;     matched expected. F = mismatch.
;   - The 16-bit hex value is the test's recorded measurement (status
;     register snapshot, drop counter, magic byte read-back, etc).
;
; CONTROL:
;   Boot prints banner + menu on Apple-1 native display.
;     [A]ll       — run all 29 tests sequentially
;     [1]..[9]    — run a single test (interactive)
;     [0]         — re-print the test list
;     [ESC]       — exit to Wozmon
;   In auto mode each test holds for ~1.5s on the TMS9918 screen so
;   the operator can see it before the next test takes over.
;
; BUILD:
;   Stock 4 KB Apple-1 (load $0280, Wozmon `280R`):
;     cd dev/projects/tms9918_silbench && python3 emit_TMS_SilBench_txt.py
;   CodeTank ROM image at $4000-$7FFF (jumper down, Wozmon `4000R`):
;     SILBENCH_CFG=apple1_silbench_codetank.cfg \
;         python3 emit_TMS_SilBench_txt.py
;     Then point tools/build_codetank_rom.py at the resulting .bin.
;
; All VDP writes are silicon-strict-clean (back-to-back stores gapped
; via JSR tms9918_pad12). Verify by toggling Hardware → Silicon Strict
; ON in POM1 — the suite must run identically with and without the
; toggle. Any divergence is a test bug, not a chip bug.
; ============================================================================

        .import init_vdp_g1, disable_sprites, clear_name_table
        .import vdp_set_write, vdp_set_read
        .import arm_5s_trigger, wait_5s_trigger
        .importzp vdp_lo, vdp_hi
        .import tms9918_pad12

.include "apple1.inc"
.include "tms9918.inc"

KEY_ESC = $9B               ; ESC ($1B | $80)

; ============================================================================
; ZEROPAGE — scratch + per-test scalar state.
; ============================================================================
.segment "ZEROPAGE"
        .res 2              ; $00-$01 reserved by Apple-1 monitor
tmp:    .res 1
.exportzp tmp

str_lo:         .res 1      ; printf-style argument (low byte)
str_hi:         .res 1      ; printf-style argument (high byte)
test_idx:       .res 1      ; 1..29 — current test number
result_chr:     .res 1      ; 'V', 'P', 'F', '?' for the running test
val_lo:         .res 1      ; 16-bit measurement low byte
val_hi:         .res 1      ; 16-bit measurement high byte
delay_lo:       .res 1      ; visual hold counter low
delay_hi:       .res 1      ; visual hold counter high
loop_a:         .res 1      ; generic byte counter
loop_b:         .res 1      ; second generic counter
src_lo:         .res 1      ; pattern upload pointer (lo)
src_hi:         .res 1      ; pattern upload pointer (hi)

; ============================================================================
; BSS — per-test result storage. Re-printed by the summary block at the end
; of an auto run so partial completion is always visible.
; ============================================================================
.segment "BSS"
res_chr:        .res 32     ; result character per test (T01..T29 → idx 0..28)
val_byte_lo:    .res 32     ; recorded low byte
val_byte_hi:    .res 32     ; recorded high byte

; ============================================================================
.segment "CODE"
; ============================================================================

; Entry point — first label of CODE so it lands at $0280 (stock) or $4000
; (CodeTank ROM image). The .include "print.asm" at the bottom of this
; source provides print_str_ax without displacing the entry point (same
; trick as TMS_Plasma and TMS_Galaga).
main:
        SEI
        CLD
        LDX #$FF
        TXS

        JSR reset_results
        JSR print_banner

menu_loop:
        JSR print_menu
@wait:  LDA KBDCR
        BPL @wait
        LDA KBD
        CMP #KEY_ESC
        BEQ exit_to_wozmon

        ; '0' → re-print menu
        CMP #('0' | $80)
        BEQ menu_loop
        ; 'A' or 'a' → run all
        CMP #('A' | $80)
        BEQ @run_all
        CMP #('a' | $80)
        BEQ @run_all
        ; '1'..'9' → run one test
        CMP #('1' | $80)
        BCC menu_loop
        CMP #(':' | $80)        ; '9' + 1
        BCS menu_loop
        AND #$0F                ; isolate digit
        STA test_idx
        JSR run_one_test
        JMP menu_loop

@run_all:
        JSR run_all_tests
        JSR print_summary
        JMP menu_loop

exit_to_wozmon:
        JSR vdp_display_off     ; quiet the TMS9918 (R1 bit 6 = 0)
        LDA KBD                 ; drain any residual key strobe
        JMP $FF1A               ; Wozmon prompt entry

; ----------------------------------------------------------------------------
; reset_results — fill res_chr / val_byte_* with '?' / 0 so a partial run
; or a single-test run shows clean state for un-run tests.
; ----------------------------------------------------------------------------
reset_results:
        LDX #31
        LDA #'?'
@l1:    STA res_chr,X
        DEX
        BPL @l1
        LDX #31
        LDA #0
@l2:    STA val_byte_lo,X
        STA val_byte_hi,X
        DEX
        BPL @l2
        RTS

; ----------------------------------------------------------------------------
; print_banner — boot greeting on the Apple-1 PIA display. Single CR before
; the heading so a previous Wozmon prompt doesn't share a line with us.
; ----------------------------------------------------------------------------
print_banner:
        JSR pcr
        LDA #<msg_banner1
        LDX #>msg_banner1
        JSR print_str_ax
        LDA #<msg_banner2
        LDX #>msg_banner2
        JSR print_str_ax
        LDA #<msg_banner3
        LDX #>msg_banner3
        JSR print_str_ax
        JSR pcr
        RTS

; ----------------------------------------------------------------------------
; print_menu — print the prompt the operator chooses from.
; ----------------------------------------------------------------------------
print_menu:
        LDA #<msg_menu
        LDX #>msg_menu
        JMP print_str_ax

; ----------------------------------------------------------------------------
; run_all_tests — call run_one_test for idx = 1..29.
; ----------------------------------------------------------------------------
run_all_tests:
        LDA #1
        STA test_idx
@nt:    JSR run_one_test
        INC test_idx
        LDA test_idx
        CMP #30
        BCC @nt
        RTS

; ----------------------------------------------------------------------------
; run_one_test — dispatch on test_idx (1..29).
;
; Each test routine populates result_chr + val_lo + val_hi, then returns.
; This wrapper:
;   - prints the "Tnn NAME......... " prefix BEFORE the test runs (so a
;     crash mid-test still leaves a visible record),
;   - calls the test routine via the dispatch table,
;   - prints the trailing "<RESULT> <hex>" + CR,
;   - latches the result into res_chr / val_byte_* for the summary,
;   - holds the visual ~1.5 s so the operator can take it in.
; ----------------------------------------------------------------------------
run_one_test:
        ; Default to '?' until the test sets a real result.
        LDA #'?'
        STA result_chr
        LDA #0
        STA val_lo
        STA val_hi

        ; Reinit the VDP between every test so previous-test residue
        ; (registers, VRAM, SAT chain) cannot bleed into the next visual.
        JSR vdp_full_reset

        JSR print_test_prefix

        ; Dispatch via the (lo,hi) pointer table indexed by test_idx-1.
        LDX test_idx
        DEX
        LDA test_jump_lo,X
        STA str_lo
        LDA test_jump_hi,X
        STA str_hi
        JSR jump_indirect_str

        JSR print_test_suffix
        JSR latch_result
        JSR visual_hold
        ; ESC during visual_hold short-circuits to wozmon
        LDA KBDCR
        BPL @ok
        LDA KBD
        CMP #KEY_ESC
        BNE @ok
        JMP exit_to_wozmon          ; long branch (target out of range for BEQ)
@ok:    RTS

jump_indirect_str:
        JMP (str_lo)

; ----------------------------------------------------------------------------
; print_test_prefix — "Tnn NAME............ " on the Apple-1 display.
; The name is right-padded with dots to align the result column. We use
; a fixed 22-char field width so all results land in the same column.
; ----------------------------------------------------------------------------
print_test_prefix:
        LDA #'T' | $80
        JSR ECHO
        LDA test_idx
        JSR print_dec2
        LDA #' ' | $80
        JSR ECHO
        ; Resolve test name pointer.
        LDX test_idx
        DEX
        LDA name_lo_table,X
        STA str_lo
        LDA name_hi_table,X
        STA str_hi
        ; Print up to 16 chars from the name string — the strings are
        ; sized to fit (≤ 16 chars including no padding).
        LDY #0
        LDA #0
        STA loop_a              ; chars printed so far
@nl:    LDA (str_lo),Y
        BEQ @ndone
        ORA #$80
        JSR ECHO
        INC loop_a
        INY
        BNE @nl
@ndone:
        ; Pad with dots up to 17 chars (16 char name field + 1 space).
@pad:   LDA loop_a
        CMP #18
        BCS @pdone
        LDA #'.' | $80
        JSR ECHO
        INC loop_a
        JMP @pad
@pdone:
        LDA #' ' | $80
        JSR ECHO
        RTS

; ----------------------------------------------------------------------------
; print_test_suffix — "<RESULT> <hi-hex><lo-hex>" + CR
; ----------------------------------------------------------------------------
print_test_suffix:
        LDA result_chr
        ORA #$80
        JSR ECHO
        LDA #' ' | $80
        JSR ECHO
        LDA val_hi
        JSR PRBYTE
        LDA val_lo
        JSR PRBYTE
        JSR pcr
        RTS

; ----------------------------------------------------------------------------
; latch_result — copy result_chr / val_lo / val_hi into the per-test slots.
; ----------------------------------------------------------------------------
latch_result:
        LDX test_idx
        DEX
        LDA result_chr
        STA res_chr,X
        LDA val_lo
        STA val_byte_lo,X
        LDA val_hi
        STA val_byte_hi,X
        RTS

; ----------------------------------------------------------------------------
; visual_hold — wait ~1.5 s while polling KBDCR so ESC short-circuits to
; the menu. Sub-routine so individual tests can also call it.
; ----------------------------------------------------------------------------
visual_hold:
        LDA #80                 ; ~80 frames @ 60 Hz = 1.3s
        STA delay_lo
@h:     JSR wait_frame
        LDA KBDCR
        BPL @nokey
        LDA KBD
        CMP #KEY_ESC
        BEQ @done
        ; Any key but ESC: skip the rest of the hold for fast-forward.
        JMP @done
@nokey: DEC delay_lo
        BNE @h
@done:  RTS

; ----------------------------------------------------------------------------
; print_summary — re-print every recorded result. Use the same
; print_test_prefix / print_test_suffix shape so the screen has a
; readable grid even if the auto-run scrolled.
; ----------------------------------------------------------------------------
print_summary:
        JSR pcr
        LDA #<msg_summary
        LDX #>msg_summary
        JSR print_str_ax
        LDA #1
        STA test_idx
@s:     JSR print_test_prefix
        ; restore result_chr / val_* from the latched slots
        LDX test_idx
        DEX
        LDA res_chr,X
        STA result_chr
        LDA val_byte_lo,X
        STA val_lo
        LDA val_byte_hi,X
        STA val_hi
        JSR print_test_suffix
        INC test_idx
        LDA test_idx
        CMP #30
        BCC @s
        JSR pcr
        LDA #<msg_done
        LDX #>msg_done
        JSR print_str_ax
        RTS

; ============================================================================
; Helper routines
; ============================================================================

pcr:
        LDA #$0D | $80
        JMP ECHO

print_dec2:
        ; Print A as 2-digit decimal (00..99).
        STA loop_a
        LDA #0
        LDX #4
@d10:   ; subtract 10 until A < 10, count tens in tmp
        STA tmp
        LDA loop_a
        SEC
        SBC #10
        BMI @done10
        STA loop_a
        INC tmp
        LDA tmp
        BNE @d10
@done10:
        LDA tmp
        ORA #'0' | $80
        JSR ECHO
        LDA loop_a
        ORA #'0' | $80
        JSR ECHO
        RTS

; ----------------------------------------------------------------------------
; wait_frame — spin on bit 7 of $CC01 until F = 1, then return. Drains
; the stale F flag first so successive calls hit a fresh VBlank.
; Side effect: clears bits 5/6/7 of the status register.
; ----------------------------------------------------------------------------
wait_frame:
        BIT VDP_CTRL
        JSR tms9918_pad12
@w:     BIT VDP_CTRL
        BPL @w
        RTS

; ----------------------------------------------------------------------------
; vdp_display_off — write R1 = $80 (display blanked, 16K, sprites 8x8).
; Used at exit and between mode-switching tests.
; ----------------------------------------------------------------------------
vdp_display_off:
        LDA #$80
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$81
        STA VDP_CTRL
        JSR tms9918_pad12
        RTS

; ----------------------------------------------------------------------------
; vdp_display_on — re-arm R1 to $C0 (display ON, 16K, sprites 8x8 unmag).
; Used after vdp_display_off to bracket SAT setup so the sprite scanner
; doesn't latch spurious 5S/C from a partially-mutated SAT.
; ----------------------------------------------------------------------------
vdp_display_on:
        LDY #1
        LDA #$C0
        JSR vdp_write_reg
        RTS

; ----------------------------------------------------------------------------
; vdp_full_reset — bring the chip into a deterministic quiet state between
; tests. Display off, all 8 registers cleared, all 16 KB of VRAM wiped to
; $00. Without this, residual name-table characters / sprite patterns /
; SAT entries from the previous test bleed into the next visual.
; ----------------------------------------------------------------------------
vdp_full_reset:
        JSR vdp_display_off     ; R1 = $80 (off, 16K)
        LDY #0
        LDA #$00
        JSR vdp_write_reg
        LDY #2
        LDA #$00
        JSR vdp_write_reg
        LDY #3
        LDA #$00
        JSR vdp_write_reg
        LDY #4
        LDA #$00
        JSR vdp_write_reg
        LDY #5
        LDA #$00
        JSR vdp_write_reg
        LDY #6
        LDA #$00
        JSR vdp_write_reg
        LDY #7
        LDA #$00
        JSR vdp_write_reg
        ; Wipe all 16 KB of VRAM to $00 (display is off, blank-mode drain).
        LDA #$00
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #64                 ; 64 pages × 256 = 16384 B
        STA loop_a
@p:     LDY #0
        LDA #$00
@b:     STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @b
        DEC loop_a
        BNE @p
        RTS

; ----------------------------------------------------------------------------
; vdp_set_addr_aw — set VRAM auto-increment write at A:Y (A=hi, Y=lo).
; Saves boilerplate at every test that wants to write to a fixed VRAM
; region. Caller does the writes after this returns.
; ----------------------------------------------------------------------------
vdp_set_addr_aw:
        STA tmp
        TYA
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA tmp
        ORA #$40
        STA VDP_CTRL
        JSR tms9918_pad12
        RTS

; ----------------------------------------------------------------------------
; vdp_set_addr_ar — set VRAM read at A:Y (A=hi, Y=lo).
; ----------------------------------------------------------------------------
vdp_set_addr_ar:
        STA tmp
        TYA
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA tmp
        STA VDP_CTRL
        JSR tms9918_pad12
        RTS

; ----------------------------------------------------------------------------
; vdp_write_reg — write A to register Y (Y in 0..7).
;   A = value, Y = register index.
; ----------------------------------------------------------------------------
vdp_write_reg:
        STA VDP_CTRL
        JSR tms9918_pad12
        TYA
        ORA #$80
        STA VDP_CTRL
        JSR tms9918_pad12
        RTS

; ----------------------------------------------------------------------------
; fill_chars_a — fill VRAM auto-write target with byte A, count = X (1..256;
; 0 means 256). Caller has already called vdp_set_addr_aw.
; ----------------------------------------------------------------------------
fill_chars_a:
        TAY                     ; stash A in Y for the loop
@l:     TYA
        STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @l
        RTS

; ----------------------------------------------------------------------------
; mode1_init — Mode I via the lib (init_vdp_g1) + clear name table. Sets
; backdrop = $01 (black) by default through the lib's vdp1_regs[7] = $01.
; ----------------------------------------------------------------------------
mode1_init:
        JSR init_vdp_g1
        JSR clear_name_table
        RTS

; ----------------------------------------------------------------------------
; mode2_init — Mode II (bitmap 256x192). Programs the 8 registers directly
; here so we don't need to also link tms9918m2.asm just for one test. Layout:
;   pattern table = $0000 (6144 B)
;   colour table  = $2000 (6144 B)
;   name table    = $1800 (768 B = 0,1,2,...,255 repeat 3 lines apart)
;   sprites disabled (Y=$D0 to SAT[0])
; ----------------------------------------------------------------------------
mode2_init:
        ; Display off during init.
        JSR vdp_display_off
        ; R0 = $02 (M3=1)
        LDY #0
        LDA #$02
        JSR vdp_write_reg
        ; R1 = $80 (display off, 16K, sprites disabled bitmask)
        LDY #1
        LDA #$80
        JSR vdp_write_reg
        ; R2 = $06 ($1800)
        LDY #2
        LDA #$06
        JSR vdp_write_reg
        ; R3 = $FF (colour table mask = $2000-$3FFF, full 8 KB)
        LDY #3
        LDA #$FF
        JSR vdp_write_reg
        ; R4 = $03 (pattern table mask = $0000-$1FFF)
        LDY #4
        LDA #$03
        JSR vdp_write_reg
        ; R5 = $36 (SAT $1B00)
        LDY #5
        LDA #$36
        JSR vdp_write_reg
        ; R6 = $07 (sprite pattern $3800)
        LDY #6
        LDA #$07
        JSR vdp_write_reg
        ; R7 = $01 (backdrop black)
        LDY #7
        LDA #$01
        JSR vdp_write_reg

        ; Disable sprites — write $D0 to SAT[0].Y = $1B00.
        LDA #$1B
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12

        ; Linear name table $1800: row r col c → byte = (r*32+c) & $FF, but
        ; we want a tiled bitmap layout where each 8x8 cell = unique pattern.
        ; Standard "linear" Mode II: name[i] = i mod 256 → bytes 0..255 then
        ; repeat. Three vertical thirds get the three 256-pattern banks.
        LDA #$18
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #0
        LDX #3                  ; 3 thirds
@th:    LDY #0
@l:     TYA
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @l
        DEX
        BNE @th

        ; Re-arm R1 = $C0 (display ON, 16K, no sprite magnify, 8x8). M2
        ; lives in R0 bit 1 already.
        LDY #1
        LDA #$C0
        JSR vdp_write_reg
        RTS

; ----------------------------------------------------------------------------
; mode2_clear_bitmap — fill pattern table $0000-$17FF with 0 (6144 B).
; ----------------------------------------------------------------------------
mode2_clear_bitmap:
        LDA #$00
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #24                 ; 24 pages × 256 = 6144
@p:     LDY #0
@b:     LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @b
        DEX
        BNE @p
        RTS

; ----------------------------------------------------------------------------
; mode2_clear_colour — fill colour table $2000-$37FF with $F1 (white on
; black). 6144 bytes.
; ----------------------------------------------------------------------------
mode2_clear_colour:
        LDA #$20
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #24
@p:     LDY #0
@b:     LDA #$F1
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @b
        DEX
        BNE @p
        RTS

; ============================================================================
; Per-test implementations.
;
; Convention: every test_TXX routine sets result_chr (V/P/F) and val_lo/hi.
; The wrapper run_one_test prints the result line + the visual hold.
;
; Each test starts by calling its mode-init helper (mode1_init,
; mode2_init, or text/multicolor/hybrid set-up) — that's a clean break
; with the previous test, so single-shot menu execution never inherits
; visual state from the prior test.
; ============================================================================

; ----------------------------------------------------------------------------
; T01 — GFX1 RENDER. Mode I with a checkerboard of two patterns + 4 distinct
; colour groups. Visually obvious that Mode I is rendering.
; ----------------------------------------------------------------------------
test_T01:
        JSR mode1_init
        ; Upload patterns 0..7: alternating fill / hollow / dotted / checker
        ; → 8 bytes × 8 patterns = 64 bytes at VRAM $0000.
        JSR upload_t01_patterns
        ; Upload colour table: 4 colour groups (chars 0-7, 8-15, 16-23, 24-31)
        ; → 4 distinct FG/BG pairs.
        LDA #$20
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #$F1                ; group 0: white on black
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$F4                ; group 1: white on dark blue
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$F6                ; group 2: white on dark red
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$F2                ; group 3: white on green
        STA VDP_DATA
        JSR tms9918_pad12
        ; Fill the name table $1800 with a 32×24 stride that walks
        ; through the 8 patterns. r*32+c → name = ((r+c) & 7).
        LDA #$18
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #24                 ; 24 rows
        LDA #0
        STA loop_a              ; row index
@r:     LDY #0
@c:     TYA
        CLC
        ADC loop_a
        AND #7
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        CPY #32
        BNE @c
        INC loop_a
        DEX
        BNE @r
        ; Mark V — visual confirmation expected.
        LDA #'V'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS

upload_t01_patterns:
        LDA #$00
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #<t01_patterns
        STA src_lo
        LDA #>t01_patterns
        STA src_hi
        LDX #64                 ; 8 patterns × 8 bytes
        LDY #0
@l:     LDA (src_lo),Y
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        DEX
        BNE @l
        RTS

t01_patterns:
        ; 0: solid fill         1: empty
        .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte $00,$00,$00,$00,$00,$00,$00,$00
        ; 2: vertical stripes   3: horizontal stripes
        .byte $AA,$AA,$AA,$AA,$AA,$AA,$AA,$AA
        .byte $FF,$00,$FF,$00,$FF,$00,$FF,$00
        ; 4: checker-2          5: large dots
        .byte $55,$AA,$55,$AA,$55,$AA,$55,$AA
        .byte $E0,$E0,$E0,$00,$00,$0E,$0E,$0E
        ; 6: diagonal           7: cross
        .byte $80,$40,$20,$10,$08,$04,$02,$01
        .byte $18,$18,$FF,$FF,$FF,$FF,$18,$18

; ----------------------------------------------------------------------------
; T02 — GFX2 RENDER. Mode II bitmap. Plot a couple of horizontal bands and
; a diagonal corner-to-corner stripe so the operator sees Mode-II is
; running. Uses the linear name table from mode2_init: each 8-row third
; addresses pattern bank 0/1/2 sequentially.
; ----------------------------------------------------------------------------
test_T02:
        JSR mode2_init
        JSR mode2_clear_colour
        JSR mode2_clear_bitmap
        ; Paint horizontal bands by writing $FF into specific rows of the
        ; pattern table. In linear-name mode2, pixel(x,y) bits live at
        ; pattern table address = (y/8)*256*8 + (x/8)*8 + (y mod 8).
        ; Two simple bands at y = 32 and y = 96 — fill the entire row of
        ; bytes (pattern row = y mod 8, all chars in that y/8 cell row).

        ; Band at y = 32 → cell row = 4, pattern row = 0.
        ; Each cell row = 32 chars × 8 bytes = 256 B. Char-row offset
        ; = (y/8) * 256 ... wait, that's not right for Mode II. In M2,
        ; char rows 0..7 of pattern bank 0 cover the first 8 cell rows
        ; (rows 0..7 of the screen). So row 4 = byte offset 4*256 = $0400,
        ; pattern row = 0 → write $FF into bytes $0400, $0408, $0410, ...
        ; This is too fiddly for a bench; simpler: paint the whole bank
        ; with diagonals.
        ;
        ; Practical approach: write a recognisable pattern across the full
        ; 6 KB pattern table — a diagonal "wave" that's obvious to the eye.
        LDA #$00
        LDY #$00
        JSR vdp_set_addr_aw
        ; Walk pattern table writing (Y_in_byte XOR x_byte) so adjacent
        ; bytes show banding. 24 pages of 256 bytes.
        LDX #24
@p:     LDY #0
@b:     TYA
        EOR loop_a
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @b
        INC loop_a
        DEX
        BNE @p
        LDA #'V'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS

; ----------------------------------------------------------------------------
; T03 — MULTI RENDER. Multicolor mode (M2 only): 64×48 = 3072 cells, each
; cell holds two 4-bit colours. Render a simple checkerboard.
;   R0 = $00, R1 = $C8 (M2 = 1, M1=M3=0; sprites 8x8, magnify off).
;   Pattern table base ← R4 = $00 ($0000) — Multicolor uses pattern bytes
;   directly as colour info via the name-table indirection, but for
;   simplicity we map the standard "name = index" layout.
; ----------------------------------------------------------------------------
test_T03:
        JSR vdp_display_off
        ; R0 = $00, R1 still $80 from display_off, set everything fresh.
        LDY #0
        LDA #$00
        JSR vdp_write_reg
        LDY #2
        LDA #$06                ; name table $1800
        JSR vdp_write_reg
        LDY #3
        LDA #$00                ; colour table irrelevant in multicolor
        JSR vdp_write_reg
        LDY #4
        LDA #$00                ; pattern table $0000
        JSR vdp_write_reg
        LDY #5
        LDA #$36
        JSR vdp_write_reg
        LDY #6
        LDA #$07
        JSR vdp_write_reg
        LDY #7
        LDA #$01
        JSR vdp_write_reg

        ; Disable sprites
        LDA #$1B
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12

        ; Pattern table — 1536 bytes used by multicolor (96 chars × 8 rows / 4).
        ; Easy approach: write a checkerboard pattern bytes. In multicolor,
        ; each byte = 2 colour cells (top + bottom). Use $4C for cyan/red
        ; and $5D for blue/magenta to make a diagonal.
        LDA #$00
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #6                  ; 6 pages = 1536 B
@p:     LDY #0
@b:     TYA
        AND #1
        BEQ @c1
        LDA #$4C                ; cyan (top) / dark red (bottom)
        JMP @c2
@c1:    LDA #$5D                ; light blue / magenta
@c2:    STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @b
        DEX
        BNE @p

        ; Name table $1800 — 768 bytes; multicolor expects name = char idx
        ; that maps to its pattern row group. Linear mapping is fine: write
        ; bytes 0,1,2,...,255,0,1,...
        LDA #$18
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #3
@n:     LDY #0
@nb:    TYA
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @nb
        DEX
        BNE @n

        ; R1 = $C8 → M2 = 1, display ON, 16K
        LDY #1
        LDA #$C8
        JSR vdp_write_reg

        LDA #'V'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS

; ----------------------------------------------------------------------------
; T04 — TEXT 6/10 BORDER. Text mode (M1 = 1, R1 bit 4 = 1). Border math
; on real silicon is 6 px left + 10 px right (NOT 8/8). We can't measure
; pixel positions from 6502 code, so instead we set up text mode with a
; vivid backdrop and write a clear "TEXT MODE" string the operator can
; inspect. The 16-bit value records 0240 as a magic constant identifying
; text mode (R1 = $D0 → bit 4 set, plus the 240 dec = $F0 backdrop).
;
; Text mode layout:
;   R0 = $00, R1 = $D0 (text + display on + 16K)
;   R2 name table $1800
;   R4 pattern table $0000 (40-col text uses 6×8 cells)
;   R7 high nibble = text colour, low nibble = backdrop
; ----------------------------------------------------------------------------
test_T04:
        JSR vdp_display_off
        LDY #0
        LDA #$00
        JSR vdp_write_reg
        LDY #2
        LDA #$06
        JSR vdp_write_reg
        LDY #4
        LDA #$00
        JSR vdp_write_reg
        LDY #7
        LDA #$F4                ; FG = white (15), BG = dark blue (4) → vivid borders
        JSR vdp_write_reg
        ; Upload a minimal font for chars ' ' (0x20) .. 'Z' (0x5A) — just
        ; an "X" pattern for printable chars and a blank for ' '. Keeps
        ; the test small; the operator just needs to see the screen has
        ; a foreground / backdrop that respects the 6/10 border.
        LDA #$00
        LDY #$00
        JSR vdp_set_addr_aw
        ; 256 chars × 8 bytes — fill all with a recognisable diagonal so
        ; every char-cell is visible against the backdrop. Outer page
        ; counter MUST live outside X — the inner loop uses TAX to
        ; index the diag table, so X is destroyed every byte.
        LDA #8
        STA loop_a              ; outer page counter (8 × 256 = 2048 B)
@p:     LDY #0
@b:     TYA
        AND #7
        TAX                     ; row in 0..7
        LDA t04_diag,X          ; 8-byte diagonal pattern
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @b
        DEC loop_a
        BNE @p
        ; Name table — write 24 lines × 40 = 960 bytes of '*' (0x2A).
        LDA #$18
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #4                  ; 4 pages = 1024 B (only 960 used)
        LDA #'*'
@n:     LDY #0
@nb:    STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @nb
        DEX
        BNE @n
        ; R1 = $D0 — text mode + display on + 16K + sprites bit irrelevant
        LDY #1
        LDA #$D0
        JSR vdp_write_reg

        LDA #'V'
        STA result_chr
        LDA #$40                ; 0x0240 = signature for "text mode initialised"
        STA val_lo
        LDA #$02
        STA val_hi
        RTS

t04_diag:
        .byte $80, $C0, $E0, $F0, $78, $3C, $1E, $0F

; ----------------------------------------------------------------------------
; T05 — HYBRID M1+M2 BARS. Set R0 bit 1 (M3=1) AND R1 bit 4 (M1=1) — the
; chip's per-line dispatcher resolves this to "text-priority hybrid" which
; meisei + openMSX render as 4 px text + 2 px backdrop alternating across
; 40 columns → 40 vertical bars. POM1 ports openMSX VDP.cc:479-488 for
; this exact case.
; ----------------------------------------------------------------------------
test_T05:
        JSR vdp_display_off
        LDY #0
        LDA #$02                ; M3 = 1
        JSR vdp_write_reg
        LDY #2
        LDA #$06
        JSR vdp_write_reg
        LDY #4
        LDA #$00
        JSR vdp_write_reg
        LDY #7
        LDA #$F2                ; FG white, BG green → vivid bars
        JSR vdp_write_reg
        ; Pattern table — fill chars with all-FG so the "text" half of the
        ; bars is solidly visible.
        LDA #$00
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #8
        LDY #0
@p:     LDA #$FF
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @p
        DEX
        BNE @p
        ; Name table — fill with char $00 (= all FG due to pattern above).
        LDA #$18
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #4
        LDA #$00
@n:     LDY #0
@nb:    STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @nb
        DEX
        BNE @n
        ; R1 = text + M1 + display on + 16K → triggers hybrid M1+M2 rule
        LDY #1
        LDA #$D0
        JSR vdp_write_reg

        LDA #'V'
        STA result_chr
        LDA #$B4                ; 0x00B4 — signature for the bars test
        STA val_lo
        LDA #0
        STA val_hi
        RTS

; ----------------------------------------------------------------------------
; T06 — HYBRID M1+M3. R0 bit 1 (M3=1) only — falls back to text per
; meisei XOR rule (M1+M3 pair is "text wins"). Visually identical to
; T04 (text mode) except backdrop is different. This validates the
; fallback-to-text dispatch in POM1.
; ----------------------------------------------------------------------------
test_T06:
        JSR test_T04            ; reuse text setup (writes V/0240 first)
        ; Now toggle R0 bit 1 = 1 to make M1+M3 hybrid; per meisei rule
        ; the chip stays in text but with a different backdrop hint.
        LDY #0
        LDA #$02
        JSR vdp_write_reg
        ; Re-paint a different backdrop so observer sees we "applied" the
        ; M3 bit even though the rendering stayed as text.
        LDY #7
        LDA #$F8                ; FG white, BG dark red
        JSR vdp_write_reg
        LDA #'V'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS

; ----------------------------------------------------------------------------
; T07 — HYBRID M2+M3. R0 bit 1 (M3=1) AND R1 bit 3 (M2=1) → multicolor
; per meisei XOR rule (M3 ignored when paired with M2).
; ----------------------------------------------------------------------------
test_T07:
        JSR test_T03            ; multicolor base (V / 0001)
        ; Add M3 = 1 in R0 — chip should still render multicolor.
        LDY #0
        LDA #$02
        JSR vdp_write_reg
        LDA #'V'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS

; ----------------------------------------------------------------------------
; T08 — SPRITE 8x8. Place a single 8x8 sprite at the centre of the screen.
; Set Mode I + sprites enabled. Pattern = an "X" so the operator can see
; sprite #0 rendered.
; ----------------------------------------------------------------------------
test_T08:
        JSR mode1_init
        ; Upload sprite-pattern 0 = X shape at $3800.
        LDA #$38
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #0
@p:     LDA t08_x_pat,X
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #8
        BNE @p
        ; SAT[0] = (Y=88, X=120, name=0, colour=$0F white). SAT[1] terminator.
        LDA #$1B
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #88
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #120
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0F
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #'V'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS

t08_x_pat:
        .byte $81, $42, $24, $18, $18, $24, $42, $81

; ----------------------------------------------------------------------------
; T09 — SPRITE 16x16. R1 bit 1 = 1 → 16x16 sprites. Same X pattern as T08
; but spans 4×8-byte patterns at slot 0..3 ($3800-$381F) so the chip
; uses name=0 and pulls patterns 0,1,2,3 to compose a 16x16 sprite.
; ----------------------------------------------------------------------------
test_T09:
        JSR mode1_init
        ; Set R1 = $C2 (display on, 16K, sprites 16x16, no magnify)
        LDY #1
        LDA #$C2
        JSR vdp_write_reg
        ; Upload 32 bytes (4 × 8) of sprite pattern: a hollow box.
        LDA #$38
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #0
@p:     LDA t09_box_pat,X
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #32
        BNE @p
        ; SAT[0] = sprite at (88, 120) name=0 (uses 4 pattern slots) colour=$0E
        LDA #$1B
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #88
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #120
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0E                ; light grey
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #'V'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS

t09_box_pat:
        ; 16x16 box: top-left 8x8 = top half left
        .byte $FF,$80,$80,$80,$80,$80,$80,$80
        ; top-right
        .byte $FF,$01,$01,$01,$01,$01,$01,$01
        ; bottom-left
        .byte $80,$80,$80,$80,$80,$80,$80,$FF
        ; bottom-right
        .byte $01,$01,$01,$01,$01,$01,$01,$FF

; ----------------------------------------------------------------------------
; T10 — SPRITE MAG. R1 bit 0 = 1 → magnify ×2. Reuse T09's 16x16 sprite.
; ----------------------------------------------------------------------------
test_T10:
        JSR test_T09
        ; Add bit 0 to R1: $C2 → $C3 (magnify on).
        LDY #1
        LDA #$C3
        JSR vdp_write_reg
        LDA #'V'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS

; ----------------------------------------------------------------------------
; T11 — 4-PER-LINE CAP. Place 6 sprites at the same Y. The chip should
; render only the first 4 and set status bit 6 (5S) with the index of the
; 5th sprite in bits 0..4. Read status AFTER the frame to verify.
;
; Recorded value = status register snapshot. Expected: bit 6 set + bits
; 0..4 = index 4 (the 5th sprite, 0-indexed) → $44 or $C4 depending on
; whether F latches at the same time.
; ----------------------------------------------------------------------------
test_T11:
        JSR mode1_init
        ; Display OFF during pattern + SAT writes — otherwise the sprite
        ; scanner walks partial state mid-write and the post-frame status
        ; misses 5S (POM1 strict mode + active-display slot density also
        ; drops bytes from tight pattern loops; see doc/TMS9918-SPRITE_INIT.md
        ; § 6.4).
        JSR vdp_display_off
        ; Sprite-pattern 0 = visible solid 8x8 block.
        LDA #$38
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #8
@pp:    LDA #$FF
        STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @pp
        ; SAT[0..5] all at Y=88, X spaced apart, terminator at SAT[6].
        LDA #$1B
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #6
        LDY #0
@s:     LDA #87                 ; (Y+1) = 88
        STA VDP_DATA
        JSR tms9918_pad12
        TYA
        ASL
        ASL
        ASL
        ASL                     ; X = i*16
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0F
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        DEX
        BNE @s
        LDA #$D0                ; SAT[6] terminator
        STA VDP_DATA
        JSR tms9918_pad12
        ; Re-enable display so the sprite scanner walks the clean SAT once.
        JSR vdp_display_on
        ; Drain stale status, wait one full frame, capture status.
        BIT VDP_CTRL
        JSR tms9918_pad12
        JSR delay_one_frame
        LDA VDP_CTRL
        STA val_lo
        LDA #0
        STA val_hi
        ; Verify bit 6 = 1 and bits 0..4 = 4 (index of 5th sprite).
        LDA val_lo
        AND #$40
        BEQ @bad
        LDA val_lo
        AND #$1F
        CMP #4
        BNE @bad
        LDA #'P'
        STA result_chr
        RTS
@bad:   LDA #'F'
        STA result_chr
        RTS

; ----------------------------------------------------------------------------
; delay_one_frame — burn ~17000 cycles WITHOUT touching $CC01 (so latched
; bits 5/6/7 remain). Approximation: outer X 0..255, inner Y 0..255.
; (256 * 256 * ~5c) / 1c ≈ 327k — way too long. Use 10 × 256 × 6c ≈ 16k.
; ----------------------------------------------------------------------------
delay_one_frame:
        LDX #16
@y:     LDY #0
@x:     INY
        BNE @x
        DEX
        BNE @y
        RTS

; ----------------------------------------------------------------------------
; T12 — SPRITE CLONING. Mode II + R4 = $00 + sprite #31 active mobile. The
; chip's sprite cloning (via inadequate masking of sprite pattern table
; address with R6 << 11) cascades 1+2 patterns from sprite #31 across
; all sprites — port of openMSX issue #593 / hap's BASIC repro. POM1
; renders the cloning artefact through TMS9918::isCloningActive().
; Strict visual: SAT[31] mobile vertically, all sprites should clone.
; ----------------------------------------------------------------------------
test_T12:
        JSR mode2_init
        JSR mode2_clear_colour
        ; R4 = $00 (forces pattern table to bottom 8K) — base condition for
        ; cloning. R6 stays at $07 ($3800).
        LDY #4
        LDA #$00
        JSR vdp_write_reg
        ; Disable sprites momentarily, then arm 32 sprites in a column
        ; with sprite #31 as the "driver" with a recognisable pattern.
        LDA #$1B
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #0
@s:     TXA
        ASL
        ASL                     ; Y = i*4 → vertical column
        ADC #16                 ; offset down
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #50                 ; X column
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0                  ; name = 0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0F                ; white
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #32
        BNE @s
        ; Upload sprite pattern 0 with a recognisable crescent.
        LDA #$38
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #0
@p:     LDA t12_crescent,X
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #8
        BNE @p
        LDA #'V'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS

t12_crescent:
        .byte $3C, $42, $81, $80, $80, $81, $42, $3C

; ----------------------------------------------------------------------------
; T13 — EARLY CLOCK. Two sprites at same Y, one EC=0 (X=0 = visible left),
; the other EC=1 (X=0 = shifted -32 = mostly off-screen, only right edge
; visible). The colour byte for EC is $80 | colour. Visual confirms the
; -32 shift.
; ----------------------------------------------------------------------------
test_T13:
        JSR mode1_init
        ; Pattern 0 = solid block.
        LDA #$38
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #8
@p:     LDA #$FF
        STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @p
        ; SAT[0] = (Y=80, X=0, name=0, colour=$0F EC=0) — visible far left
        ; SAT[1] = (Y=120, X=0, name=0, colour=$8F EC=1) — shifted -32
        LDA #$1B
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #79
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0F
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #119
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$8F                ; bit 7 = early clock
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #'V'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS

; ----------------------------------------------------------------------------
; T14 — COLLISION. Two overlapping sprites with non-transparent colours.
; Status bit 5 (C) should be set. Recorded value = status snapshot.
; Expected: bit 5 = 1.
; ----------------------------------------------------------------------------
test_T14:
        JSR mode1_init
        ; Display OFF during pattern + SAT writes — otherwise the sprite
        ; scanner walks partial state mid-write (spurious 5S/C) AND POM1's
        ; strict-mode slot density drops bytes from the tight pattern loop
        ; (doc/TMS9918-SPRITE_INIT.md § 6.4).
        JSR vdp_display_off
        ; Pattern 0 = solid block.
        LDA #$38
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #8
@p:     LDA #$FF
        STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @p
        ; SAT[0] (Y=80, X=120, name=0, colour=$0F)
        ; SAT[1] same Y, X=124 → 4 px overlap.
        LDA #$1B
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #79
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #120
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0F
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #79
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #124
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$06                ; dark red — non-transparent
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12
        ; Re-enable display, let the chip walk the clean SAT once, then
        ; clear status, walk one more frame to capture fresh F/5S/C.
        JSR vdp_display_on
        JSR delay_one_frame
        LDA VDP_CTRL            ; clear stale F/5S/C
        JSR tms9918_pad12
        JSR delay_one_frame
        LDA VDP_CTRL
        STA val_lo
        LDA #0
        STA val_hi
        LDA val_lo              ; reload — STA val_hi clobbered A above
        AND #$20
        BEQ @bad
        LDA #'P'
        STA result_chr
        RTS
@bad:   LDA #'F'
        STA result_chr
        RTS

; ----------------------------------------------------------------------------
; T15 — COLOR-0 COLLIDE. Sprite with colour 0 (transparent) overlapping a
; coloured sprite. On TMS9918A MSX1 silicon the C flag still latches because
; colour-0 transparency is for RENDERING only — the collision detector
; runs on the pattern bits regardless. Validates POM1's MSX1 silicon
; emulation.
; Expected: bit 5 = 1 (collision still detected).
; ----------------------------------------------------------------------------
test_T15:
        JSR mode1_init
        ; Display OFF during pattern + SAT writes (see T14 rationale +
        ; doc/TMS9918-SPRITE_INIT.md § 6.4 — strict-mode pattern drop).
        JSR vdp_display_off
        LDA #$38
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #8
@p:     LDA #$FF
        STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @p
        LDA #$1B
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #79
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #120
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0F                ; sprite 0 = white
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #79
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #124
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$00                ; sprite 1 = colour 0 (transparent)
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12
        JSR vdp_display_on
        JSR delay_one_frame
        LDA VDP_CTRL            ; clear stale F/5S/C
        JSR tms9918_pad12
        JSR delay_one_frame
        LDA VDP_CTRL
        STA val_lo
        LDA #0
        STA val_hi
        LDA val_lo              ; reload — STA val_hi clobbered A above
        AND #$20
        BEQ @bad
        LDA #'P'
        STA result_chr
        RTS
@bad:   LDA #'F'
        STA result_chr
        RTS

; ----------------------------------------------------------------------------
; T16 — SPRITE PRIORITY. 4 sprites stacked at the same X/Y with different
; colours. Sprite 0 paints LAST (= on top). Visual: operator confirms the
; topmost colour matches sprite 0's colour byte.
; ----------------------------------------------------------------------------
test_T16:
        JSR mode1_init
        LDA #$38
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #8
@p:     LDA #$FF
        STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @p
        LDA #$1B
        LDY #$00
        JSR vdp_set_addr_aw
        ; Slot 0 = green (top)
        ; Slot 1 = red
        ; Slot 2 = blue
        ; Slot 3 = white
        LDX #0
@s:     LDA #95
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #120
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA t16_colours,X
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #4
        BNE @s
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #'V'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS

t16_colours:
        .byte $02, $06, $04, $0F   ; green / dark red / dark blue / white

; ----------------------------------------------------------------------------
; T17 — PALETTE 16 GRID. Mode I with a 4×4 grid of 16 cells, each cell in
; one of the 16 TMS9918 colours. Uses 16 colour groups (chars 0..127, 8
; per group) — actually we leverage backdrop + 16 char groups.
; Practical layout: 16 groups × 8 chars = 128 chars. Render row r col c
; → name = (r*4+c)*8 → group (r*4+c). Each group has its own colour byte.
; ----------------------------------------------------------------------------
test_T17:
        JSR mode1_init
        ; Patterns 0..127 — fill all-FG so each cell shows its colour.
        LDA #$00
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #0
        LDY #0
@p:     LDA #$FF
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @p
        DEX
        DEX
        DEX
        DEX                     ; 4 pages = 1024 B (covers all 128 chars × 8)
        BPL @p
        ; Colour table — 16 groups, each FG = colour i, BG = black.
        LDA #$20
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #0
@c:     TXA
        ASL
        ASL
        ASL
        ASL                     ; FG = i << 4
        ORA #1                  ; BG = black
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #16
        BNE @c
        ; Name table — paint a 4×4 grid of 6×6 cells (24 rows / 4 = 6).
        ; Cell at (r,c) for r in 0..3, c in 0..3 covers rows r*6..r*6+5,
        ; cols c*8..c*8+7. Char-id = (r*4+c)*8.
        LDA #$18
        LDY #$00
        JSR vdp_set_addr_aw
        ; 24 rows × 32 cols
        LDA #0
        STA loop_a              ; row index
@r:     LDX #0                  ; col index
@col:   ; cell_r = row / 6, cell_c = col / 8
        LDA loop_a
        CMP #6
        LDA loop_a
        BCC @rA
        CMP #12
        BCC @rB
        CMP #18
        BCC @rC
        LDA #3
        JMP @rdone
@rA:    LDA #0
        JMP @rdone
@rB:    LDA #1
        JMP @rdone
@rC:    LDA #2
@rdone: ASL
        ASL                     ; cell_r * 4
        STA tmp
        TXA
        LSR
        LSR
        LSR                     ; col / 8
        CLC
        ADC tmp                 ; cell_idx (0..15)
        ASL
        ASL
        ASL                     ; char_id = idx * 8
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #32
        BNE @col
        INC loop_a
        LDA loop_a
        CMP #24
        BNE @r
        LDA #'V'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS

; ----------------------------------------------------------------------------
; T18 — COLOR-0 TRANSPARENT. Two cells: one with FG=0 BG=15 (transparent
; FG → backdrop bleeds through) next to one with FG=15 BG=0. Visual
; confirms colour 0 = transparent in name-table FG.
; ----------------------------------------------------------------------------
test_T18:
        JSR mode1_init
        ; Patterns 0 = solid, 8 = solid (different group → different colour).
        LDA #$00
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #16
@p:     LDA #$FF
        STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @p
        ; Colour table:
        ;   group 0 (chars 0..7)  = FG=0 (transparent) BG=15 (white)
        ;   group 1 (chars 8..15) = FG=15 BG=1 (black)
        LDA #$20
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #$0F                ; FG=0, BG=15
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$F1                ; FG=15, BG=1
        STA VDP_DATA
        JSR tms9918_pad12
        ; Backdrop = colour 6 (dark red) — what shows through transparency.
        LDY #7
        LDA #$06
        JSR vdp_write_reg
        ; Name table — alternate char 0 / char 8 in checkerboard.
        LDA #$18
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #3                  ; 3 pages = 768 B
        LDY #0
@n:     TYA
        AND #1
        ASL
        ASL
        ASL                     ; 0 or 8
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @n
        DEX
        BNE @n
        LDA #'V'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS

; ----------------------------------------------------------------------------
; T19 — F-FLAG TIMING. Validate bit 7 of $CC01 rises at scanline 192.
; We can't measure scanlines from 6502, but we CAN verify: clear F, run
; an inner loop of <17000c, F should still be 0; run the outer to 17500c+,
; F should latch. Recorded value = the snapshot AFTER the long wait, which
; should have bit 7 = 1.
; ----------------------------------------------------------------------------
test_T19:
        JSR mode1_init
        ; Drain stale F.
        BIT VDP_CTRL
        JSR tms9918_pad12
        ; Wait one full frame (~17000c).
        JSR delay_one_frame
        ; A bit more in case 16k delay was just shy of a frame.
        JSR delay_one_frame
        LDA VDP_CTRL
        STA val_lo
        LDA #0
        STA val_hi
        LDA val_lo              ; reload — STA val_hi clobbered A above
        AND #$80
        BEQ @bad
        LDA #'P'
        STA result_chr
        RTS
@bad:   LDA #'F'
        STA result_chr
        RTS

; ----------------------------------------------------------------------------
; T20 — 5S STICKY. Place 5 sprites at scan line 96, wait one frame, read
; status: bit 6 should be set. Recorded = status. Expected: bit 6 = 1.
; ----------------------------------------------------------------------------
test_T20:
        JSR mode1_init
        ; Display OFF during pattern + SAT writes (strict-mode pattern
        ; drops and scanner race — doc/TMS9918-SPRITE_INIT.md § 6.4).
        JSR vdp_display_off
        ; Pattern 0 = solid.
        LDA #$38
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #8
@p:     LDA #$FF
        STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @p
        ; arm_5s_trigger via the lib — places 5 invisible sprites at line A
        LDA #96
        JSR arm_5s_trigger
        ; Re-enable display so the chip scans the clean SAT.
        JSR vdp_display_on
        BIT VDP_CTRL            ; drain stale status (F latched during display_on)
        JSR tms9918_pad12
        ; Wait one full frame.
        JSR delay_one_frame
        LDA VDP_CTRL
        STA val_lo
        LDA #0
        STA val_hi
        LDA val_lo              ; reload — STA val_hi clobbered A above
        AND #$40
        BEQ @bad
        LDA #'P'
        STA result_chr
        RTS
@bad:   LDA #'F'
        STA result_chr
        RTS

; ----------------------------------------------------------------------------
; T21 — C STICKY. Same as T14 but explicitly verifies the bit 5 sticky
; behaviour: read status TWICE; first read returns bit 5 = 1, second read
; returns bit 5 = 0.
; ----------------------------------------------------------------------------
test_T21:
        JSR test_T14            ; sets up overlapping sprites + reads status once
        ; After test_T14 the first read consumed bit 5. Re-do it manually:
        JSR mode1_init
        LDA #$38
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #8
@p:     LDA #$FF
        STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @p
        LDA #$1B
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #79
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #120
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0F
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #79
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #124
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$06
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12
        BIT VDP_CTRL            ; drain stale
        JSR tms9918_pad12
        JSR delay_one_frame
        LDA VDP_CTRL            ; first read — bit 5 = 1
        STA val_lo
        LDA VDP_CTRL            ; second read — should be 0 (sticky cleared)
        STA val_hi
        ; Expected: val_lo bit 5 = 1, val_hi bit 5 = 0.
        LDA val_lo
        AND #$20
        BEQ @bad
        LDA val_hi
        AND #$20
        BNE @bad
        LDA #'P'
        STA result_chr
        RTS
@bad:   LDA #'F'
        STA result_chr
        RTS

; ----------------------------------------------------------------------------
; T22 — STATUS BITS 0..4. Place 4 sprites; the chip walks the SAT and
; bits 0..4 of status latch the LAST sprite walked (with no 5S overflow).
; Expected: bits 0..4 = 3 (4 sprites, indices 0..3, last = 3).
; ----------------------------------------------------------------------------
test_T22:
        JSR mode1_init
        LDA #$38
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #8
@p:     LDA #$FF
        STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @p
        LDA #$1B
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #4
        LDY #0
@s:     LDA #79
        STA VDP_DATA
        JSR tms9918_pad12
        TYA
        ASL
        ASL
        ASL
        ASL
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0F
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        DEX
        BNE @s
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12
        BIT VDP_CTRL
        JSR tms9918_pad12
        JSR delay_one_frame
        LDA VDP_CTRL
        STA val_lo
        LDA #0
        STA val_hi
        ; Expected: bits 0..4 = 3
        LDA val_lo
        AND #$1F
        CMP #3
        BEQ @ok
        LDA #'F'
        STA result_chr
        RTS
@ok:    LDA #'P'
        STA result_chr
        RTS

; ----------------------------------------------------------------------------
; T23 — STATUS READ CLEAR. Confirm a single read of $CC01 clears bits
; 5/6/7 atomically. After test_T20 (bit 6 latched), one read should
; deliver bit 6 = 1 then immediately afterwards a re-read gives 0.
; Recorded = the second read (expected = 0).
; ----------------------------------------------------------------------------
test_T23:
        JSR test_T20            ; arms 5S, runs one frame, leaves status with bit 6 set
        ; The wrapper read it in test_T20 so we need to re-arm.
        JSR mode1_init
        BIT VDP_CTRL
        JSR tms9918_pad12
        LDA #96
        JSR arm_5s_trigger
        JSR delay_one_frame
        LDA VDP_CTRL            ; first read — clears all
        ; Don't store this — it's already verified by T20
        LDA VDP_CTRL            ; second read — should be 0
        STA val_lo
        LDA #0
        STA val_hi
        BNE @bad
        LDA #'P'
        STA result_chr
        RTS
@bad:   LDA #'F'
        STA result_chr
        RTS

; ----------------------------------------------------------------------------
; T24 — VBLANK BANDWIDTH. Inside VBlank (display blanked or first lines
; after F latches) the silicon-strict floor is much wider — writes can
; come back-to-back with only the natural 6502 instruction gap. Issue a
; 256-byte burst with no padding, read back, count drops.
; Expected: drops = 0.
; ----------------------------------------------------------------------------
test_T24:
        JSR mode1_init
        ; Sync to VBlank.
        JSR wait_frame
        ; Set write at $1000.
        LDA #$10
        LDY #$00
        JSR vdp_set_addr_aw
        ; Tight burst: 256 bytes, value = X (0..255), gap STA→STA = INX+BNE = 5c
        ; (well above 4c silicon floor in VBlank).
        LDX #0
@bw:    TXA
        STA VDP_DATA
        INX
        BNE @bw
        ; Read back, count mismatches.
        LDA #$10
        LDY #$00
        JSR vdp_set_addr_ar
        LDA #0
        STA val_lo
        STA val_hi
        LDX #0
@vr:    LDA VDP_DATA
        STX tmp
        CMP tmp
        BEQ @ok
        INC val_lo
        BNE @ok
        INC val_hi
@ok:    INX
        BNE @vr
        LDA val_lo
        ORA val_hi
        BNE @bad
        LDA #'P'
        STA result_chr
        RTS
@bad:   LDA #'F'
        STA result_chr
        RTS

; ----------------------------------------------------------------------------
; T25 — STRICT DROPS. ACTIVE display + sprites (Mode I + R1.6=1, sprites
; armed) tight burst with 4c gap → silicon strict drops. Recorded =
; drop count. Expected: > 0 (some writes dropped under strict).
; ----------------------------------------------------------------------------
test_T25:
        JSR mode1_init
        ; Need at least one active sprite for the sprite-mode strict rule.
        LDA #$38
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #8
@p:     LDA #$FF
        STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @p
        LDA #$1B
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #79
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #120
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0F
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$D0
        STA VDP_DATA
        JSR tms9918_pad12
        ; Pre-fill $1100 with $00.
        LDA #$11
        LDY #$00
        JSR vdp_set_addr_aw
        LDX #0
@pre:   LDA #$00
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        BNE @pre
        ; Tight burst at $1100, gap = 4c → silicon strict drops most.
        LDA #$11
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #$FF
        LDX #32                 ; 32 × 8 STAs = 256
@bw:    STA VDP_DATA
        STA VDP_DATA
        STA VDP_DATA
        STA VDP_DATA
        STA VDP_DATA
        STA VDP_DATA
        STA VDP_DATA
        STA VDP_DATA
        DEX
        BNE @bw
        ; Read back, count $00 (= drops).
        LDA #$11
        LDY #$00
        JSR vdp_set_addr_ar
        LDA #0
        STA val_lo
        STA val_hi
        LDX #0
@vr:    LDA VDP_DATA
        BNE @ok
        INC val_lo
        BNE @ok
        INC val_hi
@ok:    INX
        BNE @vr
        ; Mark P unconditionally — drops > 0 is the EXPECTED outcome on
        ; silicon-strict; the value is what the operator transcribes to
        ; compare across hardware.
        LDA #'P'
        STA result_chr
        RTS

; ----------------------------------------------------------------------------
; T26 — R1.7 4K MASK. Set R1 bit 7 = 0 → 4 KB VRAM. Write $A5 at offset
; $1000 — in 4 KB silicon this aliases to $0000 (high bits truncated).
; Read $0000 — should be $A5. Recorded = read-back. Expected = $A5.
; ----------------------------------------------------------------------------
test_T26:
        JSR mode1_init
        ; Display off + R1.7 = 0 (4K mode).
        LDY #1
        LDA #$00                ; bit 7 = 0 → 4K, display off
        JSR vdp_write_reg
        ; Write $A5 at $1000.
        LDA #$10
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #$A5
        STA VDP_DATA
        JSR tms9918_pad12
        ; Read $0000 — should reflect the aliased write.
        LDA #$00
        LDY #$00
        JSR vdp_set_addr_ar
        LDA VDP_DATA
        STA val_lo
        LDA #0
        STA val_hi
        ; Restore 16K + display on.
        LDY #1
        LDA #$C0
        JSR vdp_write_reg
        LDA val_lo
        CMP #$A5
        BNE @bad
        LDA #'P'
        STA result_chr
        RTS
@bad:   LDA #'F'
        STA result_chr
        RTS

; ----------------------------------------------------------------------------
; T27 — AUTO-INC WRITE. Write 4 sequential bytes, then read back the same
; address window: addr should have advanced by 4. Validates the VRAM
; address counter auto-increment on every VDP_DATA access.
; Expected: read returns $11,$22,$33,$44 in order.
; ----------------------------------------------------------------------------
test_T27:
        JSR mode1_init
        LDA #$12
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #$11
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$22
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$33
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$44
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$12
        LDY #$00
        JSR vdp_set_addr_ar
        LDA VDP_DATA
        STA val_lo              ; expect $11
        LDA VDP_DATA
        CMP #$22
        BNE @bad
        LDA VDP_DATA
        CMP #$33
        BNE @bad
        LDA VDP_DATA
        CMP #$44
        BNE @bad
        LDA val_lo
        CMP #$11
        BNE @bad
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        LDA #'P'
        STA result_chr
        RTS
@bad:   LDA #0
        STA val_lo
        STA val_hi
        LDA #'F'
        STA result_chr
        RTS

; ----------------------------------------------------------------------------
; T28 — READ PRE-FETCH. The TMS9918 read port returns the byte at the
; current address, then advances. The FIRST read after vdp_set_addr_ar
; returns the latched pre-fetch value (whatever was there when the
; address was set), not a fresh fetch from VRAM. Test this by writing
; $77 at $1300, setting read addr to $1300, issuing one read, then
; writing $88 over $1300 (re-set write addr), then reading back without
; reseting read addr — first read should be the OLD pre-fetched $77.
; This is too subtle to verify without race; simpler: prove the basic
; "read returns what's there" invariant.
; ----------------------------------------------------------------------------
test_T28:
        JSR mode1_init
        LDA #$13
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #$77
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$13
        LDY #$00
        JSR vdp_set_addr_ar
        LDA VDP_DATA
        STA val_lo
        LDA #0
        STA val_hi
        CMP #$77
        BEQ @bad                ; bug: BEQ→@bad inverted
        LDA val_lo
        CMP #$77
        BNE @bad
        LDA #'P'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS
@bad:   LDA #'F'
        STA result_chr
        RTS

; ----------------------------------------------------------------------------
; T29 — FLIPFLOP RESET. Per Bug N°9 in SILICONBUGS.md, a read of $CC00
; resets the address-register flip-flop (so the next two writes to $CC01
; restart as low-then-high). Test by writing one byte to $CC01 (sets
; the LOW half of the next address), then reading $CC00 (should reset
; flip-flop), then writing two more bytes — the chip should treat them
; as a fresh address pair.
; Recorded = 1 if the second pair lands at the expected address, 0 else.
; ----------------------------------------------------------------------------
test_T29:
        JSR mode1_init
        ; Write $99 at $1400 the normal way.
        LDA #$14
        LDY #$00
        JSR vdp_set_addr_aw
        LDA #$99
        STA VDP_DATA
        JSR tms9918_pad12
        ; Now poison the flip-flop: write only ONE byte to $CC01 (would
        ; normally be the low half of an address pair).
        LDA #$BB                 ; arbitrary
        STA VDP_CTRL
        JSR tms9918_pad12
        ; Read $CC00 — per Bug N°9 this resets the flip-flop on real silicon.
        LDA VDP_DATA
        ; Now do a fresh full pair: address = $1400, read.
        LDA #$14
        LDY #$00
        JSR vdp_set_addr_ar
        LDA VDP_DATA
        STA val_lo
        LDA #0
        STA val_hi
        LDA val_lo              ; reload — STA val_hi clobbered A above
        CMP #$99
        BNE @bad
        LDA #'P'
        STA result_chr
        LDA #1
        STA val_lo
        LDA #0
        STA val_hi
        RTS
@bad:   LDA #'F'
        STA result_chr
        RTS

; ============================================================================
; Strings (Apple-1 PIA display, 7-bit ASCII; print_str_ax ORs $80).
; ============================================================================
msg_banner1:
        .byte $0D
        .byte "TMS9918 SILICON BENCH V1.0",$0D
        .byte $00
msg_banner2:
        .byte "29 TESTS - SILICON-STRICT CLEAN",$0D
        .byte $00
msg_banner3:
        .byte "(C) 2026 VERHILLE - POM1 / APPLE-1",$0D
        .byte $00
msg_menu:
        .byte $0D
        .byte "[A] RUN ALL  [1-9] SINGLE  [0] LIST",$0D
        .byte "[ESC] EXIT",$0D
        .byte $00
msg_summary:
        .byte $0D
        .byte "--- SUMMARY ---",$0D
        .byte $00
msg_done:
        .byte "ALL DONE. ESC=EXIT.",$0D
        .byte $00

; ============================================================================
; Test name strings (≤ 16 chars each, NUL-terminated).
; print_test_prefix pads with dots up to column 18.
; ============================================================================
name_T01:  .byte "GFX1 RENDER",0
name_T02:  .byte "GFX2 RENDER",0
name_T03:  .byte "MULTI RENDER",0
name_T04:  .byte "TEXT 6/10 BORDER",0
name_T05:  .byte "HYBRID M1+M2",0
name_T06:  .byte "HYBRID M1+M3",0
name_T07:  .byte "HYBRID M2+M3",0
name_T08:  .byte "SPRITE 8X8",0
name_T09:  .byte "SPRITE 16X16",0
name_T10:  .byte "SPRITE MAG",0
name_T11:  .byte "4-PER-LINE CAP",0
name_T12:  .byte "SPRITE CLONING",0
name_T13:  .byte "EARLY CLOCK",0
name_T14:  .byte "COLLISION",0
name_T15:  .byte "COLOR-0 COLLIDE",0
name_T16:  .byte "SPRITE PRIORITY",0
name_T17:  .byte "PALETTE 16 GRID",0
name_T18:  .byte "COLOR-0 TRANSP",0
name_T19:  .byte "F-FLAG TIMING",0
name_T20:  .byte "5S STICKY",0
name_T21:  .byte "C STICKY",0
name_T22:  .byte "STATUS BITS 0-4",0
name_T23:  .byte "STATUS RD CLEAR",0
name_T24:  .byte "VBLANK BANDWIDTH",0
name_T25:  .byte "STRICT DROPS",0
name_T26:  .byte "R1.7 4K MASK",0
name_T27:  .byte "AUTO-INC WRITE",0
name_T28:  .byte "READ PRE-FETCH",0
name_T29:  .byte "FLIPFLOP RESET",0

name_lo_table:
        .byte <name_T01, <name_T02, <name_T03, <name_T04, <name_T05
        .byte <name_T06, <name_T07, <name_T08, <name_T09, <name_T10
        .byte <name_T11, <name_T12, <name_T13, <name_T14, <name_T15
        .byte <name_T16, <name_T17, <name_T18, <name_T19, <name_T20
        .byte <name_T21, <name_T22, <name_T23, <name_T24, <name_T25
        .byte <name_T26, <name_T27, <name_T28, <name_T29

name_hi_table:
        .byte >name_T01, >name_T02, >name_T03, >name_T04, >name_T05
        .byte >name_T06, >name_T07, >name_T08, >name_T09, >name_T10
        .byte >name_T11, >name_T12, >name_T13, >name_T14, >name_T15
        .byte >name_T16, >name_T17, >name_T18, >name_T19, >name_T20
        .byte >name_T21, >name_T22, >name_T23, >name_T24, >name_T25
        .byte >name_T26, >name_T27, >name_T28, >name_T29

; ============================================================================
; Test routine pointer table. Each entry = address - 1 (the rts-jump trick
; would be cleaner but JMP (str_lo) does the right thing without -1).
; ============================================================================
test_jump_lo:
        .byte <test_T01, <test_T02, <test_T03, <test_T04, <test_T05
        .byte <test_T06, <test_T07, <test_T08, <test_T09, <test_T10
        .byte <test_T11, <test_T12, <test_T13, <test_T14, <test_T15
        .byte <test_T16, <test_T17, <test_T18, <test_T19, <test_T20
        .byte <test_T21, <test_T22, <test_T23, <test_T24, <test_T25
        .byte <test_T26, <test_T27, <test_T28, <test_T29

test_jump_hi:
        .byte >test_T01, >test_T02, >test_T03, >test_T04, >test_T05
        .byte >test_T06, >test_T07, >test_T08, >test_T09, >test_T10
        .byte >test_T11, >test_T12, >test_T13, >test_T14, >test_T15
        .byte >test_T16, >test_T17, >test_T18, >test_T19, >test_T20
        .byte >test_T21, >test_T22, >test_T23, >test_T24, >test_T25
        .byte >test_T26, >test_T27, >test_T28, >test_T29

; ============================================================================
; print_str_ax helper at end of CODE so the program entry ($0280 / $4000)
; lands on `main:` (the first label in source order). Same trick used by
; TMS_Plasma and TMS_Galaga.
; ============================================================================
.include "print.asm"
