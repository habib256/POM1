; =============================================
; sokoban_common.inc - shared Sokoban plumbing
; =============================================
; Routines and lookup tables that are identical across the text, HGR
; (GEN2) and TMS9918 Sokoban front-ends. Include this once per program
; *inside .code*, after the zero-page/I/O/constants and before the
; program's own routines that reference them.
;
; The caller must define, before the include:
;   I/O:
;     ECHO    = $FFEF
;     KBD     = $D010
;     KBDCR   = $D011
;   Tile type constants TILE_FLOOR ... TILE_PLAYER_TARGET (0..6).
;   STATE_GRID       (absolute address of the playfield tile array)
;   STATE_GRID_LEN   (240 for 20x12, 192 for 16x12, ...; used by check_win)
;   LEVEL_BUF        (absolute address of a >=240 byte scratch buffer)
;   LEVELS_HAVE_PTRS (optional): 1 if caller has already declared its own
;                    level_ptrs_lo/level_ptrs_hi, 0 otherwise.
;   Zero-page byte variables:
;     sptr_lo, sptr_hi, str_lo, str_hi, temp, temp2,
;     lvl_w, lvl_h, row_offset, col_offset, level_idx
; =============================================

; ---------------------------------------------
; ascii_to_tile: map a Sokoban ASCII glyph to a tile type code.
; Input:  A = ASCII.   Output: A = TILE_* (0..6).
; ---------------------------------------------
ascii_to_tile:
        CMP #'#'
        BEQ @wall
        CMP #'.'
        BEQ @target
        CMP #'$'
        BEQ @box
        CMP #'*'
        BEQ @box_t
        CMP #'@'
        BEQ @player
        CMP #'+'
        BEQ @player_t
        LDA #TILE_FLOOR
        RTS
@wall:
        LDA #TILE_WALL
        RTS
@target:
        LDA #TILE_TARGET
        RTS
@box:
        LDA #TILE_BOX
        RTS
@box_t:
        LDA #TILE_BOX_TARGET
        RTS
@player:
        LDA #TILE_PLAYER
        RTS
@player_t:
        LDA #TILE_PLAYER_TARGET
        RTS

; ---------------------------------------------
; leave_tile:  occupant (player/box) leaves cell  -> floor or target
; enter_player: player enters empty cell (0/2)    -> 5 or 6
; enter_as_box: put a box back (used by undo)     -> 3 or 4
; All three preserve X (callers use it as a cell index); scratch Y only.
; ---------------------------------------------
leave_tile:
        TAY
        LDA leave_tbl,Y
        RTS
enter_player:
        TAY
        LDA enter_player_tbl,Y
        RTS
enter_as_box:
        TAY
        LDA enter_box_tbl,Y
        RTS

; ---------------------------------------------
; check_win: returns A=1 if every target has a box on it, else A=0.
; A target is "empty" if the cell still shows TILE_TARGET (2) or the
; player is standing on it (TILE_PLAYER_TARGET, 6).
; ---------------------------------------------
check_win:
        LDY #$00
@loop:  LDA STATE_GRID,Y
        CMP #TILE_TARGET
        BEQ @no
        CMP #TILE_PLAYER_TARGET
        BEQ @no
        INY
        CPY #STATE_GRID_LEN
        BNE @loop
        LDA #$01
        RTS
@no:    LDA #$00
        RTS

; ---------------------------------------------
; wait_key (block on key) and print_str_ax (print ASCIIZ via ECHO) used to
; live inline in this file. Now factored to lib/apple1/kbd.asm and
; lib/apple1/print.asm — pulled in below via textual .include. The
; print_ptr_lo/hi aliases fold print.asm's ZP slots onto the project's
; existing str_lo/str_hi (consistent in all three Sokoban variants
; — text 4 KB, HGR, TMS9918).
; ---------------------------------------------
print_ptr_lo = str_lo
print_ptr_hi = str_hi
.include "kbd.asm"
.include "print.asm"

; ---------------------------------------------
; load_level: fetch the level at index level_idx, expand its RLE body
; into LEVEL_BUF, and set lvl_w / lvl_h / row_offset / col_offset.
;
; Preserves: level_idx.
; Trashes:   A, X, Y, temp, temp2, sptr_lo, sptr_hi.
; Output:    LEVEL_BUF[0 .. lvl_w*lvl_h-1] = raw ASCII playfield.
;            Callers iterate LEVEL_BUF exactly like they used to
;            iterate the old uncompressed (sptr_lo),Y stream.
;
; RLE format (produced by tools/sokoban_rle.py):
;   byte < $80 : emit byte as literal (Sokoban ASCII is 7-bit so this
;                always applies to a level character).
;   byte >=$80 : run. (byte & $7F) = count (2..127), next byte = glyph.
; Decoder assumes the compressed blob is <=256 bytes (largest level so
; far is 117 raw cells -> always holds in one page).
; ---------------------------------------------
load_level:
        LDX level_idx
        LDA level_ptrs_lo,X
        STA sptr_lo
        LDA level_ptrs_hi,X
        STA sptr_hi

        ; Read the 4-byte header
        LDY #$00
        LDA (sptr_lo),Y
        STA lvl_w
        INY
        LDA (sptr_lo),Y
        STA lvl_h
        INY
        LDA (sptr_lo),Y
        STA row_offset
        INY
        LDA (sptr_lo),Y
        STA col_offset

        ; Skip past header
        CLC
        LDA sptr_lo
        ADC #$04
        STA sptr_lo
        LDA sptr_hi
        ADC #$00
        STA sptr_hi

        ; total = lvl_w * lvl_h (stored in temp; max 240 fits in a byte)
        LDA #$00
        STA temp
        LDY lvl_h
        BEQ @decode                     ; paranoid: h==0
@mul:
        CLC
        LDA temp
        ADC lvl_w
        STA temp
        DEY
        BNE @mul

@decode:
        LDY #$00                        ; source index (compressed)
        LDX #$00                        ; dest   index (LEVEL_BUF)
@main:
        LDA (sptr_lo),Y
        INY
        CMP #$80
        BCC @literal

        AND #$7F
        STA temp2                       ; run count
        LDA (sptr_lo),Y
        INY
@runlp:
        STA LEVEL_BUF,X
        INX
        DEC temp2
        BNE @runlp
        JMP @chk

@literal:
        STA LEVEL_BUF,X
        INX
@chk:
        CPX temp
        BCC @main
        RTS

; ---------------------------------------------
; Shared lookup tables
; ---------------------------------------------
leave_tbl:
        .byte 0, 1, 2, 0, 2, 0, 2
enter_player_tbl:
        .byte 5, 0, 6, 0, 0, 0, 0
enter_box_tbl:
        .byte 3, 0, 4, 0, 0, 0, 0
