; ============================================================================
; TMS_SilTest.asm  --  TMS9918 Silicon-Strict Validation Suite v2.0
;                      (c) 2026 VERHILLE Arnaud
; ============================================================================
; Comprehensive silicon-divergence test battery for the P-LAB TMS9918
; Graphic Card on a Replica-1 + P-LAB TMS9918 setup. Each test probes
; one of the silicon behaviours catalogued in dev/SILICONBUGS.md (Bug
; N°1 to N°11) and either auto-records a quantitative result (drop count,
; status-bit value, frame counter) or prompts the observer for a Y/N
; answer when the test outcome is visual (raster split, rainbow,
; illegal-mode sprite cloning).
;
; Runtime budget: ~60 seconds total. 13 tests + a 30-second sprite
; multiplexing stress benchmark at the end. The text-mode summary screen
; and per-test progress lines render IMMEDIATELY at boot — if anything
; downstream crashes, the operator still sees what was supposed to run.
;
; Workflow:
;   1. Boot, observe banner + test plan (always visible).
;   2. Press SPACE to start the auto battery.
;   3. Auto tests run sequentially (~15 sec). Each prints its result
;      in real time on its dedicated row.
;   4. Interactive tests: a question + Y/N prompt is displayed; observer
;      presses Y or N; the answer is logged.
;   5. 30-second sprite-multiplexing stress benchmark — live drop counter
;      and frame counter on row 23.
;   6. Final summary grid stays on screen forever (power cycle to rerun).
;
; Goal: lockstep silicon ↔ POM1 silicon-strict comparison. Any divergence
; flags a candidate POM1 bug to investigate. The test suite is the
; reference workflow for "validate software in POM1 strict before
; deploying to silicon".
;
; Build:
;     cd dev/projects/tms9918_siltest && make
; Standalone run from Wozmon:
;     paste software/tms9918/TMS_SilTest.txt
;     280R
; CodeTank ROM (lower jumper):
;     python3 tools/build_codetank_rom.py --rom=tools
;     Insert Codetank_TOOLS.rom, jumper down, 4000R from Wozmon.
; ============================================================================

        .import tms9918_pad12  ; silicon-strict pad12-v3 (helper from tms9918_pad.asm)
.include "apple1.inc"
.include "tms9918.inc"

; --- Library imports -------------------------------------------------------
.import   init_vdp_text, upload_charmap, clear_screen_text
.import   vdp_set_write, vdp_set_read, name_at_rc_text, print_at_rc_text
.importzp vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col
.exportzp tmp

; ============================================================================
; ZP layout
; ============================================================================
.segment "ZEROPAGE"
tmp:            .res 1          ; consumed by tms9918_text.asm
str_lo:         .res 1
str_hi:         .res 1
loop_ctr:       .res 1          ; generic test loop counter
gap_ctr:        .res 1          ; T01 gap variant counter
drop_lo:        .res 1          ; drop counter LSB
drop_hi:        .res 1          ; drop counter MSB
frame_lo:       .res 1          ; frame counter LSB
frame_hi:       .res 1          ; frame counter MSB
hex_buf:        .res 4          ; 4-char hex display buffer
arg_lo:         .res 1
arg_hi:         .res 1

; ============================================================================
; BSS
; ============================================================================
.segment "BSS"
; Per-test result chars (single ASCII byte). 'Y'/'N'/'?'/'P'/'F' depending.
res_T01:        .res 1
res_T02:        .res 1
res_T03:        .res 1
res_T04:        .res 1
res_T05:        .res 1
res_T06:        .res 1
res_T07:        .res 1
res_T08:        .res 1
res_T09:        .res 1
res_T10:        .res 1
res_T11:        .res 1
res_T12:        .res 1
res_T13:        .res 1
res_T14:        .res 1
res_T15:        .res 1
res_T16:        .res 1
res_T17:        .res 1
; Numeric extras: per-test drop count or status snapshot (1 byte each).
val_T01:        .res 1          ; T01 worst-case drop count
val_T07:        .res 1          ; T07 R1.7 read-back byte
val_T08:        .res 1          ; T08 status snapshot
val_T14_lo:     .res 1          ; T14 frame-rate measurement low byte
val_T14_hi:     .res 1          ; T14 frame-rate measurement high byte
stress_drops_lo:.res 1
stress_drops_hi:.res 1
stress_frames:  .res 1

; ============================================================================
.segment "CODE"
; ============================================================================

