; =============================================================================
; HGR_Bestiary.asm  --  browse the SCROLL-O-SPRITES "bestiary" on Uncle Bernie's
; GEN2 Color Graphics Card.
; -----------------------------------------------------------------------------
; A consumer for the pre-baked HGR sprite library (dev/lib/gen2/sprites/, the
; auto-generated GEN2 mirror of the TMS9918 SCROLL-O-SPRITES that TMS_Rogue uses
; on the TMS side). Before this, only hgr_symbols consumed one category; this
; pages through SIX of the fantasy-adventure categories:
;
;     CREATURES  TROLLKIND  UNLIVING  FAUNA  MAGICK  MUSIC   (54 sprites)
;
; Each category is shown as a 6x4 grid of its 16x16 sprites; the category name
; prints to the Apple-1 text terminal. SPACE advances to the next category
; (wraps); ESC quits to Wozmon.
;
; Colour: each SLOT is drawn in its own HGR artifact colour — a diagonal rainbow
; across the grid (colour = (slot_row + slot_col) & 3 → green / orange / blue /
; violet), so every page shows all four colours and no two neighbouring sprites
; match. The blit keeps one NTSC column-parity per dest byte ($55 / $2A) and sets
; the palette high bit (pal_* tables + colourise_row).
;
; Blit: byte-aligned STA fast path (sprites sit at HGR byte columns, so each
; 16x16 sprite is 16 rows x 3 bytes stored straight into the framebuffer via the
; hgr_lo/hgr_hi scanline table — no per-pixel plotting, no bit shifting). One
; generalised draw_sprite serves every category (data base in zp).
;
; Single-bank 4 KB layout (apple1_gen2.cfg): code + all sprite data live in the
; $E000-$EFFF high bank. Build: `make`. Run: load the .txt, then  E000R.
; =============================================================================

.include "apple1.inc"

; Sprites are drawn at DOUBLE size (2x): each 16x16 source sprite paints as a
; 32x32 block -- 6 dest bytes wide (each 7-px source byte stretches to two
; 7-px bytes via dblL/dblR) x 32 scanlines (each source row painted twice).
GRID_COLS    = 4
GRID_ROWS    = 4
GRID_X0      = 4                    ; HGR byte column of the grid's left edge
GRID_Y0      = 12                   ; top scanline of the first slot row
SLOT_X_PITCH = 8                    ; bytes per slot (6 sprite + 2 gap)
SLOT_Y_PITCH = 40                   ; scanlines per slot (32 sprite + 8 gap)
NUM_CATS     = 6                    ; entries in cat_* tables below

.zeropage
            .res 2                  ; leave $00..$01 reserved
ptr_lo:     .res 1                  ; HGR scanline pointer (clear_hgr + blit)
ptr_hi:     .res 1
fpl:        .res 1                  ; sprite data pointer
fph:        .res 1
hcol:       .res 1                  ; current slot's left HGR byte
top_y:      .res 1                  ; current slot's top scanline
gline:      .res 1                  ; current row within sprite (0..15)
sm_idx:     .res 1                  ; sprite index within category
slot_col:   .res 1                  ; current slot column (0..GRID_COLS-1)
slot_row:   .res 1                  ; current slot row (0..GRID_ROWS-1)
cur_cat:    .res 1                  ; current category (0..NUM_CATS-1)
base_lo:    .res 1                  ; current category data base
base_hi:    .res 1
ccount:     .res 1                  ; current category sprite count
nm_lo:      .res 1                  ; current category name string
nm_hi:      .res 1
cur_y:      .res 1                  ; scanline being written by the 2x blit
d0:         .res 1                  ; six doubled dest bytes for the current row
d1:         .res 1
d2:         .res 1
d3:         .res 1
d4:         .res 1
d5:         .res 1
; Per-category HGR artifact colour (see cat_col_* tables + draw_sprite). col_ev /
; col_od are the column-parity masks for even / odd dest byte columns ($55 or $2A,
; or $7F for white); col_hi is the palette high bit ($00 = green/violet family,
; $80 = blue/orange). Loaded once per category in show_cat.
col_ev:     .res 1
col_od:     .res 1
col_hi:     .res 1

.code

