; ============================================================================
; tmsgfx.inc -- TMS9918 graphics + console handlers for Applesoft TMS9918
; ============================================================================
; .include'd at the end of applesoft-tms9918.s, so it shares the interpreter's
; namespace (GETBYT, GTNUM, COMBYTE, CHKOPN, CHKCLS, CHKCOM, SYNCHR, CHRGET,
; CHRGOT, SNGFLT, PRINTCORE, TOKEN_TO, TOKEN_SCRN, LINNUM, CSW, AECHO via $FFEF).
;
; Same command surface + token layout as the sibling sketchs/gen2/applesoft_gen2
; (so a program tokenizes identically -- the two BASIC dialects are "raccord"),
; but every primitive drives the P-LAB TMS9918 VDP ($CC00 data / $CC01 control)
; instead of a memory-mapped Apple-II framebuffer.
;
; Hardware reality vs. the GEN2 build:
;   * No memory-mapped framebuffer -- all pixels live in the card's private
;     16 KB VRAM, reached only through the two ports (read/modify/write per
;     pixel, with silicon-strict pad gaps between back-to-back VDP stores).
;   * The 16 KB VRAM holds exactly ONE 256x192 bitmap (6 KB pattern + 6 KB
;     colour + name table), so there is NO hi-res page 2: HGR2 == HGR, GR2 == GR,
;     SHOW is a no-op. TMS9918 also has no Apple-style mixed text+graphics, so
;     MIX/NOMIX are no-ops. VBL is a real status-register vblank poll.
;
; Mode mapping (Apple statement -> TMS9918 display mode):
;   TEXT  -> Text Mode F1   40x24 mono   (bbfont uploaded to pattern $0000)
;   GR    -> Multicolor      64x48 / 16c  (PLOT/HLIN/VLIN/COLOR=, SCRN reads it)
;   HGR   -> Graphics II    256x192       (HPLOT/HCOLOR=, 1 fg colour per 8 px row)
;
; Output model: CSW (Apple-II char-out vector) defaults to the Apple-1 WOZ ECHO
; ($FFEF) so the prompt / LIST / errors / INPUT echo stay on the Apple-1
; terminal. The PRINT wrapper points CSW at the TMS text console (GCOUT) for its
; output; APRINT forces CSW back to $FFEF. GCOUT only paints VRAM while in TEXT
; mode (writing the text name table behind a graphics mode would corrupt the
; bitmap), otherwise it echoes to the Apple-1 terminal.
; ============================================================================

.include "tms9918.inc"          ; VDP_DATA $CC00 / VDP_CTRL $CC01

AECHO     = $FFEF               ; Apple-1 WOZ Monitor ECHO (CSW default)

; --- zero page scratch (verified free in Applesoft Lite: $06-$0C) -----------
GVL       = $06                 ; 16-bit VRAM address / source pointer lo
GVH       = $07                 ;                                      hi
GTMP      = $08                 ; scratch
GTMP2     = $09                 ; scratch
GCNT      = $0A                 ; loop counter
GSRCL     = $0B                 ; register/font source pointer lo
GSRCH     = $0C                 ;                              hi

; --- TMS graphics state block ($0300-$0316; CSW lives at $031E, zeropage.s) --
GST       = $0300
MODEFLG   = GST+0               ; 0 = TEXT, 1 = lo-res (MC), 2 = hi-res (G2)
PENCOL    = GST+1               ; hi-res pen colour 0..15
LRCOL     = GST+2               ; lo-res colour 0..15
CURCH     = GST+3               ; text cursor column 0..39
CURCV     = GST+4               ; text cursor row 0..23
PIXX      = GST+5               ; current plot X (hi-res 0..255 / lo-res 0..63)
PIXY      = GST+6               ; current plot Y (hi-res 0..191 / lo-res 0..47)
PADDRL    = GST+7               ; computed VRAM pixel address lo
PADDRH    = GST+8               ;                            hi
PMASK     = GST+9               ; hi-res pixel bitmask
PBYTE     = GST+10              ; VRAM byte being read-modified-written
LX0       = GST+11              ; Bresenham endpoints / line cursors
LY0       = GST+12
LX1       = GST+13              ; also HLIN/VLIN endpoint
LY1       = GST+14
LDXX      = GST+15              ; |dx|
LDYY      = GST+16              ; |dy|
LSX       = GST+17              ; x step $01/$FF
LSY       = GST+18              ; y step $01/$FF
LERR      = GST+19              ; Bresenham error (16-bit signed)
LERRH     = GST+20
GCSX      = GST+21              ; GCOUT saved X
GCSY      = GST+22              ; GCOUT saved Y