; ============================================================================
; main: entry at $0280 (Wozmon `280R`) or $4000 (CodeTank lower jumper).
; ============================================================================
main:
        SEI                     ; mask IRQs throughout — tests poll bit 7

        ; Default all results to '?' so partial completion still shows up.
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

        ; --- Bring up text mode + font ------------------------------------
        JSR     init_vdp_text
        JSR     upload_charmap
        JSR     clear_screen_text

        ; --- Banner + permanent test plan (visible even on later crash) --
        JSR     paint_banner
        JSR     paint_test_grid_skeleton

        ; --- Wait for SPACE to start the auto battery --------------------
        LDA     #22
        STA     vdp_row
        LDA     #1
        STA     vdp_col
        LDA     #<msg_press_space
        LDX     #>msg_press_space
        JSR     print_string

        JSR     wait_key_any

        ; Clear the prompt line.
        LDA     #22
        STA     vdp_row
        LDA     #1
        STA     vdp_col
        LDA     #<msg_running
        LDX     #>msg_running
        JSR     print_string

        ; ==========================================================
        ; PHASE 1 — Auto tests (status reads + drop counts).
        ; ==========================================================
        JSR     test_T01_slot_active_burst
        JSR     test_T02_vblank_free
        JSR     test_T03_blank_free
        JSR     test_T04_text_tight_burst
        JSR     test_T05_multicolor_tight_burst
        JSR     test_T06_R1_4K_16K
        JSR     test_T07_overscan_collision
        JSR     test_T08_status_bits_0_4
        JSR     test_T09_color0_collision
        JSR     test_T10_5S_first_occurrence
        JSR     test_T11_status_sticky
        JSR     test_T12_blank_sprite_scan
        JSR     test_T13_flipflop_reset
        JSR     test_T14_frame_rate

        ; ==========================================================
        ; PHASE 2 — Interactive tests (observer answers Y/N).
        ; ==========================================================
        JSR     test_T15_raster_split
        JSR     test_T16_illegal_mode_clone
        JSR     test_T17_mid_frame_rainbow

        ; ==========================================================
        ; PHASE 3 — 30-second sprite multiplexing stress benchmark.
        ; ==========================================================
        JSR     stress_benchmark

        ; ==========================================================
        ; Final summary — bring text mode back, redraw the grid with
        ; all results, hold forever.
        ; ==========================================================
        JSR     init_vdp_text
        JSR     clear_screen_text
        JSR     paint_banner
        JSR     paint_test_grid_skeleton
        JSR     paint_all_results
        JSR     paint_final_footer

@halt:  JMP     @halt

; ============================================================================
; paint_banner — header rows 0..1.
; ============================================================================
paint_banner:
        LDA     #0
        STA     vdp_row
        LDA     #2
        STA     vdp_col
        LDA     #<banner1
        LDX     #>banner1
        JSR     print_string
        LDA     #1
        STA     vdp_row
        LDA     #4
        STA     vdp_col
        LDA     #<banner2
        LDX     #>banner2
        JMP     print_string

; ============================================================================
; paint_test_grid_skeleton — paints the 17-row test plan in the result
; area (rows 3..21 leaving row 21 free as a separator). Each test's
; result column stays '?' until the test runs and overwrites it.
; ============================================================================
paint_test_grid_skeleton:
        LDA     #<grid_text
        STA     str_lo
        LDA     #>grid_text
        STA     str_hi
        LDA     #3                  ; first row in the grid
        STA     vdp_row
@row:   LDA     #0
        STA     vdp_col
        LDY     #0
@col:
        LDA     (str_lo),Y
        BEQ     @next_row
        CMP     #'|'                ; '|' = end of one row in the table
        BEQ     @next_row
        PHA
        TYA
        PHA
        JSR     name_at_rc_text
        JSR     vdp_set_write
        PLA
        TAY
        PLA
        JSR     tms9918_pad12
        STA     VDP_DATA
        INC     vdp_col
        INY
        BNE     @col
@next_row:
        ; advance the source pointer past the consumed bytes (Y of them)
        ; plus the trailing '|' (1 more) and increment row.
        TYA
        SEC
        ADC     str_lo              ; +Y+1 (carry from SEC)
        STA     str_lo
        BCC     @nc
        INC     str_hi
@nc:    LDA     (str_lo),Y          ; peek next byte (Y is meaningless now;
                                    ; we just want byte 0)
        ; Reset Y for the next row.
        ; Detect end of table: NUL byte at byte 0 of new row.
        LDY     #0
        LDA     (str_lo),Y
        BEQ     @done
        INC     vdp_row
        BNE     @row                ; always (vdp_row never wraps to 0)
@done:
        RTS

; ============================================================================
; paint_all_results — overwrites the result column at col 32 for each
; test with its res_T<NN> char. Called at the final summary.
;
; 17 tests at rows 3..19. Result column = 32 (5 chars: result + 4 numeric).
; ============================================================================
paint_all_results:
        LDA     #3
        STA     vdp_row
        ; Walk an array of test result pointers and print each at col 32.
        ; Avoid the per-test inline pattern by using `loop_ctr` as index.
        LDA     #0
        STA     loop_ctr
@loop:
        LDA     loop_ctr
        CMP     #17
        BCS     @done

        ; Print result char for test loop_ctr+1.
        LDA     #32
        STA     vdp_col
        LDX     loop_ctr
        LDA     res_table_lo,X
        STA     str_lo
        LDA     res_table_hi,X
        STA     str_hi
        LDY     #0
        LDA     (str_lo),Y          ; the result byte (1-byte indirect read)
        PHA
        JSR     name_at_rc_text
        JSR     vdp_set_write
        PLA
        JSR     tms9918_pad12
        STA     VDP_DATA

        INC     vdp_row
        INC     loop_ctr
        BNE     @loop
@done:
        RTS

; Lookup tables of result-byte addresses (low / high), one entry per test.
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