; --- Per-category constants (immediate-mode counts) from the .inc headers. The
;     data labels (<cat>_hgr_data) come from the .asm blocks .include'd at the
;     bottom. Pick the HGR variant only — linking the TMS sprites_<cat>.asm too
;     would collide on the shared per-sprite labels. ---
.include "sprites_creatures_hgr.inc"
.include "sprites_trollkind_hgr.inc"
.include "sprites_unliving_hgr.inc"
.include "sprites_fauna_hgr.inc"
.include "sprites_magick_hgr.inc"
.include "sprites_music_hgr.inc"

main:
        JSR gen2_hgr_init
        LDA #<str_intro
        LDX #>str_intro
        JSR print_str_ax
        LDA #0
        STA cur_cat

; -----------------------------------------------------------------------------
; Render the current category, then wait for a key.
; -----------------------------------------------------------------------------
show_cat:
        LDX cur_cat
        LDA cat_base_lo,X
        STA base_lo
        LDA cat_base_hi,X
        STA base_hi
        LDA cat_count,X
        STA ccount
        LDA cat_name_lo,X
        STA nm_lo
        LDA cat_name_hi,X
        STA nm_hi

        ; print "category (N) ..." header to the text terminal
        LDA nm_lo
        LDX nm_hi
        JSR print_str_ax

        JSR clear_hgr

        ; --- iterate slots row-major; stop after ccount sprites ---
        LDA #0
        STA sm_idx
        STA slot_row
@row_lp:
        LDA #0
        STA slot_col
@col_lp:
        ; hcol = GRID_X0 + slot_col * SLOT_X_PITCH (=8)
        LDA slot_col
        ASL
        ASL
        ASL
        CLC
        ADC #GRID_X0
        STA hcol

        ; top_y = GRID_Y0 + slot_row * 40  (40 = row*32 + row*8)
        LDA slot_row
        ASL                         ; row*2
        ASL                         ; row*4
        ASL                         ; row*8
        STA top_y                   ; = row*8
        ASL                         ; row*16
        ASL                         ; row*32
        CLC
        ADC top_y                   ; row*32 + row*8 = row*40
        CLC
        ADC #GRID_Y0
        STA top_y

        ; per-slot colour: a diagonal rainbow over the grid, colour =
        ; (slot_row + slot_col) & 3 into the 4-entry HGR palette, so no two
        ; neighbouring sprites (across or down) share a colour.
        LDA slot_row
        CLC
        ADC slot_col
        AND #3
        TAX
        LDA pal_ev,X
        STA col_ev
        LDA pal_od,X
        STA col_od
        LDA pal_hi,X
        STA col_hi

        LDA sm_idx
        JSR draw_sprite

        INC sm_idx
        LDA sm_idx
        CMP ccount
        BCS @cat_done

        INC slot_col
        LDA slot_col
        CMP #GRID_COLS
        BCC @col_lp
        INC slot_row
        LDA slot_row
        CMP #GRID_ROWS
        BCC @row_lp

@cat_done:
@wait:
        LDA KBDCR
        BPL @wait
        LDA KBD
        AND #$7F
        CMP #$1B                    ; ESC -> quit to Wozmon
        BEQ @quit
        ; any other key -> next category (wrap)
        INC cur_cat
        LDA cur_cat
        CMP #NUM_CATS
        BCC @same
        LDA #0
        STA cur_cat
@same:
        JMP show_cat
@quit:
        RTS


; -----------------------------------------------------------------------------
; draw_sprite -- A = sprite index within the current category, drawn at 2x.
;   Reads base_lo/hi (category data), hcol, top_y from zp. For each of the 16
;   source rows: stretch the 3 source bytes into 6 dest bytes (dblL/dblR turn
;   one 7-px byte into two) and paint that row onto TWO consecutive scanlines,
;   giving a 32x32 doubled sprite.
; Clobbers: A, X, Y, ptr_lo/hi, fpl/fph, gline, cur_y, d0..d5.
; -----------------------------------------------------------------------------
draw_sprite:
        ; fp = A*48 + base   (A*48 by repeated add; counts <= 33 -> <= 1584)
        TAX
        LDA #0
        STA fpl
        STA fph
        TXA
        BEQ @add_base
@mul:
        LDA fpl
        CLC
        ADC #48
        STA fpl
        BCC @nc
        INC fph
@nc:
        DEX
        BNE @mul
@add_base:
        LDA fpl
        CLC
        ADC base_lo
        STA fpl
        LDA fph
        ADC base_hi
        STA fph

        LDA #0
        STA gline