SCRLBUF   = $0400               ; 40-byte text-scroll buffer (free RAM on TMS:
                                ; the card has no memory-mapped framebuffer, so
                                ; $0400-$07FF is unused; program lives at $0800)

; ============================================================================
; Cold-start init (called from COLDSTART)
; ============================================================================
TMS_BOOT:
        lda     #<AECHO         ; CSW := Apple-1 WOZ ECHO ($FFEF)
        sta     CSW
        lda     #>AECHO
        sta     CSW+1
        lda     #$0F
        sta     PENCOL          ; default hi-res pen = white (15)
        lda     #$00
        sta     LRCOL
        sta     CURCH
        sta     CURCV
        sta     MODEFLG         ; boot into TEXT mode
        jmp     text_setup      ; init VDP text mode + font + clear, rts

; ============================================================================
; PRINT (-> TMS text console) / APRINT (-> Apple-1 terminal)
; ============================================================================
; Dispatch enters with A = first char after the token and Z = end-of-list;
; SET_CSW_* preserve both, then we jsr the stock PRINTCORE.
PRINT:                          ; statement token $98
        jsr     SET_CSW_GCOUT
        jsr     PRINTCORE
        jmp     SET_CSW_AECHO   ; restore default (Apple-1) + rts

GFX_APRINT:                     ; statement APRINT
        jsr     SET_CSW_AECHO
        jmp     PRINTCORE       ; output to Apple-1; default already AECHO

SET_CSW_GCOUT:
        php
        pha
        lda     #<GCOUT
        sta     CSW
        lda     #>GCOUT
        sta     CSW+1
        pla
        plp
        rts
SET_CSW_AECHO:
        php
        pha
        lda     #<AECHO
        sta     CSW
        lda     #>AECHO
        sta     CSW+1
        pla
        plp
        rts

; GCOUT -- TMS text console char-out. A = char (bit7 set, as OUTDO passes).
; In TEXT mode: writes the ASCII code to the $0800 name table at (CURCH,CURCV),
; handling CR + wrap + scroll. In any graphics mode: echoes to the Apple-1
; terminal instead (the text name table overlaps the active graphics tables, so
; painting it would corrupt the picture). Preserves X and Y for PRINTCORE.
GCOUT:
        sty     GCSY
        stx     GCSX
        pha                     ; save char (MODEFLG load below clobbers A)
        lda     MODEFLG
        beq     @txt            ; TEXT mode -> paint VRAM
        pla                     ; graphics mode -> Apple-1 ECHO
        ldx     GCSX
        ldy     GCSY
        jmp     AECHO           ; A holds the char again; ECHO preserves A/X/Y
@txt:   pla                     ; char back
        and     #$7F
        cmp     #$0D            ; CR -> newline
        beq     @nl
        sta     GTMP            ; stash glyph (ASCII $20-$7F)
        ldx     CURCV
        lda     name_row_lo,x
        clc
        adc     CURCH
        sta     GVL
        lda     name_row_hi,x
        adc     #0
        sta     GVH
        jsr     vdp_waddr
        lda     GTMP
        sta     VDP_DATA
        jsr     vpad
        inc     CURCH
        lda     CURCH
        cmp     #40
        bcc     @done           ; same line
@nl:    lda     #0              ; carriage return / wrap
        sta     CURCH
        inc     CURCV
        lda     CURCV
        cmp     #24
        bcc     @done
        jsr     text_scroll     ; off the bottom -> scroll, stay on row 23
        lda     #23
        sta     CURCV
