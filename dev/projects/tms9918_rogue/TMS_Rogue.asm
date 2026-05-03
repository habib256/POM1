; =============================================
; TMS_ROGUE - P-LAB TMS9918 Graphic Card
; VERHILLE Arnaud - 2026
; Roguelike for the Apple-1 + P-LAB TMS9918 Graphic Card.
; MVP1: hardcoded bordered room rendered in true 16x16 Quale tiles
; (each tile expanded into a 2x2 char block in the name table),
; hardware-sprite player (char_adventurer 16x16) navigated with
; HJKL (vi keys). Collision via tile-id whitelist on the RAM map.
; =============================================
; Display: TMS9918 Graphics I, 32x24 cells of 8x8 px = 256x192. The
; pattern table holds Quale 16x16 tile graphics split into 4 chars
; each (TL, TR, BL, BR at base+0..3) and ASCII-aligned font glyphs
; at chars 32..127 ('A'=65, '0'=48, etc.).
;
;   Char rows  0..19   = playfield, 16 wide x 10 tall logical tiles.
;   Char rows 20..23   = HUD area, 32 wide x 4 tall font glyphs (MVP3).
;
; Sprite mode 16x16 no magnify (R1 = $C2). Player rides as sprite #0
; with anchor (X, Y) = (lcol*16, lrow*16) so the 16x16 sprite sits
; exactly over one logical 16x16 tile.
; =============================================

.include "apple1.inc"
.include "tms9918.inc"

; --- Lib (tms9918m1.asm) ---
.import init_vdp_g1, clear_name_table, vdp_set_write
.importzp vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi

; --- Geometry: logical 16x10 tile grid in the top 20 char rows ---
COL_WHITE      = 15
LOGICAL_COLS   = 16
LOGICAL_ROWS   = 10
PLAY_TOP_ROW   = 1              ; first walkable logical row
PLAY_BOT_ROW   = 8              ; last walkable logical row
PLAY_LEFT_COL  = 1              ; first walkable logical col
PLAY_RIGHT_COL = 14             ; last walkable logical col

; --- Procedural dungeon tuning (gen_dungeon) ---
N_ROOMS        = 5              ; rooms carved per level

; --- Apple 1 keyboard codes (high bit set, KBD always has bit 7 = 1) ---
; The four movement-key slots are loaded at runtime from the title-screen
; layout choice (QWERTY HJKL vs AZERTY QZSD), so the game loop compares
; the live key against the configured codes rather than literal constants.

; --- Zero page ---
.zeropage
tmp:            .res 1          ; required by tms9918m1 lib (name_at_rc)
.exportzp tmp
prng_lo:        .res 1          ; 16-bit Galois LFSR state (lib/m6502/prng16.asm)
prng_hi:        .res 1          ; pre-declared so prng16's .ifndef skips its alloc

map_ptr:        .res 2          ; pointer scratch into map_buffer
player_col:     .res 1          ; logical 0..15
player_row:     .res 1          ; logical 0..9
tgt_col:        .res 1          ; movement target — set by handle_input;
                                ; doubles as scratch (col/row) for set_tile
tgt_row:        .res 1
key_west:       .res 1          ; e.g. 'H'|$80 (QWERTY) or 'Q'|$80 (AZERTY)
key_east:       .res 1
key_north:      .res 1
key_south:      .res 1

; --- Procedural-gen scratch ---
room_x:         .res 1          ; current room: top-left corner + dims
room_y:         .res 1
room_w:         .res 1
room_h:         .res 1
cx:             .res 1          ; current room centre
cy:             .res 1
prev_cx:        .res 1          ; previous room centre (for corridor)
prev_cy:        .res 1
room_idx:       .res 1          ; loop counter through gen_dungeon


; --- Map buffer (160 B, 16x10 logical tiles, one byte = tile base id) ---
.segment "MAPSEG"
map_buffer:     .res LOGICAL_COLS * LOGICAL_ROWS


; =============================================
; CODE
; =============================================
.code

