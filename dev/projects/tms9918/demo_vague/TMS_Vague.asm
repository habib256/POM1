; ============================================================================
; TMS_Vague.asm  --  "Boat on a wave", tile-based MSX1 technique
; ----------------------------------------------------------------------------
; P-LAB TMS9918 Graphic Card / POM1 / Apple 1, Mode 1 (Graphics I).
;
; This is the canonical MSX1-scene way of doing a sea / wave effect:
;
;   * The wave surface is NOT made of sprites. It is the BACKGROUND tile
;     map. 9 "water-height" tile patterns are pre-uploaded (#0 = empty sky,
;     #1 = 1 row of water at the bottom, ... #8 = full-water cell). Each
;     frame, for every column of the 3-row wave band we write the right
;     water-height tile based on the sine LUT, and the rows below stay
;     filled with tile #8 (solid water). Result: a real water column from
;     the wavy surface all the way down to the bottom of the screen.
;
;   * The boat is ONE 16x16 hardware sprite (slot 0 in the SAT, single
;     attribute record + a $D0 terminator). It samples the SAME sine
;     function at the boat's current X so its hull stays glued to the
;     wave's local height.
;
; Per-frame work (~5.5k cycles on a 1.022 MHz 6502 -> ~32% CPU):
;   PHASE 1 (active display, no VRAM)  : compute wave_y_buf[32] from sine
;   PHASE 2 (active display, no VRAM)  : compute tile_buf[96] for 3 rows
;   WAIT_VBLANK                        : drain stale F flag, wait raster
;   PHASE 3 (vblank, VRAM bandwidth)   : stream 96 bytes into name-table
;                                        rows 11..13 (one address-load +
;                                        auto-increment through to row 14)
;   PHASE 4 (vblank, VRAM bandwidth)   : 4 bytes of boat SAT + $D0 term
;
; Keys: ESC -> exit to Wozmon.
; ============================================================================

        .import init_vdp_g1, disable_sprites, clear_name_table
        .import tms9918_pad12

.include "apple1.inc"
.include "tms9918.inc"

KEY_ESC      = $9B

; ----- wave geometry -------------------------------------------------------
; Wave zone occupies rows 11..13 (pixel Y 88..111).
; Static water below: rows 14..23 (pixel Y 112..191) -> fixed tile $08.
; Static sky above:   rows 0..10  (pixel Y 0..87)    -> fixed tile $00.
;
; Sine LUT amplitude: +/- 8 (so wave_y always falls inside the 3-row band).
; WAVE_BASELINE_PIX = water-surface pixel Y at sine = 0.
WAVE_BASELINE_PIX = 96
WAVE_ROW_11_TOP   = 88            ; pixel Y of top of row 11
WAVE_ROW_12_TOP   = 96            ; pixel Y of top of row 12
WAVE_ROW_13_TOP   = 104           ; pixel Y of top of row 13
WAVE_ROW_11_BOT_P1 = 96           ; row_top + 8 (one past bottom)
WAVE_ROW_12_BOT_P1 = 104
WAVE_ROW_13_BOT_P1 = 112

; Boat: hull bottom = sprite Y_attr + 14 (hull pixels are rows 10..13 of the
; 16x16 sprite + the chip's +1 display offset). To plant the hull on the
; wave surface: Y_attr = wave_y - 14.
BOAT_RISE    = 14

; Boat horizontal pingpong
BOAT_X_MIN   = 24
BOAT_X_MAX   = 216
BOAT_SUB_MAX = 1                  ; advance boat_x every (BOAT_SUB_MAX+1) frames

PHASE_SPEED  = 1                  ; sine phase advance per frame

; ----- ZP ------------------------------------------------------------------
.segment "ZEROPAGE"
        .res 2                    ; $00-$01 reserved
tmp:    .res 1
phase:    .res 1
boat_x:   .res 1
boat_dir: .res 1
boat_sub: .res 1
.exportzp tmp

; ----- BSS -----------------------------------------------------------------
.segment "BSS"
wave_y_buf: .res 32              ; per-column water-surface pixel Y
tile_buf:   .res 96              ; 3 rows * 32 cols of tile bytes (stream order)

; ----- CODE ----------------------------------------------------------------
.segment "CODE"

start:
        SEI
        CLD
        LDX #$FF
        TXS

        JSR init_vdp_g1
        JSR disable_sprites
        JSR clear_name_table

        ; --- R1 = $C2 (16K + display ON + 16x16 sprites, no magnify) ---
        LDA #$C2
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$81
        STA VDP_CTRL
        JSR tms9918_pad12

        ; --- R7 = $05 (backdrop = light blue; mostly invisible since every
        ;     tile in the name table covers it, but a clean value avoids
        ;     bleeding on the very first frame before init finishes).
        LDA #$05
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$87
        STA VDP_CTRL
        JSR tms9918_pad12

        ; --- Colour table $2000:
        ;     entry 0 (tiles 0..7)  = $45  -> FG=dark blue, BG=light blue
        ;                                     tile 0 (all 0) -> all BG = sky
        ;                                     tile 1..7      -> mix water/sky
        ;     entry 1 (tiles 8..15) = $44  -> FG=BG=dark blue
        ;                                     tile 8 (all 1) -> all FG = water
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$60                  ; $20 | $40 -> write at $2000
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$45
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$44
        STA VDP_DATA
        JSR tms9918_pad12
        LDX #30
        LDA #$00
@ct:    STA VDP_DATA
        JSR tms9918_pad12
        DEX
        BNE @ct

        ; --- Pattern table $0000: upload 9 water-height tiles (#0..#8).
        ;     Tile N has top (8-N) rows of $00 (sky) and bottom N rows of
        ;     $FF (water). Written via auto-increment from $0000.
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$40                  ; $00 | $40 -> write at $0000
        STA VDP_CTRL
        JSR tms9918_pad12
        LDY #0
@pt:    LDA tile_patterns,Y
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        CPY #72
        BNE @pt

        ; --- Sprite pattern table $3800: upload boat (4 quadrants = 32 bytes).
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$78                  ; $38 | $40 -> write at $3800
        STA VDP_CTRL
        JSR tms9918_pad12
        LDY #0
@sp:    LDA boat_pattern,Y
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        CPY #32
        BNE @sp

        ; --- Name table rows 14..23 -> tile $08 (solid water).
        ;     addr = $1800 + 14*32 = $1800 + $1C0 = $19C0
        ;     10 rows * 32 cols = 320 bytes (fits before SAT at $1B00).
        LDA #$C0
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$59                  ; $19 | $40 -> write at $19C0
        STA VDP_CTRL
        JSR tms9918_pad12
        ; 320 bytes = 1 full page + 64 bytes
        LDY #0
        LDA #$08
@w1:    STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @w1
        LDY #0
@w2:    LDA #$08
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        CPY #64                   ; 320 - 256
        BNE @w2

        LDA #0
        STA phase
        STA boat_dir
        STA boat_sub
        LDA #BOAT_X_MIN+40
        STA boat_x

; ============================================================================
main_loop:
        ; --- PHASE 1 + 2 run during active display (RAM-only, no VRAM) ----
        JSR compute_wave_y
        JSR compute_tile_buf

        WAIT_VBLANK

        ; ESC?
        LDA KBDCR
        BPL @no_key
        LDA KBD
        CMP #KEY_ESC
        BEQ exit
@no_key:

        ; --- PHASE 3: stream tile_buf -> name table rows 11..13 ----------
        ;     addr = $1800 + 11*32 = $1960
        LDA #$60
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$59                  ; $19 | $40 -> write at $1960
        STA VDP_CTRL
        JSR tms9918_pad12
        LDY #0
@p3:    LDA tile_buf,Y
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        CPY #96
        BNE @p3

        ; --- PHASE 4: boat SAT slot 0 + $D0 terminator at slot 1 ---------
        JSR write_boat_sat

        ; --- advance frame state -----------------------------------------
        LDA phase
        CLC
        ADC #PHASE_SPEED
        STA phase

        INC boat_sub
        LDA boat_sub
        CMP #(BOAT_SUB_MAX+1)
        BCC @bd
        LDA #0
        STA boat_sub
        LDA boat_dir
        BMI @bl
        INC boat_x
        LDA boat_x
        CMP #BOAT_X_MAX
        BCC @bd
        LDA #$FF
        STA boat_dir
        JMP @bd
@bl:    DEC boat_x
        LDA boat_x
        CMP #BOAT_X_MIN
        BCS @bd
        LDA #$00
        STA boat_dir
@bd:    JMP main_loop


exit:
        LDA #$80                  ; display OFF
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$81
        STA VDP_CTRL
        JSR tms9918_pad12
        JSR disable_sprites
        LDA KBD
        JMP $FF00


; ----------------------------------------------------------------------------
; compute_wave_y: for each column C = 0..31, compute
;   wave_y_buf[C] = WAVE_BASELINE_PIX + sine_table[(C*2 + phase) & $3F]
; SLOPE = 2 sine units per column -> 32 cols * 2 = 64 = exactly 1 full
; wave cycle across the 256-px screen, no visible seam at the right edge.
; ----------------------------------------------------------------------------
compute_wave_y:
        LDX #0
@cw:    TXA
        ASL                       ; A = C * 2
        CLC
        ADC phase
        AND #$3F
        TAY
        LDA sine_table,Y
        CLC
        ADC #WAVE_BASELINE_PIX
        STA wave_y_buf,X
        INX
        CPX #32
        BNE @cw
        RTS


; ----------------------------------------------------------------------------
; compute_tile_buf: for each of the 3 wave-zone rows (11, 12, 13), for each
; column 0..31, decide which water-height tile (0..8) goes there based on
; wave_y_buf[C]:
;
;   if wy <  row_top:    tile = 8     (cell entirely below water surface)
;   if wy >= row_top+8:  tile = 0     (cell entirely above water surface)
;   else:                tile = (row_top + 8) - wy   (1..8, partial cell)
;
; Tiles get streamed in name-table row-major order so PHASE 3 can dump them
; with a single auto-increment write.
; ----------------------------------------------------------------------------
compute_tile_buf:
        LDX #0                    ; col index 0..31
        LDY #0                    ; tile_buf write index 0..95

        ; --- row 11 -------------------------------------------------------
@r11l:  LDA wave_y_buf,X
        CMP #WAVE_ROW_11_TOP
        BCC @r11w
        CMP #WAVE_ROW_11_BOT_P1
        BCC @r11p
        LDA #0                    ; sky
        BEQ @r11s
@r11w:  LDA #8                    ; full water
        BNE @r11s
@r11p:  LDA #WAVE_ROW_11_BOT_P1
        SEC
        SBC wave_y_buf,X          ; A = (row_top+8) - wy  -> 1..8
@r11s:  STA tile_buf,Y
        INY
        INX
        CPX #32
        BNE @r11l

        ; --- row 12 -------------------------------------------------------
        LDX #0
@r12l:  LDA wave_y_buf,X
        CMP #WAVE_ROW_12_TOP
        BCC @r12w
        CMP #WAVE_ROW_12_BOT_P1
        BCC @r12p
        LDA #0
        BEQ @r12s
@r12w:  LDA #8
        BNE @r12s
@r12p:  LDA #WAVE_ROW_12_BOT_P1
        SEC
        SBC wave_y_buf,X
@r12s:  STA tile_buf,Y
        INY
        INX
        CPX #32
        BNE @r12l

        ; --- row 13 -------------------------------------------------------
        LDX #0
@r13l:  LDA wave_y_buf,X
        CMP #WAVE_ROW_13_TOP
        BCC @r13w
        CMP #WAVE_ROW_13_BOT_P1
        BCC @r13p
        LDA #0
        BEQ @r13s
@r13w:  LDA #8
        BNE @r13s
@r13p:  LDA #WAVE_ROW_13_BOT_P1
        SEC
        SBC wave_y_buf,X
@r13s:  STA tile_buf,Y
        INY
        INX
        CPX #32
        BNE @r13l

        RTS


; ----------------------------------------------------------------------------
; write_boat_sat: 4 bytes of boat-sprite SAT @ $1B00 + $D0 terminator.
;   boat_sine_idx = (boat_x / 4 + phase) & $3F      (same SLOPE as columns:
;                                                    pixel X / 4 maps to
;                                                    sine_idx, equivalent
;                                                    to col*2 since col*8/4 =
;                                                    col*2)
;   boat_wave_y   = WAVE_BASELINE_PIX + sine_table[idx]
;   boat_Y_attr   = boat_wave_y - BOAT_RISE
; ----------------------------------------------------------------------------
write_boat_sat:
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$5B                  ; $1B | $40 -> write at $1B00
        STA VDP_CTRL
        JSR tms9918_pad12

        LDA boat_x
        LSR
        LSR                       ; A = boat_x / 4
        CLC
        ADC phase
        AND #$3F
        TAX
        LDA sine_table,X
        CLC
        ADC #WAVE_BASELINE_PIX
        SEC
        SBC #BOAT_RISE
        STA VDP_DATA              ; Y_attr
        JSR tms9918_pad12

        LDA boat_x
        STA VDP_DATA              ; X_attr
        JSR tms9918_pad12

        LDA #0                    ; pattern name 0 -> patterns 0..3 (boat)
        STA VDP_DATA
        JSR tms9918_pad12

        LDA #$0A                  ; colour = dark yellow (sail + hull)
        STA VDP_DATA
        JSR tms9918_pad12

        LDA #$D0                  ; chain terminator -> chip stops at slot 1
        STA VDP_DATA
        JSR tms9918_pad12
        RTS


; ============================================================================
; Tile patterns -- 9 cells, 8 bytes each, in tile order (0..8).
; ============================================================================
tile_patterns:
        ; tile 0: empty sky
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        ; tile 1: 1 row of water at the bottom
        .byte $00, $00, $00, $00, $00, $00, $00, $FF
        ; tile 2: 2 rows
        .byte $00, $00, $00, $00, $00, $00, $FF, $FF
        ; tile 3: 3 rows
        .byte $00, $00, $00, $00, $00, $FF, $FF, $FF
        ; tile 4: 4 rows
        .byte $00, $00, $00, $00, $FF, $FF, $FF, $FF
        ; tile 5: 5 rows
        .byte $00, $00, $00, $FF, $FF, $FF, $FF, $FF
        ; tile 6: 6 rows
        .byte $00, $00, $FF, $FF, $FF, $FF, $FF, $FF
        ; tile 7: 7 rows
        .byte $00, $FF, $FF, $FF, $FF, $FF, $FF, $FF
        ; tile 8: full water cell
        .byte $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF


; ============================================================================
; Boat sprite pattern -- single 16x16 (4 quadrants, 32 bytes).
; Triangular sail in the upper-left, hull spanning the full bottom width.
; ============================================================================
boat_pattern:
        ; Top-left quad (rows 0..7, cols 0..7) -- sail
        .byte $01, $03, $07, $0F, $1F, $3F, $7F, $7F
        ; Bottom-left quad (rows 8..15, cols 0..7) -- mast + hull-left
        .byte $18, $18, $FF, $7F, $3F, $1F, $00, $00
        ; Top-right quad (rows 0..7, cols 8..15) -- empty
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        ; Bottom-right quad (rows 8..15, cols 8..15) -- hull-right
        .byte $00, $00, $F0, $E0, $C0, $80, $00, $00


; ============================================================================
; Sine LUT -- 64 entries, signed two's-complement, amplitude +/- 8.
; sin(2*pi*i/64) * 8, rounded to nearest int. Half the amplitude of the
; usual +/- 16 table so the wave fits inside the 3-row wave band exactly:
; wave_y range = 88..104, which never escapes rows 11..13.
; ============================================================================
sine_table:
        .byte $00, $01, $02, $02, $03, $04, $04, $05
        .byte $06, $06, $07, $07, $07, $08, $08, $08
        .byte $08, $08, $08, $08, $07, $07, $07, $06
        .byte $06, $05, $04, $04, $03, $02, $02, $01
        .byte $00, $FF, $FE, $FE, $FD, $FC, $FC, $FB
        .byte $FA, $FA, $F9, $F9, $F9, $F8, $F8, $F8
        .byte $F8, $F8, $F8, $F8, $F9, $F9, $F9, $FA
        .byte $FA, $FB, $FC, $FC, $FD, $FE, $FE, $FF