@line:
        ; --- stretch the 3 source bytes into d0..d5 ---
        LDY #0
        LDA (fpl),Y
        AND #$7F                    ; bit 7 = NTSC selector, keep off the index
        TAX
        LDA dblL,X
        STA d0
        LDA dblR,X
        STA d1
        LDY #1
        LDA (fpl),Y
        AND #$7F
        TAX
        LDA dblL,X
        STA d2
        LDA dblR,X
        STA d3
        LDY #2
        LDA (fpl),Y
        AND #$7F
        TAX
        LDA dblL,X
        STA d4
        LDA dblR,X
        STA d5

        JSR colourise_row       ; tint d0..d5 with the category's HGR colour

        ; --- paint d0..d5 onto two scanlines: cur_y = top_y + gline*2, +1 ---
        LDA gline
        ASL
        CLC
        ADC top_y
        STA cur_y
        TAX
        JSR blit_row6
        INC cur_y
        LDX cur_y
        JSR blit_row6

        ; advance sprite pointer by 3 (next source row)
        LDA fpl
        CLC
        ADC #3
        STA fpl
        BCC @nc2
        INC fph
@nc2:
        INC gline
        LDA gline
        CMP #16
        BCC @line
        RTS

; -----------------------------------------------------------------------------
; blit_row6 -- X = scanline. Writes d0..d5 to hcol..hcol+5 on that scanline.
; Clobbers A, Y, ptr_lo/hi.
; -----------------------------------------------------------------------------
blit_row6:
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi
        LDY hcol
        LDA d0
        STA (ptr_lo),Y
        INY
        LDA d1
        STA (ptr_lo),Y
        INY
        LDA d2
        STA (ptr_lo),Y
        INY
        LDA d3
        STA (ptr_lo),Y
        INY
        LDA d4
        STA (ptr_lo),Y
        INY
        LDA d5
        STA (ptr_lo),Y
        RTS

; -----------------------------------------------------------------------------
; colourise_row -- give d0..d5 this category's HGR artifact colour: keep one
;   column-parity per dest byte (col_ev for even bytes d0/d2/d4, col_od for odd
;   bytes d1/d3/d5) and set the palette high bit (col_hi). White categories use
;   col_ev = col_od = $7F, so the mask is a no-op and the sprite stays white.
;   Called once per source row (16x per sprite) before painting.
; -----------------------------------------------------------------------------
colourise_row:
        LDA d0
        AND col_ev
        ORA col_hi
        STA d0
        LDA d1
        AND col_od
        ORA col_hi
        STA d1
        LDA d2
        AND col_ev
        ORA col_hi
        STA d2
        LDA d3
        AND col_od
        ORA col_hi
        STA d3
        LDA d4
        AND col_ev
        ORA col_hi
        STA d4
        LDA d5
        AND col_od
        ORA col_hi
        STA d5
        RTS


; -----------------------------------------------------------------------------
; Category dispatch tables (parallel arrays, indexed by cur_cat).
; -----------------------------------------------------------------------------
cat_base_lo:
        .byte <creatures_hgr_data, <trollkind_hgr_data, <unliving_hgr_data
        .byte <fauna_hgr_data, <magick_hgr_data, <music_hgr_data
cat_base_hi:
        .byte >creatures_hgr_data, >trollkind_hgr_data, >unliving_hgr_data
        .byte >fauna_hgr_data, >magick_hgr_data, >music_hgr_data
cat_count:
        .byte CREATURES_HGR_COUNT, TROLLKIND_HGR_COUNT, UNLIVING_HGR_COUNT
        .byte FAUNA_HGR_COUNT, MAGICK_HGR_COUNT, MUSIC_HGR_COUNT
cat_name_lo:
        .byte <name_creatures, <name_trollkind, <name_unliving
        .byte <name_fauna, <name_magick, <name_music
cat_name_hi:
        .byte >name_creatures, >name_trollkind, >name_unliving
        .byte >name_fauna, >name_magick, >name_music

; The 4 chromatic HGR artifact colours, indexed per slot for a diagonal rainbow.
; Each column-parity mask keeps only the pixels of one NTSC phase ($55 / $2A,
; alternating even/odd dest byte), and the high bit picks the palette family:
;   0 green · 1 orange · 2 blue · 3 violet
pal_ev:
        .byte $55, $2A, $55, $2A
pal_od:
        .byte $2A, $55, $2A, $55