@done:  ldx     GCSX
        ldy     GCSY
        rts

; ============================================================================
; Text mode setup / clear / scroll  (Text Mode F1, name table $0800)
; ============================================================================
GFX_TEXT:
        lda     #0
        sta     MODEFLG
        ; fall through to text_setup

text_setup:
        lda     #<vdpt_regs     ; write 8 regs with display forced OFF
        sta     GSRCL
        lda     #>vdpt_regs
        sta     GSRCH
        jsr     load_regs_off
        lda     #<tms_bbfont    ; upload font to pattern $0100 (= ASCII $20*8)
        sta     GSRCL
        lda     #>tms_bbfont
        sta     GSRCH
        jsr     upload_font
        jsr     text_clear      ; blank the name table (display still off)
        lda     #$D0            ; R1 = text mode, display ON
        jsr     display_on
        lda     #0
        sta     CURCH
        sta     CURCV
        rts

; upload_font -- copy the 768-byte bbfont ($20-$7F) to pattern table $0100.
;   bbfont is already in TMS bit order (MSB = leftmost), so no bit-reversal:
;   ASCII code C in the name table fetches pattern at C*8, and C=$20 maps to
;   $0100, exactly where this upload lands the first glyph.
upload_font:
        lda     #$00
        sta     GVL
        lda     #$01            ; $0100
        sta     GVH
        jsr     vdp_waddr
        ldx     #3              ; 3 * 256 = 768 bytes
@pg:    ldy     #0
@by:    lda     (GSRCL),y
        sta     VDP_DATA
        jsr     vpad
        iny
        bne     @by
        inc     GSRCH
        dex
        bne     @pg
        rts

; text_clear -- fill the 960-byte name table at $0800 with $20 (space).
text_clear:
        lda     #$00
        sta     GVL
        lda     #$08
        sta     GVH
        jsr     vdp_waddr
        lda     #$20
        ldx     #3
@p:     ldy     #0
@b:     sta     VDP_DATA
        jsr     vpad
        iny
        bne     @b
        dex
        bne     @p
        ldy     #192            ; tail: 960 - 768 = 192 bytes
@t:     sta     VDP_DATA
        jsr     vpad
        dey
        bne     @t
        rts

; text_scroll -- scroll the 40x24 text page up one row, blank row 23. Reads
;   each source row out of VRAM into SCRLBUF, writes it one row up.
text_scroll:
        lda     #0
        sta     GCNT            ; GCNT = destination row 0..22
@row:   ldx     GCNT            ; --- read source row (dest+1) into SCRLBUF ---
        inx
        lda     name_row_lo,x
        sta     GVL
        lda     name_row_hi,x
        sta     GVH
        jsr     vdp_raddr
        jsr     vpad
        ldy     #0
@rd:    lda     VDP_DATA
        sta     SCRLBUF,y
        jsr     vpad
        iny
        cpy     #40
        bne     @rd
        ldx     GCNT            ; --- write it to the destination row ---
        lda     name_row_lo,x
        sta     GVL
        lda     name_row_hi,x
        sta     GVH
        jsr     vdp_waddr
        ldy     #0
@wr:    lda     SCRLBUF,y
        sta     VDP_DATA
        jsr     vpad
        iny
        cpy     #40
        bne     @wr
        inc     GCNT
        lda     GCNT
        cmp     #23
        bne     @row
        ldx     #23             ; --- blank the bottom row ---
        lda     name_row_lo,x
        sta     GVL
        lda     name_row_hi,x
        sta     GVH
        jsr     vdp_waddr
        lda     #$20
        ldy     #40
@bl:    sta     VDP_DATA
        jsr     vpad
        dey
        bne     @bl
        rts

; ============================================================================
; Text cursor commands
; ============================================================================
GFX_HOME:                       ; clear TMS text page (if showing it) + home
        lda     MODEFLG
        bne     @home           ; not text mode -> don't touch graphics VRAM
        jsr     text_clear
@home:  lda     #0
        sta     CURCH
        sta     CURCV
        rts

