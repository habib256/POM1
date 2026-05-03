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
; Sprite-mode colour indices (TMS9918 palette: 4 dk-blue, 5 lt-blue,
; 14 gray, 15 white). The hero sprite uses lt-blue so it stands out
; against the gray-on-black wall pattern.
COL_LT_BLUE    = 5
COL_PLAYER     = COL_LT_BLUE
LOGICAL_COLS   = 16
LOGICAL_ROWS   = 10
PLAY_TOP_ROW   = 1              ; first walkable logical row
PLAY_BOT_ROW   = 8              ; last walkable logical row
PLAY_LEFT_COL  = 1              ; first walkable logical col
PLAY_RIGHT_COL = 14             ; last walkable logical col

; --- Procedural dungeon tuning (gen_dungeon) ---
N_ROOMS        = 2              ; "two-rooms" mode: first in left half,
                                ; second in right half, separated by a
                                ; ≥1-column gap so they never overlap.
                                ; The L-corridor between centres is the
                                ; only inter-room connection.
                                ; "big-room" mode generates a single
                                ; full-screen room with edge doors
                                ; (no corridor).

; Internal tile ids used by gen_dungeon's dig+finalize pipeline.
; TILE_CORR is the transient marker carve_corridor_marker writes onto
; every wall cell the L-corridor traverses. finalize_doors then
; classifies each marker as either:
;   - TILE_DOOR   (boundary cell — neighbours a room interior)
;   - TILE_CORR_DROP (high-bit-flagged "convert-to-empty in pass 2")
; A two-pass design is necessary so that pass-1 decisions never read
; back a freshly-converted EMPTY: pass 1 only writes TILE_DOOR or the
; high-bit drop value, both of which compare unequal to TILE_EMPTY.
; Char id 1 isn't in the tileset PALETTE so it renders all-zero blank
; (same as TILE_EMPTY) — even an unfinalised buffer would draw cleanly.
TILE_CORR       = 1
TILE_CORR_DROP  = $81

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
corr_row:       .res 1          ; cy of room 0 — preserved across iter 1
                                ; so stairs-down can avoid placing its
                                ; west-anchor cell on a corridor cell
                                ; (would replace the wall-on-left rule
                                ; with a door-on-left otherwise).


; --- Map buffer (160 B, 16x10 logical tiles, one byte = tile base id) ---
; The 2-pass corridor algorithm (mark every dug cell as TILE_DOOR, then
; finalize_doors converts non-boundary markers back to TILE_EMPTY) means
; we don't need to remember room rects after carving — the marker tile
; itself is the bookkeeping.
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
        CMP #('N' | $80)        ; 'N' regenerates a fresh random level
        BEQ @new_level
        JSR handle_input        ; carry clear -> tgt_col/tgt_row set
        BCS main_loop           ; no movement key -> just wait again
        JSR check_collision     ; carry clear -> passable
        BCS main_loop           ; blocked -> ignore the move
        ; A successful move whose target is on the screen frame can
        ; only mean the player walked onto a big-room edge door —
        ; check_collision lets TILE_DOOR through regardless of bounds.
        ; Treat that as a level transition: regenerate a fresh map
        ; rather than placing the sprite off the playable grid.
        LDA tgt_col
        BEQ @new_level          ; col 0 (west edge)
        CMP #15
        BEQ @new_level          ; col 15 (east edge)
        LDA tgt_row
        BEQ @new_level          ; row 0 (north edge)
        CMP #9
        BEQ @new_level          ; row 9 (south edge)
        ; Regular move within the playable interior.
        LDA tgt_col
        STA player_col
        LDA tgt_row
        STA player_row
        JSR place_player        ; rewrite SAT slot 0 with new (Y, X)
        JMP main_loop
@new_level:
        JSR new_level
        JMP main_loop


