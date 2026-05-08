; =============================================
; TMS_Nyan_Fantasy — full 12-frame port of jblang's TMS9918 Nyan Cat.
;
; Same algorithm as dev/projects/tms9918_nyan/ but uses the FULL
; animation: 12 frames × 1 536 B = 18 432 B baked in. Targets the
; POM1 Multiplexing Fantasy preset (12 or 14) — needs ≥24 KB of
; contiguous RAM at $0280, which only Fantasy provides.
;
; Animation:
;   Each vsync, decrement vsync_count. When it hits zero, advance
;   frame_idx (wraps 0..11) and bulk-copy the new frame's 1 536 B
;   into VRAM pattern table at $0000. VSYNC_DIV = 3 matches jblang's
;   z80 nyan.asm "VsyncDiv equ 3" (≈ 20 fps).
;
; Cycle budget per frame upload:
;   1 536 bytes × ~22c per byte ≈ 33 800 c, well under one frame's
;   17 062 c-per-vsync × VSYNC_DIV = 51 186 c budget. Comfortable
;   headroom for adding music or input later.
;
; Memory map:
;   $0000-$007F  ZP (128 B): VDP write addr, frame index, vsync count
;   $0100-$01FF  6502 stack
;   $0280-$5FFF  CODE + 12 frames + delta-free static (~19 KB used)
;
; Keys: ESC = exit to Wozmon.
; =============================================

        .import tms9918_pad12
        .import nyan_frames
        .importzp nyan_num_frames, nyan_frame_size_pages

.include "apple1.inc"
.include "tms9918.inc"

KEY_ESC   = $9B
VSYNC_DIV = 3                     ; matches jblang's nyan.asm (≈ 20 fps)

; =============================================
.segment "ZEROPAGE"
        .res 2                    ; $00-$01 reserved
src_lo:        .res 1
src_hi:        .res 1
frame_idx:     .res 1             ; 0..nyan_num_frames-1
vsync_count:   .res 1
section_base:  .res 1
section_cnt:   .res 1
line_cnt:      .res 1

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

        JSR init_vdp_g3           ; Mode III + 6-section name-table fan-out
        JSR disable_sprites       ; Y=$D0 sentinel at SAT[0]

        LDA #0
        STA frame_idx
        JSR upload_current_frame  ; show frame 0 immediately

        LDA #VSYNC_DIV
        STA vsync_count

main_loop:
        ; --- wait for next vsync ---
        BIT VDP_CTRL              ; drain stale F flag
@v_wait:
        BIT VDP_CTRL
        BPL @v_wait

        ; --- check ESC between frames ---
        LDA KBDCR
        BPL @no_key
        LDA KBD
        CMP #KEY_ESC
        BEQ exit_to_wozmon
@no_key:

        ; --- divide vsync rate down ---
        DEC vsync_count
        BNE main_loop
        LDA #VSYNC_DIV
        STA vsync_count

        ; --- advance frame, wrap at NUM_FRAMES ---
        LDA frame_idx
        CLC
        ADC #1
        CMP #nyan_num_frames
        BCC @no_wrap
        LDA #0
@no_wrap:
        STA frame_idx
        JSR upload_current_frame
        JMP main_loop

exit_to_wozmon:
        LDA #$80                  ; R1 = display OFF
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$81
        STA VDP_CTRL
        LDA KBD
        JMP $FF00


; ----------------------------------------------------------------------------
; upload_current_frame — copy 1 536 bytes from nyan_frames + frame_idx*1 536
;   to VRAM pattern table at $0000.
;
; Frame stride = 6 pages of 256 = $0600. We compute src_hi by adding
; (frame_idx * 6) to the base high byte; the low byte stays $00.
; ----------------------------------------------------------------------------
upload_current_frame:
        ; src = nyan_frames + frame_idx * $0600
        LDA #<nyan_frames
        STA src_lo
        LDA #>nyan_frames
        STA src_hi

        ; Add (frame_idx * 6) to src_hi.
        LDX frame_idx
        BEQ @addr_done
@add_step:
        CLC
        LDA src_hi
        ADC #nyan_frame_size_pages
        STA src_hi
        DEX
        BNE @add_step
@addr_done:

        ; Set VDP write addr to $0000.
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$00 | $40            ; high byte $00 OR $40 (write flag)
        STA VDP_CTRL
        JSR tms9918_pad12

        LDX #nyan_frame_size_pages
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
; init_vdp_g3 — same as the 4 KB variant. Mode III + name-table fan-out.
;
; Register values:
;   R0=$00, R1=$C8 (16K, display, M2), R2=$06 (name $1800), R3=$00,
;   R4=$00 (pattern $0000), R5=$36, R6=$03, R7=$04 (dark blue border).
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
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$18 | $40            ; $58 — VRAM-write protocol, NOT $98 (= $80 register-write)
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
; disable_sprites — Y=$D0 sentinel at SAT[0] = $1B00.
; ----------------------------------------------------------------------------
disable_sprites:
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$1B | $40
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$D0
        STA VDP_DATA
        RTS


; =============================================
greeting:
        .byte $0D
        .byte "TMS_NYAN_FANTASY - 12 FRAMES", $0D
        .byte "ALGO: J.B. LANGSTON / DROMEDAAR", $0D
        .byte "MODE-3 MULTICOLOR (64X48 BLOCKS)", $0D
        .byte "REQUIRES POM1 FANTASY PRESET", $0D
        .byte "ESC = EXIT TO WOZMON", $0D
        .byte $0D
        .byte $00

.include "print.asm"
