; =============================================
; TMS_Nyan — 6502 port of J.B. Langston's TMS9918 Nyan Cat demo
;
; Source of truth (algorithm reference):
;   github.com/jblang/TMS9918A/blob/master/examples/nyan.asm
;   - Frames: nyan/nyan.bin (12 frames × 1 536 B = 18 KB)
;   - Original artwork: Passan Kiskat by Dromedaar Vision
;     (dromedaar.com)
;
; Scope of this 6502 port (POM1 / Apple-1 + P-LAB TMS9918):
;   - Mode III multicolor (jblang's "TmsMulticolor" — register names
;     in the TMS9918 docs call this Mode I; jblang's Tms* prefix
;     uses M2 = bit 3 of R1, hence the name confusion. Same chip
;     mode either way: 64x48 colored 4x4 blocks.)
;   - Stock 4 KB Apple-1 layout. The full 18 KB animation cannot fit,
;     so we ship ONE static frame (#0) plus a delta table to frame #6.
;     Toggling between the two anchors at ~7 fps gives the cat its
;     classic bob and approximates the rainbow scroll without
;     requiring extended RAM.
;   - Polling-only V-blank sync (P-LAB stock leaves /INT floating —
;     see SILICONBUGS Bug N°2). One toggle per VSYNC_DIV vsyncs.
;
; Memory map (stock 4 KB Apple-1, low bank):
;   $0000-$002F  ZP (48 B): VDP write addr, frame state, vsync count,
;                source pointer for bulk upload
;   $0100-$01FF  6502 stack
;   $0280-$0FFF  CODE (~3.5 KB available; image ~2.6 KB)
;                 - code (~700 B)
;                 - nyan_frame0 (1 536 B static base)
;                 - 4 delta tables (4 × 102 = 408 B)
;
; Mode III layout in VRAM:
;   $0000-$05FF  Pattern table (1 536 B = 192 entries × 8 B)
;   $1800-$1AFF  Name table (32 × 24 = 768 B fan-out: rows 0-3 use
;                names 0-31, rows 4-7 use 32-63, ..., rows 20-23 use
;                160-191. Built once at boot, never touched again.)
;   $1B00-$1B7F  Sprite attribute table (we write Y=$D0 to SAT[0]
;                so sprites stay disabled — silicon-strict floor
;                drops to ~6c)
;
; Cycle budget per frame:
;   - 1 vsync wait (~17 062c worst case, but typically ~3 000c)
;   - VSYNC_DIV polling cycles between toggles (no work, just wait)
;   - Toggle: 102 deltas × ~80c/delta = ~8 200c per state-flip
;   ~7 fps with VSYNC_DIV=8 (≈ 60Hz / 8). Plenty of headroom.
;
; Keys: ESC = exit to Wozmon.
; =============================================

        .import tms9918_pad12
        .import nyan_frame0
        .import nyan_delta_off_lo, nyan_delta_off_hi
        .import nyan_delta_val_f0, nyan_delta_val_f1
        .importzp nyan_delta_count

.include "apple1.inc"
.include "tms9918.inc"

KEY_ESC   = $9B
VSYNC_DIV = 8                     ; toggle every 8 vsyncs ≈ 7.5 fps

; =============================================
.segment "ZEROPAGE"
        .res 2                    ; $00-$01 reserved (Wozmon LBVAL/HBVAL)
src_lo:  .res 1
src_hi:  .res 1
state:        .res 1              ; 0 = frame-0 in VRAM, 1 = frame-1 in VRAM
vsync_count:  .res 1
section_base: .res 1
section_cnt:  .res 1
line_cnt:     .res 1

; =============================================
.segment "CODE"
; =============================================

start:
        SEI
        CLD
        LDX #$FF
        TXS

        LDA #<greeting
        LDX #>greeting
        JSR print_str_ax

        JSR init_vdp_g3           ; Mode III + name-table fan-out
        JSR disable_sprites       ; Y=$D0 sentinel at SAT[0]
        JSR upload_frame0         ; 1 536 B pattern → VRAM $0000

        LDA #0
        STA state
        LDA #VSYNC_DIV
        STA vsync_count

main_loop:
        ; --- wait for the next VSYNC tick ---
        BIT VDP_CTRL              ; drain any stale F flag
@v_wait:
        BIT VDP_CTRL
        BPL @v_wait               ; bit 7 = 0 → not yet vsync

        ; --- check ESC between frames ---
        LDA KBDCR
        BPL @no_key
        LDA KBD
        CMP #KEY_ESC
        BEQ exit_to_wozmon
@no_key:

        ; --- tick frame divider ---
        DEC vsync_count
        BNE main_loop
        LDA #VSYNC_DIV
        STA vsync_count

        ; --- toggle state ---
        LDA state
        EOR #1
        STA state
        BEQ @apply_f0
        JSR apply_delta_f1
        JMP main_loop
@apply_f0:
        JSR apply_delta_f0
        JMP main_loop

exit_to_wozmon:
        LDA #$80                  ; R1 high byte = display OFF
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$81                  ; cmd = $80 | reg-1
        STA VDP_CTRL
        LDA KBD                   ; drain ESC
        JMP $FF00


; ----------------------------------------------------------------------------
; init_vdp_g3 — write 8 registers for Mode III, build name-table fan-out.
;
; Register values (see vdp3_regs):
;   R0 = $00 — no external video, no M3
;   R1 = $C8 — 16 KB, display ON, M2=1 (multicolor), 8x8 sprites
;   R2 = $06 — name table at $1800
;   R3 = $00 — color table base (irrelevant in Mode III, no color table)
;   R4 = $00 — pattern table at $0000
;   R5 = $36 — sprite attribute table at $1B00
;   R6 = $03 — sprite pattern table at $1800 (also irrelevant — sprites off)
;   R7 = $04 — border color = TmsDarkBlue (matches jblang's TmsBackground)
;
; Name-table fan-out: 6 sections × 4 identical lines × 32 cells. Section S
; (cell rows 4S..4S+3) uses pattern entries (32S + cell_x). Same logic as
; jblang's TmsMulticolor inner loop but unrolled for the 6502.
; ----------------------------------------------------------------------------
init_vdp_g3:
        LDX #0
@rg:    LDA vdp3_regs,X
        STA VDP_CTRL
        JSR tms9918_pad12
        TXA
        ORA #$80
        STA VDP_CTRL
        JSR tms9918_pad12
        INX
        CPX #8
        BNE @rg

        ; Set VDP write addr to $1800 (name table base).
        ;   $1800 high byte = $18, OR with $40 (write flag) = $58.
        ;   NOT $98 — that's $18 | $80 which is the REGISTER-WRITE protocol
        ;   and would silently send the writes into register $18 (which
        ;   doesn't exist) instead of VRAM at $1800. Burned an evening on
        ;   that one — chip then displays VRAM noise as the name table.
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$18 | $40
        STA VDP_CTRL
        JSR tms9918_pad12

        LDA #6
        STA section_cnt
        LDA #0
        STA section_base
@sec:
        LDA #4
        STA line_cnt
@line:
        LDX #0
@cell:
        TXA
        CLC
        ADC section_base
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #32
        BNE @cell
        DEC line_cnt
        BNE @line
        LDA section_base
        CLC
        ADC #32
        STA section_base
        DEC section_cnt
        BNE @sec
        RTS

vdp3_regs:
        .byte $00, $C8, $06, $00, $00, $36, $03, $04


; ----------------------------------------------------------------------------
; disable_sprites — Y=$D0 sentinel at SAT[0] = $1B00. Stops the chip's
; sprite scan, which lets the silicon-strict slot table relax (Mode III
; with sprites enabled would penalise our VDP write rate).
; ----------------------------------------------------------------------------
disable_sprites:
        LDA #$00                  ; low byte of $1B00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$1B | $40            ; high byte | $40 (write flag)
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$D0
        STA VDP_DATA
        RTS


; ----------------------------------------------------------------------------
; upload_frame0 — copy 1 536 bytes from nyan_frame0 (RAM) to VRAM $0000.
;   1 536 = 6 pages of 256 bytes; outer loop counts pages, inner Y wraps.
; ----------------------------------------------------------------------------
upload_frame0:
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$00 | $40            ; high byte $00 OR write flag = $40
        STA VDP_CTRL
        JSR tms9918_pad12

        LDA #<nyan_frame0
        STA src_lo
        LDA #>nyan_frame0
        STA src_hi

        LDX #6                    ; 6 pages × 256 = 1 536 bytes
@pg:    LDY #0
@by:    LDA (src_lo),Y
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @by
        INC src_hi
        DEX
        BNE @pg
        RTS


; ----------------------------------------------------------------------------
; apply_delta_fX — replay the 102 (offset, value) deltas to the pattern
;   table at $0000, switching the visible frame from the OTHER state to
;   the requested one. The delta tables are exported by nyan_data.s.
;
;   Per delta: set VDP write addr to $0000+offset, write one byte.
;   That's 4 VDP CTRL writes + 1 VDP DATA write = 5 silicon-strict gaps
;   per delta. ~80c each → 102 × 80c ≈ 8 200c per state-flip — well below
;   one frame's 17 062c budget.
; ----------------------------------------------------------------------------
apply_delta_f0:
        LDX #0
@lp:    LDA nyan_delta_off_lo,X
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA nyan_delta_off_hi,X
        ORA #$40
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA nyan_delta_val_f0,X
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #nyan_delta_count
        BNE @lp
        RTS

apply_delta_f1:
        LDX #0
@lp:    LDA nyan_delta_off_lo,X
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA nyan_delta_off_hi,X
        ORA #$40
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA nyan_delta_val_f1,X
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #nyan_delta_count
        BNE @lp
        RTS


; =============================================
greeting:
        .byte $0D
        .byte "TMS_NYAN - 6502 PORT", $0D
        .byte "ALGO: J.B. LANGSTON / DROMEDAAR", $0D
        .byte "MODE-3 MULTICOLOR (64X48 BLOCKS)", $0D
        .byte "FRAME-0 + DELTA-TO-FRAME-6", $0D
        .byte "ESC = EXIT TO WOZMON", $0D
        .byte $0D
        .byte $00

.include "print.asm"