GFX_HTAB:                       ; HTAB c  (1..40 -> column 0..39)
        jsr     GETBYT
        dex
        txa
        cmp     #40
        bcc     @ok
        lda     #39
@ok:    sta     CURCH
        rts

GFX_VTAB:                       ; VTAB r  (1..24 -> row 0..23)
        jsr     GETBYT
        dex
        txa
        cmp     #24
        bcc     @ok
        lda     #23
@ok:    sta     CURCV
        rts

; ============================================================================
; Mode commands (single-bitmap hardware: HGR2/GR2 alias HGR/GR; SHOW/MIX/NOMIX
; are no-ops -- see the header note on the 16 KB VRAM budget)
; ============================================================================
GFX_HGR2:
        jmp     GFX_HGR
GFX_GR2:
        jmp     GFX_GR
GFX_SHOW:
        jsr     GETBYT          ; consume the page number (single-page card)
        rts
GFX_MIX:
        rts
GFX_NOMIX:
        rts

; VBL -- wait for vertical blank by polling the VDP status register (bit 7 = F).
GFX_VBL:
        bit     VDP_CTRL        ; drain stale F flag (also clears 5S/collision)
@w:     bit     VDP_CTRL
        bpl     @w              ; bit7 = 0 -> not yet vblank, keep spinning
        rts

; ============================================================================
; Hi-res  (Graphics II, 256x192, pattern $0000 + colour $2000 + name $3800)
; ============================================================================
GFX_HGR:
        lda     #2
        sta     MODEFLG
        ; fall through to hgr_setup

hgr_setup:
        lda     #<vdp2_regs
        sta     GSRCL
        lda     #>vdp2_regs
        sta     GSRCH
        jsr     load_regs_off
        jsr     g2_name_table   ; $3800 = 0,1,..255 x3 (linear bitmap)
        jsr     g2_clear        ; zero 6144-byte pattern table $0000
        jsr     g2_color_clear  ; colour table $2000 -> $F1 (white fg / black bg)
        lda     #$00            ; park sprites: SAT $3B00 (R5=$76), sprite-0 Y=$D0
        sta     GVL             ; (Y=$D0 stops the sprite scan -> all 32 hidden;
        lda     #$3B            ;  the SAT sits just past the name table and is
        sta     GVH             ;  never cleared otherwise -> garbage sprites)
        jsr     park_sat
        lda     #$C0            ; R1 = Graphics II, display ON
        jmp     display_on

; park_sat -- hide all sprites: write $D0 to sprite 0's Y at the SAT base in
; GVL/GVH. The VDP stops scanning sprites at the first Y=$D0.
park_sat:
        jsr     vdp_waddr
        lda     #$D0
        sta     VDP_DATA
        rts

g2_name_table:
        lda     #$00
        sta     GVL
        lda     #$38
        sta     GVH
        jsr     vdp_waddr
        ldx     #3
@blk:   ldy     #0
@b:     tya
        sta     VDP_DATA
        jsr     vpad
        iny
        bne     @b
        dex
        bne     @blk
        rts

g2_clear:                       ; zero the 6144-byte pattern table at $0000
        lda     #$00
        sta     GVL
        sta     GVH
        jsr     vdp_waddr
        ldx     #24             ; 24 * 256 = 6144
        lda     #$00
@p:     ldy     #0
@b:     sta     VDP_DATA
        jsr     vpad
        iny
        bne     @b
        dex
        bne     @p
        rts

g2_color_clear:                 ; colour table $2000 -> $F1 (fg white, bg black)
        lda     #$00
        sta     GVL
        lda     #$20
        sta     GVH
        jsr     vdp_waddr
        ldx     #24
        lda     #$F1
@p:     ldy     #0
@b:     sta     VDP_DATA
        jsr     vpad
        iny
        bne     @b
        dex
        bne     @p
        rts

GFX_HCOLOR:                     ; HCOLOR= n  (0..15 TMS palette; 0..7 Apple-like)
        jsr     GETBYT
        txa
        and     #$0F
        sta     PENCOL
        rts