; ============================================================================
; paint_final_footer — replace the prompt on row 22 with a "DONE" message
; and dump stress benchmark numbers on row 23.
; ============================================================================
paint_final_footer:
        LDA     #22
        STA     vdp_row
        LDA     #1
        STA     vdp_col
        LDA     #<msg_done
        LDX     #>msg_done
        JSR     print_string

        LDA     #23
        STA     vdp_row
        LDA     #1
        STA     vdp_col
        LDA     #<msg_stress_label
        LDX     #>msg_stress_label
        JSR     print_string
        ; Stress drop count (16-bit) at col 14
        LDA     #14
        STA     vdp_col
        LDA     stress_drops_hi
        JSR     print_hex
        LDA     stress_drops_lo
        JSR     print_hex
        ; Stress frame count at col 22
        LDA     #22
        STA     vdp_col
        LDA     stress_frames
        JSR     print_hex
        RTS

; ============================================================================
; T01 — Slot-table active-display Mode 0 + sprites burst (Bug N°1 main).
;
; Setup: Mode 0 (Graphic I), display ON, 8x8 sprites, R5=$36 (SAT @
; $1B00), 1 sprite at SAT[0] (Y=10, opaque) so spritesActive=true. Then
; burst-write 256 bytes (0..255) into $1000-$10FF with NO pad12 between
; STA VDP_DATA — exactly the Galaga-damiers pattern at 4c gap. Read
; back, count mismatches.
;
; Result code:
;   'P' = 0 mismatches (silicon clean — surprising!)
;   'D' = >= 1 mismatch (silicon dropped — expected)
; val_T01 = mismatch count (capped at 255)
; ============================================================================
test_T01_slot_active_burst:
        ; Spawn 1 active sprite so the timing model picks Gfx12+sprites.
        JSR     setup_mode0_active_sprite

        ; --- Burst write 256 bytes at $1000-$10FF, no pad12 ---------------
        ; Each iteration: TXA / STA VDP_DATA / INX / BNE = 2+4+2+3 = 11c
        ; minus branch penalty. Effective 4c gap between consecutive
        ; STA VDP_DATA writes — silicon-fail in active Mode 0+sprites.
        JSR     vram_addr_1000
        LDX     #0
@bw:    TXA
        STA     VDP_DATA
        INX
        BNE     @bw

        ; --- Verify (read back, count mismatches) -------------------------
        JSR     vram_addr_1000_read
        LDA     #0
        STA     drop_lo
@vr:    LDA     VDP_DATA
        STX     tmp                 ; hold X
        TAY                         ; A = read byte → Y
        TXA                         ; A = expected (= X)
        STY     tmp                 ; expected (= X) → tmp wait reorder...
        ; Simpler: compare LDA result vs X directly.
        LDX     tmp
        STX     arg_lo              ; expected
        TAX                         ; got back into X
        TYA                         ; A = got
        CMP     arg_lo
        BEQ     @ok
        INC     drop_lo
        BNE     @ok
        ; (overflow → cap at 255)
        DEC     drop_lo
@ok:    LDX     arg_lo
        INX
        STX     tmp
        BNE     @vr_loop_check
        BEQ     @vr_done
@vr_loop_check:
        LDX     tmp
        TXA
        TAY
        BNE     @vr_continue
@vr_done:
        LDA     drop_lo
        STA     val_T01
        BEQ     @clean
        LDA     #'D'
        STA     res_T01
        RTS
@clean: LDA     #'P'
        STA     res_T01
        RTS
@vr_continue:
        ; should not reach here
        JMP     @vr_done

; ============================================================================
; T02 — VBlank free-bandwidth (Bug N°1 §VBlank).
;
; In VBlank with display ON, silicon's CPU access window is fully open
; (~4.3 ms continuous). Burst 256 bytes 0..255 with NO pad. Read back,
; mismatches should be 0.
;
; Result: 'P' if 0 drops, 'F' otherwise.
; ============================================================================
test_T02_vblank_free:
        JSR     setup_mode0_active_sprite

        ; Sync to VBlank entry: spin until F flag rises, then immediately
        ; burst. We have ~4.3 ms = ~4400 cycles before active display
        ; resumes — plenty for 256 STA VDP_DATA at 4c each = 1024 cycles.
        JSR     wait_frame
        JSR     vram_addr_1100
        LDX     #0
@bw:    TXA
        STA     VDP_DATA
        INX
        BNE     @bw

        ; Verify
        JSR     vram_addr_1100_read
        LDA     #0
        STA     drop_lo
        LDX     #0
@vr:    LDA     VDP_DATA
        STX     tmp
        CMP     tmp
        BEQ     @ok
        INC     drop_lo
@ok:    LDX     tmp
        INX
        BNE     @vr

        LDA     drop_lo
        BNE     @bad
        LDA     #'P'
        STA     res_T02
        RTS
@bad:   LDA     #'F'
        STA     res_T02
        RTS

; ============================================================================
; T03 — Display-blanked free-bandwidth (Bug N°1 §blanked).
;
; R1.6 = 0 (display off) → window permanently open. Same burst, expect 0.
; ============================================================================
test_T03_blank_free:
        ; Blank display
        LDA     #$80
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
        LDX     #0
@vr:    LDA     VDP_DATA
        STX     tmp
        CMP     tmp
        BEQ     @ok
        INC     drop_lo
@ok:    LDX     tmp
        INX
        BNE     @vr

        LDA     drop_lo
        BNE     @bad
        LDA     #'P'
        STA     res_T03
        RTS