pal_hi:
        .byte $00, $80, $80, $00

str_intro:
        .byte $0D
        .byte " * SCROLL-O-SPRITES HGR BESTIARY *", $0D
        .byte " GEN2 16x16 sprites -- Quale CC-BY-3.0", $0D
        .byte " SPACE = next category, ESC = quit", $0D, 0
name_creatures:
        .byte $0D, " CREATURES  (8)", $0D, 0
name_trollkind:
        .byte $0D, " TROLLKIND  (4)", $0D, 0
name_unliving:
        .byte $0D, " UNLIVING   (8)", $0D, 0
name_fauna:
        .byte $0D, " FAUNA      (13)", $0D, 0
name_magick:
        .byte $0D, " MAGICK     (15)", $0D, 0
name_music:
        .byte $0D, " MUSIC      (6)", $0D, 0

; -----------------------------------------------------------------------------
; 2x horizontal-stretch tables. Index = a 7-px source byte (bit 7 masked off).
; dblL = the left doubled byte  (src pixels 0,0,1,1,2,2,3),
; dblR = the right doubled byte (src pixels 3,4,4,5,5,6,6), so one source byte
; becomes two, doubling every pixel horizontally with output bit 7 clear.
; -----------------------------------------------------------------------------
dblL:
        .byte $00, $03, $0C, $0F, $30, $33, $3C, $3F, $40, $43, $4C, $4F, $70, $73, $7C, $7F
        .byte $00, $03, $0C, $0F, $30, $33, $3C, $3F, $40, $43, $4C, $4F, $70, $73, $7C, $7F
        .byte $00, $03, $0C, $0F, $30, $33, $3C, $3F, $40, $43, $4C, $4F, $70, $73, $7C, $7F
        .byte $00, $03, $0C, $0F, $30, $33, $3C, $3F, $40, $43, $4C, $4F, $70, $73, $7C, $7F
        .byte $00, $03, $0C, $0F, $30, $33, $3C, $3F, $40, $43, $4C, $4F, $70, $73, $7C, $7F
        .byte $00, $03, $0C, $0F, $30, $33, $3C, $3F, $40, $43, $4C, $4F, $70, $73, $7C, $7F
        .byte $00, $03, $0C, $0F, $30, $33, $3C, $3F, $40, $43, $4C, $4F, $70, $73, $7C, $7F
        .byte $00, $03, $0C, $0F, $30, $33, $3C, $3F, $40, $43, $4C, $4F, $70, $73, $7C, $7F
dblR:
        .byte $00, $00, $00, $00, $00, $00, $00, $00, $01, $01, $01, $01, $01, $01, $01, $01
        .byte $06, $06, $06, $06, $06, $06, $06, $06, $07, $07, $07, $07, $07, $07, $07, $07
        .byte $18, $18, $18, $18, $18, $18, $18, $18, $19, $19, $19, $19, $19, $19, $19, $19
        .byte $1E, $1E, $1E, $1E, $1E, $1E, $1E, $1E, $1F, $1F, $1F, $1F, $1F, $1F, $1F, $1F
        .byte $60, $60, $60, $60, $60, $60, $60, $60, $61, $61, $61, $61, $61, $61, $61, $61
        .byte $66, $66, $66, $66, $66, $66, $66, $66, $67, $67, $67, $67, $67, $67, $67, $67
        .byte $78, $78, $78, $78, $78, $78, $78, $78, $79, $79, $79, $79, $79, $79, $79, $79
        .byte $7E, $7E, $7E, $7E, $7E, $7E, $7E, $7E, $7F, $7F, $7F, $7F, $7F, $7F, $7F, $7F

; -----------------------------------------------------------------------------
; Mutualised text printer + HGR helpers + the six sprite data blocks.
; -----------------------------------------------------------------------------
.include "print.asm"
.include "hgr_clear.asm"           ; clear_hgr (needs ZP ptr_lo/ptr_hi)
.include "hgr_scanline.inc"        ; hgr_lo / hgr_hi scanline base tables

.include "sprites/sprites_creatures_hgr.asm"
.include "sprites/sprites_trollkind_hgr.asm"
.include "sprites/sprites_unliving_hgr.asm"
.include "sprites/sprites_fauna_hgr.asm"
.include "sprites/sprites_magick_hgr.asm"
.include "sprites/sprites_music_hgr.asm"
.include "gen2_init.asm"