; ----------------------------------------------------------------------------
; new_level: regenerate a fresh dungeon (random big-room or two-rooms
; layout) and refresh the screen. Called from main_loop when the user
; presses 'N'. Reuses the LFSR state — no need to reseed.
; ----------------------------------------------------------------------------
new_level:
        JSR gen_dungeon         ; new map_buffer + new player_col/row
        JSR clear_name_table    ; wipe the previous level off VRAM
        JSR render_map          ; paint the new logical map
        JSR place_player        ; rewrite SAT slot 0 at the new spawn
        RTS


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
; gen_dungeon: top-level dispatcher. Coin-flip picks one of two layouts:
;
;   - "big-room"  : a single full-screen room with stairs at opposite
;                   corners and decorative doors at each cardinal
;                   wall midpoint (transition portals to other levels,
;                   cosmetic for now).
;   - "two-rooms" : two non-overlapping rooms in opposite halves of
;                   the screen, connected by an L-corridor. Stairs-up
;                   sits on the right edge of the left room (so the
;                   sprite's right side abuts a wall), stairs-down at
;                   the right room's centre.
;
; All randomness comes from prng16 (16-bit Galois LFSR, lib/m6502),
; seeded from time-to-keypress during wait_kb_choice.
; ----------------------------------------------------------------------------
gen_dungeon:
        JSR     fill_with_walls
        LDA     #2
        JSR     rand_mod                ; 0 or 1
        ; rand_mod's final flag set comes from `CMP tmp` inside the
        ; reduction loop, so Z reflects "A == tmp" (always 0 here),
        ; NOT "A == 0". Re-test A explicitly so BEQ actually fires.
        CMP     #0
        BEQ     @big                    ; 6502 branch is ±128 — use a
        JMP     gen_two_rooms           ; trampoline JMP for either path
@big:
        JMP     gen_big_room


; ----------------------------------------------------------------------------
; gen_big_room: single full-screen room (cols [1..14], rows [1..8]).
; Stairs-up at the top-right interior cell so its east neighbour is
; the screen-edge wall (col 15). Stairs-down at the bottom-left
; interior cell. Four decorative doors, one per cardinal wall, at
; random positions along the wall — these are the "exits to other
; rooms" suggested by the screen-edge perimeter.
; ----------------------------------------------------------------------------
gen_big_room:
        LDA     #1
        STA     room_x
        STA     room_y
        LDA     #14
        STA     room_w
        LDA     #8
        STA     room_h
        JSR     carve_room

        ; Stairs-up at top-right interior corner — east neighbour is
        ; col 15 (screen-edge wall). Player spawns here.
        LDA     #14
        STA     tgt_col
        STA     player_col
        LDA     #2
        STA     tgt_row
        STA     player_row
        LDA     #TILE_STAIRS_UP
        JSR     set_tile

        ; Stairs-down at bottom-left interior corner: west neighbour
        ; is col 0 (screen frame wall), satisfying the wall-on-left rule.
        LDA     #1
        STA     tgt_col
        LDA     #8
        STA     tgt_row
        LDA     #TILE_STAIRS_DOWN
        JSR     set_tile

        JSR     place_perimeter_doors
        RTS


; ----------------------------------------------------------------------------
; place_perimeter_doors: four TILE_DOOR cells, one per cardinal screen
; edge. Doors REPLACE wall cells on the frame (rows 0/9, cols 0/15)
; rather than sitting on interior cells — walking onto them is the
; "exit to another map" mechanic. main_loop detects when the target of
; a move is at a screen edge and triggers new_level instead of moving
; the player off-grid (so player coords stay valid for handle_input).
; Random positions stay inside [3..12] / [3..6] so doors line up with
; reachable interior cells (the player walks from (col, 1) onto the
; door at (col, 0), etc.).
; ----------------------------------------------------------------------------
place_perimeter_doors:
        ; --- North door: (col rand[3..12], row 0) ---
        LDA     #10
        JSR     rand_mod
        CLC
        ADC     #3
        STA     tgt_col
        LDA     #0
        STA     tgt_row
        LDA     #TILE_DOOR
        JSR     set_tile

        ; --- South door: (col rand[3..12], row 9) ---
        LDA     #10
        JSR     rand_mod
        CLC
        ADC     #3
        STA     tgt_col
        LDA     #9
        STA     tgt_row
        LDA     #TILE_DOOR
        JSR     set_tile

        ; --- West door: (col 0, row rand[3..6]) ---
        LDA     #4
        JSR     rand_mod
        CLC
        ADC     #3
        STA     tgt_row
        LDA     #0
        STA     tgt_col
        LDA     #TILE_DOOR
        JSR     set_tile

        ; --- East door: (col 15, row rand[3..6]) ---
        LDA     #4
        JSR     rand_mod
        CLC
        ADC     #3
        STA     tgt_row
        LDA     #15
        STA     tgt_col
        LDA     #TILE_DOOR
        JSR     set_tile
        RTS


; ----------------------------------------------------------------------------
; gen_two_rooms: two non-overlapping rooms, one per half of the screen,
; joined by an L-corridor. dig_corridor + carve_corridor_marker +
; finalize_doors place exactly two doors (one at each end of the
; corridor). Stairs-up at the right edge of room 0 so its east
; neighbour is room 0's east wall — visually anchors the sprite.
; Stairs-down at room 1's centre.
; ----------------------------------------------------------------------------
gen_two_rooms:
        LDA     #0
        STA     room_idx
@room_lp:
        JSR     pick_random_room        ; uses room_idx to pick half
        JSR     carve_room

        ; Compute (cx, cy) = centre of the room we just carved.
        ; LSR puts bit 0 into Carry; a bare ADC (no CLC) rounds the
        ; half-width up — still inside the room interior.
        LDA     room_w
        LSR
        ADC     room_x
        STA     cx
        LDA     room_h
        LSR
        ADC     room_y
        STA     cy

        ; Iter 0: save room 0's top-right interior corner as the
        ;         player's spawn / stairs-up location.
        ;         (rx+rw-1, ry): east neighbour is the room's east wall
        ;         (row ry ≠ cy means the corridor doesn't run here, so
        ;         the wall stays intact through finalize_doors). This
        ;         honours the "stairs-up sprite has its right side
        ;         against a wall" rule and leaves the corridor's
        ;         room-0 exit door free to render at (rx+rw, cy).
        ; Iter 1: dig the L-corridor from the previous centre.
        LDA     room_idx
        BNE     @subsequent
        LDA     room_x
        CLC
        ADC     room_w
        SEC
        SBC     #1
        STA     player_col              ; rx + rw - 1
        LDA     room_y
        STA     player_row              ; ry  (top row of interior)
        ; Remember room 0's centre row — that's the corridor's H row,
        ; which determines where the corridor enters room 1's west wall
        ; (if at all). Stairs-down placement uses this to avoid landing
        ; on the west-entry door cell.
        LDA     cy
        STA     corr_row
        JMP     @advance
@subsequent:
        JSR     dig_corridor
@advance:
        LDA     cx
        STA     prev_cx
        LDA     cy
        STA     prev_cy
        INC     room_idx
        LDA     room_idx
        CMP     #N_ROOMS
        BNE     @room_lp

        ; Demote mid-corridor markers to TILE_EMPTY; promote boundary
        ; markers to real TILE_DOOR. Exactly two doors survive: the
        ; corridor's room-0 exit and its room-1 entry.
        JSR     finalize_doors

        ; Stairs-up at room 0's right-edge cell (east neighbour = wall).
        LDA     player_col
        STA     tgt_col
        LDA     player_row
        STA     tgt_row
        LDA     #TILE_STAIRS_UP
        JSR     set_tile

        ; Stairs-down at room 1's top-left interior corner so its WEST
        ; neighbour is room 1's west wall. Edge case: if the corridor
        ; entered room 1 horizontally at row ry_1, that wall cell is a
        ; door, not a wall — bump stairs-down down by one row to keep
        ; the wall-on-left invariant. (rh_1 ≥ 3 guarantees ry_1+1 stays
        ; inside room 1's interior.)
        LDA     room_x                  ; rx_1
        STA     tgt_col
        LDA     room_y                  ; ry_1
        CMP     corr_row
        BNE     @stairs_down_row_ok
        CLC
        ADC     #1                      ; ry_1 + 1
@stairs_down_row_ok:
        STA     tgt_row
        LDA     #TILE_STAIRS_DOWN
        JSR     set_tile
        RTS


; ----------------------------------------------------------------------------
; carve_corridor_marker: if the current cell (tgt_col, tgt_row) is a
; TILE_WALL, replace it with TILE_CORR (a transient marker — a
; distinct char id from TILE_DOOR so finalize_doors can do its
; classification pass without confusing already-classified cells
; with un-classified ones). Cells already non-wall are left alone —
; never overwriting room interiors is what makes finalize_doors'
; "neighbour is TILE_EMPTY" test mean exactly "neighbour is a room
; interior we haven't touched".
; ----------------------------------------------------------------------------
carve_corridor_marker:
        JSR     tile_at_target
        CMP     #TILE_WALL
        BNE     @skip
        LDA     #TILE_CORR
        JSR     set_tile
@skip:  RTS


; ----------------------------------------------------------------------------
; finalize_doors: classify every TILE_CORR marker the dig pass left
; behind into either TILE_DOOR (room boundary) or TILE_EMPTY (mid-
; corridor floor). Done in two passes so pass-1 decisions can never
; contaminate pass-1 reads:
;
;   Pass 1: for each TILE_CORR cell, check the four cardinal
;     neighbours for TILE_EMPTY (= original room interior). If any
;     match → write TILE_DOOR (final). Otherwise → write
;     TILE_CORR_DROP ($81 — high bit makes it ≠ TILE_EMPTY for any
;     subsequent in-pass check, while staying clearly distinct from
;     all real tiles).
;   Pass 2: scan again, convert every TILE_CORR_DROP to TILE_EMPTY.
;
; Why two passes: writing TILE_EMPTY mid-pass would make later cells
; falsely see a room-interior neighbour and stay flagged as doors —
; that was the bug producing the alternating-door cascade.
;
; Bounds: corridor cells stay inside [1..14] × [1..8] by construction
; (rooms can't reach the screen frame), so col±1 ∈ [0..15] and
; row±1 ∈ [0..9] are always valid map_buffer indices.
;
; Clobbers A, X, Y, tgt_col, tgt_row, map_ptr.
; ----------------------------------------------------------------------------
finalize_doors:
        ; ---- Pass 1: classify TILE_CORR cells ----
        LDX     #LOGICAL_COLS * LOGICAL_ROWS    ; 160; X = 159..0 via DEX
@p1:
        DEX
        LDA     map_buffer,X
        CMP     #TILE_CORR
        BNE     @p1_next
        ; Decode (col, row) from index X = row*16 + col.
        TXA
        AND     #$0F
        STA     tgt_col
        TXA
        LSR
        LSR
        LSR
        LSR
        STA     tgt_row
        ; West neighbour.
        DEC     tgt_col
        JSR     tile_at_target
        INC     tgt_col
        CMP     #TILE_EMPTY
        BEQ     @p1_keep
        ; East neighbour.
        INC     tgt_col
        JSR     tile_at_target
        DEC     tgt_col
        CMP     #TILE_EMPTY
        BEQ     @p1_keep
        ; North neighbour.
        DEC     tgt_row
        JSR     tile_at_target
        INC     tgt_row
        CMP     #TILE_EMPTY
        BEQ     @p1_keep
        ; South neighbour.
        INC     tgt_row
        JSR     tile_at_target
        DEC     tgt_row
        CMP     #TILE_EMPTY
        BEQ     @p1_keep
        ; Mid-corridor: flag for pass-2 conversion to EMPTY.
        LDA     #TILE_CORR_DROP
        STA     map_buffer,X
        JMP     @p1_next
@p1_keep:
        ; Boundary cell — promote marker to a real door.
        LDA     #TILE_DOOR
        STA     map_buffer,X
@p1_next:
        TXA
        BNE     @p1

        ; ---- Pass 2: convert flagged drops to TILE_EMPTY ----
        LDX     #LOGICAL_COLS * LOGICAL_ROWS
@p2:
        DEX
        LDA     map_buffer,X
        CMP     #TILE_CORR_DROP
        BNE     @p2_next
        LDA     #TILE_EMPTY
        STA     map_buffer,X
@p2_next:
        TXA
        BNE     @p2

        ; ---- Pass 3: collapse adjacent doors ----
        ; A corridor that runs alongside a room (one cell parallel to
        ; the room's wall) makes every cell in that run flag as a
        ; boundary, producing a horizontal/vertical conga line of
        ; doors. We want at most one door per run. For each TILE_DOOR
        ; cell, if its west OR north neighbour is also TILE_DOOR,
        ; demote this cell to TILE_EMPTY. Iterating X = 159..0 means
        ; the top-leftmost door of any run survives; the rest collapse.
        ; Diagonally-placed doors (e.g. one west wall + one north wall
        ; entry on the same room) stay because they're never directly
        ; adjacent in the same row or column.
        LDX     #LOGICAL_COLS * LOGICAL_ROWS
@p3:
        DEX
        LDA     map_buffer,X
        CMP     #TILE_DOOR
        BNE     @p3_next
        ; Decode (col, row) from index X.
        TXA
        AND     #$0F
        STA     tgt_col
        TXA
        LSR
        LSR
        LSR
        LSR
        STA     tgt_row
        ; West neighbour.
        DEC     tgt_col
        JSR     tile_at_target
        INC     tgt_col
        CMP     #TILE_DOOR
        BEQ     @p3_demote
        ; North neighbour.
        DEC     tgt_row
        JSR     tile_at_target
        INC     tgt_row
        CMP     #TILE_DOOR
        BNE     @p3_next
@p3_demote:
        LDA     #TILE_EMPTY
        STA     map_buffer,X
@p3_next:
        TXA
        BNE     @p3
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
; pick_random_room: pick a rectangular room in the half of the screen
; selected by room_idx (0 = left half, cols [1..7]; 1 = right half,
; cols [9..14]). The 1-col gap at col 8 guarantees rooms never abut
; or overlap, so the L-corridor between their centres always crosses
; at least one wall — which is what makes the door-marker pass clean.
;
;   room_w in [4, 6]
;   room_h in [3, 5]
;   room_y in [1, 9 - room_h]   ; computed from h so the room always
;                                 fits inside rows [1..8]
;   left  half: room_x in [1, 8  - room_w]
;   right half: room_x in [9, 15 - room_w]
;
; Right half has 6 usable cols [9..14], so w=6 is the maximum and lands
; at x=9 only — picking that combo gives a half-wide room on the right.
;
; rand_mod modulates prng16 down to the requested span.
; ----------------------------------------------------------------------------
pick_random_room:
        ; Width and height are independent of which half.
        LDA     #3
        JSR     rand_mod
        CLC
        ADC     #4
        STA     room_w          ; [4, 6]
        LDA     #3
        JSR     rand_mod
        CLC
        ADC     #3
        STA     room_h          ; [3, 5]
        ; y in [1, 9 - h]  → rand_mod(9 - h) + 1
        LDA     #9
        SEC
        SBC     room_h
        JSR     rand_mod
        CLC
        ADC     #1
        STA     room_y

        ; X depends on the half (room_idx 0 = left, 1 = right).
        LDA     room_idx
        BNE     @right_half
        ; Left: x in [1, 8-w]  → rand_mod(8-w) + 1
        LDA     #8
        SEC
        SBC     room_w
        JSR     rand_mod
        CLC
        ADC     #1
        STA     room_x
        RTS
@right_half:
        ; Right: x in [9, 15-w]  → rand_mod(7-w) + 9
        LDA     #7
        SEC
        SBC     room_w
        JSR     rand_mod
        CLC
        ADC     #9
        STA     room_x
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
; dig_corridor: dig an L-shaped corridor between (prev_cx, prev_cy)
; and (cx, cy). Horizontal segment runs along row = prev_cy, then
; the vertical segment runs along col = cx, so the corner sits at
; (cx, prev_cy). Inclusive endpoints on both segments.
;
; Each cell along the path is processed via carve_corridor_marker:
; walls become TILE_DOOR markers, room interiors are left intact.
; finalize_doors then converts mid-corridor markers back to
; TILE_EMPTY, leaving only boundary cells as proper TILE_DOORs.
; ----------------------------------------------------------------------------
dig_corridor:
        ; --- Horizontal segment at row = prev_cy ---
        LDA     prev_cy
        STA     tgt_row
        ; tgt_col = min(prev_cx, cx),  tmp = max(prev_cx, cx)
        LDA     prev_cx
        CMP     cx
        BCC     @h_lo_prev      ; prev_cx < cx
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
        JSR     carve_corridor_marker
        LDA     tgt_col
        CMP     tmp
        BCS     @h_done         ; tgt_col >= max -> we already processed it
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
        JSR     carve_corridor_marker
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
        JSR     calc_map_ptr
        LDY     tgt_col
        PLA
        STA     (map_ptr),Y
        RTS


; ----------------------------------------------------------------------------
; calc_map_ptr: set map_ptr to &map_buffer[tgt_row * 16].
; Caller adds tgt_col via (map_ptr),Y. Clobbers A; preserves X.
; Logical map is 16x10 = 160 B; row * 16 max = 144 fits in one page,
; so the high-byte INC is a defensive carry guard (rare but cheap).
; ----------------------------------------------------------------------------
calc_map_ptr:
        LDA     #<map_buffer
        STA     map_ptr
        LDA     #>map_buffer
        STA     map_ptr+1
        LDA     tgt_row
        ASL
        ASL
        ASL
        ASL
        CLC
        ADC     map_ptr
        STA     map_ptr
        BCC     @done
        INC     map_ptr+1
@done:  RTS


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
; upload_player_pat: stream char_paladin2_pat (32 bytes, 16x16 layout
; native to the TMS9918) into sprite pattern slot 0 at VRAM $3800.
; ----------------------------------------------------------------------------
upload_player_pat:
        LDA     #$00
        STA     VDP_CTRL
        NOP
        LDA     #$78            ; $38 | $40 -> write at $3800
        STA     VDP_CTRL
        LDX     #0
@lp:    LDA     char_paladin2_pat,X
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
;   Slot 0: Y=lrow*16, X=lcol*16, pattern=0, color=COL_PLAYER (lt-blue)
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
        LDA     #COL_PLAYER
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
;           carry SET   -> blocked.
;
; Special case: TILE_DOOR cells are ALWAYS passable, even when they
; sit on the screen frame (rows 0/9, cols 0/15). Big-room mode places
; doors at frame positions to mark map exits — main_loop intercepts
; the move and triggers new_level instead of leaving the player off-grid.
; Other tiles are still bounded to the playable interior [1..14] x [1..8].
; ----------------------------------------------------------------------------
check_collision:
        JSR     tile_at_target
        CMP     #TILE_DOOR
        BEQ     @pass
        STA     tmp                     ; cache tile for the second test below
        ; Bounds check (only non-door tiles need playable interior).
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
        ; Allowed-tile whitelist (door already handled above).
        LDA     tmp
        CMP     #TILE_EMPTY
        BEQ     @pass
        CMP     #TILE_STAIRS_DOWN
        BEQ     @pass
        CMP     #TILE_STAIRS_UP
        BEQ     @pass
@blocked:
        SEC
        RTS
@pass:
        CLC
        RTS


; ----------------------------------------------------------------------------
; tile_at_target: read map_buffer[tgt_row * 16 + tgt_col].
; Returns A = tile base id. Clobbers Y, map_ptr; preserves X.
; ----------------------------------------------------------------------------
tile_at_target:
        JSR     calc_map_ptr
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
; char_paladin2_pat -- player sprite (16x16, 32 bytes), inlined from
; dev/lib/tms9918/sprites_characters.asm so we don't pull in the full
; 33-sprite library and overflow the 3 456 B low-bank CODE budget.
; (Bytes copied post-shift, i.e. after tools/shift_characters_up.py
; ran — the lib's char sprites had a 2-row blank top + 0-row blank
; bottom that got rebalanced to 1+1.)
; ============================================================================
char_paladin2_pat:
        .byte $01, $07, $0F, $0F, $09, $08, $0E, $02
        .byte $7A, $68, $6B, $6B, $69, $6A, $34, $00
        .byte $82, $E7, $F0, $F2, $92, $12, $72, $72
        .byte $66, $16, $D8, $E2, $82, $62, $22, $00

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