start:
        SEI                     ; we drive the chip ourselves
        CLD

        JSR init_vdp_g1         ; 8 registers + disable_sprites
        JSR override_r1_16x16   ; switch sprite mode 8x8 -> 16x16
        JSR upload_tileset      ; 2048 B pattern table -> VRAM $0000
        JSR upload_colour_table ; 32 B colour table   -> VRAM $2000
        JSR clear_name_table    ; whole 32x24 -> char 0 (black)

        JSR draw_title          ; ROGUE banner + key-layout prompt
        JSR wait_kb_choice      ; bind keys + seed prng_lo/hi from key timing

        JSR clear_name_table    ; wipe title before the game view appears
        JSR gen_dungeon         ; procedural rooms + corridors + stairs;
                                ; sets player_col/row to first room centre.
        JSR render_map          ; expand 16x10 logical map to 32x20 char block

        JSR upload_player_pat   ; char_adventurer -> sprite pattern slot 0
        JSR place_player        ; SAT slot 0 = (Y, X, name=0, color)

main_loop:
        JSR wait_key            ; A = raw key (high bit still set)
        JSR handle_input        ; carry clear -> tgt_col/tgt_row set
        BCS main_loop           ; no movement key -> just wait again
        JSR check_collision     ; carry clear -> passable
        BCS main_loop           ; blocked -> ignore the move
        LDA tgt_col
        STA player_col
        LDA tgt_row
        STA player_row
        JSR place_player        ; rewrite SAT slot 0 with new (Y, X)
        JMP main_loop


