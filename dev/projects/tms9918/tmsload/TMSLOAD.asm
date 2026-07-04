; ============================================================================
; TMSLOAD.asm -- display one TMS9918 VRAM image on the P-LAB Graphic Card.
; ============================================================================
; The TMS Paint editor exports a raw 16 KB VRAM dump (offset i -> VRAM $i). That
; dump is NOT directly loadable onto the screen because the TMS9918 VRAM sits
; behind the $CC00/$CC01 ports, not on the CPU bus. Workflow on the Apple-1
; (microSD + TMS9918 both plugged, >= ~24 KB RAM):
;
;     CD TMS            ; enter sdcard/TMS/ in the SD CARD OS
;     @L NAME           ; SD OS drops the 16 KB image into CPU RAM at $0800
;     @R TMSLOAD        ; (or @R TMSLOADM) -> this program copies it into VRAM
;
; This program sets the editor's 8 canonical registers for the mode, copies the
; 16384 bytes from $0800 into VRAM $0000 (display blanked during the burst so the
; silicon-strict Gfx-II slots don't drop bytes -- exactly like init_vdp_g2), then
; re-enables the display and returns to the Woz monitor.
;
; Two builds from this one source:
;   default          -> TMSLOAD   (Graphics II registers)
;   -D MULTICOLOR    -> TMSLOADM  (Multicolor registers)
; The register bytes mirror tmspaint/TmsPaintModel.cpp::canonicalRegisters.
; ============================================================================

        .import tms9918_pad18       ; silicon-strict 18c cushion (tms9918_pad.asm)
        .include "tms9918.inc"      ; VDP_DATA=$CC00, VDP_CTRL=$CC01

WOZMON  = $FF1A                     ; Woz monitor prompt entry
IMG     = $0800                     ; where `@L NAME` drops the 16 KB image

.segment "ZEROPAGE"
src_lo: .res 1
src_hi: .res 1

.segment "CODE"

; ---- entry (first byte, loaded + run at $0300) -----------------------------
start:
        JSR     tms9918_pad18       ; cross-entry cushion (harmless if none needed)

        ; --- program the 8 registers, R1 display-enable OFF during the copy ---
        LDX     #0
@rg:    LDA     regs,X
        CPX     #1
        BNE     @rg_store
        AND     #$BF                ; R1: clear bit6 (display) while copying
@rg_store:
        STA     VDP_CTRL            ; value
        TXA
        ORA     #$80                ; index | $80
        JSR     tms9918_pad18       ; back-to-back CTRL cushion
        STA     VDP_CTRL
        INX
        CPX     #8
        JSR     tms9918_pad18
        BNE     @rg

        ; --- VRAM write address = $0000 (auto-increment) ---
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad18
        LDA     #$40                ; $0000 | $4000 = write mode
        STA     VDP_CTRL
        JSR     tms9918_pad18

        ; --- copy 16384 bytes IMG -> VDP_DATA (display OFF -> no per-byte pad) ---
        LDA     #<IMG
        STA     src_lo
        LDA     #>IMG
        STA     src_hi
        LDX     #64                 ; 64 pages * 256 = 16384
@pg:    LDY     #0
@by:    LDA     (src_lo),Y
        STA     VDP_DATA            ; ScreenOff drain ~2c << inner gap -> safe
        INY
        BNE     @by
        INC     src_hi
        DEX
        BNE     @pg

        ; --- re-enable display: write the real R1 value ---
        LDA     regs+1
        STA     VDP_CTRL
        JSR     tms9918_pad18
        LDA     #$81                ; index 1 | $80
        STA     VDP_CTRL

        JMP     WOZMON

; ---- editor canonical registers (TmsPaintModel.cpp::canonicalRegisters) -----
regs:
.ifdef MULTICOLOR
        .byte   $00, $C8, $06, $00, $00, $36, $07, $01   ; Multicolor
.else
        .byte   $02, $C0, $06, $FF, $03, $36, $07, $01   ; Graphics II
.endif
