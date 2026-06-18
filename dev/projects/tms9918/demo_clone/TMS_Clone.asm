; =============================================
; TMS_Clone — sprite cloning bug exhibition (hap-faithful, Mode II)
; P-LAB TMS9918 Graphic Card / POM1 / Apple 1
;
; 6502 port of the BASIC test program by hap (meisei author)
; published in openMSX issue #593:
;
;   10 color15,1,1: screen 2,3: definta-z:
;      sprite$(0) = string$(32, chr$(255)): open"grp:" as #1
;   20 line(0,0)-(255,0),4: line(0,128)-(255,128),4: line(0,64)-(255,64),4
;   30 y=0: vdp(4)=0: vdp(3)=159+64+32
;   40 i=stick(0): if i=1 then y=y-1 else if i=5 then y=y+1
;   50 y=y and 255: putsprite 31, (0,y), 15, 0
;   60 line(100,30)-(130,40),1,bf: preset(100,30): print#1,y
;   70 goto 40
;
; This demo exercises the **silicon-correct** sprite cloning
; (Bug N°8) modelled by POM1's renderCloneSpritesLineRaw — which
; was rewritten in 2026-05 to a verbatim port of meisei vdp.c
; (the only known correct implementation per hap).
;
; SETUP (matches hap):
;   - Mode II (Graphics II / bitmap, M3=1) — fully LEGAL mode.
;     Cloning fires NOT because of hybrid-mode but because the
;     R4 condition `(R4 & 3) != 3` is met.
;   - R4 = $00       — pattern table mirror (max cloning effect)
;   - R3 = $00       — colour table mirror   (hap's setup)
;   - R7 = $04       — backdrop = dark blue (so the white sprite
;                       is visible against a non-black background)
;   - R1 includes sprite 16x16 + magnify ($C3 = 32x32 effective)
;   - Sprite pattern 0 = 32 bytes of $FF (fully lit 16x16 block)
;   - SAT[0..30] = off-screen (Y=$D1, color=transparent)
;   - SAT[31]    = visible white block at (X=0, Y=cur_y), animates
;                  vertically by 1 px per frame, wraps at 256
;
; EXPECTED RENDERING on POM1 silicon-strict (post-meisei port):
;   - As cur_y sweeps Y = 0..255, sprite #31 moves vertically.
;   - Cloning blocks per meisei algorithm:
;       block 0 (Y=0..63):    no cloning (sprites 0..7 only path —
;                              sprite #31 doesn't appear here as a
;                              clone, but its NORMAL render shows
;                              when cur_y maps here).
;       block 1 (Y=64..127):  cloning iff R4 bit 0 = 0 → ACTIVE.
;                              cm[0]=$3F, cm[5]=(~R3 << 1) & $40
;       block 2 (Y=128..191): cloning iff R4 bit 1 = 0 → ACTIVE.
;                              cm[1]=cm[4]=$80, cm[5]=(R3 << 1) & $80
;     With R3=0:
;       block 1: cm[5] = (~0 << 1) & $40 = $40
;       block 2: cm[5] = (0 << 1)  & $80 = $00
;   - You should see the sprite move + ghost copies cascade in
;     blocks 1 and 2 according to silicon-correct yc formula.
;
; Compare against openMSX (which uses meisei's exact algorithm)
; running the same BASIC program on an MSX1 (or MSX2 with
; PALMODE=NTSC). Result should match pixel-for-pixel.
;
; Keys: ESC = exit
; =============================================

        .import init_vdp_g2, clear_bitmap, disable_sprites
        .import vdp_set_write
        .importzp pix_addr_lo, pix_addr_hi
        .import tms9918_pad12

.include "apple1.inc"
.include "tms9918.inc"

KEY_ESC = $9B

; ----- Sprite slot we animate (matches hap's PUTSPRITE 31) -----
ANIMATED_SLOT = 31

.segment "ZEROPAGE"
        .res 2                  ; $00-$01 reserved
tmp:    .res 1
tmp2:   .res 1                  ; required by tms9918m2.asm (line_xy/etc)
.exportzp tmp, tmp2

cur_y:  .res 1

; plot_mode must be ABSOLUTE (the lib's .import expects abs).
.segment "BSS"
plot_mode: .res 1
.export plot_mode

.segment "CODE"

start:
        SEI
        CLD
        LDX #$FF
        TXS

        JSR init_vdp_g2           ; Mode II init from the lib
        JSR clear_bitmap          ; zero the 6 KB bitmap pattern table
        JSR disable_sprites       ; SAT[0].Y = $D0 (we'll overwrite below)

        ; --- Override R3 = $00 (colour table mirror) ---
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$83                  ; cmd = $80 | reg-3
        STA VDP_CTRL
        JSR tms9918_pad12

        ; --- Override R4 = $00 (pattern table mirror — hap setup) ---
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$84                  ; cmd = $80 | reg-4
        STA VDP_CTRL
        JSR tms9918_pad12

        ; --- Override R1 = $C3 (16K + display ON + sprite 16x16 + magnify) ---
        LDA #$C3
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$81                  ; cmd = $80 | reg-1
        STA VDP_CTRL
        JSR tms9918_pad12

        ; --- Override R7 = $04 (dark-blue backdrop, visible behind sprite) ---
        LDA #$04
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$87                  ; cmd = $80 | reg-7
        STA VDP_CTRL
        JSR tms9918_pad12

        ; --- Upload sprite pattern 0 = 32 bytes of $FF ---
        ; SPGT base = R6 << 11 = $03 << 11 = $1800.
        ; 16x16 sprites read pattern names 0/1/2/3 → 32 bytes from $1800.
        LDA #$00
        STA pix_addr_lo
        LDA #$18
        STA pix_addr_hi
        JSR vdp_set_write
        LDX #32
        LDA #$FF
@p:     STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @p

        ; --- Init SAT slots 0..30: off-screen (Y=$D1, NOT $D0
        ;     so the chain continues to slot 31). Color = 0
        ;     (transparent — defensive, even off-screen we don't
        ;     want stray pixels).
        ;     SAT base in Mode II = R5 << 7 = $76 << 7 = $3B00. ---
        LDA #$00
        STA pix_addr_lo
        LDA #$3B
        STA pix_addr_hi
        JSR vdp_set_write

        LDX #0
@off:   LDA #$D1
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$00                  ; X
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$00                  ; pattern
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$00                  ; colour = transparent
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #31                   ; slots 0..30 (31 entries)
        BNE @off

        ; --- Animate cur_y starting at 0 ---
        LDA #0
        STA cur_y

main_loop:
        WAIT_VBLANK               ; ~30 fps render budget on POM1

        ; ESC check
        LDA KBDCR
        BPL @no_key
        LDA KBD
        CMP #KEY_ESC
        BEQ exit_to_wozmon
@no_key:

        ; --- Write SAT[31] = visible sprite at (X=0, Y=cur_y) ---
        ; SAT[31] address = $3B00 + 31*4 = $3B7C
        LDA #$7C
        STA pix_addr_lo
        LDA #$3B
        STA pix_addr_hi
        JSR vdp_set_write

        LDA cur_y
        SEC
        SBC #1                    ; Y_attr = cur_y - 1 (TMS9918 displays at Y_attr+1)
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0                    ; X = 0 (matches PUTSPRITE 31, (0, y))
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0                    ; pattern name = 0
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0F                  ; colour = white, no early clock
        STA VDP_DATA
        JSR tms9918_pad12

        INC cur_y                 ; sweep Y = 0..255 then wrap
        JMP main_loop

exit_to_wozmon:
        LDA #$80                  ; R1 high byte = display OFF
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$81
        STA VDP_CTRL
        JSR tms9918_pad12
        JSR disable_sprites
        LDA KBD                   ; drain ESC
        JMP WOZMON
