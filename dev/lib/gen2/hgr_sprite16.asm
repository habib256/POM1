; ============================================================================
; hgr_sprite16.asm -- TMS-format 16x16 sprite blitter for the GEN2 HGR card
; ----------------------------------------------------------------------------
; Draws a 32-byte TMS9918-format 16x16 pattern (left column rows 0..15,
; right column rows 0..15, bit 7 = leftmost pixel -- the SCROLL-O-SPRITES
; layout of dev/lib/tms9918/sprites_*.asm) into the HGR page-1 framebuffer,
; magnified x1 (16x16), x2 (32x32) or x4 (64x64), with optional NTSC
; artifact colour. Pure byte STORES: the box overwrites the background,
; which doubles as occlusion behind the sprite.
;
; THE technique this module exists for: each output row is first built as
; an msb-first pixel BIT-STREAM in sp_rb, then sp_pack_row repacks it
; 7 px per HGR byte -- NO source column is ever dropped. A naive mapping
; of one 8-px TMS byte column onto one 7-px HGR byte column loses one
; pixel column per source byte; at x2 the resulting seams skew the art
; visibly (proven on TMS_Maze3D's goblin). Byte-column mapping stays fine
; for 8-px-grid text and line work -- never for sprite art. Cost of the
; repack: ~130 cycles per output byte (a x4 blit = 64 rows x 10 B ~ 80k
; cycles, fine for screen builds).
;
; API (all coordinates in pixels, sp_x / sp_y MULTIPLES OF 8):
;   hgr_spr16_x1 / _x2 / _x4  blit sp_ptr's pattern at (sp_x, sp_y).
;                             Output is ceil(w/7) bytes wide (3/5/10) --
;                             one byte wider than the sprite, zero-padded.
;                             Clobbers A, X, Y and the sp_* ZP.
;   hgr_spr16_color_a         A = HSPR_* colour code -> arms the artifact
;                             colour attributes (sp_cm_ev / sp_cm_od /
;                             sp_cbit) applied by every following blit.
;                             Clobbers A, Y.
;
; Colour model: HGR pixel x = byte_column*7 + bit, so a pixel-parity mask
; must ALTERNATE between byte columns. A pure colour keeps one pixel of
; each doubled pair -- half the density, the price of colour on HGR:
;   HSPR_WHITE   both parities, palette 0 (full-density monochrome)
;   HSPR_GREEN   odd-x pixels,  palette 0
;   HSPR_ORANGE  odd-x pixels,  palette 1   (the HGR "red")
;   HSPR_VIOLET  even-x pixels, palette 0
;   HSPR_BLUE    even-x pixels, palette 1
;
; Caller must provide hgr_lo / hgr_hi (include hgr_scanline.inc) and a
; rev7-capable CODE budget (~430 B incl. the 256 B rev7_tab). The module
; allocates its own ZP (guarded, ~24 B) and owns rev7_tab / dblnib /
; quadbits -- projects may reference all three (e.g. rev7_tab for text
; glyph conversion, dblnib for x2 text doubling).
;
; First consumer: sketchs/gen2/game_maze3d (title mascot, corridor
; clusters, x4 combat portrait, tinted x2 title text via the sp_cm_*
; attributes).
; ============================================================================

.ifndef _HGR_SPRITE16_LOADED_
_HGR_SPRITE16_LOADED_ = 1

; --- Colour codes for hgr_spr16_color_a -------------------------------------
HSPR_WHITE  = 0
HSPR_GREEN  = 1
HSPR_ORANGE = 2
HSPR_VIOLET = 3
HSPR_BLUE   = 4

.zeropage
sp_ptr:     .res 2      ; -> 32-byte TMS-format pattern (public input)
sp_x:       .res 1      ; dest top-left pixel x, multiple of 8 (public)
sp_y:       .res 1      ; dest top-left pixel y, multiple of 8 (public)
sp_cm_ev:   .res 1      ; colour: pixel mask for EVEN byte columns (public)
sp_cm_od:   .res 1      ; colour: pixel mask for ODD byte columns (public)
sp_cbit:    .res 1      ; colour: palette bit $00/$80 (public)
sp_rb:      .res 8      ; one output row as an msb-first pixel stream
sp_i:       .res 1      ; output row counter
sp_b:       .res 1      ; source byte cache
sp_px:      .res 1      ; packed byte cache across the colour mask
sp_col:     .res 1      ; current dest byte column
sp_col0:    .res 1      ; leftmost dest byte column
sp_wout:    .res 1      ; output bytes per row (3/5/10)
sp_lin_lo:  .res 1      ; current scanline pointer
sp_lin_hi:  .res 1
sp_yy:      .res 1      ; current scanline
sp_t:       .res 1      ; source-row scratch

.code

; ----------------------------------------------------------------------------
; hgr_spr16_color_a: A = HSPR_* -> sp_cm_ev / sp_cm_od / sp_cbit.
; ----------------------------------------------------------------------------
hgr_spr16_color_a:
        TAY
        LDA hspr_cmask_ev,Y
        STA sp_cm_ev
        LDA hspr_cmask_od,Y
        STA sp_cm_od
        LDA hspr_cbit,Y
        STA sp_cbit
        RTS

hspr_cmask_ev:  .byte $7F, $2A, $2A, $55, $55
hspr_cmask_od:  .byte $7F, $55, $55, $2A, $2A
hspr_cbit:      .byte $00, $00, $80, $00, $80

; ----------------------------------------------------------------------------
; sp_pack_row: emit sp_wout HGR bytes from the sp_rb bit-stream at
; scanline sp_yy, byte columns sp_col0.. ; advances sp_yy one line.
; Bottom-clipped at y=192. Consumes (shifts out) sp_rb.
; ----------------------------------------------------------------------------
sp_pack_row:
        LDY sp_yy
        CPY #192
        BCS @clip
        LDA hgr_lo,Y
        STA sp_lin_lo
        LDA hgr_hi,Y
        STA sp_lin_hi
        LDA sp_col0
        STA sp_col
        LDX #0
@ob:    ; pull 7 stream bits into a TMS-order byte (1st bit -> bit 7)
        LDA #0
        STA sp_px
        LDY #7
@bit:   ASL sp_rb+7
        ROL sp_rb+6
        ROL sp_rb+5
        ROL sp_rb+4
        ROL sp_rb+3
        ROL sp_rb+2
        ROL sp_rb+1
        ROL sp_rb+0             ; leftmost stream bit -> C
        ROL sp_px
        DEY
        BNE @bit
        ASL sp_px               ; 1st bit up to bit 7 (bit 0 stays 0)
        LDY sp_px
        LDA rev7_tab,Y          ; -> HGR bit order (bit 0 = leftmost)
        STA sp_px
        LDA sp_col
        AND #1
        BNE @od
        LDA sp_cm_ev
        JMP @msk
@od:    LDA sp_cm_od
@msk:   AND sp_px
        ORA sp_cbit
        LDY sp_col
        STA (sp_lin_lo),Y
        INC sp_col
        INX
        CPX sp_wout
        BNE @ob
@clip:  INC sp_yy
        RTS

; sp_col_base: sp_col0 := 4 + sp_x/8 (the 32-byte-column window at byte
; cols 4..35 that the TMS-port games use), sp_yy := sp_y.
sp_col_base:
        LDA sp_x
        LSR
        LSR
        LSR
        CLC
        ADC #4
        STA sp_col0
        LDA sp_y
        STA sp_yy
        RTS

; sp_quad4: expand source byte A into sp_rb+X..X+3 (quadbits of the
; four 2-bit groups, msb first) -- the x4 horizontal magnify.
sp_quad4:
        STA sp_b
        LSR
        LSR
        LSR
        LSR
        LSR
        LSR                     ; bits 7..6
        TAY
        LDA quadbits,Y
        STA sp_rb,X
        LDA sp_b
        LSR
        LSR
        LSR
        LSR
        AND #3                  ; bits 5..4
        TAY
        LDA quadbits,Y
        STA sp_rb+1,X
        LDA sp_b
        LSR
        LSR
        AND #3                  ; bits 3..2
        TAY
        LDA quadbits,Y
        STA sp_rb+2,X
        LDA sp_b
        AND #3                  ; bits 1..0
        TAY
        LDA quadbits,Y
        STA sp_rb+3,X
        RTS

; ----------------------------------------------------------------------------
; hgr_spr16_x1: plain 16x16 -- stream = the two source bytes, 3 B/row.
; ----------------------------------------------------------------------------
hgr_spr16_x1:
        JSR sp_col_base
        LDA #0
        STA sp_i                ; output row 0..15
@row:   LDY sp_i
        LDA (sp_ptr),Y          ; left column byte
        STA sp_rb+0
        TYA
        CLC
        ADC #16
        TAY
        LDA (sp_ptr),Y          ; right column byte
        STA sp_rb+1
        LDA #0
        STA sp_rb+2
        STA sp_rb+3
        STA sp_rb+4
        STA sp_rb+5
        STA sp_rb+6
        STA sp_rb+7
        LDA #3
        STA sp_wout
        JSR sp_pack_row
        INC sp_i
        LDA sp_i
        CMP #16
        BNE @row
        RTS

; ----------------------------------------------------------------------------
; hgr_spr16_x2: 32x32 -- stream = dblnib of the 4 source nibbles, 5 B/row.
; ----------------------------------------------------------------------------
hgr_spr16_x2:
        JSR sp_col_base
        LDA #0
        STA sp_i                ; output row 0..31
@row:   LDA sp_i
        LSR                     ; /2 -> source row 0..15
        TAY
        STY sp_t
        LDA (sp_ptr),Y          ; left column byte
        STA sp_b
        LSR
        LSR
        LSR
        LSR
        TAX
        LDA dblnib,X
        STA sp_rb+0
        LDA sp_b
        AND #$0F
        TAX
        LDA dblnib,X
        STA sp_rb+1
        LDA sp_t
        CLC
        ADC #16
        TAY
        LDA (sp_ptr),Y          ; right column byte
        STA sp_b
        LSR
        LSR
        LSR
        LSR
        TAX
        LDA dblnib,X
        STA sp_rb+2
        LDA sp_b
        AND #$0F
        TAX
        LDA dblnib,X
        STA sp_rb+3
        LDA #0
        STA sp_rb+4
        STA sp_rb+5
        STA sp_rb+6
        STA sp_rb+7
        LDA #5
        STA sp_wout
        JSR sp_pack_row
        INC sp_i
        LDA sp_i
        CMP #32
        BNE @row
        RTS

; ----------------------------------------------------------------------------
; hgr_spr16_x4: 64x64 -- stream = quadbits of the 8 source 2-bit groups,
; 10 B/row.
; ----------------------------------------------------------------------------
hgr_spr16_x4:
        JSR sp_col_base
        LDA #0
        STA sp_i                ; output row 0..63
@row:   LDA sp_i
        LSR
        LSR                     ; /4 -> source row 0..15
        TAY
        STY sp_t
        LDA (sp_ptr),Y          ; left column byte -> sp_rb+0..3
        LDX #0
        JSR sp_quad4
        LDA sp_t
        CLC
        ADC #16
        TAY
        LDA (sp_ptr),Y          ; right column byte -> sp_rb+4..7
        LDX #4
        JSR sp_quad4
        LDA #10
        STA sp_wout
        JSR sp_pack_row
        INC sp_i
        LDA sp_i
        CMP #64
        BNE @row
        RTS

; ----------------------------------------------------------------------------
; Shared data tables
; ----------------------------------------------------------------------------
; rev7_tab (TMS bit order -> HGR, shared with hgr_text8.asm) lives in
; rev7.inc -- guarded, so pulling both modules is fine. The bit-stream
; repack above always presents 7 fresh stream bits + a zero bit 0 to
; the table, which is what makes THIS path lossless.
.include "rev7.inc"

; nibble -> byte with every bit doubled (x2 horizontal magnify)
dblnib: .byte $00,$03,$0C,$0F,$30,$33,$3C,$3F
        .byte $C0,$C3,$CC,$CF,$F0,$F3,$FC,$FF
; 2 bits -> byte with every bit quadrupled (x4 horizontal magnify)
quadbits:
        .byte $00,$0F,$F0,$FF

.endif  ; _HGR_SPRITE16_LOADED_
