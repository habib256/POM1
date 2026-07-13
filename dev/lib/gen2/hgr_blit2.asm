; ============================================================================
; hgr_blit2.asm -- byte-aligned rectangle blits for the GEN2 HGR framebuffer
; ----------------------------------------------------------------------------
; The cell/tile workhorse of the TMS->GEN2 game ports: copy a
; 2-byte-wide (hgr_blit2) or 4-byte-wide (hgr_blit4) bitmap of bl_h rows
; into the framebuffer at byte column bl_col, top scanline bl_sl, with a
; raster op:
;   bl_mode = 0   OR     entities over a tile layer
;   bl_mode = 1   FLASH  EOR #$7F then OR -- lights everything EXCEPT
;                        the silhouette (the "hurt" inverted-box flash of
;                        the Rogue port; HGR has no per-sprite colour)
;   bl_mode = 2   STORE  tiles repainting their whole cell
;   bl_mode = 3   PALFLIP EOR #$80 then OR -- flips the NTSC palette bit
;                        of every source byte: a parity-masked coloured
;                        sprite swaps colour family (green<->orange,
;                        violet<->blue) -- the "hurt" flash of the x2
;                        colour builds. Empty source bytes become $80
;                        (no lit pixels: visually nothing on black)
;
; Source bitmaps are HGR-packed rows (7 px/byte, bit 0 = leftmost,
; palette bit as stored), bl_h rows x width bytes, top to bottom --
; e.g. the 14x16 px tiles/sprites of tools/build_rogue_hgr_assets.py
; (2 bytes x 16 rows) or a 28x32 boss (4 bytes x 32 rows). For
; TMS-format 16x16 SCROLL-O-SPRITES patterns use hgr_sprite16.asm
; instead (it converts bit order + magnifies on the fly).
;
; Bottom-clipped at scanline 192. None of the routines touch X -- the
; pool-iteration loops of the game ports keep slot offsets there.
; Clobbers A and Y. Caller provides hgr_lo / hgr_hi
; (include hgr_scanline.inc). The module allocates its own ZP (~11 B).
;
; First consumer: sketchs/gen2/game_rogue (map tiles + entities + boss).
; Migration candidate: game_sokoban's draw_tile inner loop.
; ============================================================================

.ifndef _HGR_BLIT2_LOADED_
_HGR_BLIT2_LOADED_ = 1

.zeropage
bl_src:     .res 2      ; -> source bitmap (public)
bl_col:     .res 1      ; dest: byte column of the leftmost byte (public)
bl_sl:      .res 1      ; dest: top scanline (public)
bl_h:       .res 1      ; rows (public)
bl_mode:    .res 1      ; 0 = OR, 1 = FLASH, 2 = STORE (public)
bl_a:       .res 1      ; byte cache across the mode dispatch
bl_i:       .res 1      ; source byte index
bl_y:       .res 1      ; current scanline
bl_r:       .res 1      ; rows remaining
bl_lin_lo:  .res 1      ; scanline pointer
bl_lin_hi:  .res 1
bl_page:    .res 1      ; HGR page selector, EORed into the scanline
                        ; high byte: $00 = page 1 ($2000), $60 = page 2
                        ; ($4000 — $2x EOR $60 = $4x exactly). INIT AT
                        ; BOOT; double-buffered games flip it per frame

.code

; bl_put / _put0..3: store A at the scanline pointer, byte column
; bl_col + n, applying bl_mode.
bl_put0:
        LDY bl_col
        JMP bl_put
bl_put1:
        LDY bl_col
        INY
        JMP bl_put
bl_put2:
        LDY bl_col
        INY
        INY
        JMP bl_put
bl_put3:
        LDY bl_col
        INY
        INY
        INY
bl_put:
        STA bl_a
        LDA bl_mode
        BEQ @or
        CMP #2
        BEQ @st
        CMP #3
        BEQ @pal
        LDA bl_a                ; FLASH: light everything EXCEPT the
        EOR #$7F                ; silhouette
        ORA (bl_lin_lo),Y
        JMP @w
@pal:   LDA bl_a                ; PALFLIP: same pixels, other palette
        EOR #$80
        ORA (bl_lin_lo),Y
        JMP @w
@or:    LDA bl_a
        ORA (bl_lin_lo),Y
        JMP @w
@st:    LDA bl_a
@w:     STA (bl_lin_lo),Y
        RTS

; ----------------------------------------------------------------------------
; hgr_blit2: 2-byte-wide bitmap, bl_h rows.
; ----------------------------------------------------------------------------
hgr_blit2:
        LDA #0
        STA bl_i
        LDA bl_sl
        STA bl_y
        LDA bl_h
        STA bl_r
@row:   LDY bl_y
        CPY #192
        BCS @done               ; bottom clip (defensive)
        LDA hgr_lo,Y
        STA bl_lin_lo
        LDA hgr_hi,Y
        EOR bl_page
        STA bl_lin_hi
        LDY bl_i
        LDA (bl_src),Y
        JSR bl_put0
        INC bl_i
        LDY bl_i
        LDA (bl_src),Y
        JSR bl_put1
        INC bl_i
        INC bl_y
        DEC bl_r
        BNE @row
@done:  RTS

; ----------------------------------------------------------------------------
; hgr_blit4: 4-byte-wide bitmap, bl_h rows.
; ----------------------------------------------------------------------------
hgr_blit4:
        LDA #0
        STA bl_i
        LDA bl_sl
        STA bl_y
        LDA bl_h
        STA bl_r
@row:   LDY bl_y
        CPY #192
        BCS @done
        LDA hgr_lo,Y
        STA bl_lin_lo
        LDA hgr_hi,Y
        EOR bl_page
        STA bl_lin_hi
        LDY bl_i
        LDA (bl_src),Y
        JSR bl_put0
        INC bl_i
        LDY bl_i
        LDA (bl_src),Y
        JSR bl_put1
        INC bl_i
        LDY bl_i
        LDA (bl_src),Y
        JSR bl_put2
        INC bl_i
        LDY bl_i
        LDA (bl_src),Y
        JSR bl_put3
        INC bl_i
        INC bl_y
        DEC bl_r
        BNE @row
@done:  RTS

.endif  ; _HGR_BLIT2_LOADED_