GFX_HPLOT:                      ; HPLOT x,y  [TO x2,y2]...
        jsr     GTNUM           ; "x,y": x -> LINNUM (16-bit), y -> X
        jsr     clampx          ; A = min(x, 255) for the 256-wide screen
        sta     PIXX
        stx     PIXY
        jsr     hplot_pt
@to:    jsr     CHRGOT
        cmp     #TOKEN_TO
        bne     @rts
        jsr     CHRGET
        lda     PIXX
        sta     LX0
        lda     PIXY
        sta     LY0
        jsr     GTNUM
        jsr     clampx
        sta     LX1
        stx     LY1
        jsr     line_xy
        lda     LX1             ; current point := endpoint
        sta     PIXX
        lda     LY1
        sta     PIXY
        jmp     @to
@rts:   rts

; clampx -- A := min(LINNUM, 255). The TMS9918 bitmap is 256 wide, so an x from a
;   GEN2-dimensioned listing (0..279) is clamped to the right edge rather than
;   wrapping to its low byte (which made HPLOT 279,.. draw a narrow x=23 line).
;   Preserves X (GTNUM left y there).
clampx:
        lda     LINNUM+1
        beq     @lo
        lda     #255
        rts
@lo:    lda     LINNUM
        rts

; hplot_pt -- set pixel (PIXX,PIXY) in PENCOL on the Graphics II bitmap. Guards
;   y < 192 (x is a byte, always 0..255 on the 256-wide screen).
hplot_pt:
        lda     PIXY
        cmp     #192
        bcs     @skip
        lda     PIXX            ; addr lo = (x & $F8) | (y & 7)
        and     #$F8
        sta     GTMP
        lda     PIXY
        and     #$07
        ora     GTMP
        sta     PADDRL
        lda     PIXY            ; addr hi = y >> 3
        lsr
        lsr
        lsr
        sta     PADDRH
        lda     PIXX            ; bitmask for column x & 7
        and     #$07
        tax
        lda     bitmask,x
        sta     PMASK
        lda     PADDRL          ; read the pattern byte
        sta     GVL
        lda     PADDRH
        sta     GVH
        jsr     vdp_raddr
        jsr     vpad
        lda     VDP_DATA
        ora     PMASK
        sta     PBYTE
        lda     PADDRL          ; write it back
        sta     GVL
        lda     PADDRH
        sta     GVH
        jsr     vdp_waddr
        lda     PBYTE
        sta     VDP_DATA
        jsr     vpad
        lda     PADDRL          ; colour cell at $2000 + addr = pen<<4 | black
        sta     GVL
        lda     PADDRH
        clc
        adc     #$20
        sta     GVH
        jsr     vdp_waddr
        lda     PENCOL
        asl
        asl
        asl
        asl
        ora     #$01            ; low nibble = colour 1 (black) background
        sta     VDP_DATA
        jsr     vpad
@skip:  rts

; line_xy -- 8-bit Bresenham (LX0,LY0)->(LX1,LY1) via hplot_pt. 16-bit signed
;   error term in LERR:LERRH. Adapted from dev/lib/tms9918/tms9918m2.asm.
line_xy:
        sec                     ; dx, sx
        lda     LX1
        sbc     LX0
        bcs     @sxp
        eor     #$FF
        clc
        adc     #1
        sta     LDXX
        lda     #$FF
        sta     LSX
        jmp     @dy
@sxp:   sta     LDXX
        lda     #$01
        sta     LSX
@dy:    sec                     ; dy, sy
        lda     LY1
        sbc     LY0
        bcs     @syp
        eor     #$FF
        clc
        adc     #1
        sta     LDYY
        lda     #$FF
        sta     LSY
        jmp     @init
@syp:   sta     LDYY
        lda     #$01
        sta     LSY
@init:  sec                     ; err = dx - dy (sign-extended)
        lda     LDXX
        sbc     LDYY
        sta     LERR
        lda     #0
        sbc     #0
        sta     LERRH
@step:  lda     LX0
        sta     PIXX
        lda     LY0
        sta     PIXY
        jsr     hplot_pt
        lda     LX0
        cmp     LX1
        bne     @do
        lda     LY0
        cmp     LY1
        beq     @end
