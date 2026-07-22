; =============================================
; TMS_Nyan_CodeTank — RLE-compressed 12-frame Nyan Cat for CodeTank.
;
; Same algorithm as the original Multiplexing-Fantasy port (no longer
; in-tree), packed for the
; 16 KB CodeTank ROM slot at $4000-$7FFF. Run-in-place from ROM —
; no RAM copy of the animation.
;
; Compression: each of the 12 frames (1 536 B raw) is RLE-encoded with
; a PackBits-flavoured format. 18 432 B → 6 830 B (37 %). The decoder
; streams bytes directly to VDP_DATA — no intermediate RAM buffer.
;
; RLE format (per frame, until 1 536 output bytes emitted):
;   header H in [0..127]:   literal — output next (H+1) bytes
;   header H in [128..255]: repeat — output (H-127) copies of next byte
;
; Decoder cost per frame:
;   ~1 536 output writes × ~22c (STA + pad12 + decoder overhead)
;     ≈ 33 800 c, well under one vsync × VSYNC_DIV = 51 186 c (DIV=3).
;
; Memory map (host Apple-1):
;   $0000-$007F  ZP (~80 B): src ptr, RLE counters, frame index, vsync
;   $0100-$01FF  6502 stack
;   $4000-$7FFF  CodeTank ROM (this binary, 16 KB slot)
;
; Run from Wozmon: `4000R`. Identical operation to the Fantasy variant
; — same 12-frame loop at VSYNC_DIV=3 (~20 fps).
;
; Keys: ESC = exit to Wozmon.
; =============================================

        .import tms9918_pad18
        .import nyan_frame_lo, nyan_frame_hi
        .importzp nyan_num_frames

.include "apple1.inc"
.include "tms9918.inc"

