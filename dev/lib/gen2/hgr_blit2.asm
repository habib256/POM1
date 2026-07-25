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
; Clobbers A and Y; bl_src is preserved. Caller provides hgr_lo /
; hgr_hi (include hgr_scanline.inc). The module allocates its own ZP
; (~11 B).
;
; Speed: bl_mode is dispatched ONCE per call to a specialised, unrolled
; row loop (one loop per width x mode) -- ~101 cycles per 4-byte STORE
; row vs ~250 for the old per-byte JSR bl_put dispatch, i.e. a 28x32
; tile drops from ~8000 to ~3300 cycles. The byte column is folded into
; the row pointer up front: hgr_lo peaks at $D0 and bl_col <= 39, so
; the add can never carry into the high byte.
;
; First consumer: sketchs/gen2/game_rogue (map tiles + entities + boss).
; Migration candidate: game_sokoban's draw_tile inner loop.
; ============================================================================

.ifndef _HGR_BLIT2_LOADED_
_HGR_BLIT2_LOADED_ = 1

.zeropage
bl_src:     .res 2      ; -> source bitmap (public, preserved)
bl_col:     .res 1      ; dest: byte column of the leftmost byte (public)
bl_sl:      .res 1      ; dest: top scanline (public)
bl_h:       .res 1      ; rows (public)
bl_mode:    .res 1      ; 0 = OR, 1 = FLASH, 2 = STORE, 3 = PALFLIP (public)
bl_sp:      .res 2      ; walking source pointer (bl_src stays untouched)
bl_y:       .res 1      ; current scanline
bl_r:       .res 1      ; rows remaining
bl_lin_lo:  .res 1      ; scanline pointer (base + bl_col pre-added)
bl_lin_hi:  .res 1
bl_page:    .res 1      ; HGR page selector, EORed into the scanline
                        ; high byte: $00 = page 1 ($2000), $60 = page 2
                        ; ($4000 — $2x EOR $60 = $4x exactly). INIT AT
                        ; BOOT; double-buffered games flip it per frame

.code

; BL_PUT: A holds the source byte, Y the dest offset -- apply the mode's
; raster op and store at (bl_lin_lo),Y. Compile-time specialised.
.macro BL_PUT mode
        .if mode = 1
        EOR #$7F                ; FLASH: light everything EXCEPT the shape
        .elseif mode = 3
        EOR #$80                ; PALFLIP: same pixels, other palette
        .endif
        .if mode <> 2
        ORA (bl_lin_lo),Y
        .endif
        STA (bl_lin_lo),Y
.endmacro

; BL_LOOP: the whole row loop for one width x mode pair. Row address =
; scanline base + bl_col (no carry possible, see header); source bytes
; and dest bytes then share Y = 0..width-1, fully unrolled.
.macro BL_LOOP width, mode
@row:   LDY bl_y
        LDA hgr_lo,Y
        CLC
        ADC bl_col
        STA bl_lin_lo
        LDA hgr_hi,Y
        EOR bl_page
        STA bl_lin_hi
        LDY #0
        .repeat width-1
        LDA (bl_sp),Y
        BL_PUT mode
        INY
        .endrepeat
        LDA (bl_sp),Y
        BL_PUT mode
        LDA bl_sp               ; advance the source one row
        CLC
        ADC #width
        STA bl_sp
        BCC @nc
        INC bl_sp+1
@nc:    INC bl_y
        DEC bl_r
        BNE @row
        RTS
.endmacro

; BL_HEAD: shared per-call setup -- copy bl_src to the walking pointer,
; clip the row count against scanline 192, bail if nothing to draw,
; then dispatch bl_mode to the specialised loop.
.macro BL_HEAD or_lp, flash_lp, store_lp, pal_lp
        LDA bl_src
        STA bl_sp
        LDA bl_src+1
        STA bl_sp+1
        LDA bl_sl
        STA bl_y
        CMP #192
        BCS @rts                ; fully below the screen
        LDA #192                ; rows = min(bl_h, 192 - bl_sl)
        SEC
        SBC bl_y
        CMP bl_h
        BCC @clip
        LDA bl_h
@clip:  STA bl_r
        BEQ @rts
        LDA bl_mode             ; the unrolled loops sit past branch
        BNE @n0                 ; range -- dispatch through JMPs
        JMP or_lp
@n0:    CMP #2
        BNE @n2
        JMP store_lp
@n2:    CMP #3
        BNE @n3
        JMP pal_lp
@n3:    JMP flash_lp
@rts:   RTS
.endmacro

; ----------------------------------------------------------------------------
; hgr_blit2: 2-byte-wide bitmap, bl_h rows.
; ----------------------------------------------------------------------------
hgr_blit2:
        BL_HEAD b2_or, b2_flash, b2_store, b2_pal
b2_or:
        BL_LOOP 2, 0
b2_flash:
        BL_LOOP 2, 1
b2_store:
        BL_LOOP 2, 2
b2_pal:
        BL_LOOP 2, 3

; ----------------------------------------------------------------------------
; hgr_blit4: 4-byte-wide bitmap, bl_h rows.
; ----------------------------------------------------------------------------
hgr_blit4:
        BL_HEAD b4_or, b4_flash, b4_store, b4_pal
b4_or:
        BL_LOOP 4, 0
b4_flash:
        BL_LOOP 4, 1
b4_store:
        BL_LOOP 4, 2
b4_pal:
        BL_LOOP 4, 3

.endif  ; _HGR_BLIT2_LOADED_
