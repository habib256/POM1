; ============================================================================
; gen2gfx.inc -- GEN2 graphics + console handlers for Applesoft GEN2
; ============================================================================
; .include'd at the end of applesoft-gen2.s, so it shares the interpreter's
; namespace (GETBYT, GTNUM, COMBYTE, CHKOPN, CHKCLS, CHKCOM, SYNCHR, CHRGET,
; CHRGOT, SNGFLT, PRINTCORE, TOKEN_TO, TOKEN_SCRN, CSW, MONECHO via $FFEF).
;
; Commands (dispatched from TOKEN_ADDRESS_TABLE):
;   Graphics modes : TEXT GR HGR GR2 HGR2 MIX NOMIX SHOW
;   Lo-res draw    : COLOR= PLOT HLIN..AT VLIN..AT          (page $0400/$0800)
;   Hi-res draw    : HCOLOR= HPLOT..TO                      (page $2000/$4000)
;   Text console   : HOME HTAB VTAB     PRINT->GEN2  APRINT->Apple-1
;   Sync           : VBL                (coarse vertical-blank wait)
;   Function       : SCRN(x,y)          (lo-res colour read)
;
; Output model: CSW (Apple II char-out vector) defaults to the Apple-1 WOZ ECHO
; ($FFEF), so the prompt/LIST/errors/INPUT echo stay on the Apple-1 terminal.
; The PRINT wrapper points CSW at the GEN2 text console (GCOUT) for its output;
; APRINT forces CSW back to $FFEF. Double-buffering: SHOW n displays page n and
; routes subsequent drawing to the hidden page.
; ============================================================================

.include "gen2.inc"             ; GEN2_TEXTOFF..GEN2_HIRES soft switches

AECHO     = $FFEF               ; Apple-1 WOZ Monitor ECHO (CSW default)

; --- zero page scratch (verified free in Applesoft Lite: $06-$0C) -----------
GP        = $06                 ; 16-bit framebuffer / text pointer
GTMP      = $08                 ; pixel bitmask / scratch
GBYTE     = $09                 ; framebuffer byte being modified
GIDX      = $0A                 ; byte index within the scanline (0..39)

; --- GEN2 state block ($0300-$031C; CSW lives at $031E, see zeropage.s) ------
GST       = $0300
HCOLORVAL = GST+0               ; hi-res colour 0..7
LRCOLOR   = GST+1               ; lo-res colour, duplicated into both nibbles
HX_LO     = GST+2               ; hi-res current X (0..279)
HX_HI     = GST+3
HY        = GST+4               ; hi-res current Y (0..191)
X2_LO     = GST+5               ; line endpoint X
X2_HI     = GST+6
Y2        = GST+7               ; line endpoint Y
DX_LO     = GST+8               ; Bresenham |dx|
DX_HI     = GST+9
DY_LO     = GST+10              ; Bresenham |dy|
DY_HI     = GST+11
SX        = GST+12              ; x step: $01 or $FF
SY        = GST+13              ; y step: $01 or $FF
ERRL      = GST+14             ; Bresenham error term (16-bit signed)
ERRH      = GST+15
E2L       = GST+16             ; 2*err / div7 dividend / scroll src ptr
E2H       = GST+17
LRX       = GST+18             ; lo-res plot X / loop cursor
LRY       = GST+19             ; lo-res plot Y / loop cursor
LREND     = GST+20             ; lo-res line endpoint (x2 or y2)
DPGHI     = GST+21             ; hi-res draw-page offset ($00 page1, $20 page2)
DPGLR     = GST+22             ; lo-res draw-page offset ($00 page1, $04 page2)
CH        = GST+23             ; GEN2 text cursor column 0..39
CV        = GST+24             ; GEN2 text cursor row 0..23
GCSX      = GST+25             ; GCOUT saved X
GCSY      = GST+26             ; GCOUT saved Y
INVFLG    = GST+27             ; math2026 video mask: $FF normal, $3F inverse, $7F flash
WNDTOP    = GST+28             ; text-window top row (Apple II WNDTOP): 0 in TEXT,
                               ; 20 in any graphics mode so HOME/scroll touch only
                               ; the 4-line text window, never the lo-res picture.
                               ; ($031C; CSW is $031E, $031C/$031D were free.)