KEY_ESC   = $9B
VSYNC_DIV = 3                     ; ≈ 20 fps (matches jblang's nyan.asm)

; =============================================
.segment "ZEROPAGE"
        .res 2                    ; $00-$01 reserved (Wozmon LBVAL/HBVAL)
src_lo:        .res 1
src_hi:        .res 1
out_lo:        .res 1
out_hi:        .res 1
rle_cnt:       .res 1
rle_val:       .res 1
frame_idx:     .res 1
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

        JSR init_vdp_g3           ; Mode III + 6-section name-table fan-out
                                  ;   (display kept OFF by the reg loop)
        JSR disable_sprites       ; Y=$D0 sentinel + $D1 scrub at SAT[0..]

        LDA #0
        STA frame_idx
        JSR upload_current_frame  ; decode frame 0 while still blanked

        ; Display ON only now — tables, SAT and frame 0 are all valid, so
        ; real silicon never flashes power-on VRAM garbage / ghost sprites
        ; (best-practices §1: init order is display-off → tables → SAT →
        ; display-on). The blanked init also rides the free ScreenOff slot
        ; table, so the ~3 KB of setup writes cannot drop.
        LDA #$C8                  ; R1 = 16K | display ON | M2 (Multicolor)
        STA VDP_CTRL
        JSR tms9918_pad18
        LDA #$81
        STA VDP_CTRL
        JSR tms9918_pad18

        LDA #VSYNC_DIV
        STA vsync_count

main_loop:
        ; VBlank sync via the shared hang-proof macro (drain + bounded
        ; poll). The old manual `BIT/BPL @v_wait` spin here was
        ; UNBOUNDED — on silicon revisions that occasionally miss the
        ; F flag (the TMS_Rogue black-screen class, see tms9918.inc)
        ; it froze the animation for good.
        WAIT_VBLANK_SAFE

        LDA KBDCR
        BPL @no_key
        LDA KBD
        CMP #KEY_ESC
        BEQ exit_to_wozmon
@no_key:

        DEC vsync_count
        BNE main_loop
        LDA #VSYNC_DIV
        STA vsync_count

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
        LDA #$80
        STA VDP_CTRL
        JSR tms9918_pad18
        LDA #$81
        STA VDP_CTRL
        LDA KBD
        JMP WOZMON


; ----------------------------------------------------------------------------
; upload_current_frame — set VDP write addr to $0000, then RLE-decode
;   nyan_frame[frame_idx] streaming into VDP_DATA.
; ----------------------------------------------------------------------------
upload_current_frame:
        ; src = nyan_frame{frame_idx}
        LDX frame_idx
        LDA nyan_frame_lo,X
        STA src_lo
        LDA nyan_frame_hi,X
        STA src_hi

        ; Set VDP write addr to $0000.
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad18
        LDA #$00 | $40            ; $40 = write flag (NOT $80 = register-write)
        STA VDP_CTRL
        JSR tms9918_pad18

        ; fall through to rle_decode


; ----------------------------------------------------------------------------
; rle_decode — stream-decode 1 536 bytes to VDP_DATA from (src_lo:hi).
;   Outputs are tracked via a 16-bit countdown (out_lo:out_hi) so the
;   decoder stops cleanly at 1 536 regardless of where the source ends.
;   Y indexes within the current source page; src_hi advances on Y wrap.
; ----------------------------------------------------------------------------
rle_decode:
        LDA #$00
        STA out_lo
        LDA #$06                  ; 1 536 = $0600
        STA out_hi
        LDY #0
@chunk:
        LDA (src_lo),Y            ; header byte; A's N flag = bit 7 of header
        BMI @repeat               ; MUST branch BEFORE the INY/INC that follows —
                                  ; INY clobbers N (reflects Y after increment),
                                  ; so testing BMI after would branch on Y's bit 7.
                                  ; A itself is preserved across INY/INC, so we
                                  ; can reuse it after the src advance below.
        ; advance src past the header byte (literal path)
        INY
        BNE @hdr_lit_done
        INC src_hi
@hdr_lit_done:
        ; --- LITERAL: count = A + 1 (1..128) ---
        STA rle_cnt
        INC rle_cnt
@lit_lp:
        LDA (src_lo),Y
        STA VDP_DATA
        JSR tms9918_pad18
        INY
        BNE @lit_no_inc
        INC src_hi
@lit_no_inc:
        ; decrement output countdown
        LDA out_lo
        BNE @lit_no_borrow
        DEC out_hi
@lit_no_borrow:
        DEC out_lo
        LDA out_hi
        ORA out_lo
        BEQ @done
        DEC rle_cnt
        BNE @lit_lp
        JMP @chunk

@repeat:
        ; --- REPEAT: count = A - 127 (1..128) ---
        ; A still holds the header (128..255). Advance src past it first.
        INY
        BNE @hdr_rep_done
        INC src_hi
@hdr_rep_done:
        SEC
        SBC #127
        STA rle_cnt
        ; Read the value-to-repeat byte, then advance src past it.
        LDA (src_lo),Y
        STA rle_val
        INY
        BNE @rep_no_inc
        INC src_hi
@rep_no_inc:
@rep_lp:
        LDA rle_val
        STA VDP_DATA
        JSR tms9918_pad18
        LDA out_lo
        BNE @rep_no_borrow
        DEC out_hi
@rep_no_borrow:
        DEC out_lo
        LDA out_hi
        ORA out_lo
        BEQ @done
        DEC rle_cnt
        BNE @rep_lp
        JMP @chunk

@done:
        RTS


; ----------------------------------------------------------------------------
; init_vdp_g3 — Mode III register init + 6-section name-table fan-out.
; ----------------------------------------------------------------------------
init_vdp_g3:
        LDX #0
@rg:    LDA vdp3_regs,X
        CPX #1
        BNE @nomask
        AND #$BF                  ; R1: keep display OFF for the whole init —
                                  ; hides power-on VRAM garbage on real
                                  ; silicon and opens the free ScreenOff
                                  ; write window (mirrors init_vdp_g1).
                                  ; start: re-arms $C8 after frame 0.
@nomask:
        STA VDP_CTRL
        JSR tms9918_pad18
        TXA
        ORA #$80
        STA VDP_CTRL
        JSR tms9918_pad18
        INX
        CPX #8
        BNE @rg

        ; Set VDP write addr to $1800 (name table base).
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad18
        LDA #$18 | $40            ; = $58 (write flag), NOT $98 (register-write)
        STA VDP_CTRL
        JSR tms9918_pad18

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
        JSR tms9918_pad18
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
; disable_sprites — defensive SAT init at $1B00. $D0 at SAT[0].Y terminates the
;   chain; SAT[1..127].Y = $D1 (off-screen) scrubs power-on noise. A lone $D0
;   was observed insufficient under POM1 silicon-strict (ghost sprites from
;   noise SAT entries past slot 0) — matches dev/lib/tms9918/tms9918m1.asm.
; ----------------------------------------------------------------------------
disable_sprites:
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad18
        LDA #$1B | $40
        STA VDP_CTRL
        JSR tms9918_pad18
        LDA #$D0            ; SAT[0].Y = chain terminator
        STA VDP_DATA
        JSR tms9918_pad18
        LDX #127           ; SAT[1..127].Y = $D1 off-screen via auto-increment
        LDA #$D1
@sat:   STA VDP_DATA
        JSR tms9918_pad18
        DEX
        BNE @sat
        RTS