@bad:   LDA     #'F'
        STA     res_T03
        RTS

; ============================================================================
; T04 — Text mode tight burst (Bug N°1 §Text).
;
; Text mode (M1=1) has wider bandwidth (~1 slot / 3 mem cycles). 256
; back-to-back STA VDP_DATA should mostly succeed. Result reports drop
; count.
; ============================================================================
test_T04_text_tight_burst:
        ; Text mode, display ON
        LDA     #$D0
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
        LDX     #0
@vr:    LDA     VDP_DATA
        STX     tmp
        CMP     tmp
        BEQ     @ok
        INC     drop_lo
@ok:    LDX     tmp
        INX
        BNE     @vr

        LDA     drop_lo
        BNE     @drops
        LDA     #'P'
        STA     res_T04
        RTS
@drops: LDA     #'D'
        STA     res_T04
        RTS

; --- T05 = Multicolor tight burst, same shape ------------------------------
test_T05_multicolor_tight_burst:
        LDA     #$C8            ; M2=1, display on, 16K
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
        LDX     #0
@vr:    LDA     VDP_DATA
        STX     tmp
        CMP     tmp
        BEQ     @ok
        INC     drop_lo
@ok:    LDX     tmp
        INX
        BNE     @vr

        LDA     drop_lo
        BNE     @drops
        LDA     #'P'
        STA     res_T05
        RTS
@drops: LDA     #'D'
        STA     res_T05
        RTS

; ============================================================================
; T06 — R1 bit 7 (4K vs 16K mode) — Bug N°3.
;
; Set R1 = $40 (display on, R1.7=0 → 4K mode). Write a sentinel ($A5)
; at VRAM $1000 (which silicon will mirror to $0000 since high 2 bits
; are masked out in 4K mode). Then read back $0000. Should match $A5.
;
; Then set R1 = $C0 (16K mode), write $5A at $1000, read $0000 — should
; NOT match (because 16K mode addresses are independent). Result PASS.
; ============================================================================
test_T06_R1_4K_16K:
        ; Phase A — 4K mode
        LDA     #$40
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; Write $A5 at $1000 → silicon truncates to $0000.
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$50
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$A5
        STA     VDP_DATA
        JSR     tms9918_pad12

        ; Read $0000 — should be $A5 if 4K mode is honoured.
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     VDP_DATA
        STA     val_T07
        CMP     #$A5
        BNE     @no
        LDA     #'Y'
        STA     res_T06
        RTS
@no:    LDA     #'N'
        STA     res_T06
        RTS

; ============================================================================
; T07 — Overscan collision (Bug N°4).
;
; 2 early-clock sprites at X=10 (real X = -22), Y=50, opaque. Wait one
; frame, read status. silicon (Nouspikel): bit 5 set ('Y'). POM1 post-
; fix: 'Y'. Pre-fix: 'N'.
; ============================================================================
test_T07_overscan_collision:
        JSR     setup_mode0_no_sprite
        JSR     fill_pat0_FF

        ; SAT[0] = (Y=49, X=10, name=0, color=$8F)
        ; SAT[1] = identical
        ; SAT[2] = $D0
        JSR     vram_addr_sat0
        LDA     #49
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

        LDA     #49
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

        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12

        LDA     VDP_CTRL        ; clear stale
        JSR     tms9918_pad12

        JSR     wait_frame
        JSR     wait_frame

        LDA     VDP_CTRL
        STA     val_T08
        AND     #$20
        BEQ     @no
        LDA     #'Y'
        STA     res_T07
        RTS
@no:    LDA     #'N'
        STA     res_T07
        RTS

; ============================================================================
; T08 — Status bits 0..4 = last sprite scanned (Bug N°6).
;
; Place 4 sprites (slots 0..3) at Y=50, terminator at SAT[4]. After a
; frame the chip should report bit 6 = 0 (no overflow) and bits 0..4 = 4
; (the SAT index of the terminator entry, the last one walked). POM1
; post-fix: 'Y'.
; ============================================================================
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
        ASL                     ; X*16
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

        LDA     #$D0            ; terminator at SAT[4]
        STA     VDP_DATA
        JSR     tms9918_pad12

        LDA     VDP_CTRL
        JSR     tms9918_pad12

        JSR     wait_frame
        JSR     wait_frame

        LDA     VDP_CTRL
        TAX                     ; full status
        AND     #$40            ; 5S bit?
        BNE     @overflow       ; unexpected — we placed only 4
        TXA
        AND     #$1F
        CMP     #4
        BNE     @wrong
        LDA     #'Y'
        STA     res_T08
        RTS
@overflow:
        LDA     #'F'
        STA     res_T08
        RTS
@wrong: LDA     #'N'
        STA     res_T08
        RTS