; ============================================================================
; Cold-start init (called from COLDSTART)
; ============================================================================
GEN2_BOOT:
        lda     #<AECHO         ; CSW := Apple-1 WOZ ECHO ($FFEF)
        sta     CSW
        lda     #>AECHO
        sta     CSW+1
        lda     #$00
        sta     DPGHI           ; draw page 1
        sta     DPGLR
        sta     LRCOLOR
        sta     CH
        sta     CV
        sta     WNDTOP          ; boot is TEXT mode: text window = full 24 rows
        lda     #$FF            ; math2026: default video = NORMAL
        sta     INVFLG
        lda     #3
        sta     HCOLORVAL       ; default hi-res colour = white
        lda     GEN2_TEXTON     ; GEN2 to text mode, page 1
        lda     GEN2_PAGE1
        jmp     clear_text_window ; WNDTOP=0 here -> blanks all 24 rows, rts

; ============================================================================
; PRINT (-> GEN2 text console) / APRINT (-> Apple-1 terminal)
; ============================================================================
; The dispatch enters with A = first char after the token and Z = end-of-list;
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

; GCOUT -- GEN2 text console char-out. A = char (bit7 set, as OUTDO passes).
; Writes Apple II screen codes to the $0400 page at (CH,CV); handles CR + wrap +
; scroll. Preserves X and Y (PRINTCORE's STRPRT relies on them).
GCOUT:
        sty     GCSY
        stx     GCSX
        and     #$7F
        cmp     #$0D            ; CR -> newline
        beq     @nl
        ora     #$80            ; to normal screen-code range $80-$FF
        and     INVFLG          ; math2026: NORMAL=$FF, INVERSE=$3F, FLASH=$7F
        pha
        ldx     CV
        lda     lores_lo,x      ; text row base (page 1)
        sta     GP
        lda     lores_hi,x
        sta     GP+1
        ldy     CH
        pla
        sta     (GP),y          ; store the glyph
        inc     CH
        lda     CH
        cmp     #40
        bcc     @done           ; same line
@nl:    lda     #0              ; carriage return / wrap
        sta     CH
        inc     CV
        lda     CV
        cmp     #24
        bcc     @done
        jsr     text_scroll     ; off the bottom -> scroll, stay on row 23
        lda     #23
        sta     CV
@done:  ldx     GCSX
        ldy     GCSY
        rts

; text_scroll -- scroll the $0400 text page up one row; blank row 23.
; GP = dest row ptr ($06/$07); GTMP/GBYTE ($08/$09) = src row ptr (both ZP).
text_scroll:
        ldx     WNDTOP          ; copy row x+1 -> row x, for x = WNDTOP..22
                                ; (so a mixed-mode scroll never drags the lo-res
                                ; picture in rows 0..19 up through the window)
@row:   lda     lores_lo,x      ; dest = row x
        sta     GP
        lda     lores_hi,x
        sta     GP+1
        inx
        lda     lores_lo,x      ; src = row x+1
        sta     GTMP
        lda     lores_hi,x
        sta     GBYTE
        dex
        ldy     #39
@cp:    lda     (GTMP),y
        sta     (GP),y
        dey
        bpl     @cp
        inx
        cpx     #23
        bne     @row
        lda     lores_lo,x      ; x = 23: blank the bottom row
        sta     GP
        lda     lores_hi,x
        sta     GP+1
        lda     #$A0
        ldy     #39
@bl:    sta     (GP),y
        dey
        bpl     @bl
        rts

; ============================================================================
; Text cursor commands
; ============================================================================
GFX_HOME:                       ; clear the text WINDOW + home cursor to its top
        jsr     clear_text_window
        lda     #0
        sta     CH
        lda     WNDTOP
        sta     CV
        rts

; clear_text_window -- blank text rows WNDTOP..23 (40 bytes each) with $A0. In a
; graphics mode WNDTOP=20, so the lo-res picture (rows 0..19 = lo-res 0..39) is
; preserved -- the Apple II HOME-respects-the-text-window semantics. (The old
; GFX_HOME blanked the whole $0400 page, wiping the picture: AppleBite.apf draws
; an apple, then HOME, then the COLOR=0 "bite" -- the wipe destroyed the apple.)
clear_text_window:
        ldx     WNDTOP
@row:   lda     lores_lo,x
        sta     GP
        lda     lores_hi,x
        sta     GP+1
        lda     #$A0
        ldy     #39
@col:   sta     (GP),y
        dey
        bpl     @col
        inx
        cpx     #24
        bcc     @row
        rts

GFX_HTAB:                       ; HTAB c  (1..40 -> column 0..39)
        jsr     GETBYT
        dex
        txa
        cmp     #40
        bcc     @ok
        lda     #39
@ok:    sta     CH
        rts

GFX_VTAB:                       ; VTAB r  (1..24 -> row 0..23)
        jsr     GETBYT
        dex
        txa
        cmp     #24
        bcc     @ok
        lda     #23
@ok:    sta     CV
        rts

; ============================================================================
; math2026 video attributes -- set the screen-code mask applied in GCOUT.
; The GEN2 renderer interprets $00-$3F as inverse, $40-$7F as flashing,
; $80-$FF as normal video.
; ============================================================================
GFX_NORMAL:                     ; NORMAL
        lda     #$FF
        .byte   $2C             ; TRICK: skip next LDA (BIT abs)
GFX_INVERSE:                    ; INVERSE
        lda     #$3F
        .byte   $2C             ; TRICK: skip next LDA (BIT abs)
GFX_FLASH:                      ; FLASH
        lda     #$7F
        sta     INVFLG
        rts

; ============================================================================
; Mode commands
; ============================================================================
GFX_TEXT:
        lda     #0              ; full-screen text: window = all 24 rows
        sta     WNDTOP
        lda     GEN2_TEXTON
        lda     GEN2_PAGE1
        rts

GFX_NOMIX:
        lda     GEN2_MIXOFF
        rts

GFX_MIX:
        lda     GEN2_MIXON
        ; The bottom 4 rows now show TEXT. After GR they hold graphics-black
        ; ($00), which the text renderer reads as inverse '@'. Blank them to
        ; space ($A0). WNDTOP=20 (set by the preceding GR/HGR) scopes the clear
        ; to exactly the 4-line window; the graphics rows 0..19 are untouched.
        jmp     clear_text_window

GFX_GR:                         ; lo-res, page 1
        lda     #20             ; text window = bottom 4 rows (20..23)
        sta     WNDTOP
        lda     #$00
        sta     DPGLR
        lda     GEN2_TEXTOFF
        lda     GEN2_LORES
        lda     GEN2_PAGE1
        lda     GEN2_MIXOFF
        lda     #0
        sta     LRCOLOR
        jmp     lores_clear

GFX_GR2:                        ; lo-res, page 2 ($0800)
        lda     #20             ; text window = bottom 4 rows (20..23)
        sta     WNDTOP
        lda     #$04
        sta     DPGLR
        lda     GEN2_TEXTOFF
        lda     GEN2_LORES
        lda     GEN2_PAGE2
        lda     GEN2_MIXOFF
        lda     #0
        sta     LRCOLOR
        jmp     lores_clear

GFX_HGR:                        ; hi-res, page 1 ($2000)
        lda     #20             ; text window = bottom 4 rows (20..23)
        sta     WNDTOP
        lda     #$00
        sta     DPGHI
        lda     GEN2_TEXTOFF
        lda     GEN2_HIRES
        lda     GEN2_PAGE1
        lda     GEN2_MIXOFF
        lda     #3
        sta     HCOLORVAL       ; default colour white (no black-pixel gotcha)
        jmp     clear_hgr_draw

GFX_HGR2:                       ; hi-res, page 2 ($4000)
        lda     #20             ; text window = bottom 4 rows (20..23)
        sta     WNDTOP
        lda     #$20
        sta     DPGHI
        lda     GEN2_TEXTOFF
        lda     GEN2_HIRES
        lda     GEN2_PAGE2
        lda     GEN2_MIXOFF
        lda     #3
        sta     HCOLORVAL
        jmp     clear_hgr_draw

; SHOW n -- display page n (1|2) and route drawing to the HIDDEN page (3-n),
; the page-flip primitive for tear-free double buffering.
GFX_SHOW:
        jsr     GETBYT
        cpx     #2
        beq     @p2
        lda     GEN2_PAGE1      ; show page 1, draw page 2
        lda     #$20
        sta     DPGHI
        lda     #$04
        sta     DPGLR
        rts
@p2:    lda     GEN2_PAGE2      ; show page 2, draw page 1
        lda     #$00
        sta     DPGHI
        sta     DPGLR
        rts

; VBL -- coarse wait for vertical blank. Polls GEN2_TEXTOFF (no-op read in any
; graphics mode); a blank that outlives an H-blank is the V-blank. Use in GR/HGR.
GFX_VBL:
@nb:    lda     GEN2_TEXTOFF    ; if currently blank, wait for live first
        ora     GEN2_TEXTOFF
        bmi     @nb
@wb:    lda     GEN2_TEXTOFF    ; wait for a blank to begin
        ora     GEN2_TEXTOFF
        bpl     @wb
        ldx     #0              ; count consecutive blank samples
@cnt:   lda     GEN2_TEXTOFF
        ora     GEN2_TEXTOFF
        bpl     @nb             ; blank ended quickly (H-blank) -> restart
        inx
        cpx     #20             ; stayed blank -> V-blank
        bne     @cnt
        rts

; ============================================================================
; Framebuffer clear (draw page)
; ============================================================================
clear_hgr_draw:                 ; zero the hi-res draw page (8 KB)
        lda     #$20
        clc
        adc     DPGHI           ; $20 (page1) or $40 (page2)
        sta     GP+1
        lda     #$00
        sta     GP
        ldx     #$20            ; 32 pages
        tay                     ; Y=0, A=0
@l:     sta     (GP),y
        iny
        bne     @l
        inc     GP+1
        dex
        bne     @l
        rts

lores_clear:                    ; zero the lo-res draw page (1 KB)
        lda     #$04
        clc
        adc     DPGLR           ; $04 (page1) or $08 (page2)
        sta     GP+1
        lda     #$00
        sta     GP
        ldx     #$04            ; 4 pages
        tay
@l:     sta     (GP),y
        iny
        bne     @l
        inc     GP+1
        dex
        bne     @l
        rts

; ============================================================================
; Lo-res colour / point / lines  ($0400 page, Apple II interleave)
; ============================================================================
GFX_COLOR:                      ; COLOR= n  (0..15)
        jsr     GETBYT
        txa
        and     #$0F
        sta     GTMP
        asl     a
        asl     a
        asl     a
        asl     a
        ora     GTMP            ; colour in both nibbles
        sta     LRCOLOR
        rts

; lores_plot -- plot (LRX,LRY) in LRCOLOR on the lo-res draw page. Guards x<40,y<48.
lores_plot:
        lda     LRX
        cmp     #40
        bcs     @skip
        lda     LRY
        cmp     #48
        bcs     @skip
        lda     LRY
        lsr     a               ; row = y/2
        tax
        lda     lores_lo,x
        sta     GP
        lda     lores_hi,x
        clc
        adc     DPGLR
        sta     GP+1
        ldy     LRX
        lda     (GP),y
        sta     GBYTE
        lda     LRY
        and     #$01
        bne     @odd
        lda     GBYTE           ; even y -> low nibble
        and     #$F0
        sta     GBYTE
        lda     LRCOLOR
        and     #$0F
        ora     GBYTE
        jmp     @wr
@odd:   lda     GBYTE           ; odd y -> high nibble
        and     #$0F
        sta     GBYTE
        lda     LRCOLOR
        and     #$F0
        ora     GBYTE
@wr:    ldy     LRX
        sta     (GP),y
@skip:  rts

GFX_PLOT:                       ; PLOT x,y
        jsr     GETBYT
        stx     LRX
        jsr     COMBYTE
        stx     LRY
        jmp     lores_plot

GFX_HLIN:                       ; HLIN x1,x2 AT y
        jsr     GETBYT
        stx     LRX
        jsr     COMBYTE
        stx     LREND
        jsr     synch_at
        jsr     GETBYT
        stx     LRY
@loop:  jsr     lores_plot
        lda     LRX
        cmp     LREND
        beq     @done
        bcc     @inc
        dec     LRX
        jmp     @loop
@inc:   inc     LRX
        jmp     @loop
@done:  rts

GFX_VLIN:                       ; VLIN y1,y2 AT x
        jsr     GETBYT
        stx     LRY
        jsr     COMBYTE
        stx     LREND
        jsr     synch_at
        jsr     GETBYT
        stx     LRX
@loop:  jsr     lores_plot
        lda     LRY
        cmp     LREND
        beq     @done
        bcc     @inc
        dec     LRY
        jmp     @loop
@inc:   inc     LRY
        jmp     @loop
@done:  rts

; Require the literal "AT" (no token for it in this BASIC).
synch_at:
        lda     #'A'
        jsr     SYNCHR
        lda     #'T'
        jmp     SYNCHR

; SCRN(x,y) -- read lo-res colour at (x,y) on the draw page -> FAC. Entered from
; UNARY with A = SCRN token, TXTPTR at the token.
SCRN_PARSE:
        jsr     CHRGET          ; past SCRN
        jsr     CHKOPN          ; "("
        jsr     GTNUM           ; "x,y": x -> LINNUM, y -> X
        stx     LRY
        lda     LINNUM
        sta     LRX
        jsr     CHKCLS          ; ")"
        lda     LRY
        cmp     #48
        bcs     @zero
        lda     LRX
        cmp     #40
        bcs     @zero
        lda     LRY
        lsr     a
        tax
        lda     lores_lo,x
        sta     GP
        lda     lores_hi,x
        clc
        adc     DPGLR
        sta     GP+1
        ldy     LRX
        lda     (GP),y
        ldy     LRY
        tax                     ; X = byte
        tya
        and     #$01
        beq     @lo
        txa                     ; odd y -> high nibble
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        jmp     @ret
@lo:    txa                     ; even y -> low nibble
        and     #$0F
@ret:   tay
        jmp     SNGFLT          ; Y -> FAC
@zero:  ldy     #0
        jmp     SNGFLT

; ============================================================================
; Hi-res colour / point / line  ($2000 page, Apple II layout)
; ============================================================================
GFX_HCOLOR:                     ; HCOLOR= n  (0..7)
        jsr     GETBYT
        txa
        and     #$07
        sta     HCOLORVAL
        rts

GFX_HPLOT:                      ; HPLOT x,y  [TO x2,y2]...
        jsr     GTNUM           ; "x,y": x -> LINNUM, y -> X
        lda     LINNUM
        sta     HX_LO
        lda     LINNUM+1
        sta     HX_HI
        stx     HY
        jsr     hplot_pt
@to:    jsr     CHRGOT
        cmp     #TOKEN_TO
        bne     @rts
        jsr     CHRGET
        jsr     GTNUM
        lda     LINNUM
        sta     X2_LO
        lda     LINNUM+1
        sta     X2_HI
        stx     Y2
        jsr     hline_draw
        jmp     @to
@rts:   rts

; div7 -- byte index + pixel bitmask for HX (0..279).
;   out: GIDX = byte index (0..39), A = bitmask (one of bits 0..6)
div7:
        lda     HX_LO
        sta     E2L
        lda     HX_HI
        sta     E2H
        ldx     #$FF
@d:     inx
        lda     E2L
        sec
        sbc     #7
        sta     E2L
        lda     E2H
        sbc     #0
        sta     E2H
        bcs     @d
        lda     E2L
        clc
        adc     #7
        tay                     ; remainder 0..6
        stx     GIDX            ; quotient = byte index
        lda     #1
@s:     dey
        bmi     @done
        asl     a
        jmp     @s
@done:  rts

; hplot_pt -- set pixel (HX,HY) in HCOLORVAL on the hi-res draw page. Guards.
hplot_pt:
        lda     HX_HI
        bne     @hi
        jmp     @yck
@hi:    cmp     #$01
        bne     @skip           ; hi > 1 -> off screen
        lda     HX_LO
        cmp     #$18            ; 280 & $FF
        bcs     @skip
@yck:   lda     HY
        cmp     #192
        bcs     @skip
        ldx     HY
        lda     hgr_lo,x
        sta     GP
        lda     hgr_hi,x
        clc
        adc     DPGHI           ; draw page offset
        sta     GP+1
        jsr     div7
        sta     GTMP
        ldy     GIDX
        lda     (GP),y
        sta     GBYTE
        lda     HCOLORVAL       ; palette high bit
        and     #$04
        beq     @pal0
        lda     GBYTE
        ora     #$80
        sta     GBYTE
        jmp     @pix
@pal0:  lda     GBYTE
        and     #$7F
        sta     GBYTE
@pix:   lda     HCOLORVAL
        and     #$03
        beq     @off            ; black
        cmp     #$03
        beq     @on             ; white
        cmp     #$01
        beq     @oddc           ; green/orange -> on if x odd
        lda     HX_LO           ; violet/blue -> on if x even
        and     #$01
        beq     @on
        jmp     @off
@oddc:  lda     HX_LO
        and     #$01
        bne     @on
        jmp     @off
@on:    lda     GBYTE
        ora     GTMP
        sta     GBYTE
        jmp     @wr
@off:   lda     GTMP
        eor     #$FF
        and     GBYTE
        sta     GBYTE
@wr:    ldy     GIDX
        lda     GBYTE
        sta     (GP),y
@skip:  rts

; hline_draw -- Bresenham line (HX,HY)->(X2,Y2). Updates HX/HY to (X2,Y2).
hline_draw:
        lda     X2_LO           ; dx = X2 - HX (signed 16-bit)
        sec
        sbc     HX_LO
        sta     DX_LO
        lda     X2_HI
        sbc     HX_HI
        sta     DX_HI
        bpl     @xpos
        lda     #0
        sec
        sbc     DX_LO
        sta     DX_LO
        lda     #0
        sbc     DX_HI
        sta     DX_HI
        lda     #$FF
        sta     SX
        jmp     @dy
@xpos:  lda     #$01
        sta     SX
@dy:    lda     #0
        sta     DY_HI
        lda     Y2
        sec
        sbc     HY
        bcs     @ypos
        lda     HY
        sec
        sbc     Y2
        sta     DY_LO
        lda     #$FF
        sta     SY
        jmp     @err
@ypos:  sta     DY_LO
        lda     #$01
        sta     SY
@err:   lda     DX_LO
        sec
        sbc     DY_LO
        sta     ERRL
        lda     DX_HI
        sbc     DY_HI
        sta     ERRH
@loop:  jsr     hplot_pt
        lda     HX_LO
        cmp     X2_LO
        bne     @step
        lda     HX_HI
        cmp     X2_HI
        bne     @step
        lda     HY
        cmp     Y2
        bne     @step
        jmp     @done           ; reached endpoint
@step:  lda     ERRL            ; e2 = 2*err
        asl     a
        sta     E2L
        lda     ERRH
        rol     a
        sta     E2H
        clc                     ; T = e2 + dy ; step x if T > 0
        lda     E2L
        adc     DY_LO
        sta     GTMP
        lda     E2H
        adc     DY_HI
        sta     GBYTE
        bmi     @noX
        lda     GBYTE
        ora     GTMP
        beq     @noX
        lda     ERRL            ; err -= dy
        sec
        sbc     DY_LO
        sta     ERRL
        lda     ERRH
        sbc     DY_HI
        sta     ERRH
        lda     SX              ; HX += SX
        bmi     @xdec
        inc     HX_LO
        bne     @noX
        inc     HX_HI
        jmp     @noX
@xdec:  lda     HX_LO
        bne     @xd1
        dec     HX_HI
@xd1:   dec     HX_LO
@noX:   lda     E2L             ; step y if e2 < dx (signed)
        cmp     DX_LO
        lda     E2H
        sbc     DX_HI
        bvc     @nov
        eor     #$80
@nov:   bpl     @noY
        clc                     ; err += dx
        lda     ERRL
        adc     DX_LO
        sta     ERRL
        lda     ERRH
        adc     DX_HI
        sta     ERRH
        lda     SY              ; HY += SY
        bmi     @ydec
        inc     HY
        jmp     @noY
@ydec:  dec     HY
@noY:   jmp     @loop
@done:  rts

; ============================================================================
; Lookup tables
; ============================================================================
; lo-res / text row base ($0400 page, Apple II interleave): 24 byte-rows
lores_lo:
        .byte $00,$80,$00,$80,$00,$80,$00,$80
        .byte $28,$A8,$28,$A8,$28,$A8,$28,$A8
        .byte $50,$D0,$50,$D0,$50,$D0,$50,$D0
lores_hi:
        .byte $04,$04,$05,$05,$06,$06,$07,$07
        .byte $04,$04,$05,$05,$06,$06,$07,$07
        .byte $04,$04,$05,$05,$06,$06,$07,$07

.include "hgr_scanline.inc"     ; hgr_lo[192] / hgr_hi[192]  ($2000 scanlines)
