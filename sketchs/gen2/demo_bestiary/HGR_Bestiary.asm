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
; Blit: byte-aligned STA fast path (sprites sit at HGR byte columns, so each
; 16x16 sprite is 16 rows x 3 bytes stored straight into the framebuffer via the
; hgr_lo/hgr_hi scanline table — no per-pixel plotting, no bit shifting). One
; generalised draw_sprite serves every category (data base in zp).
;
; Single-bank 4 KB layout (apple1_gen2.cfg): code + all sprite data live in the
; $E000-$EFFF high bank. Build: `make`. Run: load the .txt, then  E000R.
; =============================================================================

.include "apple1.inc"

GRID_COLS    = 6
GRID_ROWS    = 4
GRID_X0      = 8                    ; HGR byte column of the grid's left edge
GRID_Y0      = 36                   ; top scanline of the first slot row
SLOT_X_PITCH = 4                    ; bytes per slot (3 sprite + 1 gap)
SLOT_Y_PITCH = 22                   ; scanlines per slot (16 sprite + 6 gap)
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
        ; hcol = GRID_X0 + slot_col * SLOT_X_PITCH (=4)
        LDA slot_col
        ASL
        ASL
        CLC
        ADC #GRID_X0
        STA hcol

        ; top_y = GRID_Y0 + slot_row * 22  (22 = 16 + 4 + 2 = row*18 + row*4)
        LDA slot_row
        ASL                         ; row*2
        STA top_y
        ASL                         ; row*4
        ASL                         ; row*8
        ASL                         ; row*16
        CLC
        ADC top_y                   ; row*16 + row*2 = row*18
        CLC
        ADC top_y                   ; + row*2 = row*20
        CLC
        ADC top_y                   ; + row*2 = row*22
        CLC
        ADC #GRID_Y0
        STA top_y

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
; draw_sprite -- A = sprite index within the current category.
;   Reads base_lo/hi (category data), hcol, top_y from zp. Blits 16 rows x 3
;   bytes from (base + A*48) straight into the framebuffer via hgr_lo/hgr_hi.
; Clobbers: A, X, Y, ptr_lo/hi, fpl/fph, gline.
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
        LDA gline
        CLC
        ADC top_y
        TAX
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi

        ; read the 3 sprite bytes (Y=2,1,0), push, write at hcol/+1/+2
        LDY #2
        LDA (fpl),Y
        PHA
        DEY
        LDA (fpl),Y
        PHA
        DEY
        LDA (fpl),Y
        PHA

        LDY hcol
        PLA
        STA (ptr_lo),Y              ; byte 0 (cols 0..6)
        INY
        PLA
        STA (ptr_lo),Y              ; byte 1 (cols 7..13)
        INY
        PLA
        STA (ptr_lo),Y              ; byte 2 (cols 14..15)

        ; advance sprite pointer by 3 (next row)
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