; ============================================================================
; T09 — Color-0 sprite collision (Nouspikel: "occurs even if the sprite
; color is transparent"). 2 sprites color = 0 (transparent), opaque
; pattern, fully overlap. Status bit 5 should set.
; ============================================================================
test_T09_color0_collision:
        JSR     setup_mode0_no_sprite
        JSR     fill_pat0_FF

        JSR     vram_addr_sat0
        LDA     #49
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #80
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$00            ; color = 0 (transparent)
        STA     VDP_DATA
        JSR     tms9918_pad12

        LDA     #49
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #80
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #0
        STA     VDP_DATA
        JSR     tms9918_pad12
        LDA     #$00
        STA     VDP_DATA
        JSR     tms9918_pad12

        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12

        LDA     VDP_CTRL
        JSR     tms9918_pad12
        JSR     wait_frame
        JSR     wait_frame

        LDA     VDP_CTRL
        AND     #$20
        BEQ     @no
        LDA     #'Y'
        STA     res_T09
        RTS
@no:    LDA     #'N'
        STA     res_T09
        RTS

; ============================================================================
; T10 — 5S latch first-occurrence (Nouspikel).
;
; 5 sprites at Y=50 (slots 0..4), 5 OTHER sprites at Y=100 (slots 5..9).
; Silicon raster scans top→bottom → first 5S overflow at Y=50 latches
; bit 6 + bits 0..4 = 4. The Y=100 group's 5S is shadowed.
; ============================================================================
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
        JMP     @y_done
@grp2:  LDA     #99
@y_done:
        STA     VDP_DATA
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
        JSR     wait_frame
        JSR     wait_frame

        LDA     VDP_CTRL
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

; ============================================================================
; T11 — Status sticky-until-readControl (Nouspikel).
;
; Cause collision. Read status TWICE in immediate succession.
;   1st read: bit 5 set
;   2nd read: bit 5 CLEAR (silicon clears bits 5/6/7 on every CTRL read)
; ============================================================================
test_T11_status_sticky:
        JSR     setup_mode0_no_sprite
        JSR     fill_pat0_FF

        JSR     vram_addr_sat0
        LDA     #49
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

        LDA     #49
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

        LDA     #$D0
        STA     VDP_DATA
        JSR     tms9918_pad12

        LDA     VDP_CTRL
        JSR     tms9918_pad12

        JSR     wait_frame
        JSR     wait_frame

        ; First read — bit 5 should be set
        LDA     VDP_CTRL
        AND     #$20
        BEQ     @no
        ; Second read — bit 5 should be clear
        LDA     VDP_CTRL
        AND     #$20
        BNE     @no             ; still set → not silicon-correct
        LDA     #'Y'
        STA     res_T11
        RTS
@no:    LDA     #'N'
        STA     res_T11
        RTS

; ============================================================================
; T12 — Sprite engine in display blank (Bug N°7, OPEN QUESTION).
;
; 2 colliding sprites, R1.6=0 (blanked). Frame. Read bit 5.
;   Y = silicon scans during blank — POM1 needs fix
;   N = silicon doesn't — POM1 already correct
; ============================================================================
test_T12_blank_sprite_scan:
        JSR     setup_mode0_no_sprite
        JSR     fill_pat0_FF

        JSR     vram_addr_sat0
        LDA     #79
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

        LDA     #79
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

        JSR     wait_frame
        JSR     wait_frame

        LDA     VDP_CTRL
        AND     #$20
        BEQ     @no
        LDA     #'Y'
        STA     res_T12
        RTS
@no:    LDA     #'N'
        STA     res_T12
        RTS

; ============================================================================
; T13 — Flip-flop reset on readControl (Bug N°9).
;
; The 2-byte register-write protocol is sensitive to a stale flip-flop
; state. Procedure:
;   1. Issue 1st byte of 2-byte write (e.g. STA #$E0 → CTRL).
;   2. Read CTRL (status read — silicon resets the flip-flop).
;   3. Issue another 2-byte write FROM SCRATCH: STA #$22 → CTRL ;
;      STA #$82 → CTRL.
;   4. Read R-2's effect by reading status (no observable VRAM effect
;      directly, but if the flip-flop was sticky, our 2nd write would
;      have been interpreted as the second byte of the abandoned 1st
;      write, and R0 might end up with the wrong value).
;
; Auto-detection: tricky without R0/R2 readback (write-only registers).
; We use VRAM addr writes instead:
;   1. STA #$00 → CTRL (intent: 1st byte of "set read addr to $0000")
;   2. LDA CTRL (status read — flip-flop resets)
;   3. STA #$00 → CTRL ; STA #$50 → CTRL  (set write addr = $1000)
;   4. STA #$A5 → DATA  (write $A5 at $1000)
;   5. Read VRAM $1000 — should be $A5 if flip-flop reset worked.
;   6. If silicon's flip-flop is sticky, step 3's first byte would be
;      the abandoned step 1's second byte, address gets corrupted.
; ============================================================================
test_T13_flipflop_reset:
        JSR     setup_mode0_no_sprite

        ; Step 1: orphan 1st byte
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; Step 2: status read — should reset flip-flop
        LDA     VDP_CTRL
        JSR     tms9918_pad12

        ; Step 3-4: clean 2-byte sequence to set write addr = $1000
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$50
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$A5
        STA     VDP_DATA
        JSR     tms9918_pad12

        ; Step 5: read back $1000
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$10
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     VDP_DATA        ; pre-fetch (read-ahead consumes nothing)
        JSR     tms9918_pad12
        LDA     VDP_DATA
        CMP     #$A5
        BEQ     @yes
        LDA     #'N'
        STA     res_T13
        RTS
@yes:   LDA     #'Y'
        STA     res_T13
        RTS

; ============================================================================
; T14 — Frame-rate measurement (Bug N°11).
;
; Spin-loop counting cycles between two consecutive F-flag rises. With
; 6502 at 1.022727 MHz and silicon at 59.94 Hz, the frame budget is
; ~17062 cycles. Inner loop = 7 cycles → ~2437 iterations / frame.
;
; We display the count as 16-bit hex on row 17. POM1 strict expects
; ~$0985 (= 2437) at 59.94 Hz, ~$0978 at the legacy 60 Hz value.
; ============================================================================
test_T14_frame_rate:
        ; Setup minimal mode (display on, IRQ off)
        LDA     #$C0
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; Sync: wait for F flag rise
        JSR     wait_frame

        ; Count cycles until next F rise. Inner loop:
        ;   INX           ; 2c
        ;   BNE @inner    ; 3c (taken) / 2c (fall)
        ;   INY           ; 2c
        ;   LDA $CC01     ; 4c
        ;   BPL @inner    ; 3c taken
        ;   = roughly 7c per visit-to-status iteration through INX wrap.
        ; We keep a 16-bit loop_ctr in (frame_lo, frame_hi).
        LDA     #0
        STA     frame_lo
        STA     frame_hi
        LDX     #0
        LDY     #0
@inner: INX                     ; 2c
        BNE     @check          ; 3c if no overflow
        INY                     ; 2c every 256 INX
@check: LDA     VDP_CTRL        ; 4c
        BPL     @inner          ; 3c taken
        ; F set — measurement done.
        STX     frame_lo
        STY     frame_hi
        STX     val_T14_lo
        STY     val_T14_hi

        ; Reasonable silicon range: $0700-$0A00. POM1 at 59.94 Hz: ~$0980.
        ; 'P' if frame_hi == 9 (matches). 'F' otherwise.
        LDA     val_T14_hi
        CMP     #$09
        BEQ     @ok
        LDA     #'F'
        STA     res_T14
        RTS
@ok:    LDA     #'P'
        STA     res_T14
        RTS

; ============================================================================
; T15 — Raster split via 5S (Bug N°10) — INTERACTIVE.
;
; Place 5 sprites at Y=95, opaque. Spin-poll bit 6. When it triggers
; (silicon: at scanline 95; POM1 pre-fix: at VBlank), change R7 to a
; vivid colour. Visual: top half = backdrop A, bottom half = backdrop B.
;
; Observer answers: do you see a TWO-COLOUR split mid-screen (Y) or
; is the screen ONE solid colour (N)?
; ============================================================================
test_T15_raster_split:
        JSR     setup_mode0_no_sprite
        JSR     fill_pat0_FF

        ; 5 sprites at Y=95, X spread
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

        ; Repeated frames of polling + R7 change. ~120 frames = ~2 sec.
        LDA     #120
        STA     loop_ctr
@frame_loop:
        ; Set R7 = $04 (dark blue) at frame start
        LDA     #$04
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; Sync to VBlank-end (= start of active)
        JSR     wait_frame
        JSR     wait_frame

        ; Clear bit 6 by reading status
        LDA     VDP_CTRL
        JSR     tms9918_pad12

        ; Now spin until bit 6 sets (= raster crosses line 95)
        LDX     #0
@poll:  LDA     VDP_CTRL
        AND     #$40
        BNE     @split
        DEX
        BNE     @poll
        ; timeout — fall through to next frame
        JMP     @next_frame
@split:
        ; Change R7 to $0C (dark green)
        LDA     #$0C
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12
@next_frame:
        DEC     loop_ctr
        BNE     @frame_loop

        ; Ask observer
        JSR     init_vdp_text
        JSR     clear_screen_text
        JSR     paint_banner
        LDA     #5
        STA     vdp_row
        LDA     #1
        STA     vdp_col
        LDA     #<q_T15_1
        LDX     #>q_T15_1
        JSR     print_string
        LDA     #6
        STA     vdp_row
        LDA     #1
        STA     vdp_col
        LDA     #<q_T15_2
        LDX     #>q_T15_2
        JSR     print_string
        LDA     #8
        STA     vdp_row
        LDA     #1
        STA     vdp_col
        LDA     #<q_yn
        LDX     #>q_yn
        JSR     print_string
        JSR     wait_yn
        STA     res_T15
        RTS

; ============================================================================
; T16 — Illegal-mode sprite cloning (Bug N°8) — INTERACTIVE.
;
; Set R0 + R1 to make M1 + M2 active simultaneously (illegal). Place 32
; sprites at Y=70, X=80 (single position). Silicon (TI/NMOS): the chip
; produces ghost-cloned sprites in Y=0..63 zone (Block 1 echo). POM1
; today: backdrop only (illegal mode).
; ============================================================================
test_T16_illegal_mode_clone:
        ; R0 = $02 (M3=1 — Mode 2 / Multicolor)
        LDA     #$02
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$80
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; R1 = $D8 (display on, M1=1 + M2=1 — illegal combo)
        LDA     #$D8
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12

        JSR     fill_pat0_FF

        ; 4 sprites stacked at Y=70, X=80
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

        ; 2 sec hold
        LDX     #120
@h:     JSR     wait_frame
        DEX
        BNE     @h

        JSR     init_vdp_text
        JSR     clear_screen_text
        JSR     paint_banner
        LDA     #5
        STA     vdp_row
        LDA     #1
        STA     vdp_col
        LDA     #<q_T16_1
        LDX     #>q_T16_1
        JSR     print_string
        LDA     #6
        STA     vdp_row
        LDA     #1
        STA     vdp_col
        LDA     #<q_T16_2
        LDX     #>q_T16_2
        JSR     print_string
        LDA     #8
        STA     vdp_row
        LDA     #1
        STA     vdp_col
        LDA     #<q_yn
        LDX     #>q_yn
        JSR     print_string
        JSR     wait_yn
        STA     res_T16
        RTS

; ============================================================================
; T17 — Mid-frame R7 rainbow (progressive raster) — INTERACTIVE.
;
; Loop changing R7 once per frame between bright values, but ALSO change
; R7 mid-frame (after a fixed delay from VBlank). Silicon (raster scan):
; horizontal split between two backdrop colours. POM1 pre-fix
; (snapshot render): solid colour each frame.
; ============================================================================
test_T17_mid_frame_rainbow:
        JSR     setup_mode0_no_sprite

        LDA     #180            ; 3 sec at 60 Hz
        STA     loop_ctr
@frame:
        ; Set R7 = $04 at frame start
        LDA     #$04
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12

        JSR     wait_frame      ; wait for VBlank end
        JSR     wait_frame

        ; Spin ~5000 cycles to reach mid-screen (line ~96)
        LDX     #0
        LDY     #5
@delay:
        DEX
        BNE     @delay
        DEY
        BNE     @delay

        ; Mid-frame R7 change
        LDA     #$0A            ; dark yellow
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad12

        DEC     loop_ctr
        BNE     @frame

        JSR     init_vdp_text
        JSR     clear_screen_text
        JSR     paint_banner
        LDA     #5
        STA     vdp_row
        LDA     #1
        STA     vdp_col
        LDA     #<q_T17_1
        LDX     #>q_T17_1
        JSR     print_string
        LDA     #6
        STA     vdp_row
        LDA     #1
        STA     vdp_col
        LDA     #<q_T17_2
        LDX     #>q_T17_2
        JSR     print_string
        LDA     #8
        STA     vdp_row
        LDA     #1
        STA     vdp_col
        LDA     #<q_yn
        LDX     #>q_yn
        JSR     print_string
        JSR     wait_yn
        STA     res_T17
        RTS

; ============================================================================
; stress_benchmark — 30-second sprite multiplexing stress test.
;
; Continuously rewrites the SAT (32 sprites × 4 bytes = 128 B) every
; frame at 4c gap (no pad12) — Galaga-class stress. Counts mismatches
; against a known-pattern (silicon: many drops; POM1: many drops in
; strict mode).
;
; No interaction. The final stress_drops + stress_frames are displayed
; on row 23 of the summary screen.
; ============================================================================
stress_benchmark:
        JSR     setup_mode0_active_sprite

        ; Reset counters
        LDA     #0
        STA     stress_drops_lo
        STA     stress_drops_hi
        STA     stress_frames

        ; Run ~1800 frames (~30 sec at 60 Hz)
@frame:
        ; Sync to VBlank
        JSR     wait_frame

        ; Stress write: 256 bytes in $1500-$15FF, no padding
        JSR     vram_addr_1500
        LDX     #0
@bw:    TXA
        STA     VDP_DATA
        INX
        BNE     @bw

        ; Verify (count mismatches)
        JSR     vram_addr_1500_read
        LDX     #0
@vr:    LDA     VDP_DATA
        STX     tmp
        CMP     tmp
        BEQ     @ok
        INC     stress_drops_lo
        BNE     @ok
        INC     stress_drops_hi
@ok:    LDX     tmp
        INX
        BNE     @vr

        INC     stress_frames
        LDA     stress_frames
        ; Stop at 180 frames (~3 sec; 1800 would be ~30 sec but binary
        ; would block too long for routine testing — ramp up later).
        CMP     #180
        BCC     @frame
        RTS

; ============================================================================
; setup_mode0_active_sprite — Mode 0, display ON, sprite at SAT[0]
; (Y=10, opaque) so the slot-table model picks Gfx12+sprites.
; ============================================================================
setup_mode0_active_sprite:
        LDA     #$C0            ; R1 display on, 16K
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$81
        STA     VDP_CTRL
        JSR     tms9918_pad12

        LDA     #$00            ; R0 = 0 → Mode 0
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$80
        STA     VDP_CTRL
        JSR     tms9918_pad12

        LDA     #$36            ; R5 = $36 → SAT @ $1B00
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$85
        STA     VDP_CTRL
        JSR     tms9918_pad12

        LDA     #$00            ; R6 = 0 → sprite pattern @ $0000
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$86
        STA     VDP_CTRL
        JSR     tms9918_pad12

        ; SAT[0] = (Y=10, X=0, name=0, color=$0F) — opaque sentinel
        ; SAT[1] = $D0 terminator
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

; ============================================================================
; setup_mode0_no_sprite — same as above but caller is expected to fill
; SAT separately (no terminator pre-written).
; ============================================================================
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

; ============================================================================
; fill_pat0_FF — 8 bytes of $FF at VRAM $0000 (sprite pattern 0).
; ============================================================================
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

; ============================================================================
; vram_addr_* — pre-baked VRAM-write address setters for the burst tests.
; ============================================================================
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
        LDA     VDP_DATA        ; read-ahead prefetch
        JSR     tms9918_pad12
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
        LDA     VDP_DATA
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
        LDA     VDP_DATA
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
        LDA     VDP_DATA
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
        LDA     VDP_DATA
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
        LDA     VDP_DATA
        JSR     tms9918_pad12
        RTS

; ============================================================================
; wait_frame — poll until F flag (bit 7) rises. Reading clears F.
; ============================================================================
wait_frame:
@spin:  LDA     VDP_CTRL
        BPL     @spin
        RTS

; ============================================================================
; wait_key_any — block until any key is pressed on the Apple-1 keyboard.
; Returns key in A (high bit cleared, ASCII-ish).
; ============================================================================
wait_key_any:
@spin:  LDA     KBDCR
        BPL     @spin
        LDA     KBD
        AND     #$7F
        RTS

; ============================================================================
; wait_yn — block until 'Y' or 'N' (case-insensitive). Returns the
; uppercase letter in A.
; ============================================================================
wait_yn:
        JSR     wait_key_any
        ; Apple-1 returns uppercase ASCII already. Accept Y/N only.
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
        RTS
@n:     LDA     #'N'
        RTS

; ============================================================================
; print_string — A:X = pointer to NUL-terminated string. Prints at
; (vdp_row, vdp_col), advancing vdp_col. Stops at NUL or col >= 40.
; ============================================================================
print_string:
        STA     str_lo
        STX     str_hi
        LDY     #0
@l:     LDA     (str_lo),Y
        BEQ     @done
        PHA
        TYA
        PHA
        JSR     name_at_rc_text
        JSR     vdp_set_write
        PLA
        TAY
        PLA
        JSR     tms9918_pad12
        STA     VDP_DATA
        INC     vdp_col
        LDA     vdp_col
        CMP     #40
        BCS     @done
        INY
        BNE     @l
@done:
        RTS

; ============================================================================
; print_hex — print A as 2 hex digits at (vdp_row, vdp_col). Advances
; vdp_col by 2. Clobbers Y, str_lo, str_hi.
; ============================================================================
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
        PHA
        JSR     name_at_rc_text
        JSR     vdp_set_write
        PLA
        JSR     tms9918_pad12
        STA     VDP_DATA
        INC     vdp_col
        RTS

; ============================================================================
; Strings
; ============================================================================
banner1:
        .byte   "TMS9918 SILICON STRICT VALIDATOR V2", 0
banner2:
        .byte   "17 TESTS ~60 SEC SEE SILICONBUGS.MD", 0
msg_press_space:
        .byte   "PRESS ANY KEY TO START THE BATTERY", 0
msg_running:
        .byte   "RUNNING -- WATCH RESULTS COLUMN ->",  0
msg_done:
        .byte   "DONE - POWER CYCLE TO RERUN", 0
msg_stress_label:
        .byte   "STRESS DROPS:0000 FR:00", 0

; The test grid: each row is the text to draw at the test row.
; '|' separates rows. NUL terminates the whole grid.
; Format: "NN NAME-PADDED-TO-29  ?     "
; Result column starts at col 32 (5 chars), painted by paint_all_results.
; Each row is up to col 31 (col 0..31 = name area, 32 = result), NUL/| ends.
grid_text:
        .byte   "01 SLOT TABLE ACTIVE MODE0   ?", '|'
        .byte   "02 VBLANK FREE BANDWIDTH     ?", '|'
        .byte   "03 BLANK FREE BANDWIDTH      ?", '|'
        .byte   "04 ACTIVE TEXT TIGHT BURST   ?", '|'
        .byte   "05 ACTIVE MULTICOL TIGHT     ?", '|'
        .byte   "06 R1B7 4K VS 16K VRAM MASK  ?", '|'
        .byte   "07 OVERSCAN COLLISION (X<0)  ?", '|'
        .byte   "08 STATUS BITS 0..4 LAST     ?", '|'
        .byte   "09 COLOR-0 SPRITE COLLISION  ?", '|'
        .byte   "10 5S LATCH FIRST OCCURRENCE ?", '|'
        .byte   "11 STATUS STICKY UNTIL READ  ?", '|'
        .byte   "12 SPRITE SCAN IN BLANK      ?", '|'
        .byte   "13 FLIP FLOP RESET ON READ   ?", '|'
        .byte   "14 FRAME RATE 59.94 NTSC     ?", '|'
        .byte   "15 RASTER SPLIT 5S VISIBLE   ?", '|'
        .byte   "16 ILLEGAL MODE SPRITE CLONE ?", '|'
        .byte   "17 MID FRAME R7 RAINBOW      ?", '|'
        .byte   0

q_yn:
        .byte   "PRESS Y OR N", 0
q_T15_1:
        .byte   "T15 RASTER SPLIT 5S", 0
q_T15_2:
        .byte   "DID YOU SEE A 2-COLOUR SPLIT?", 0
q_T16_1:
        .byte   "T16 ILLEGAL MODE SPRITE CLONE", 0
q_T16_2:
        .byte   "DID YOU SEE GHOST SPRITES?", 0
q_T17_1:
        .byte   "T17 MID FRAME R7 RAINBOW", 0
q_T17_2:
        .byte   "WERE THERE TWO COLOUR BANDS?", 0