@do:    lda     LERR            ; e2 = 2*err in GTMP:GTMP2
        sta     GTMP
        lda     LERRH
        sta     GTMP2
        asl     GTMP
        rol     GTMP2
        clc                     ; step x if (e2 + dy) >= 0
        lda     GTMP
        adc     LDYY
        lda     GTMP2
        adc     #0
        bmi     @nox
        sec                     ; err -= dy
        lda     LERR
        sbc     LDYY
        sta     LERR
        lda     LERRH
        sbc     #0
        sta     LERRH
        lda     LSX             ; x += sx
        bpl     @sxi
        dec     LX0
        jmp     @nox
@sxi:   inc     LX0
@nox:   sec                     ; step y if (e2 - dx) < 0
        lda     GTMP
        sbc     LDXX
        lda     GTMP2
        sbc     #0
        bpl     @noy
        clc                     ; err += dx
        lda     LERR
        adc     LDXX
        sta     LERR
        lda     LERRH
        adc     #0
        sta     LERRH
        lda     LSY             ; y += sy
        bpl     @syi
        dec     LY0
        jmp     @noy
@syi:   inc     LY0
@noy:   jmp     @step
@end:   rts

; ============================================================================
; Lo-res  (Multicolor, 64x48 of 4x4 blocks, pattern $0000 + name $0800)
; ============================================================================
GFX_GR:
        lda     #1
        sta     MODEFLG
        lda     #0
        sta     LRCOL
        ; fall through to gr_setup

gr_setup:
        lda     #<mc_regs
        sta     GSRCL
        lda     #>mc_regs
        sta     GSRCH
        jsr     load_regs_off
        jsr     mc_name_table   ; banded name table at $0800
        jsr     mc_clear        ; zero 1536-byte pattern (colour) table $0000
        lda     #$00            ; park sprites: SAT $0B00 (R5=$16), sprite-0 Y=$D0
        sta     GVL
        lda     #$0B
        sta     GVH
        jsr     park_sat
        lda     #$C8            ; R1 = Multicolor, display ON
        jmp     display_on

; mc_name_table -- 768 bytes at $0800. Band b (0..5) covers 4 char-rows; each
;   of its 4 rows holds names b*32+0 .. b*32+31, so the pattern table at $0000
;   becomes a flat 64x48 nibble framebuffer.
mc_name_table:
        lda     #$00
        sta     GVL
        lda     #$08
        sta     GVH
        jsr     vdp_waddr
        lda     #0
        sta     GTMP            ; band base 0,32,..,160
        ldx     #6              ; 6 bands
@band:  ldy     #4              ; 4 char-rows per band
@rep:   lda     #0
        sta     GTMP2           ; column 0..31
@col:   lda     GTMP
        clc
        adc     GTMP2
        sta     VDP_DATA
        jsr     vpad
        inc     GTMP2
        lda     GTMP2
        cmp     #32
        bne     @col
        dey
        bne     @rep
        lda     GTMP
        clc
        adc     #32
        sta     GTMP
        dex
        bne     @band
        rts

mc_clear:                       ; zero the 1536-byte pattern (colour) table
        lda     #$00
        sta     GVL
        sta     GVH
        jsr     vdp_waddr
        ldx     #6              ; 6 * 256 = 1536
        lda     #$00
@p:     ldy     #0
@b:     sta     VDP_DATA
        jsr     vpad
        iny
        bne     @b
        dex
        bne     @p
        rts

GFX_COLOR:                      ; COLOR= n  (0..15 TMS palette)
        jsr     GETBYT
        txa
        and     #$0F
        sta     LRCOL
        rts