; ----------------------------------------------------------------------------
; override_r1_16x16: rewrite VDP register 1 to enable 16x16 sprites
; (no magnify). Lib's init_vdp_g1 leaves R1=$C0 (8x8); Quale's character
; sprites are 16x16 so we need R1=$C2.
; ----------------------------------------------------------------------------
override_r1_16x16:
        LDA     #$C2
        STA     VDP_CTRL
        NOP                     ; +2c silicon-strict gap (LDA #imm bridge)
        LDA     #$81            ; $01 | $80 -> register 1
        STA     VDP_CTRL
        RTS


; ----------------------------------------------------------------------------
; upload_tileset: stream tileset_rogue (2048 bytes, 256 chars * 8) into
; the pattern table at VRAM $0000. Auto-increment write — one big block.
; ----------------------------------------------------------------------------
upload_tileset:
        LDA     #$00
        STA     vdp_lo
        STA     vdp_hi
        JSR     vdp_set_write   ; addr = $0000

        LDA     #<tileset_rogue
        STA     vdp_src_lo
        LDA     #>tileset_rogue
        STA     vdp_src_hi

        ; 2048 bytes = 8 pages of 256.
        LDX     #8
@page:  LDY     #0
@byte:  LDA     (vdp_src_lo),Y
        STA     VDP_DATA        ; 4c
        NOP                     ; 2c
        NOP                     ; 2c silicon-strict gap (back-to-back STA)
        INY
        BNE     @byte
        INC     vdp_src_hi
        DEX
        BNE     @page
        RTS


; ----------------------------------------------------------------------------
; upload_colour_table: stream 32 colour bytes to VRAM $2000.
; ----------------------------------------------------------------------------
upload_colour_table:
        LDA     #$00
        STA     vdp_lo
        LDA     #$20
        STA     vdp_hi
        JSR     vdp_set_write   ; addr = $2000

        LDX     #0
@lp:    LDA     tileset_color_table,X
        STA     VDP_DATA        ; 4c
        NOP                     ; 2c
        NOP                     ; 2c silicon-strict gap
        INX
        CPX     #32
        BNE     @lp
        RTS


; ----------------------------------------------------------------------------
; gen_dungeon: build a procedural level in map_buffer.
;   1. Fill the whole 160-cell buffer with TILE_WALL.
;   2. Carve N_ROOMS random rectangular rooms inside the 14x8 interior.
;   3. Connect each room's centre to the previous one with an L-corridor.
;   4. Spawn the player at the first room's centre.
;   5. Stamp TILE_STAIRS_DOWN at the last room's centre.
; All randomness comes from prng16 (16-bit Galois LFSR, lib/m6502).
; The state was seeded during wait_kb_choice from the time-to-keypress.
; ----------------------------------------------------------------------------
gen_dungeon:
        JSR     fill_with_walls
        LDA     #0
        STA     room_idx
@room_lp:
        JSR     pick_random_room ; -> room_x, room_y, room_w, room_h
        JSR     carve_room       ; mark all cells in the rect TILE_EMPTY

        ; Compute (cx, cy) = (room_x + room_w/2, room_y + room_h/2).
        LDA     room_w
        LSR
        CLC
        ADC     room_x
        STA     cx
        LDA     room_h
        LSR
        CLC
        ADC     room_y
        STA     cy

        ; First room? Spawn the player at its centre.
        LDA     room_idx
        BNE     @after_first
        LDA     cx
        STA     player_col
        LDA     cy
        STA     player_row
        JMP     @save_centre
@after_first:
        ; Subsequent room: dig an L-corridor from the previous centre.
        JSR     dig_corridor
@save_centre:
        LDA     cx
        STA     prev_cx
        LDA     cy
        STA     prev_cy
        INC     room_idx
        LDA     room_idx
        CMP     #N_ROOMS
        BNE     @room_lp

        ; Last room (cx/cy still holds its centre): stamp stairs-down.
        LDA     cx
        STA     tgt_col
        LDA     cy
        STA     tgt_row
        LDA     #TILE_STAIRS_DOWN
        JSR     set_tile
        RTS


; ----------------------------------------------------------------------------
; fill_with_walls: set every byte of map_buffer to TILE_WALL.
; Walks 160 bytes via abs,X. X=159..0; BPL exits when X wraps to $FF.
; ----------------------------------------------------------------------------
fill_with_walls:
        ; The earlier `LDX #159 / STA / DEX / BPL` form had a subtle bug:
        ; 159 = $9F has bit 7 = 1, so the very first DEX leaves X with
        ; bit 7 still set ($9E) → BPL never branches → only the topmost
        ; byte gets written. Iterate down with a sentinel BNE instead:
        ; X=160 (DEX-then-write), exit when X reaches 0 after writing
        ; the last byte at index 0.
        LDA     #TILE_WALL
        LDX     #160
@lp:    DEX
        STA     map_buffer,X
        BNE     @lp
        RTS


; ----------------------------------------------------------------------------
; pick_random_room: pick a rectangular room with random origin + size.
;   room_x in [1, 10]   so room_x + room_w(max=5) - 1 <= 14 (last interior col)
;   room_y in [1, 6]    so room_y + room_h(max=3) - 1 <= 8  (last interior row)
;   room_w in [3, 5],   room_h in [2, 3]
; rand_mod modulates prng16 down to the requested span.
; ----------------------------------------------------------------------------
pick_random_room:
        LDA     #10
        JSR     rand_mod
        CLC
        ADC     #1
        STA     room_x          ; [1, 10]
        LDA     #6
        JSR     rand_mod
        CLC
        ADC     #1
        STA     room_y          ; [1, 6]
        LDA     #3
        JSR     rand_mod
        CLC
        ADC     #3
        STA     room_w          ; [3, 5]
        LDA     #2
        JSR     rand_mod
        CLC
        ADC     #2
        STA     room_h          ; [2, 3]
        RTS


; ----------------------------------------------------------------------------
; rand_mod: A = max (must be > 0). Returns A in [0, max).
; Repeated subtract — fast for our small ranges (<= 16).
; Clobbers tmp.
; ----------------------------------------------------------------------------
rand_mod:
        STA     tmp
        JSR     prng16
@lp:    CMP     tmp
        BCC     @done
        SEC
        SBC     tmp
        JMP     @lp
@done:  RTS


; ----------------------------------------------------------------------------
; carve_room: write TILE_EMPTY into every cell of the current room rect.
;   Iterates rows [room_y .. room_y + room_h),
;            cols [room_x .. room_x + room_w).
; Uses set_tile to do the (col, row) -> map_buffer index math.
; ----------------------------------------------------------------------------
carve_room:
        LDA     room_y
        STA     tgt_row
@row:
        LDA     room_x
        STA     tgt_col
@col:
        LDA     #TILE_EMPTY
        JSR     set_tile
        INC     tgt_col
        LDA     tgt_col
        SEC
        SBC     room_x
        CMP     room_w
        BCC     @col
        INC     tgt_row
        LDA     tgt_row
        SEC
        SBC     room_y
        CMP     room_h
        BCC     @row
        RTS


; ----------------------------------------------------------------------------
; dig_corridor: dig an L-shaped corridor of TILE_EMPTY between
; (prev_cx, prev_cy) and (cx, cy). Goes horizontal first along
; row=prev_cy, then vertical along col=cx, so the corner sits at
; (cx, prev_cy). Inclusive endpoints on both segments.
; ----------------------------------------------------------------------------
dig_corridor:
        ; --- Horizontal segment at row = prev_cy ---
        LDA     prev_cy
        STA     tgt_row
        ; tgt_col = min(prev_cx, cx),  tmp = max(prev_cx, cx)
        LDA     prev_cx
        CMP     cx
        BCC     @h_lo_prev      ; prev_cx < cx
        ; prev_cx >= cx -> low end is cx
        LDA     cx
        STA     tgt_col
        LDA     prev_cx
        STA     tmp
        JMP     @h_lp
@h_lo_prev:
        LDA     prev_cx
        STA     tgt_col
        LDA     cx
        STA     tmp
@h_lp:
        LDA     #TILE_EMPTY
        JSR     set_tile
        LDA     tgt_col
        CMP     tmp
        BCS     @h_done         ; tgt_col >= max -> we already wrote it
        INC     tgt_col
        JMP     @h_lp
@h_done:

        ; --- Vertical segment at col = cx ---
        LDA     cx
        STA     tgt_col
        LDA     prev_cy
        CMP     cy
        BCC     @v_lo_prev      ; prev_cy < cy
        LDA     cy
        STA     tgt_row
        LDA     prev_cy
        STA     tmp
        JMP     @v_lp
@v_lo_prev:
        LDA     prev_cy
        STA     tgt_row
        LDA     cy
        STA     tmp
@v_lp:
        LDA     #TILE_EMPTY
        JSR     set_tile
        LDA     tgt_row
        CMP     tmp
        BCS     @v_done
        INC     tgt_row
        JMP     @v_lp
@v_done:
        RTS


; ----------------------------------------------------------------------------
; set_tile: write A to map_buffer[tgt_row * 16 + tgt_col].
; Saves A on the 6502 stack so the caller's value survives the
; address arithmetic. Clobbers Y, map_ptr.
; ----------------------------------------------------------------------------
set_tile:
        PHA
        LDA     #<map_buffer
        STA     map_ptr
        LDA     #>map_buffer
        STA     map_ptr+1
        LDA     tgt_row
        ASL
        ASL
        ASL
        ASL                     ; row * 16 (max 9*16 = 144, fits in byte)
        CLC
        ADC     map_ptr
        STA     map_ptr
        BCC     @noinc
        INC     map_ptr+1
@noinc:
        LDY     tgt_col
        PLA
        STA     (map_ptr),Y
        RTS


; ----------------------------------------------------------------------------
; render_map: expand the 160-byte map_buffer into the name table at
; VRAM $1800. Each logical tile (1 byte = base char id) becomes a 2x2
; block of 4 chars (base+0=TL, base+1=TR, base+2=BL, base+3=BR).
; Auto-increment streams the data row-by-row: one logical row produces
; 32 TL/TR chars (name-table row N) followed by 32 BL/BR chars (row N+1).
; ----------------------------------------------------------------------------
render_map:
        LDA     #$00
        STA     vdp_lo
        LDA     #$18
        STA     vdp_hi
        JSR     vdp_set_write   ; addr = $1800 (name table top)

        LDA     #<map_buffer
        STA     vdp_src_lo
        LDA     #>map_buffer
        STA     vdp_src_hi

        LDX     #LOGICAL_ROWS   ; logical rows remaining
@row_lp:
        ; --- TL / TR pass: 32 chars covering the upper half of all
        ;     16 tiles in the current logical row.
        LDY     #0
@tl_lp: LDA     (vdp_src_lo),Y  ; tile base id
        STA     VDP_DATA        ; TL = base+0
        NOP
        NOP                     ; +2c silicon-strict gap
        CLC
        ADC     #1
        STA     VDP_DATA        ; TR = base+1
        NOP
        NOP
        INY
        CPY     #LOGICAL_COLS
        BNE     @tl_lp

        ; --- BL / BR pass: next 32 chars (auto-increment lands us on
        ;     name-table row N+1 = bottom half of the same tiles).
        LDY     #0
@bl_lp: LDA     (vdp_src_lo),Y  ; tile base id
        CLC
        ADC     #2
        STA     VDP_DATA        ; BL = base+2
        NOP
        NOP
        CLC
        ADC     #1
        STA     VDP_DATA        ; BR = base+3
        NOP
        NOP
        INY
        CPY     #LOGICAL_COLS
        BNE     @bl_lp

        ; Advance map source pointer to next logical row.
        CLC
        LDA     vdp_src_lo
        ADC     #LOGICAL_COLS
        STA     vdp_src_lo
        BCC     @noinc
        INC     vdp_src_hi
@noinc:
        DEX
        BNE     @row_lp
        RTS


; ----------------------------------------------------------------------------
; upload_player_pat: stream char_adventurer_pat (32 bytes, 16x16 layout
; native to the TMS9918) into sprite pattern slot 0 at VRAM $3800.
; ----------------------------------------------------------------------------
upload_player_pat:
        LDA     #$00
        STA     VDP_CTRL
        NOP
        LDA     #$78            ; $38 | $40 -> write at $3800
        STA     VDP_CTRL
        LDX     #0
@lp:    LDA     char_adventurer_pat,X
        STA     VDP_DATA        ; 4c
        NOP                     ; 2c
        NOP                     ; 2c silicon-strict gap
        INX
        CPX     #32
        BNE     @lp
        RTS


; ----------------------------------------------------------------------------
; place_player: rewrite Sprite Attribute Table slot 0. Logical position
; (lcol, lrow) maps to pixel (lcol*16, lrow*16) so the 16x16 sprite
; lands exactly on one 16x16 logical tile.
;   Slot 0: Y=lrow*16, X=lcol*16, pattern=0, color=COL_WHITE
;   Slot 1: Y=$D0    -> chip stops scanning here
; ----------------------------------------------------------------------------
place_player:
        LDA     #$00
        STA     VDP_CTRL
        NOP                     ; +2c silicon-strict gap (LDA #imm bridge)
        LDA     #$5B            ; $1B | $40 -> write at $1B00
        STA     VDP_CTRL

        LDA     player_row
        ASL
        ASL
        ASL
        ASL                     ; row * 16
        STA     VDP_DATA
        NOP
        LDA     player_col
        ASL
        ASL
        ASL
        ASL                     ; col * 16
        STA     VDP_DATA
        NOP
        LDA     #$00            ; pattern slot 0
        STA     VDP_DATA
        NOP
        LDA     #COL_WHITE
        STA     VDP_DATA
        NOP

        LDA     #$D0            ; sprite #1 -> chain terminator
        STA     VDP_DATA
        RTS


; ----------------------------------------------------------------------------
; wait_key: spin until KBDCR signals a key, return KBD in A (high bit
; still set per Apple-1 convention).
; ----------------------------------------------------------------------------
wait_key:
        LDA     KBDCR
        BPL     wait_key        ; bit 7 = 0 -> no key yet, loop
        LDA     KBD
        RTS


; ----------------------------------------------------------------------------
; handle_input: parse key in A (high bit set) into tgt_col/tgt_row
; relative to player position. HJKL = west/south/north/east (vi keys).
;   Returns carry CLEAR if a movement was requested,
;           carry SET   if the key is unrecognised (no move).
; ----------------------------------------------------------------------------
handle_input:
        LDX     player_col
        STX     tgt_col
        LDX     player_row
        STX     tgt_row
        CMP     key_west
        BEQ     @west
        CMP     key_east
        BEQ     @east
        CMP     key_north
        BEQ     @north
        CMP     key_south
        BEQ     @south
        SEC
        RTS
@west:  DEC     tgt_col
        CLC
        RTS
@east:  INC     tgt_col
        CLC
        RTS
@north: DEC     tgt_row
        CLC
        RTS
@south: INC     tgt_row
        CLC
        RTS


; ----------------------------------------------------------------------------
; check_collision: read map_buffer[tgt_row * 16 + tgt_col] and decide
; if the cell is passable.
;   Returns carry CLEAR -> passable,
;           carry SET   -> blocked (wall, edge, locked door, etc.).
; ----------------------------------------------------------------------------
check_collision:
        LDA     tgt_row
        CMP     #PLAY_TOP_ROW
        BCC     @blocked
        CMP     #(PLAY_BOT_ROW + 1)
        BCS     @blocked
        LDA     tgt_col
        CMP     #PLAY_LEFT_COL
        BCC     @blocked
        CMP     #(PLAY_RIGHT_COL + 1)
        BCS     @blocked

        JSR     tile_at_target
        CMP     #TILE_EMPTY
        BEQ     @pass
        CMP     #TILE_DOOR
        BEQ     @pass
        CMP     #TILE_STAIRS_DOWN
        BEQ     @pass
@blocked:
        SEC
        RTS
@pass:
        CLC
        RTS


; ----------------------------------------------------------------------------
; tile_at_target: read map_buffer[tgt_row * 16 + tgt_col].
; Returns A = tile base id. Clobbers Y, map_ptr.
; Logical map is 16x10 = 160 B, fits in one page once offset added, so
; the row*16 multiply collapses to ASL x4 with no high-byte carry.
; ----------------------------------------------------------------------------
tile_at_target:
        LDA     #<map_buffer
        STA     map_ptr
        LDA     #>map_buffer
        STA     map_ptr+1

        LDA     tgt_row
        ASL
        ASL
        ASL
        ASL                     ; row * 16; max 9 * 16 = 144, fits in byte
        CLC
        ADC     map_ptr
        STA     map_ptr
        BCC     @noinc          ; map_buffer is page-aligned-ish; carry rare
        INC     map_ptr+1
@noinc:
        LDY     tgt_col
        LDA     (map_ptr),Y
        RTS


; ----------------------------------------------------------------------------
; draw_text: paint an $FF-terminated string at a given VRAM addr in the
; name table. Used for both the title screen and (later) the HUD.
;   On entry:
;     A   = VRAM addr low byte
;     X   = VRAM addr high byte  (caller must already OR $40 = write bit)
;     vdp_src_lo / vdp_src_hi = pointer to the source string
;   String bytes are emitted verbatim — strings written as
;   `.byte "ROGUE", $FF` work directly because the font glyphs sit at
;   their matching ASCII char IDs in the pattern table.
; ----------------------------------------------------------------------------
draw_text:
        STA     VDP_CTRL
        NOP                     ; +2c silicon-strict gap (back-to-back ctrl)
        NOP
        STX     VDP_CTRL
        LDY     #0
@lp:    LDA     (vdp_src_lo),Y
        CMP     #$FF
        BEQ     @done
        STA     VDP_DATA
        NOP
        NOP                     ; +2c silicon-strict gap (back-to-back data)
        INY
        BNE     @lp
@done:  RTS


; ----------------------------------------------------------------------------
; draw_title: paint the title screen on the cleared name table. Each
; line is centred manually via the precomputed VRAM addr (= $1800 +
; row * 32 + col). Strings live in the data tail below.
; ----------------------------------------------------------------------------
draw_title:
        ; "ROGUE" at row 4, col 13 -> $1800 + 4*32 + 13 = $188D
        LDA     #<title_rogue
        STA     vdp_src_lo
        LDA     #>title_rogue
        STA     vdp_src_hi
        LDA     #$8D
        LDX     #$58            ; $18 | $40 (write at $1800 + ...)
        JSR     draw_text

        ; "P-LAB TMS9918 ROGUELIKE" at row 7, col 4 -> $18E4
        LDA     #<title_subtitle
        STA     vdp_src_lo
        LDA     #>title_subtitle
        STA     vdp_src_hi
        LDA     #$E4
        LDX     #$58
        JSR     draw_text

        ; "BY VERHILLE ARNAUD" at row 9, col 7 -> $1927
        LDA     #<title_author
        STA     vdp_src_lo
        LDA     #>title_author
        STA     vdp_src_hi
        LDA     #$27
        LDX     #$59            ; $19 | $40
        JSR     draw_text

        ; "SELECT KEYBOARD" at row 13, col 8 -> $19A8
        LDA     #<title_select_kb
        STA     vdp_src_lo
        LDA     #>title_select_kb
        STA     vdp_src_hi
        LDA     #$A8
        LDX     #$59
        JSR     draw_text

        ; "1 QWERTY (HJKL)" at row 15, col 8 -> $19E8
        LDA     #<title_qwerty
        STA     vdp_src_lo
        LDA     #>title_qwerty
        STA     vdp_src_hi
        LDA     #$E8
        LDX     #$59
        JSR     draw_text

        ; "2 AZERTY (QZSD)" at row 17, col 8 -> $1A28
        LDA     #<title_azerty
        STA     vdp_src_lo
        LDA     #>title_azerty
        STA     vdp_src_hi
        LDA     #$28
        LDX     #$5A            ; $1A | $40
        JSR     draw_text

        RTS


; ----------------------------------------------------------------------------
; wait_kb_choice: spin until the user types '1' (QWERTY HJKL) or '2'
; (AZERTY QZSD), then bind key_west / key_east / key_north / key_south
; in zero page so handle_input routes the right physical keys to the
; right cardinal directions. Bit 7 stays set on every comparison —
; the Apple-1 KBD always returns chars with high bit set.
;   QWERTY: H=west, L=east, K=north, J=south  (vi keys, classic roguelike)
;   AZERTY: Q=west, D=east, Z=north, S=south  (ZQSD layout, French gamers)
; ----------------------------------------------------------------------------
wait_kb_choice:
        ; Seed the PRNG to a non-zero starting state. The busy-wait loop
        ; below increments prng_lo every iteration so the actual seed
        ; depends on how many cycles the user takes to press a key —
        ; effectively a hardware-RNG fed by reaction time. A final EOR
        ; with the keypress byte mixes the chosen layout into the seed
        ; so '1' and '2' produce divergent dungeons even at the same
        ; reaction-time count.
        LDA     #$01
        STA     prng_lo
        STA     prng_hi
@lp:    INC     prng_lo
        BNE     @nocascade
        INC     prng_hi
@nocascade:
        LDA     KBDCR
        BPL     @lp             ; bit 7 = 0 -> no key yet, keep counting
        LDA     KBD
        PHA                     ; preserve the raw key for the choice CMPs
        EOR     prng_lo
        STA     prng_lo         ; mix keypress into the seed
        PLA
        CMP     #('1' | $80)
        BEQ     @qwerty
        CMP     #('2' | $80)
        BEQ     @azerty
        JMP     @lp             ; not a layout key; keep waiting + seeding
@qwerty:
        LDA     #('H' | $80)
        STA     key_west
        LDA     #('L' | $80)
        STA     key_east
        LDA     #('K' | $80)
        STA     key_north
        LDA     #('J' | $80)
        STA     key_south
        RTS
@azerty:
        LDA     #('Q' | $80)
        STA     key_west
        LDA     #('D' | $80)
        STA     key_east
        LDA     #('Z' | $80)
        STA     key_north
        LDA     #('S' | $80)
        STA     key_south
        RTS


; ----------------------------------------------------------------------------
; Title-screen strings. ca65's `.byte "TEXT"` emits the raw ASCII codes,
; which match the font glyphs sliced from font_quale.asm into the
; pattern table at the same char IDs (chars 32..127). $FF terminates.
; ----------------------------------------------------------------------------
title_rogue:
        .byte   "ROGUE", $FF
title_subtitle:
        .byte   "P-LAB TMS9918 ROGUELIKE", $FF
title_author:
        .byte   "BY VERHILLE ARNAUD", $FF
title_select_kb:
        .byte   "SELECT KEYBOARD", $FF
title_qwerty:
        .byte   "1 QWERTY (HJKL)", $FF
title_azerty:
        .byte   "2 AZERTY (QZSD)", $FF


; ============================================================================
; char_adventurer_pat -- player sprite (16x16, 32 bytes), inlined from
; dev/lib/tms9918/sprites_characters.asm so we don't pull in the full
; 33-sprite library and overflow the 3 456 B low-bank CODE budget.
; ============================================================================
char_adventurer_pat:
        .byte $00, $00, $07, $0F, $1F, $1B, $1F, $5E
        .byte $37, $03, $0C, $1F, $37, $07, $06, $02
        .byte $00, $00, $E0, $F0, $F8, $D8, $F8, $7A
        .byte $EC, $C0, $30, $F8, $EC, $E0, $60, $40

; ============================================================================
; prng16 -- 16-bit Galois LFSR shared library (lib/m6502/prng16.asm).
; Textually included rather than linked as a separate .o because the
; library exposes neither .export nor .importzp directives — it expects
; the caller's compilation unit to provide prng_lo/prng_hi as ZP slots
; (we declare them in the .zeropage block above; the lib's .ifndef
; guard then skips its own .res 1 alloc).
; ============================================================================
.include "prng16.asm"


; ============================================================================
; Generated tileset (pattern table + colour table). Pulled in last so
; the .byte tables sit at the tail of the CODE segment. TILE_* equates
; declared here are visible mid-file thanks to ca65's two-pass assembly.
; ============================================================================
.include "tileset_rogue.inc"