; mc_addr -- compute the multicolor pattern address for block (PIXX,PIXY) into
;   PADDRL:PADDRH. Caller guarantees x<64, y<48. Clobbers A, GTMP, GTMP2, GVL.
mc_addr:
        lda     PIXX            ; c = x >> 1
        lsr
        sta     GTMP
        lda     PIXY            ; R = y >> 1   (char row 0..23)
        lsr
        sta     GTMP2
        lsr                     ; addr hi = R >> 2  (band 0..5)
        lsr
        sta     PADDRH
        lda     GTMP2           ; byteidx = (R & 3)*2 + (y & 1)
        and     #$03
        asl
        sta     GVL
        lda     PIXY
        and     #$01
        ora     GVL
        sta     GVL
        lda     GTMP            ; addr lo = c*8 + byteidx
        asl
        asl
        asl
        clc
        adc     GVL
        sta     PADDRL
        rts

; mc_plot -- set block (PIXX,PIXY) to LRCOL. Guards x<64, y<48.
mc_plot:
        lda     PIXX
        cmp     #64
        bcs     @skip
        lda     PIXY
        cmp     #48
        bcs     @skip
        jsr     mc_addr
        lda     PADDRL          ; read the byte (two 4x4 blocks, hi=left/lo=right)
        sta     GVL
        lda     PADDRH
        sta     GVH
        jsr     vdp_raddr
        jsr     vpad
        lda     VDP_DATA
        sta     PBYTE
        lda     PIXX            ; even x = left = high nibble; odd x = right = low
        and     #$01
        bne     @right
        lda     PBYTE
        and     #$0F
        sta     PBYTE
        lda     LRCOL
        asl
        asl
        asl
        asl
        ora     PBYTE
        sta     PBYTE
        jmp     @wr
@right: lda     PBYTE
        and     #$F0
        sta     PBYTE
        lda     LRCOL
        and     #$0F
        ora     PBYTE
        sta     PBYTE
@wr:    lda     PADDRL
        sta     GVL
        lda     PADDRH
        sta     GVH
        jsr     vdp_waddr
        lda     PBYTE
        sta     VDP_DATA
        jsr     vpad
@skip:  rts

GFX_PLOT:                       ; PLOT x,y
        jsr     GETBYT
        stx     PIXX
        jsr     COMBYTE
        stx     PIXY
        jmp     mc_plot

GFX_HLIN:                       ; HLIN x1,x2 AT y
        jsr     GETBYT
        stx     PIXX            ; running x = x1
        jsr     COMBYTE
        stx     LX1             ; endpoint x2
        jsr     synch_at
        jsr     GETBYT
        stx     PIXY
@loop:  jsr     mc_plot
        lda     PIXX
        cmp     LX1
        beq     @done
        bcc     @inc
        dec     PIXX
        jmp     @loop
@inc:   inc     PIXX
        jmp     @loop
@done:  rts

GFX_VLIN:                       ; VLIN y1,y2 AT x
        jsr     GETBYT
        stx     PIXY            ; running y = y1
        jsr     COMBYTE
        stx     LY1             ; endpoint y2
        jsr     synch_at
        jsr     GETBYT
        stx     PIXX
@loop:  jsr     mc_plot
        lda     PIXY
        cmp     LY1
        beq     @done
        bcc     @inc
        dec     PIXY
        jmp     @loop
@inc:   inc     PIXY
        jmp     @loop
@done:  rts

; Require the literal "AT" (no token for it in this BASIC).
synch_at:
        lda     #'A'
        jsr     SYNCHR
        lda     #'T'
        jmp     SYNCHR

; SCRN(x,y) -- read the lo-res block colour at (x,y) -> FAC. Entered from UNARY
;   with TXTPTR at the SCRN token.
SCRN_PARSE:
        jsr     CHRGET          ; past SCRN
        jsr     CHKOPN          ; "("
        jsr     GTNUM           ; "x,y": x -> LINNUM, y -> X
        stx     PIXY
        lda     LINNUM
        sta     PIXX
        jsr     CHKCLS          ; ")"
        lda     PIXX
        cmp     #64
        bcs     @zero
        lda     PIXY
        cmp     #48
        bcs     @zero
        jsr     mc_addr
        lda     PADDRL
        sta     GVL
        lda     PADDRH
        sta     GVH
        jsr     vdp_raddr
        jsr     vpad
        lda     VDP_DATA
        sta     PBYTE
        lda     PIXX            ; even x -> high nibble, odd x -> low nibble
        and     #$01
        bne     @low
        lda     PBYTE
        lsr
        lsr
        lsr
        lsr
        jmp     @ret
@low:   lda     PBYTE
        and     #$0F
@ret:   tay
        jmp     SNGFLT          ; Y -> FAC
@zero:  ldy     #0
        jmp     SNGFLT

; ============================================================================
; VDP primitives (silicon-strict: JSR vpad = 12c gap between back-to-back VDP
; stores, matching dev/lib/tms9918/tms9918_pad.asm's pad12 contract)
; ============================================================================
vpad:   rts                     ; JSR vpad = 6c + 6c = 12c, flag-transparent

; vdp_waddr -- set the VRAM auto-increment WRITE address from GVL:GVH.
vdp_waddr:
        lda     GVL
        sta     VDP_CTRL
        jsr     vpad
        lda     GVH
        ora     #$40            ; bit 6 = write
        sta     VDP_CTRL
        jsr     vpad
        rts

; vdp_raddr -- set the VRAM READ address from GVL:GVH.
vdp_raddr:
        lda     GVL
        sta     VDP_CTRL
        jsr     vpad
        lda     GVH             ; bit 6 = 0 -> read
        sta     VDP_CTRL
        jsr     vpad
        rts

; load_regs_off -- write 8 VDP registers from (GSRCL),Y, forcing R1's display
;   bit OFF so the table-fill bursts that follow run with the screen blanked
;   (the strict-mode access window is widest when display is off).
load_regs_off:
        ldy     #0
@l:     lda     (GSRCL),y
        cpy     #1
        bne     @st
        and     #$BF            ; R1 display OFF
@st:    sta     VDP_CTRL
        jsr     vpad
        tya
        ora     #$80
        sta     VDP_CTRL
        jsr     vpad
        iny
        cpy     #8
        bne     @l
        rts

; display_on -- re-arm register 1 with A (the mode's display-ON value).
display_on:
        sta     VDP_CTRL
        jsr     vpad
        lda     #$81            ; cmd = $80 | reg 1
        sta     VDP_CTRL
        jsr     vpad
        rts

; ============================================================================
; Lookup tables + register sets
; ============================================================================
bitmask:
        .byte   $80,$40,$20,$10,$08,$04,$02,$01

; name_row_lo/hi[row] = lo/hi of ($0800 + 40*row)  (text mode name table)
name_row_lo:
        .byte   $00,$28,$50,$78,$A0,$C8,$F0,$18
        .byte   $40,$68,$90,$B8,$E0,$08,$30,$58
        .byte   $80,$A8,$D0,$F8,$20,$48,$70,$98
name_row_hi:
        .byte   $08,$08,$08,$08,$08,$08,$08,$09
        .byte   $09,$09,$09,$09,$09,$0A,$0A,$0A
        .byte   $0A,$0A,$0A,$0A,$0B,$0B,$0B,$0B

; Text Mode F1: R1=$D0 text/on, R2=$02 name $0800, R4=$00 pattern $0000,
;               R7=$F0 white-on-black.
vdpt_regs:
        .byte   $00,$D0,$02,$00,$00,$00,$00,$F0

; Graphics II: R0=$02 M3, R1=$C0 on, R2=$0E name $3800, R3=$FF colour $2000,
;              R4=$03 pattern $0000, R5=$76 sprite attr $3B00, R6=$03, R7=$F1.
vdp2_regs:
        .byte   $02,$C0,$0E,$FF,$03,$76,$03,$F1

; Multicolor: R1=$C8 MC/on, R2=$02 name $0800, R4=$00 pattern $0000,
;             R7=$01 black border.
mc_regs:                        ; R5=$16 -> sprite attr table $0B00 (clear of the
        .byte   $00,$C8,$02,$00,$00,$16,$00,$01   ; $0000 pattern + $0800 name)

; bbfont -- Beautiful Boot 8x8 ASCII font ($20-$7F), already in TMS pattern
; bit order (MSB = leftmost). Uploaded by upload_font to pattern $0100.
        .include "bbfont_tms.inc"
