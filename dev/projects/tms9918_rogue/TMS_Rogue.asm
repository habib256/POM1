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

; --- Vital stats (finish_turn / death_screen) --------------------------
; HP_MAX caps the player's hit points. Reaching 0 triggers death_screen
; → JMP $4000 (cartridge cold-start). HP only decreases via monster hits
; (no starvation tick — the game loop pressure comes from combat itself,
; balanced by food drops that monsters leave behind on death).
HP_MAX         = 14
PLAYER_DMG     = 1              ; damage dealt per bump-attack on a monster.
                                ; At 1 every monster ≥ 2 HP needs at
                                ; least 2 bumps to kill, guaranteeing
                                ; a retaliation chance — combat costs
                                ; HP, not just turns.
FOOD_HEAL      = 3              ; HP restored when the player walks onto
                                ; a food item (capped at HP_MAX). Tuned
                                ; so a single heal covers one DEATH bite
                                ; with a little margin.

; --- Monster pool ------------------------------------------------------
; 16-slot pool at $E300 (high bank, after the 768-byte map_buffer).
; Per-slot layout (8 bytes; padding leaves room for AI state later):
;
;   +0 MON_TYPE   0=dead/empty, 1=undead, 2=ghost, 3=death
;   +1 MON_HP     remaining hit points (0 → mark dead)
;   +2 MON_COL    logical 0..15
;   +3 MON_ROW    logical 0..9
;   +4 MON_NAME   sprite-name (4/8/12 → VRAM $3820/$3840/$3860)
;   +5 MON_COLOR  TMS9918 palette index for the SAT colour byte
;   +6 MON_DMG    damage dealt to the player on a successful hit
;   +7 reserved
;
; Slot 0 of the SAT is the player; slots 1..16 mirror the monster pool
; (one SAT entry per live monster), and the next-after-last gets Y=$D0
; as the chain terminator. Sprite-pattern slot 0 ($3800-$381F) is the
; player; slots 4/8/12 are the three undead types, all uploaded once
; at start.
MON_COUNT      = 16
MON_SIZE       = 8
MON_TYPE       = 0
MON_HP         = 1
MON_COL        = 2
MON_ROW        = 3
MON_NAME       = 4
MON_COLOR      = 5
MON_DMG        = 6
MON_HURT       = 7              ; non-zero if the monster should render
                                ; in COL_HURT this frame (set by
                                ; player_attack_monster, cleared by
                                ; clear_hurt_flags at top of main_loop).
MON_TYPE_UNDEAD   = 1
MON_TYPE_GHOST    = 2
MON_TYPE_SKELETON = 3
MON_TYPE_DEATH    = 4           ; deliberately last so the depth-keyed
                                ; type pool (min(depth, 4)) only unlocks
                                ; DEATH at depth 4 — at depth 3 the
                                ; player still only faces UNDEAD/GHOST/
                                ; SKELETON, none of which one-shot.
SPRITE_NAME_PLAYER   = 0
SPRITE_NAME_UNDEAD   = 4
SPRITE_NAME_GHOST    = 8
SPRITE_NAME_DEATH    = 12
SPRITE_NAME_FOOD     = 16
SPRITE_NAME_SKELETON = 20

; TMS9918 palette indices used for monster sprites (0=transp, 7=cyan,
; 14=gray, 15=white, 10=dk yellow). UNDEAD stays bright white, GHOST
; gets the pale gray, DEATH ages into dark yellow, SKELETON rides on
; cyan to stand out as the warrior-class undead — all visible on the
; gray-on-black wall pattern.
MON_COL_UNDEAD   = 15
MON_COL_GHOST    = 14
MON_COL_DEATH    = 10
MON_COL_SKELETON = 7
COL_HURT       = 8              ; medium red — shared "took a hit" colour
                                ; for the player and any wounded monster
                                ; that survived its damage tick.
COL_FOOD       = 11             ; light yellow — food drumstick reads as
                                ; "edible" against the gray walls.

; --- Item pool (food drops) --------------------------------------------
; 8-slot pool at $E380, immediately after the monster pool ($E300-$E37F)
; — placing them contiguous lets one clear loop reset both at level
; transitions. Per-slot layout (4 bytes; +3 reserved for future item
; types like potions / scrolls):
;
;   +0 ITEM_TYPE  0=empty, 1=food (only type for now)
;   +1 ITEM_COL   logical 0..15
;   +2 ITEM_ROW   logical 0..9
;   +3 reserved
;
; Food drops where a monster died (player_attack_monster sets
; ITEM_TYPE=1 at the corpse cell). Walking onto the cell heals
; FOOD_HEAL HP (capped at HP_MAX) and frees the slot.
ITEM_COUNT     = 8
ITEM_SIZE      = 4
ITEM_TYPE      = 0
ITEM_COL       = 1
ITEM_ROW       = 2

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

; --- Field of view (compute_fov) ---------------------------------------
; vis_buffer holds one byte per logical tile (16x10 = 160 B), parallel
; to map_buffer. Two visibility bits:
;   bit 0 (VIS_SEEN)    once set, stays set for the rest of the level
;                        — "remembered" terrain is rendered greyed-out
;                        (same chars as live for now; static map can be
;                        recalled visually because it never changes).
;   bit 1 (VIS_VISIBLE) lit by the current FOV pass — gates whether
;                        place_all_sprites emits a SAT entry for any
;                        monster/item sitting on this cell.
;
; compute_fov walks Bresenham rays from the player to every cell on the
; logical perimeter (rows 0/9 + cols 0/15 = 48 cells), capped at
; FOV_RADIUS steps. Each ray marks every cell it touches with both
; bits, and stops the moment the ray crosses an opaque tile (TILE_WALL)
; — the wall itself is marked but anything past it is not.
;
; FOV_RADIUS tuning: the playable interior is only 14 wide × 8 tall, so
; a radius ≥ 5 lights most of the grid from the centre and movement
; barely changes what's visible. Even radius 4 felt "trop loin" in
; the bigger rooms because the box-perimeter casting fills the full
; 9×9 box around the player when there's no wall to clip the rays.
; Radius 3 → 7×7 box → ~half a small room visible at any time, which
; restores the "torchlight in a dark dungeon" feel and makes movement
; meaningful again.
FOV_RADIUS      = 3
VIS_SEEN        = $01
VIS_VISIBLE     = $02
VIS_BOTH        = $03

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
depth:          .res 1          ; current dungeon depth (1..255).
                                ; Only stairs-down advances depth.
                                ; 'N' regenerates at the same depth;
                                ; edge doors warp horizontally /
                                ; vertically into a sibling big-room
                                ; on the same floor.
trans_mode:     .res 1          ; level-transition kind (set by
                                ;   main_loop, consumed by new_level):
                                ;   0 = regen ('N')      — random gen, no INC
                                ;   1 = descent (stairs) — random gen, depth++
                                ;   2 = exited E (col 15) — force big-room, spawn at W
                                ;   3 = exited W (col 0)  — force big-room, spawn at E
                                ;   4 = exited N (row 0)  — force big-room, spawn at S
                                ;   5 = exited S (row 9)  — force big-room, spawn at N
wrap_anchor:    .res 1          ; row (modes 2/3) or col (modes 4/5)
                                ; the player exited at — preserved as
                                ; the spawn anchor in the wrapped room.
hp:             .res 1          ; current HP (0..HP_MAX). 0 = death.
player_hurt:    .res 1          ; non-zero if the player sprite should
                                ; render in COL_HURT this frame.
                                ; clear_hurt_flags resets it at the top
                                ; of every main_loop iteration; set by
                                ; step_monster_toward_player when a
                                ; monster lands its bump-attack.
pool_idx:       .res 1          ; current monster slot byte-offset into
                                ; `monsters` ($E300+); used by
                                ; spawn_monsters and move_monsters as a
                                ; loop cursor (advances by MON_SIZE).
pool_limit:     .res 1          ; high-water byte-offset for spawn_monsters
                                ; (set once from depth + 2, capped at
                                ; MON_COUNT * MON_SIZE).
mon_abs_dx:     .res 1          ; |player_col - mon.col| for the monster
mon_abs_dy:     .res 1          ;   currently being moved. Sign of the
                                ;   step is re-derived from a CMP at
                                ;   step time, so we don't store it.
mon_type_pool:  .res 1          ; spawn-time difficulty: max MON_TYPE id
                                ; eligible to spawn this level (= depth
                                ; capped at 4). depth 1 = UNDEAD only,
                                ; depth 2 adds GHOST, 3 adds DEATH,
                                ; 4+ adds SKELETON (all 4 types).
mon_hp_bonus:   .res 1          ; spawn-time difficulty: extra HP added
                                ; to every monster's mon_init_hp value
                                ; (= depth / 3). Sustained-damage
                                ; scaling — encounters get longer.
mon_dmg_bonus:  .res 1          ; spawn-time difficulty: extra damage
                                ; added to every monster's MON_DMG
                                ; (= depth / 6 = (depth/3) >> 1).
                                ; Lethality scaling — late-game bites
                                ; actually hurt.

; --- FOV scratch (compute_fov / cast_ray) ------------------------------
; Bresenham state. ray_endx/y are the target perimeter cell, set by the
; outer loop in compute_fov. cur_x/y walk from the player to the target;
; abs_dx/dy + sx/sy split the signed delta into magnitude + sign so the
; Bresenham core stays unsigned. fov_err is the running signed error
; (range ≈ -16..+16 — fits in signed 8-bit). fov_e2 caches err<<1 so
; the two step decisions in one iteration share the same shifted value.
ray_endx:       .res 1
ray_endy:       .res 1
cur_x:          .res 1
cur_y:          .res 1
abs_dx:         .res 1
abs_dy:         .res 1
sx:             .res 1          ; $01 (east) or $FF (west)
sy:             .res 1          ; $01 (south) or $FF (north)
fov_err:        .res 1
fov_e2:         .res 1
fov_step:       .res 1          ; cast_ray's step countdown — kept in ZP so
                                ; the X register stays available for
                                ; compute_fov's outer per-edge counter
                                ; across the JSR.


; --- Map buffer (160 B, 16x10 logical tiles, one byte = tile base id) ---
; The 2-pass corridor algorithm (mark every dug cell as TILE_DOOR, then
; finalize_doors converts non-boundary markers back to TILE_EMPTY) means
; we don't need to remember room rects after carving — the marker tile
; itself is the bookkeeping.
.segment "MAPSEG"
map_buffer:     .res LOGICAL_COLS * LOGICAL_ROWS
; Parallel visibility buffer: one byte per cell, VIS_SEEN | VIS_VISIBLE
; bits. clear_vis_buffer wipes it on every new_level.
vis_buffer:     .res LOGICAL_COLS * LOGICAL_ROWS

; --- Monster + item pools in unused high-bank RAM ----------------------
; Both pools live outside the linker-managed MAPSEG (which ends at
; $E0A0 after the 160-byte map_buffer); the rest of the high bank up
; to $EFFF is free RAM and we address it absolutely. Keeping the two
; pools contiguous (monsters then items) lets spawn_monsters wipe both
; with a single loop covering 160 B starting at `monsters`.
;
;   monsters $E300-$E37F  16 slots × 8 B = 128 B
;   items    $E380-$E39F   8 slots × 4 B =  32 B
monsters = $E300
items    = $E380


; =============================================
; CODE
; =============================================
.code

start:
        SEI                     ; we drive the chip ourselves
        CLD
        ; --- Stack pointer reset. death_screen reaches us via
        ; `JMP $4000` from inside `JSR finish_turn` — the JSR's return
        ; address is still on the stack and never gets popped, so each
        ; death leaks 2 bytes of stack. Without TXS here, ~63 deaths
        ; in one session wraps SP and corrupts return addresses. Real
        ; Apple-1 power-on doesn't guarantee SP either, so this also
        ; covers genuine cold-boot. ---
        LDX #$FF
        TXS

        JSR init_vdp_g1         ; 8 registers + disable_sprites
        JSR override_r1_16x16   ; switch sprite mode 8x8 -> 16x16
        JSR upload_tileset      ; 2048 B pattern table -> VRAM $0000
        JSR upload_colour_table ; 32 B colour table   -> VRAM $2000
        JSR clear_name_table    ; whole 32x24 -> char 0 (black)

        JSR draw_title          ; ROGUE banner + key-layout prompt
        JSR wait_kb_choice      ; bind keys + seed prng_lo/hi from key timing

        LDA #1                  ; first level — new_level INCs from here
        STA depth
        LDA #0                  ; trans_mode 0 = regen (random gen, no wrap)
        STA trans_mode          ; ensures place_perimeter_doors places
                                ; all 4 doors on the very first big-room.
        LDA #HP_MAX             ; full HP at game start
        STA hp
        ; player_hurt is consumed by the FIRST place_all_sprites below
        ; (before main_loop runs clear_hurt_flags). On restart after a
        ; death the killing-bump left it non-zero, which would render
        ; the spawn sprite in COL_HURT for one frame. Clear explicitly.
        LDA #0
        STA player_hurt

        JSR clear_name_table    ; wipe title before the game view appears
        JSR gen_dungeon         ; procedural rooms + corridors + stairs;
                                ; sets player_col/row to first room centre.
        JSR clear_vis_buffer    ; fresh dungeon -> nothing remembered yet
        JSR compute_fov         ; light up the player's spawn neighbourhood
        JSR render_map          ; expand 16x10 logical map to 32x20 char block
                                ; (now FOV-gated: dark cells render blank)

        JSR upload_sprite_pats  ; player + 3 undead patterns -> $3800-$387F
        JSR spawn_monsters      ; populate the pool for the first level
        JSR place_all_sprites   ; SAT: player slot 0 + each live monster
        JSR update_hud          ; HUD on row 20: depth + HP + food

main_loop:
        ; Reset hurt flags so any flash from last turn doesn't leak
        ; into THIS turn's place_all_sprites repaint. The SAT still
        ; holds the previous frame's red-flash colour bytes until the
        ; next place_all_sprites overwrites them, so the flash is
        ; visible during wait_key.
        JSR clear_hurt_flags
        JSR wait_key            ; A = raw key (high bit still set)
        CMP #('N' | $80)        ; 'N' regenerates a fresh random level
        BEQ @do_regen
        JSR handle_input        ; carry clear -> tgt_col/tgt_row set
        BCS main_loop           ; no movement key -> just wait again
        ; Monster on the target cell? bump-attack instead of moving —
        ; this fires BEFORE check_collision so the player can hit a
        ; monster sitting on a wall-frame door without "falling off
        ; the screen" (frame doors are passable for the player).
        JSR monster_at_target   ; on hit: C clear, X = pool offset
        BCC @bump
        JSR check_collision     ; carry clear -> passable
        BCS main_loop           ; blocked -> ignore the move
        ; Classify the move's target into one of three buckets:
        ;   - TILE_STAIRS_DOWN → descent (depth++)
        ;   - on the screen frame → edge-door wrap (same depth, force
        ;     big-room, spawn at the opposite edge)
        ;   - everything else → regular move within the playable area
        JSR tile_at_target
        STA tmp                 ; cache the target tile id for the
                                ; door-crossing reactivation check below
                                ; (CMP/BEQ/LDA chain through the edge
                                ; classifiers doesn't touch ZP, so tmp
                                ; survives until the regular-move block).
        CMP #TILE_STAIRS_DOWN
        BEQ @do_descent
        LDA tgt_col
        BEQ @exit_w             ; col 0
        CMP #15
        BEQ @exit_e             ; col 15
        LDA tgt_row
        BEQ @exit_n             ; row 0
        CMP #9
        BEQ @exit_s             ; row 9
        ; Regular move within the playable interior.
        LDA tgt_col
        STA player_col
        LDA tgt_row
        STA player_row
        ; Door-crossing fog reactivation: stepping ONTO a TILE_DOOR
        ; cell wipes vis_buffer so everything behind us (the room or
        ; corridor we just left) goes black again. compute_fov then
        ; rebuilds visibility from the door cell, lighting up only
        ; the immediate neighbourhood of the threshold. Combined with
        ; opaque doors in is_opaque_at_cur, this makes every room a
        ; fresh "scene" — no peeking through doors, no remembered
        ; layout once you've left.
        LDA tmp
        CMP #TILE_DOOR
        BNE @no_door_reset
        JSR clear_vis_buffer
@no_door_reset:
        JSR try_pickup_item     ; HP += FOOD_HEAL if a food drop is here
        JSR move_monsters       ; AI + monster→player attacks
        JSR compute_fov         ; player moved -> recompute FOV
        JSR render_map          ; redraw the playfield under the new FOV
        JSR place_all_sprites   ; rewrite the full SAT (FOV-gated)
        JSR finish_turn         ; HUD repaint (may JMP death)
        JMP main_loop

@bump:
        ; X is set by monster_at_target — pool offset of the bumped
        ; monster. Player attacks (no move), monsters take their turn,
        ; redraw, tick. No compute_fov / render_map: the player didn't
        ; move so vis_buffer is unchanged; place_all_sprites still
        ; reruns to reflect monster moves + the bump-attack flash.
        JSR player_attack_monster
        JSR move_monsters
        JSR place_all_sprites
        JSR finish_turn
        JMP main_loop

@do_regen:
        LDA #0
        STA trans_mode
        JSR new_level
        JMP main_loop           ; 'N' regen is a debug refresh, no turn cost

@do_descent:
        LDA #1
        STA trans_mode
        JSR new_level
        JSR finish_turn        ; descent counts as a turn
        JMP main_loop

@exit_e:                        ; tgt_col = 15
        LDA #2
        STA trans_mode
        LDA tgt_row
        STA wrap_anchor
        JSR new_level
        JSR finish_turn
        JMP main_loop

@exit_w:                        ; tgt_col = 0
        LDA #3
        STA trans_mode
        LDA tgt_row
        STA wrap_anchor
        JSR new_level
        JSR finish_turn
        JMP main_loop

@exit_n:                        ; tgt_row = 0
        LDA #4
        STA trans_mode
        LDA tgt_col
        STA wrap_anchor
        JSR new_level
        JSR finish_turn
        JMP main_loop

@exit_s:                        ; tgt_row = 9
        LDA #5
        STA trans_mode
        LDA tgt_col
        STA wrap_anchor
        JSR new_level
        JSR finish_turn
        JMP main_loop


; ----------------------------------------------------------------------------
; new_level: rebuild the screen according to trans_mode.
;   trans_mode = 0 ('N')      : random gen (big-room or two-rooms),
;                                no depth change.
;   trans_mode = 1 (stairs)   : random gen, depth advances by one.
;   trans_mode = 2..5 (edges) : force a big-room and respawn the
;                                player on the opposite edge so the
;                                two rooms feel like a continuous
;                                horizontal/vertical neighbourhood;
;                                depth unchanged. wrap_anchor carries
;                                the row (modes 2/3) or col (4/5).
; LFSR state survives across calls — no reseed needed.
; ----------------------------------------------------------------------------
new_level:
        ; Depth only advances on stairs-down descent.
        LDA trans_mode
        CMP #1
        BNE @no_inc
        INC depth
@no_inc:
        ; Edge-door wrap modes (2..5) force a big-room layout so the
        ; spawn-at-opposite-edge override always lands inside a known
        ; geometry. Mode 0/1 keep the random dispatcher.
        LDA trans_mode
        CMP #2
        BCS @wrap
        JSR gen_dungeon
        JMP @paint
@wrap:
        JSR fill_with_walls
        JSR gen_big_room
        JSR apply_wrap_spawn    ; overrides player_col/row to the
                                ; opposite edge of the new big-room
@paint:
        JSR spawn_monsters      ; fresh undead pool for the new layout
        JSR clear_name_table
        JSR clear_vis_buffer    ; new level -> forget the previous map
        JSR compute_fov         ; light the spawn neighbourhood
        JSR render_map
        JSR place_all_sprites
        JSR update_hud
        RTS


; ----------------------------------------------------------------------------
; apply_wrap_spawn: respawn the player at the opposite edge of the
; freshly-generated big-room AND stamp a TILE_DOOR on the entry-edge
; cell aligned with the player's anchor row/col. The aligned door
; lets the player walk back out the same way they came (re-triggers
; a wrap), so the world feels continuous even though every wrap
; regenerates a fresh map. The four random perimeter doors from
; gen_big_room remain in place — most of the time the aligned door
; sits on a different cell from any of them.
;
;   trans_mode 2 (exited E)  → spawn (1,  anchor); door at (0,  anchor)
;   trans_mode 3 (exited W)  → spawn (14, anchor); door at (15, anchor)
;   trans_mode 4 (exited N)  → spawn (anchor, 8); door at (anchor, 9)
;   trans_mode 5 (exited S)  → spawn (anchor, 1); door at (anchor, 0)
; ----------------------------------------------------------------------------
apply_wrap_spawn:
        LDA     trans_mode
        CMP     #2
        BEQ     @to_w
        CMP     #3
        BEQ     @to_e
        CMP     #4
        BEQ     @to_s
        ; mode 5 (the only remaining): exited S → spawn at N edge.
        LDA     #1
        STA     player_row
        LDA     wrap_anchor
        STA     player_col
        ; Aligned return door at (anchor, 0).
        STA     tgt_col
        LDA     #0
        STA     tgt_row
        JMP     @stamp
@to_w:                                  ; exited E → spawn at W edge.
        LDA     #1
        STA     player_col
        LDA     wrap_anchor
        STA     player_row
        STA     tgt_row
        LDA     #0
        STA     tgt_col
        JMP     @stamp
@to_e:                                  ; exited W → spawn at E edge.
        LDA     #14
        STA     player_col
        LDA     wrap_anchor
        STA     player_row
        STA     tgt_row
        LDA     #15
        STA     tgt_col
        JMP     @stamp
@to_s:                                  ; exited N → spawn at S edge.
        LDA     #8
        STA     player_row
        LDA     wrap_anchor
        STA     player_col
        STA     tgt_col
        LDA     #9
        STA     tgt_row
@stamp:
        LDA     #TILE_DOOR
        JSR     set_tile
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
        WRT_DATA_REG
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
        WRT_DATA_REG
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
; Stairs-up at the top-right interior cell (14, 2) so its east
; neighbour is the screen-edge wall (col 15). Player spawns one cell
; WEST of the stairs at (13, 2) — the "arrived from above" anchor —
; matching the spec rule "le personnage arrive à gauche de l'escalier".
;
; TODO (when HP system + sprites land):
;   - Replace TILE_STAIRS_UP with a rubble tile (bldg_rubble_pat) so
;     the visual reads "the way up just collapsed". Slot 16 of the
;     PALETTE in tools/extract_quale_8x8_tiles.py is the swap point.
;   - Stepping into the rubble cell should still block movement AND
;     subtract 1 HP (penalty for trying to climb back up). For now
;     check_collision treats TILE_STAIRS_UP as a hard wall.
;
; Stairs-down at bottom-left interior corner (1, 8) — west neighbour
; is the screen frame, so the descent sprite sits with a wall on its
; left as the spec demands. Four random perimeter doors complete the
; level's "exit portals"; in wrap mode the entry edge's random door
; is suppressed so apply_wrap_spawn's aligned door is alone there.
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

        ; Stairs-up at (14, 2). Player spawn one cell west at (13, 2).
        LDA     #14
        STA     tgt_col
        LDA     #2
        STA     tgt_row
        LDA     #TILE_STAIRS_UP
        JSR     set_tile
        LDA     #13
        STA     player_col
        LDA     #2
        STA     player_row

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
; place_perimeter_doors: up to four TILE_DOOR cells, one per cardinal
; screen edge. Doors REPLACE wall cells on the frame (rows 0/9, cols
; 0/15) rather than sitting on interior cells — walking onto them is
; the "exit to another map" mechanic. main_loop detects when the
; target of a move is at a screen edge and triggers new_level instead
; of moving the player off-grid.
;
; In wrap modes (trans_mode 2..5) the entry edge is SKIPPED so the
; aligned return door stamped by apply_wrap_spawn is the only door on
; that wall — otherwise the player would see two doors on the wall
; they just came in through, which the spec forbids.
;   trans_mode 2 (exited E → spawn W) : skip W edge here.
;   trans_mode 3 (exited W → spawn E) : skip E edge.
;   trans_mode 4 (exited N → spawn S) : skip S edge.
;   trans_mode 5 (exited S → spawn N) : skip N edge.
; Modes 0 and 1 leave trans_mode out of the [2..5] range and place all
; four random doors as before.
; ----------------------------------------------------------------------------
; Per-edge tuple (4 bytes) walked by the table-driven loop below:
;   +0 skip_mode : trans_mode value that suppresses this edge
;                  (out-of-range modes 0/1 never match → all 4 doors land)
;   +1 span      : rand_mod argument (col/row range = span cells wide)
;   +2 axis      : 0 → randomise tgt_col, fixed → tgt_row (N/S doors)
;                  1 → randomise tgt_row, fixed → tgt_col (W/E doors)
;   +3 fixed     : the screen-frame col (0/15) or row (0/9) the door sits on
; Random coord = rand_mod(span) + 3 — the +3 leaves a 3-cell margin from
; each corner so doors never land in the corner cells (they would defeat
; the wrap-spawn alignment trick in apply_wrap_spawn).
DOOR_BASE = 3
place_perimeter_doors:
        LDX     #0
@lp:    LDA     door_table+0,X          ; skip_mode for this edge
        CMP     trans_mode
        BEQ     @skip
        LDA     door_table+1,X          ; span
        JSR     rand_mod                ; A in [0, span); X preserved
        CLC
        ADC     #DOOR_BASE              ; A in [3, 3+span)
        LDY     door_table+2,X          ; axis (0 = horizontal, 1 = vertical)
        BNE     @vert
        STA     tgt_col                 ; N/S: rand into col
        LDA     door_table+3,X
        STA     tgt_row                 ; fixed row (0 or 9)
        JMP     @stamp
@vert:
        STA     tgt_row                 ; W/E: rand into row
        LDA     door_table+3,X
        STA     tgt_col                 ; fixed col (0 or 15)
@stamp:
        LDA     #TILE_DOOR
        JSR     set_tile                ; preserves X
@skip:
        TXA
        CLC
        ADC     #4
        TAX
        CPX     #(door_table_end - door_table)
        BCC     @lp
        RTS

door_table:
        ;       skip span axis fixed
        .byte   5,   10,  0,   0        ; N: skip on mode 5, col rand, row 0
        .byte   4,   10,  0,   9        ; S: skip on mode 4, col rand, row 9
        .byte   2,    4,  1,   0        ; W: skip on mode 2, row rand, col 0
        .byte   3,    4,  1,  15        ; E: skip on mode 3, row rand, col 15
door_table_end:


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
        ;         stairs-up location (rx+rw-1, ry). Player spawns ONE
        ;         CELL WEST of the stairs (rx+rw-2, ry) per the spec —
        ;         "arrive à gauche de l'escalier". Row ry ≠ cy keeps
        ;         the cell west of stairs from sitting on the corridor,
        ;         so the spawn cell is always plain interior floor.
        ; Iter 1: dig the L-corridor from the previous centre.
        LDA     room_idx
        BNE     @subsequent
        LDA     room_x
        CLC
        ADC     room_w
        SEC
        SBC     #2
        STA     player_col              ; rx + rw - 2 (left of stairs)
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

        ; Stairs-up at room 0's right-edge cell (one EAST of player
        ; spawn — see iter 0 above). East neighbour is the room's east
        ; wall, so the sprite's right side is anchored as required.
        LDA     player_col
        CLC
        ADC     #1
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
; spawn_monsters: zero out the monster + item pools, then drop a depth-
; scaled wave of undead onto random TILE_EMPTY cells. Three knobs all
; key off `depth`:
;
;   - count  = min(depth + 2, MON_COUNT)
;             depth 1 → 3 monsters, depth 14 → 16 (capped).
;
;   - type pool = min(depth, 4)
;             depth 1 = UNDEAD only,
;             depth 2 = + GHOST,
;             depth 3 = + DEATH,
;             depth 4+ = + SKELETON (full mix).
;             rand_mod uses this as the upper bound, so each level
;             unlocks one new tier of menace.
;
;   - hp bonus  = depth / 3   (subtract-3 loop, no clean shift)
;             added on top of mon_init_hp[type]. Encounters get longer
;             — a depth-9 UNDEAD has 1+3 = 4 HP, needs four PLAYER_DMG
;             hits.
;
;   - dmg bonus = depth / 6   (derived as (depth/3) >> 1)
;             added on top of mon_init_dmg[type]. Late-game bites
;             actually hurt — at depth 12 every monster carries +2 dmg
;             on top of its base, so DEATH bites for 4 of the player's
;             10 HP per turn.
;
; find_empty_cell rejects the player's cell and any cell already
; holding a monster (item pool is empty at this point so no item
; collision). If find_empty_cell fails (32 attempts exhausted) we stop
; early — the level just ends up with fewer monsters than the budget.
; ----------------------------------------------------------------------------
spawn_monsters:
        ; --- Clear both pools in one 160-byte sweep starting at the
        ; monster pool ($E300); items live contiguously at $E380.
        ; Sentinel-BNE pattern (cf. fill_with_walls) because
        ; 160 - 1 = 159 = $9F has bit 7 set, so a plain
        ; LDX/DEX/BPL countdown would exit immediately on the first DEX.
        LDA     #0
        LDX     #(MON_COUNT * MON_SIZE + ITEM_COUNT * ITEM_SIZE)
@cl:    DEX
        STA     monsters,X
        BNE     @cl

        ; --- pool_limit = min(depth + 2, MON_COUNT) * MON_SIZE ---
        LDA     depth
        CLC
        ADC     #2
        CMP     #(MON_COUNT + 1)
        BCC     @cap_count
        LDA     #MON_COUNT
@cap_count:
        ASL                             ; ×8 = byte offset (MON_SIZE = 8)
        ASL
        ASL
        STA     pool_limit
        LDA     #0
        STA     pool_idx

        ; --- mon_type_pool = min(depth, 4) — type pool widens by depth ---
        LDA     depth
        CMP     #5
        BCC     @cap_pool               ; depth < 5 → use depth as-is
        LDA     #4
@cap_pool:
        STA     mon_type_pool

        ; --- mon_hp_bonus = depth / 3 (subtract-3 loop — no clean
        ;     shift for /3 on the 6502).  mon_dmg_bonus = depth / 6 is
        ;     derived as (depth/3) >> 1, which is mathematically equal
        ;     to floor(depth/6) for any non-negative depth, so we avoid
        ;     a second division loop. ---
        LDA     depth
        LDX     #0
@d3:    CMP     #3
        BCC     @d3_done
        SEC
        SBC     #3
        INX
        JMP     @d3
@d3_done:
        STX     mon_hp_bonus
        TXA
        LSR
        STA     mon_dmg_bonus

@spawn_lp:
        LDA     pool_idx
        CMP     pool_limit
        BCS     @done                   ; placed enough

        JSR     find_empty_cell         ; tgt_col/tgt_row set on success
        BCS     @done                   ; ran out of free cells → bail

        ; --- Roll a random type 1..mon_type_pool (depth-scaled) ---
        LDA     mon_type_pool
        JSR     rand_mod
        CLC
        ADC     #1
        TAY                             ; Y = type, indexes mon_init_* tables

        LDX     pool_idx
        TYA
        STA     monsters+MON_TYPE,X
        ; HP = mon_init_hp[type] + mon_hp_bonus  (depth-scaled durability)
        LDA     mon_init_hp,Y
        CLC
        ADC     mon_hp_bonus
        STA     monsters+MON_HP,X
        LDA     tgt_col
        STA     monsters+MON_COL,X
        LDA     tgt_row
        STA     monsters+MON_ROW,X
        LDA     mon_init_name,Y
        STA     monsters+MON_NAME,X
        LDA     mon_init_color,Y
        STA     monsters+MON_COLOR,X
        ; DMG = mon_init_dmg[type] + mon_dmg_bonus  (depth-scaled lethality)
        LDA     mon_init_dmg,Y
        CLC
        ADC     mon_dmg_bonus
        STA     monsters+MON_DMG,X

        LDA     pool_idx
        CLC
        ADC     #MON_SIZE
        STA     pool_idx
        JMP     @spawn_lp
@done:
        RTS


; ----------------------------------------------------------------------------
; find_empty_cell: roll random (col, row) pairs in [0..LOGICAL_COLS) x
; [0..LOGICAL_ROWS) until one lands on a TILE_EMPTY cell that is neither
; the player's current cell nor occupied by a live monster. Up to 32
; attempts before giving up.
;   On success: tgt_col / tgt_row set, carry CLEAR.
;   On failure: carry SET.
; The PHA/PLA pair around monster_at_target preserves the X attempts
; counter (monster_at_target uses X for its scan).
; ----------------------------------------------------------------------------
find_empty_cell:
        LDX     #32                     ; attempts counter
@try:
        LDA     #LOGICAL_COLS
        JSR     rand_mod
        STA     tgt_col
        LDA     #LOGICAL_ROWS
        JSR     rand_mod
        STA     tgt_row
        JSR     tile_at_target          ; preserves X (uses Y)
        CMP     #TILE_EMPTY
        BNE     @next
        LDA     tgt_col
        CMP     player_col
        BNE     @ok_player
        LDA     tgt_row
        CMP     player_row
        BEQ     @next
@ok_player:
        TXA
        PHA                             ; save attempts counter
        JSR     monster_at_target       ; clobbers X
        PLA
        TAX                             ; restore attempts counter
        BCC     @next                   ; C clear = monster present → retry
        CLC
        RTS
@next:
        DEX
        BNE     @try
        SEC
        RTS


; ----------------------------------------------------------------------------
; monster_at_target: scan the monster pool for a live monster (type != 0)
; whose (col, row) matches (tgt_col, tgt_row).
;   On hit:  carry CLEAR, X = byte offset of the slot in `monsters`.
;   On miss: carry SET.
; Uses A and X. Preserves Y. Used by both find_empty_cell (to avoid
; double-spawning on the same cell) and the bump-attack path in
; main_loop (to detect "player tried to step into a monster").
; ----------------------------------------------------------------------------
monster_at_target:
        LDX     #0
@lp:    LDA     monsters+MON_TYPE,X
        BEQ     @next                   ; type 0 = empty slot
        LDA     monsters+MON_COL,X
        CMP     tgt_col
        BNE     @next
        LDA     monsters+MON_ROW,X
        CMP     tgt_row
        BNE     @next
        CLC                             ; hit
        RTS
@next:
        TXA
        CLC
        ADC     #MON_SIZE
        TAX
        CPX     #(MON_COUNT * MON_SIZE)
        BCC     @lp
        SEC                             ; miss
        RTS


; ----------------------------------------------------------------------------
; mon_init_*: per-type initialisation tables, indexed by MON_TYPE
; (1=undead, 2=ghost, 3=skeleton, 4=death). Index 0 is the dead/empty
; marker and never read here, so it sits as a placeholder $00.
; The order is tuned so the depth-scaled type pool (min(depth, 4))
; introduces tiers from least to most lethal: weakest at depth 1
; (UNDEAD), DEATH gated to depth 4 so the player has time to ramp HP
; before the 2-dmg/3-HP tier appears.
; ----------------------------------------------------------------------------
mon_init_hp:
        .byte   0, 1, 2, 2, 3
mon_init_name:
        .byte   0, SPRITE_NAME_UNDEAD, SPRITE_NAME_GHOST, SPRITE_NAME_SKELETON, SPRITE_NAME_DEATH
mon_init_color:
        .byte   0, MON_COL_UNDEAD, MON_COL_GHOST, MON_COL_SKELETON, MON_COL_DEATH
mon_init_dmg:
        .byte   0, 1, 1, 2, 2


; ----------------------------------------------------------------------------
; spawn_item: drop a food item at (tgt_col, tgt_row). Caller has copied
; the dying monster's MON_COL/MON_ROW into tgt_col/tgt_row. Picks the
; first empty pool slot (ITEM_TYPE == 0); if every slot is taken the
; food is silently lost — 8 simultaneous items is a generous cap, and
; the player would have already healed several times to fill the pool.
; ----------------------------------------------------------------------------
spawn_item:
        LDX     #0
@lp:    LDA     items+ITEM_TYPE,X
        BEQ     @found
        TXA
        CLC
        ADC     #ITEM_SIZE
        TAX
        CPX     #(ITEM_COUNT * ITEM_SIZE)
        BCC     @lp
        RTS                             ; pool full → food lost
@found:
        LDA     #1
        STA     items+ITEM_TYPE,X
        LDA     tgt_col
        STA     items+ITEM_COL,X
        LDA     tgt_row
        STA     items+ITEM_ROW,X
        RTS


; ----------------------------------------------------------------------------
; item_at_target: scan the item pool for a live food drop at
; (tgt_col, tgt_row).
;   On hit:  carry CLEAR, X = byte offset of the slot in `items`.
;   On miss: carry SET.
; Mirrors monster_at_target's contract so try_pickup_item can read X
; back to free the slot.
; ----------------------------------------------------------------------------
item_at_target:
        LDX     #0
@lp:    LDA     items+ITEM_TYPE,X
        BEQ     @next
        LDA     items+ITEM_COL,X
        CMP     tgt_col
        BNE     @next
        LDA     items+ITEM_ROW,X
        CMP     tgt_row
        BNE     @next
        CLC
        RTS
@next:
        TXA
        CLC
        ADC     #ITEM_SIZE
        TAX
        CPX     #(ITEM_COUNT * ITEM_SIZE)
        BCC     @lp
        SEC
        RTS


; ----------------------------------------------------------------------------
; try_pickup_item: called from main_loop's regular-move path right
; after the player's coordinates are updated. If a food item sits on
; the player's new cell, free the slot and add FOOD_HEAL to HP
; (saturating at HP_MAX). Otherwise no-op.
; ----------------------------------------------------------------------------
try_pickup_item:
        LDA     player_col
        STA     tgt_col
        LDA     player_row
        STA     tgt_row
        JSR     item_at_target
        BCS     @done
        LDA     #0
        STA     items+ITEM_TYPE,X       ; consume the item
        LDA     hp
        CLC
        ADC     #FOOD_HEAL
        CMP     #(HP_MAX + 1)
        BCC     @store
        LDA     #HP_MAX
@store:
        STA     hp
@done:
        RTS


; ----------------------------------------------------------------------------
; player_attack_monster: deal PLAYER_DMG damage to the monster at pool
; offset X. Saturates HP at 0. Survivors get MON_HURT set so they
; render in COL_HURT this frame; killed monsters free their slot AND
; drop a food item at their last position 1-in-2 times (rand_mod #2
; → BNE no_drop). 50% rate keeps the heal economy roughly neutral —
; the player can sustain combat indefinitely on average, but a streak
; of no-drops puts them in real danger.
; ----------------------------------------------------------------------------
player_attack_monster:
        LDA     monsters+MON_HP,X
        SEC
        SBC     #PLAYER_DMG
        BCS     @store
        LDA     #0
@store:
        STA     monsters+MON_HP,X
        BNE     @hurt_flash
        ; --- Killed: stash the corpse cell, free the slot, then maybe drop. ---
        LDA     monsters+MON_COL,X
        STA     tgt_col
        LDA     monsters+MON_ROW,X
        STA     tgt_row
        LDA     #0
        STA     monsters+MON_TYPE,X
        ; 50% drop rate. rand_mod #2 returns {0, 1}; only 0 fires the
        ; drop. rand_mod preserves X (it touches A + tmp only).
        LDA     #2
        JSR     rand_mod
        BNE     @no_drop
        JMP     spawn_item              ; tail-call (RTS from spawn_item)
@no_drop:
        RTS
@hurt_flash:
        LDA     #1
        STA     monsters+MON_HURT,X
        RTS


; ----------------------------------------------------------------------------
; move_monsters: walk the pool; each live monster takes one turn driven
; by step_monster, the per-MON_TYPE AI dispatcher. UNDEAD does direct
; greedy chase, GHOST wanders (random walk), DEATH skips half its turns
; before chasing, SKELETON takes the shorter axis first to flank — see
; step_monster's banner for the full table.
;
; All AIs ultimately route through apply_step which deals damage on
; player overlap (MON_DMG saturating-sub HP) or moves the monster if
; the target cell is in-bounds, passable, and unoccupied.
;
; Doors are FORBIDDEN for monsters by design — each undead is confined
; to its spawn room (or to a corridor segment between two doors), so
; the player can rest in another room and the pursuit stops at the
; threshold. Only TILE_EMPTY and TILE_STAIRS_DOWN are passable.
;
; Edge cells of the screen frame are also not enterable — the
; PLAY_*_ROW/COL bounds keep monsters inside the playable interior so
; they can never sit on a wrap-door (and the bounds check fires before
; the tile check, so frame doors are rejected even before this rule).
; ----------------------------------------------------------------------------
move_monsters:
        LDA     #0
        STA     pool_idx
@lp:
        LDX     pool_idx
        LDA     monsters+MON_TYPE,X
        BEQ     @next
        JSR     step_monster
@next:
        LDA     pool_idx
        CLC
        ADC     #MON_SIZE
        STA     pool_idx
        CMP     #(MON_COUNT * MON_SIZE)
        BCC     @lp
        RTS


; ----------------------------------------------------------------------------
; step_monster: move-or-attack dispatcher for one monster, indexed by
; MON_TYPE. Pac-Man-style behaviour split — each undead has its own AI:
;
;   UNDEAD   (MON_TYPE 1) -> ai_undead   : Blinky-style direct chase
;                                          (greedy on the larger |delta|,
;                                          fallback to the other axis if
;                                          blocked).
;   GHOST    (MON_TYPE 2) -> ai_ghost    : random walk — picks one of the
;                                          four cardinal directions
;                                          uniformly each turn. Wanders
;                                          and only hits the player by
;                                          chance.
;   DEATH    (MON_TYPE 3) -> ai_death    : sloth chase — coin-flip skip
;                                          (acts ~half the turns), but
;                                          when it does act it runs the
;                                          full greedy chase. Slow but
;                                          its 2-dmg hit is brutal.
;   SKELETON (MON_TYPE 4) -> ai_skeleton : anti-greedy chase — picks the
;                                          SHORTER axis first, so it
;                                          flanks rather than rushing
;                                          straight at the player.
;
; All AIs eventually route through try_step_x / try_step_y / apply_step
; which handle bounds + tile passability + monster/food collision +
; player overlap (= bite). On entry: X = pool offset (MON_TYPE != 0).
; Clobbers A, X, Y, mon_abs_dx, mon_abs_dy, tgt_col, tgt_row, tmp.
; ----------------------------------------------------------------------------
step_monster:
        LDA     monsters+MON_TYPE,X
        CMP     #MON_TYPE_GHOST
        BEQ     ai_ghost
        CMP     #MON_TYPE_DEATH
        BEQ     ai_death
        CMP     #MON_TYPE_SKELETON
        BEQ     ai_skeleton
        ; Default = MON_TYPE_UNDEAD = greedy chase.
        ; (also catches any future unknown type — safer than RTS.)
        ; Fall through into ai_undead.


; ----------------------------------------------------------------------------
; ai_undead: greedy chase — step on the axis with the larger |delta|.
; If that step is blocked (wall, door, monster, food, edge), fall back
; to the other axis so the undead can still navigate around obstacles.
; ----------------------------------------------------------------------------
ai_undead:
        JSR     compute_abs_deltas
        BEQ     @bail                   ; both deltas 0 → defensive
        LDA     mon_abs_dx
        CMP     mon_abs_dy
        BCC     @y_first                ; |dx| < |dy| → y-axis first
        JSR     try_step_x
        BCC     @bail                   ; success
        JSR     try_step_y
        RTS
@y_first:
        JSR     try_step_y
        BCC     @bail
        JSR     try_step_x
@bail:
        RTS


; ----------------------------------------------------------------------------
; ai_skeleton: anti-greedy chase — try the SHORTER axis first, fall
; back to the longer. The skeleton tends to circle and flank rather
; than rush head-on, which makes it dangerous from unexpected angles.
; Distinct path from undead even when both deltas are non-zero.
; ----------------------------------------------------------------------------
ai_skeleton:
        JSR     compute_abs_deltas
        BEQ     @bail
        LDA     mon_abs_dx
        CMP     mon_abs_dy
        BCC     @x_first                ; |dx| < |dy| → x-axis first (anti)
        JSR     try_step_y              ; |dx| >= |dy| → y-axis first (anti)
        BCC     @bail
        JSR     try_step_x
        RTS
@x_first:
        JSR     try_step_x
        BCC     @bail
        JSR     try_step_y
@bail:
        RTS


; ----------------------------------------------------------------------------
; ai_death: sloth chase — half the turns the death monster simply
; doesn't move (coin flip via rand_mod #2). The other half it runs
; ai_undead's greedy chase. Combined with its 2-dmg hit and 3 HP, the
; player has time to disengage but pays heavily for getting cornered.
; ----------------------------------------------------------------------------
ai_death:
        LDA     #2
        JSR     rand_mod                ; A in {0, 1}; preserves X
        BEQ     @skip
        JMP     ai_undead               ; tail-call greedy
@skip:
        RTS


; ----------------------------------------------------------------------------
; ai_ghost: random walk — picks one of the four cardinal directions
; uniformly each turn (rand_mod #4 → {N, S, W, E} via ghost_dx/dy
; signed-byte tables). The chosen step routes through apply_step like
; the chase AIs, so walls/doors/monsters/food still block. The ghost
; doesn't aim at the player at all — it bites by accident.
; ----------------------------------------------------------------------------
ai_ghost:
        LDA     #4
        JSR     rand_mod                ; A in [0..3]
        TAY
        LDA     monsters+MON_COL,X
        CLC
        ADC     ghost_dx,Y              ; signed offset (-1, 0, +1)
        STA     tgt_col
        LDA     monsters+MON_ROW,X
        CLC
        ADC     ghost_dy,Y
        STA     tgt_row
        JMP     apply_step              ; tail-call

; Direction table (signed): index 0=N, 1=S, 2=W, 3=E.
ghost_dx:
        .byte   $00, $00, $FF, $01
ghost_dy:
        .byte   $FF, $01, $00, $00


; ----------------------------------------------------------------------------
; compute_abs_deltas: shared helper for the chasing AIs.
; Sets mon_abs_dx = |player_col - mon.col| and mon_abs_dy = same for
; rows. On return A = mon_abs_dx | mon_abs_dy with the Z flag set iff
; both deltas are 0 (caller's BEQ catches the defensive "monster sits
; on player" case — never happens in normal play but cheap to guard).
; ----------------------------------------------------------------------------
compute_abs_deltas:
        LDA     player_col
        SEC
        SBC     monsters+MON_COL,X
        BCS     @abs_dx_done
        EOR     #$FF
        CLC
        ADC     #1                      ; two's-complement negate
@abs_dx_done:
        STA     mon_abs_dx
        LDA     player_row
        SEC
        SBC     monsters+MON_ROW,X
        BCS     @abs_dy_done
        EOR     #$FF
        CLC
        ADC     #1
@abs_dy_done:
        STA     mon_abs_dy
        ORA     mon_abs_dx              ; Z set iff both deltas are 0
        RTS


; ----------------------------------------------------------------------------
; try_step_x / try_step_y: attempt one cardinal step on the given axis.
; The sign is derived from a CMP against player_col / player_row, so we
; never need a stored dx/dy sign. If the monster is already aligned on
; this axis (abs == 0) the routine returns C set without touching state.
; Both routines fall through to apply_step which does the attack-or-move
; check and returns C clear on success.
;
; X (the monster pool offset) is preserved across the call by all paths.
; ----------------------------------------------------------------------------
try_step_x:
        LDA     mon_abs_dx
        BNE     @x_compute
        SEC                             ; nothing to do on this axis
        RTS
@x_compute:
        LDA     monsters+MON_ROW,X
        STA     tgt_row
        LDA     monsters+MON_COL,X
        CMP     player_col
        BCC     @x_east                 ; mon.col < player.col → +1
        SEC
        SBC     #1
        STA     tgt_col
        JMP     apply_step
@x_east:
        CLC
        ADC     #1
        STA     tgt_col
        JMP     apply_step

try_step_y:
        LDA     mon_abs_dy
        BNE     @y_compute
        SEC
        RTS
@y_compute:
        LDA     monsters+MON_COL,X
        STA     tgt_col
        LDA     monsters+MON_ROW,X
        CMP     player_row
        BCC     @y_south                ; mon.row < player.row → +1
        SEC
        SBC     #1
        STA     tgt_row
        JMP     apply_step
@y_south:
        CLC
        ADC     #1
        STA     tgt_row
        ; fall through to apply_step


; ----------------------------------------------------------------------------
; apply_step: shared tail for try_step_x / try_step_y.
; Reads tgt_col / tgt_row (already set by the caller) and:
;   - if (tgt) == player → attack player (HP -= mon.MON_DMG, set
;     player_hurt) and return CLC (success — counts as the action).
;   - else if the cell is in-bounds, passable (EMPTY or STAIRS_DOWN
;     only — doors are forbidden for monsters), and not occupied by
;     another monster or a food drop → write back into MON_COL/ROW and
;     return CLC.
;   - else return SEC (blocked — caller may try the other axis).
; ----------------------------------------------------------------------------
apply_step:
        LDA     tgt_col
        CMP     player_col
        BNE     @check_pass
        LDA     tgt_row
        CMP     player_row
        BNE     @check_pass
        ; --- Player overlap → bite ---
        LDA     monsters+MON_DMG,X
        STA     tmp
        LDA     hp
        SEC
        SBC     tmp
        BCS     @hp_ok
        LDA     #0
@hp_ok:
        STA     hp
        LDA     #1
        STA     player_hurt
        CLC                             ; counts as the monster's action
        RTS
@check_pass:
        ; In bounds (playable interior, edge frame is for the player only) ?
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
        ; Passable tile + no other monster + no food on this cell?
        ; Monsters can't trample food drops — that lets the player stake
        ; out a bait cell or back off to recover, and visually keeps the
        ; food sprite from being hidden under a monster sprite.
        TXA
        PHA                             ; save monster slot offset
        JSR     tile_at_target
        STA     tmp                     ; cache the tile id
        JSR     monster_at_target       ; clobbers X (carry: clear=hit)
        PLA
        TAX                             ; restore monster slot offset
        BCC     @blocked                ; another monster occupies this cell
        TXA
        PHA
        JSR     item_at_target          ; clobbers X (carry: clear=hit)
        PLA
        TAX
        BCC     @blocked                ; a food drop occupies this cell
        ; Doors are FORBIDDEN for monsters — they confine each undead
        ; to its spawn room (or to the corridor segment between two
        ; doors). The player can rest in another room knowing the
        ; pursuit stops at the threshold. Side-effect: monsters can
        ; never die on a door cell, so spawn_item never drops food on
        ; a door (no need to special-case the drop site).
        LDA     tmp
        CMP     #TILE_EMPTY
        BEQ     @move_ok
        CMP     #TILE_STAIRS_DOWN
        BEQ     @move_ok
@blocked:
        SEC
        RTS
@move_ok:
        LDA     tgt_col
        STA     monsters+MON_COL,X
        LDA     tgt_row
        STA     monsters+MON_ROW,X
        CLC
        RTS


; ----------------------------------------------------------------------------
; render_map: expand the 160-byte map_buffer into the name table at
; VRAM $1800. Each logical tile (1 byte = base char id) becomes a 2x2
; block of 4 chars (base+0=TL, base+1=TR, base+2=BL, base+3=BR).
; Auto-increment streams the data row-by-row: one logical row produces
; 32 TL/TR chars (name-table row N) followed by 32 BL/BR chars (row N+1).
;
; FOV gate: each cell consults vis_buffer[index] before emitting. A
; cell whose VIS_SEEN bit is clear writes 4 zeros (blank black) — so
; un-visited corridors stay black even after gen_dungeon paints them in
; the map_buffer. compute_fov is responsible for setting both bits as
; the player walks; clear_vis_buffer wipes everything on level change.
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

        ; Parallel walker into vis_buffer (map_ptr is dead during render).
        LDA     #<vis_buffer
        STA     map_ptr
        LDA     #>vis_buffer
        STA     map_ptr+1

        LDX     #LOGICAL_ROWS   ; logical rows remaining
@row_lp:
        ; --- TL / TR pass: 32 chars covering the upper half of all
        ;     16 tiles in the current logical row.
        LDY     #0
@tl_lp: LDA     (map_ptr),Y     ; visibility byte for this tile
        AND     #VIS_SEEN
        BEQ     @tl_dark        ; never seen -> 2 blank chars
        LDA     (vdp_src_lo),Y  ; tile base id
        WRT_DATA_REG            ; TL = base+0
        CLC
        ADC     #1
        WRT_DATA_REG            ; TR = base+1
        JMP     @tl_next
@tl_dark:
        LDA     #0              ; char 0 is fully blank in the tileset
        WRT_DATA_REG            ; TL = blank
        WRT_DATA_REG            ; TR = blank (A still 0)
@tl_next:
        INY
        CPY     #LOGICAL_COLS
        BNE     @tl_lp

        ; --- BL / BR pass: next 32 chars (auto-increment lands us on
        ;     name-table row N+1 = bottom half of the same tiles).
        LDY     #0
@bl_lp: LDA     (map_ptr),Y     ; visibility byte (same row of vis_buffer)
        AND     #VIS_SEEN
        BEQ     @bl_dark
        LDA     (vdp_src_lo),Y  ; tile base id
        CLC
        ADC     #2
        WRT_DATA_REG            ; BL = base+2
        CLC
        ADC     #1
        WRT_DATA_REG            ; BR = base+3
        JMP     @bl_next
@bl_dark:
        LDA     #0
        WRT_DATA_REG
        WRT_DATA_REG
@bl_next:
        INY
        CPY     #LOGICAL_COLS
        BNE     @bl_lp

        ; Advance both source + visibility pointers to the next logical
        ; row (they march in lock-step, 16 bytes per row).
        CLC
        LDA     vdp_src_lo
        ADC     #LOGICAL_COLS
        STA     vdp_src_lo
        BCC     @noinc_src
        INC     vdp_src_hi
@noinc_src:
        CLC
        LDA     map_ptr
        ADC     #LOGICAL_COLS
        STA     map_ptr
        BCC     @noinc_vis
        INC     map_ptr+1
@noinc_vis:
        DEX
        BNE     @row_lp
        RTS


; ----------------------------------------------------------------------------
; clear_vis_buffer: zero all 160 visibility bytes. Called by new_level
; (and once at start) so a fresh dungeon shows nothing until the player
; lights it up. Sentinel-BNE pattern: 160 = $A0 has bit 7 set so the
; naïve "LDX #159 / DEX / BPL" countdown exits after one iteration
; (the DEX result $9E still has bit 7 set, BPL never taken). Counting
; down with DEX-then-store-then-BNE writes indices 159..0 inclusive
; (160 bytes), exiting cleanly when X reaches 0 AFTER the final STA.
; ----------------------------------------------------------------------------
clear_vis_buffer:
        LDA     #0
        LDX     #(LOGICAL_COLS * LOGICAL_ROWS)
@lp:    DEX
        STA     vis_buffer,X
        BNE     @lp
        RTS


; ----------------------------------------------------------------------------
; compute_fov: re-flood the player's field of view. Three phases:
;
;   1. Strip every VIS_VISIBLE bit off vis_buffer (VIS_SEEN persists —
;      that's the "remembered terrain" memory).
;   2. Light the player's own cell (always visible, regardless of any
;      degenerate Bresenham that would skip it).
;   3. Cast Bresenham rays from the player to every cell on the
;      perimeter of a (2*FOV_RADIUS + 1) box centred on the player.
;      That's 8 * FOV_RADIUS rays (32 with R=4) — each ray points in a
;      genuinely distinct direction, so the rays fan out evenly around
;      the player and every cell within radius gets touched by at
;      least one. Targets that fall outside the grid are still passed
;      to cast_ray as-is — the OOB check inside cast_ray bails when
;      the ray walks off the playable area, which is fine since walls
;      always sit on rows 0/9 and cols 0/15 and stop the ray first.
;
; Targets can be NEGATIVE (8-bit signed, e.g. $FD = -3 when player_col
; = 1 and box-edge dx = -4); cast_ray's signed-diff math handles that.
;
; Cost: 32 rays × ≤ FOV_RADIUS steps × ~55 cycles/step ≈ 7 k cycles
; (~7 ms at 1 MHz). render_map adds another ~10 k. Comfortably under
; one frame, even allowing for the silicon-strict NOPs in WRT_DATA_REG.
; ----------------------------------------------------------------------------
compute_fov:
        ; --- Phase 1: wipe vis_buffer entirely. We deliberately drop
        ; the VIS_SEEN persistence — the only cells that should render
        ; this turn are the ones the rays will repaint in phase 2/3.
        ; Cells the player walked past last turn but are no longer in
        ; range plunge straight back into darkness, exactly the
        ; "torch always works" behaviour. The VIS_SEEN bit stays in
        ; the format so render_map / place_all_sprites can keep
        ; testing it without churn — phase 3 just OR's both bits
        ; together (VIS_BOTH) so VIS_SEEN tracks VIS_VISIBLE 1:1. ---
        ; Sentinel-BNE: 160 has bit 7 set, so "LDX #159 / DEX / BPL"
        ; exits after one iteration and the wipe never happens (every
        ; cell ever lit stays lit forever, "light through walls"). See
        ; clear_vis_buffer for the matching pattern.
        LDA     #0
        LDX     #(LOGICAL_COLS * LOGICAL_ROWS)
@clr_lp:
        DEX
        STA     vis_buffer,X
        BNE     @clr_lp

        ; --- Phase 2: light the player's cell (mark_visible_at_cur
        ; works off cur_x/cur_y, so seed those first). ---
        LDA     player_col
        STA     cur_x
        LDA     player_row
        STA     cur_y
        JSR     mark_visible_at_cur

        ; --- Phase 3: cast rays to the player's box-perimeter. ---
        ; Top edge: dy = -R, dx = -R..+R.
        LDA     player_row
        SEC
        SBC     #FOV_RADIUS
        STA     ray_endy
        LDA     player_col
        SEC
        SBC     #FOV_RADIUS
        STA     ray_endx
        LDX     #(2 * FOV_RADIUS + 1)
@top_lp:
        JSR     cast_ray
        INC     ray_endx
        DEX
        BNE     @top_lp

        ; Bottom edge: dy = +R, dx = -R..+R.
        LDA     player_row
        CLC
        ADC     #FOV_RADIUS
        STA     ray_endy
        LDA     player_col
        SEC
        SBC     #FOV_RADIUS
        STA     ray_endx
        LDX     #(2 * FOV_RADIUS + 1)
@bot_lp:
        JSR     cast_ray
        INC     ray_endx
        DEX
        BNE     @bot_lp

        ; Left edge: dx = -R, dy = -(R-1)..+(R-1) — corners already cast.
        LDA     player_col
        SEC
        SBC     #FOV_RADIUS
        STA     ray_endx
        LDA     player_row
        SEC
        SBC     #(FOV_RADIUS - 1)
        STA     ray_endy
        LDX     #(2 * FOV_RADIUS - 1)
@lft_lp:
        JSR     cast_ray
        INC     ray_endy
        DEX
        BNE     @lft_lp

        ; Right edge: dx = +R.
        LDA     player_col
        CLC
        ADC     #FOV_RADIUS
        STA     ray_endx
        LDA     player_row
        SEC
        SBC     #(FOV_RADIUS - 1)
        STA     ray_endy
        LDX     #(2 * FOV_RADIUS - 1)
@rgt_lp:
        JSR     cast_ray
        INC     ray_endy
        DEX
        BNE     @rgt_lp
        RTS


; ----------------------------------------------------------------------------
; cast_ray: walk a Bresenham line from (player_col, player_row) toward
; (ray_endx, ray_endy), marking each touched cell with VIS_BOTH. Stops
; on the first opaque tile (TILE_WALL — the wall itself gets marked) or
; after FOV_RADIUS steps, whichever comes first.
;
; Standard Wikipedia "all-cases" Bresenham, transposed to absolute
; deltas + sign bytes so the inner core stays unsigned. fov_err lives
; in signed 8-bit (range ≈ -16..+16 since LOGICAL_COLS=16, LOGICAL_ROWS
; =10 — well clear of overflow). The step countdown lives in fov_step
; (ZP) — early drafts used X for it, but compute_fov's per-edge outer
; loops also use X, and JSR cast_ray clobbered it: depending on which
; cell-by-cell path the ray took, X came back as anything from 0 to 4,
; turning the outer DEX/BNE into either an early exit or an infinite
; loop (= black screen on the unlucky seed).
;
; ray_endx / ray_endy are SIGNED 8-bit — compute_fov passes box-
; perimeter offsets that can land outside the grid (e.g. ray_endx = $FD
; = -3 when player_col = 1 and dx = -4). The abs/sign block below
; uses signed-difference + BPL/BMI sign-check so the slope is correct
; even with negative targets; the OOB guard in the step loop bails the
; instant cur_x or cur_y wraps past the grid.
;
; Two step decisions per iteration use the same fov_e2 = err << 1:
;   - x-step taken iff (e2 + abs_dy) >= 0   ↔ e2 >= -abs_dy
;   - y-step taken iff (abs_dx - e2) >= 0   ↔ e2 <=  abs_dx
; Both implemented as ADC/SBC + BMI-skip, which is non-strict (>= 0)
; — matches Wikipedia's reference and gives the canonical diagonal
; tie-break.
; ----------------------------------------------------------------------------
cast_ray:
        LDA     player_col
        STA     cur_x
        LDA     player_row
        STA     cur_y

        ; --- abs_dx, sx (signed-difference + sign-check) ---
        LDA     ray_endx
        SEC
        SBC     cur_x           ; signed result fits 8-bit (|.| ≤ 19)
        BMI     @neg_dx         ; bit 7 set -> negative -> sx = -1
        STA     abs_dx
        LDA     #$01
        STA     sx
        JMP     @done_dx
@neg_dx:
        EOR     #$FF
        CLC
        ADC     #1              ; A = -A (two's-complement negate)
        STA     abs_dx
        LDA     #$FF
        STA     sx
@done_dx:

        ; --- abs_dy, sy ---
        LDA     ray_endy
        SEC
        SBC     cur_y
        BMI     @neg_dy
        STA     abs_dy
        LDA     #$01
        STA     sy
        JMP     @done_dy
@neg_dy:
        EOR     #$FF
        CLC
        ADC     #1
        STA     abs_dy
        LDA     #$FF
        STA     sy
@done_dy:

        ; --- err = abs_dx - abs_dy (signed) ---
        LDA     abs_dx
        SEC
        SBC     abs_dy
        STA     fov_err

        LDA     #FOV_RADIUS
        STA     fov_step        ; step countdown in ZP — see header
@step_lp:
        ; --- e2 = err << 1 (signed; ASL preserves two's-complement
        ; for our small range) ---
        LDA     fov_err
        ASL     A
        STA     fov_e2

        ; --- x-step? if (e2 + abs_dy) >= 0 (BMI skips on negative) ---
        ; The opacity check happens BETWEEN the x-step and the y-step
        ; instead of after both. This kills the Bresenham corner-cut
        ; bug: when both x- and y-step fire in the same iteration the
        ; original code jumped diagonally over the orthogonal cell, so
        ; if THAT cell was a wall the ray slipped through the corner
        ; and lit up monsters/items inside the room behind. Now the
        ; intermediate orthogonal cell is marked + opacity-tested in
        ; sequence, so a wall on the diagonal path stops the ray.
        CLC
        ADC     abs_dy
        BMI     @no_xstep
        LDA     fov_err
        SEC
        SBC     abs_dy
        STA     fov_err
        LDA     cur_x
        CLC
        ADC     sx              ; sx is +1 or -1 ($FF)
        STA     cur_x
        ; OOB guard: cur_x wrapped past the grid (signed-wrap to $FF or
        ; ran past LOGICAL_COLS). Either way, ray dies here.
        CMP     #LOGICAL_COLS
        BCS     @done
        LDA     cur_y
        CMP     #LOGICAL_ROWS
        BCS     @done
        JSR     mark_visible_at_cur
        JSR     is_opaque_at_cur
        BNE     @done           ; orthogonal cell was a wall -> stop
@no_xstep:
        ; --- y-step? if (abs_dx - e2) >= 0 ---
        LDA     abs_dx
        SEC
        SBC     fov_e2
        BMI     @no_ystep
        LDA     fov_err
        CLC
        ADC     abs_dx
        STA     fov_err
        LDA     cur_y
        CLC
        ADC     sy
        STA     cur_y
        ; OOB guard.
        CMP     #LOGICAL_ROWS
        BCS     @done
        LDA     cur_x
        CMP     #LOGICAL_COLS
        BCS     @done
        JSR     mark_visible_at_cur
        JSR     is_opaque_at_cur
        BNE     @done
@no_ystep:
        DEC     fov_step
        BNE     @step_lp
@done:
        RTS


; ----------------------------------------------------------------------------
; mark_visible_at_cur: vis_buffer[cur_y * 16 + cur_x] |= VIS_BOTH.
; Caller has already verified cur_x / cur_y are in-range. Clobbers A,Y;
; preserves X (which cast_ray uses as the step countdown).
; ----------------------------------------------------------------------------
mark_visible_at_cur:
        LDA     cur_y
        ASL     A
        ASL     A
        ASL     A
        ASL     A               ; cur_y * 16
        CLC
        ADC     cur_x
        TAY
        LDA     vis_buffer,Y
        ORA     #VIS_BOTH
        STA     vis_buffer,Y
        RTS


; ----------------------------------------------------------------------------
; is_opaque_at_cur: returns A = 1 (Z clear) if map_buffer[cur_y * 16 +
; cur_x] holds an FOV-blocking tile, else A = 0 (Z set). Caller
; branches via BNE (opaque -> stop the ray) / BEQ (transparent ->
; continue). Clobbers A, Y; preserves X.
;
; Both walls AND doors block sight: a door is the threshold of a new
; room, and the whole point of "fog reactivates at each new room" is
; that you can't peek through a door from a corridor — the ray stops
; AT the door (the door itself gets marked visible by mark_visible_at_
; cur in the same step), and the room beyond stays dark until the
; player walks through. Doors stay PASSABLE for movement (handled in
; check_collision); FOV-opaque is purely a sight-line concept.
; ----------------------------------------------------------------------------
is_opaque_at_cur:
        LDA     cur_y
        ASL     A
        ASL     A
        ASL     A
        ASL     A
        CLC
        ADC     cur_x
        TAY
        LDA     map_buffer,Y
        CMP     #TILE_WALL
        BEQ     @opaque
        CMP     #TILE_DOOR
        BEQ     @opaque
        LDA     #0
        RTS
@opaque:
        LDA     #1
        RTS


; ----------------------------------------------------------------------------
; upload_sprite_pats: stream all 6 sprite patterns (player + 4 undead +
; food drop) as a single 192-byte block to VRAM $3800. The patterns
; sit back-to-back in source order (sprite_pats), so a single auto-
; increment loop covers slots 0/4/8/12/16/20 — names that the SAT colour
; byte indexes by (name & ~3) in 16x16 mode.
;   slot  0 ($3800-$381F) : player paladin
;   slot  4 ($3820-$383F) : undead         (MON_TYPE_UNDEAD)
;   slot  8 ($3840-$385F) : ghost          (MON_TYPE_GHOST)
;   slot 12 ($3860-$387F) : death          (MON_TYPE_DEATH)
;   slot 16 ($3880-$389F) : food meat      (ITEM_TYPE food drop)
;   slot 20 ($38A0-$38BF) : skeleton       (MON_TYPE_SKELETON)
; ----------------------------------------------------------------------------
upload_sprite_pats:
        LDA     #$00
        STA     VDP_CTRL
        NOP
        LDA     #$78            ; $38 | $40 -> write at $3800
        STA     VDP_CTRL
        LDX     #0
@lp:    LDA     sprite_pats,X
        WRT_DATA_REG
        INX
        CPX     #192
        BNE     @lp
        RTS


; ----------------------------------------------------------------------------
; clear_hurt_flags: zero player_hurt and every MON_HURT byte. Called at
; the top of each main_loop iteration so the red flash from last turn
; only persists until the next player action — by the time
; place_all_sprites runs for the new turn, it sees fresh hurt flags
; that this turn's combat has set (or not).
; ----------------------------------------------------------------------------
clear_hurt_flags:
        LDA     #0
        STA     player_hurt
        LDX     #0
@lp:    STA     monsters+MON_HURT,X
        TXA
        CLC
        ADC     #MON_SIZE
        TAX
        CPX     #(MON_COUNT * MON_SIZE)
        BCC     @lp
        RTS


; ----------------------------------------------------------------------------
; place_all_sprites: rewrite the entire Sprite Attribute Table at $1B00.
; Slot 0 is always the player (Y=row*16, X=col*16, name=0, COL_PLAYER).
; Slots 1..N are one entry per LIVE monster (MON_TYPE != 0), in pool
; order — each pulls Y/X from MON_ROW/MON_COL × 16 and (name, color)
; straight from the per-monster fields. The next-after-last slot gets
; Y=$D0, the chip's chain-terminator sentinel — every later SAT entry
; is ignored regardless of contents.
;
; Logical (lcol, lrow) → pixel (lcol*16, lrow*16) so a 16x16 sprite
; sits exactly on its 16x16 logical tile. We don't bother sub-pixel-
; centring monsters into the 4-rendered-quads tile (they ride the same
; cell grid as the player, which already aligns).
; ----------------------------------------------------------------------------
place_all_sprites:
        LDA     #$00
        STA     VDP_CTRL
        NOP                     ; +2c silicon-strict gap (LDA #imm bridge)
        LDA     #$5B            ; $1B | $40 -> write at $1B00
        STA     VDP_CTRL

        ; --- Slot 0: player ---
        LDA     player_row
        ASL
        ASL
        ASL
        ASL                     ; row * 16
        WRT_DATA_REG
        LDA     player_col
        ASL
        ASL
        ASL
        ASL                     ; col * 16
        WRT_DATA_REG
        WRT_DATA_VAL SPRITE_NAME_PLAYER
        ; Player colour: COL_HURT if hurt this frame, else COL_PLAYER.
        LDA     player_hurt
        BEQ     @p_normal
        LDA     #COL_HURT
        JMP     @p_write
@p_normal:
        LDA     #COL_PLAYER
@p_write:
        WRT_DATA_REG

        ; --- Slots 1..N: live monsters (one SAT entry per non-zero
        ;     MON_TYPE). FOV gate: a monster on a cell whose
        ;     VIS_VISIBLE bit is clear is dropped from the SAT entirely
        ;     — same effect as if the slot were dead this frame, no
        ;     halo/leak through fog. Monsters in seen-only cells stay
        ;     hidden too (Rogue convention: you remember the layout,
        ;     not the bestiary that walked through it).
        LDX     #0
@mon_lp:
        LDA     monsters+MON_TYPE,X
        BEQ     @mon_skip
        ; FOV gate: vis_buffer[row * 16 + col] & VIS_VISIBLE.
        LDA     monsters+MON_ROW,X
        ASL
        ASL
        ASL
        ASL                     ; row * 16
        CLC
        ADC     monsters+MON_COL,X
        TAY
        LDA     vis_buffer,Y
        AND     #VIS_VISIBLE
        BEQ     @mon_skip       ; not lit -> no SAT entry this frame
        LDA     monsters+MON_ROW,X
        ASL
        ASL
        ASL
        ASL
        WRT_DATA_REG
        LDA     monsters+MON_COL,X
        ASL
        ASL
        ASL
        ASL
        WRT_DATA_REG
        LDA     monsters+MON_NAME,X
        WRT_DATA_REG
        ; Monster colour: COL_HURT if MON_HURT,X set, else MON_COLOR,X.
        LDA     monsters+MON_HURT,X
        BEQ     @m_normal
        LDA     #COL_HURT
        JMP     @m_write
@m_normal:
        LDA     monsters+MON_COLOR,X
@m_write:
        WRT_DATA_REG
@mon_skip:
        TXA
        CLC
        ADC     #MON_SIZE
        TAX
        CPX     #(MON_COUNT * MON_SIZE)
        BCC     @mon_lp

        ; --- Slots after monsters: live food items (one entry per
        ; non-zero ITEM_TYPE). Items render LAST (highest SAT slot
        ; index) which gives them the LOWEST display priority on
        ; TMS9918 — so a player or monster sprite standing on the
        ; same cell covers the food, exactly the visual we want.
        ; Items also FOV-gate: a food drop sitting in a dark room is
        ; invisible until the player walks far enough to light its
        ; cell.
        LDX     #0
@item_lp:
        LDA     items+ITEM_TYPE,X
        BEQ     @item_skip
        ; FOV gate: vis_buffer[row * 16 + col] & VIS_VISIBLE.
        LDA     items+ITEM_ROW,X
        ASL
        ASL
        ASL
        ASL
        CLC
        ADC     items+ITEM_COL,X
        TAY
        LDA     vis_buffer,Y
        AND     #VIS_VISIBLE
        BEQ     @item_skip
        LDA     items+ITEM_ROW,X
        ASL
        ASL
        ASL
        ASL
        WRT_DATA_REG
        LDA     items+ITEM_COL,X
        ASL
        ASL
        ASL
        ASL
        WRT_DATA_REG
        WRT_DATA_VAL SPRITE_NAME_FOOD
        WRT_DATA_VAL COL_FOOD
@item_skip:
        TXA
        CLC
        ADC     #ITEM_SIZE
        TAX
        CPX     #(ITEM_COUNT * ITEM_SIZE)
        BCC     @item_lp

        ; --- Chain terminator: Y=$D0 ends sprite scanning ---
        LDA     #$D0
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
        ; TILE_STAIRS_UP intentionally OMITTED — stepping back up the
        ; way you came is forbidden.
        ; TODO (when HP system lands): instead of plain blocking,
        ;   "trying to go up" should subtract 1 HP and emit some
        ;   feedback — block, then deduct, so the player can poke at
        ;   the rubble at a cost. Keep the visual swap to bldg_rubble
        ;   in sync (see gen_big_room TODO comment).
        LDA     tmp
        CMP     #TILE_EMPTY
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
        WRT_DATA_REG
        INY
        BNE     @lp
@done:  RTS


; ----------------------------------------------------------------------------
; update_hud: paint a 32-char status line on name-table row 20
; (VRAM $1A80) — the HUD area below the 32x20 playfield. Layout:
;
;     "DEPTH NNN               HP HH/HM"
;       6 +  3  +     15      + 3 +  5
;     cols 0..8                cols 24..31
;
; DEPTH stays anchored on the left so the player sees it first reading
; left-to-right; HP rides on the right edge so the eye snaps to it
; after every action (the meaningful number per turn). 15 spaces in
; the middle keep the row at exactly 32 chars and visually separate
; the two stats. Repainted by finish_turn so HP digits stay live after
; monster attacks and food pickups.
; ----------------------------------------------------------------------------
update_hud:
        LDA     #$80                    ; row 20 col 0 = $1A80
        STA     VDP_CTRL
        NOP
        LDA     #$5A                    ; $1A | $40 -> write
        STA     VDP_CTRL
        ; --- "DEPTH " (6 chars) ---
        LDX     #0
@d_lp:  LDA     hud_depth,X
        WRT_DATA_REG
        INX
        CPX     #6
        BNE     @d_lp
        LDA     depth
        JSR     print_byte_3digits
        ; --- 15 spaces of right-padding (cols 9..23) ---
        LDA     #' '
        LDX     #15
@sp_lp: WRT_DATA_REG
        DEX
        BNE     @sp_lp
        ; --- "HP " (3 chars, cols 24..26) ---
        LDX     #0
@h_lp:  LDA     hud_hp,X
        WRT_DATA_REG
        INX
        CPX     #3
        BNE     @h_lp
        ; --- "HH/HM" (5 chars, cols 27..31) ---
        LDA     hp
        JSR     print_byte_2digits
        LDA     #'/'
        WRT_DATA_REG
        LDA     #HP_MAX
        JSR     print_byte_2digits
        RTS


; ----------------------------------------------------------------------------
; print_byte_3digits / print_byte_2digits: stream A as N zero-padded ASCII
; decimal digits to VDP_DATA (caller already armed the VRAM write address).
; The font-glyph chars at IDs 48..57 match ASCII '0'..'9' 1:1, so adding
; '0' to a 0..9 nibble yields the right char id directly.
; print_byte_3digits handles the hundreds digit then falls through into
; print_byte_2digits for the tens + units. Clobbers A, X.
; ----------------------------------------------------------------------------
print_byte_3digits:
        LDX     #0
@h:     CMP     #100
        BCC     @h_done
        SEC
        SBC     #100
        INX
        JMP     @h
@h_done:
        PHA                             ; save remainder for tens/units
        TXA
        CLC
        ADC     #'0'
        WRT_DATA_REG
        PLA
        ; fall through to print_byte_2digits
print_byte_2digits:
        LDX     #0
@t:     CMP     #10
        BCC     @t_done
        SEC
        SBC     #10
        INX
        JMP     @t
@t_done:
        PHA
        TXA
        CLC
        ADC     #'0'
        WRT_DATA_REG
        PLA
        ; Units (whatever's left in A is < 10).
        CLC
        ADC     #'0'
        WRT_DATA_REG
        RTS


; ----------------------------------------------------------------------------
; finish_turn: bookkeeping at the end of every accepted player action.
; Repaints the HUD (HP may have changed via monster attacks or food
; pickup) and tail-jumps to death_screen if HP hit 0. No starvation —
; the only way HP drops is a monster bump-attacking the player.
; ----------------------------------------------------------------------------
finish_turn:
        JSR     update_hud
        LDA     hp
        BNE     @alive
        JMP     death_screen            ; tail-call (never returns)
@alive:
        RTS


; ----------------------------------------------------------------------------
; death_screen: paint "YOU DIED ON LEVEL NNN" on the bottom HUD row,
; LEAVING the playfield and stats row visible — the player sees the
; frozen scene of their death (last sprite positions, HP 00/HM on the
; HUD) underneath the verdict. Then a ~1.3 s deaf-time before keys
; resume so the killing keypress (or held repeats) can't insta-restart,
; then wait for any key, then JMP $4000 (cartridge cold-start) to
; re-seed the PRNG and start over.
; ----------------------------------------------------------------------------
death_screen:
        ; "YOU DIED ON LEVEL " (18 chars) at row 22, col 5 -> $1AC5.
        ; Auto-increment lands the 3 depth digits at $1AD7-$1AD9 (col
        ; 23-25), total line spans cols 5..25 (21 chars, ~centred in
        ; the 32-wide row). Row 20 still holds the live HUD; row 21
        ; stays blank as a separator.
        LDA     #<msg_died
        STA     vdp_src_lo
        LDA     #>msg_died
        STA     vdp_src_hi
        LDA     #$C5
        LDX     #$5A                    ; $1A | $40
        JSR     draw_text
        LDA     depth
        JSR     print_byte_3digits

        ; --- Deaf time: triple-nested busy loop, ignores the keyboard
        ; for ~1.3 s at the 1.022 MHz CPU clock. Keeps the death scene
        ; on screen long enough to read AND swallows any in-flight key
        ; from the move that just killed the player (so the next press
        ; is an explicit "OK, restart"). mon_abs_dx is reused here as
        ; the outer counter — it's dead state on the death path
        ; (move_monsters never runs again).
        ;   inner  : INY/BNE = 5 c × 256 iters = 1280 c
        ;   middle : (1280 + INX/BNE) × 256 = ~329 k c
        ;   outer  : ~329 k × 4 = ~1.3 M c ≈ 1.3 s @ 1.022 MHz
        LDA     #4
        STA     mon_abs_dx
@d_outer:
        LDX     #0
@d_middle:
        LDY     #0
@d_inner:
        INY
        BNE     @d_inner
        INX
        BNE     @d_middle
        DEC     mon_abs_dx
        BNE     @d_outer

        ; Drain any latched key so a held key during the delay doesn't
        ; instantly fire wait_key (reading KBD clears the PIA strobe).
        LDA     KBD

        JSR     wait_key                ; now a fresh press acknowledges
        JMP     $4000                   ; cartridge cold-start


hud_depth:      .byte "DEPTH "                  ; 6 chars (left-anchored)
hud_hp:         .byte "HP "                     ; 3 chars (right-anchored;
                                                ;   no leading space — the
                                                ;   15-char gap lives in
                                                ;   update_hud directly)
msg_died:       .byte "YOU DIED ON LEVEL ", $FF


; ----------------------------------------------------------------------------
; draw_title: paint the title screen on the cleared name table. Each
; entry in title_table is a 4-byte tuple (str_lo, str_hi, vram_lo,
; vram_hi_with_write_bit) describing one centred line. The VRAM addr
; is precomputed as $1800 + row * 32 + col, then ORed with $40 (write
; bit) for draw_text's STX VDP_CTRL second-byte. tmp holds the table
; index across the JSR (draw_text leaves zp untouched but clobbers X).
; ----------------------------------------------------------------------------
draw_title:
        LDX     #0
@lp:    LDA     title_table+0,X         ; str_lo
        STA     vdp_src_lo
        LDA     title_table+1,X         ; str_hi
        STA     vdp_src_hi
        LDA     title_table+2,X         ; vram_lo
        PHA
        STX     tmp                     ; preserve loop index
        LDA     title_table+3,X         ; vram_hi (already | $40)
        TAX
        PLA                             ; A = vram_lo, X = vram_hi
        JSR     draw_text
        LDA     tmp
        CLC
        ADC     #4
        TAX
        CPX     #(title_table_end - title_table)
        BCC     @lp
        RTS

title_table:
        .byte   <title_rogue,     >title_rogue,     $8D, $58
        .byte   <title_subtitle,  >title_subtitle,  $E4, $58
        .byte   <title_author,    >title_author,    $27, $59
        .byte   <title_select_kb, >title_select_kb, $A8, $59
        .byte   <title_qwerty,    >title_qwerty,    $E8, $59
        .byte   <title_azerty,    >title_azerty,    $28, $5A
title_table_end:


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
        ; QWERTY → standard WASD: W=north, A=west, S=south, D=east.
        ; Matches the AZERTY ZQSD bindings below: each key sits at the
        ; same physical position on its respective layout.
        LDA     #('A' | $80)
        STA     key_west
        LDA     #('D' | $80)
        STA     key_east
        LDA     #('W' | $80)
        STA     key_north
        LDA     #('S' | $80)
        STA     key_south
        RTS
@azerty:
        ; AZERTY → ZQSD (= WASD shifted by the French keyboard's W↔Z
        ; and A↔Q swaps). Z=north, Q=west, S=south, D=east.
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
; The Quale font has no usable paren glyphs (its "lparen"/"rparen" labels
; are mis-extracted), so the keys ride the title line as bare letters.
; QWERTY = WASD (N/W/S/E mnemonic), AZERTY = ZQSD (= same physical keys
; on a French keyboard).
title_qwerty:
        .byte   "1 QWERTY WASD", $FF
title_azerty:
        .byte   "2 AZERTY ZQSD", $FF


; ============================================================================
; sprite_pats -- 4 sprite patterns (16x16, 32 B each = 128 B), uploaded
; as a single contiguous block to VRAM $3800 by upload_sprite_pats.
; Inlined here rather than .imported from the lib .asm files because
; pulling in the full 33-sprite character lib + 8-sprite undead lib
; would blow the 3 456 B low-bank CODE budget on the DRAM build.
;
;   Slot 0 / $3800 : char_paladin2_pat (player, lt-blue)
;     Source: dev/lib/tms9918/sprites_characters.asm, post-shift bytes
;     (tools/shift_characters_up.py rebalanced the 2-row blank top + 0
;     blank bottom to 1+1).
;
;   Slot 4 / $3820 : undead_undead_pat   (was Quale's "skull")
;   Slot 8 / $3840 : undead_ghost_pat
;   Slot 12 / $3860: undead_death_pat    (was Quale's "mummy")
;     Source: dev/lib/tms9918/sprites_unliving.asm — Quale's
;     SCROLL-O-SPRITES "The Unliving" row, CC-BY-3.0. Labels were
;     re-mapped (skull → UNDEAD as "first-tier undead", mummy → DEATH
;     as "strongest undead"); the lib's .export was updated to match.
;
;   Slot 16 / $3880: food_meat_pat (drumstick — dropped by dead monsters)
;     Source: dev/lib/tms9918/sprites_food_drink.asm — Quale's
;     SCROLL-O-SPRITES "Food & Drink" row, CC-BY-3.0.
;
;   Slot 20 / $38A0: undead_skeleton_pat (was Quale's "crossbones")
;     Source: dev/lib/tms9918/sprites_unliving.asm — second-tier
;     warrior undead. Sits AFTER the food slot in the sprite-pattern
;     table (food landed at slot 16 first chronologically); the
;     skip-by-name SAT addressing is unaffected by slot order.
; ============================================================================
sprite_pats:
char_paladin2_pat:                              ; slot 0 — player
        .byte $01, $07, $0F, $0F, $09, $08, $0E, $02
        .byte $7A, $68, $6B, $6B, $69, $6A, $34, $00
        .byte $82, $E7, $F0, $F2, $92, $12, $72, $72
        .byte $66, $16, $D8, $E2, $82, $62, $22, $00
undead_undead_pat:                              ; slot 4 — undead
        .byte $00, $00, $17, $0F, $09, $09, $0F, $2E
        .byte $0C, $04, $38, $57, $47, $07, $06, $02
        .byte $00, $02, $E0, $F0, $F0, $D0, $F0, $B0
        .byte $30, $22, $18, $D4, $E4, $E0, $60, $50
undead_ghost_pat:                               ; slot 8 — ghost
        .byte $00, $07, $0F, $1D, $1B, $5F, $7D, $7C
        .byte $3C, $1E, $1F, $1F, $0F, $03, $00, $00
        .byte $00, $E0, $F0, $F8, $B8, $DA, $7E, $3E
        .byte $3C, $B8, $F8, $F8, $FA, $FC, $00, $00
undead_death_pat:                               ; slot 12 — death
        .byte $00, $63, $77, $4F, $48, $51, $51, $56
        .byte $53, $53, $78, $3F, $5F, $47, $47, $4C
        .byte $00, $F0, $E0, $F0, $10, $88, $88, $68
        .byte $C8, $48, $10, $F8, $FC, $E4, $F0, $D8
                                                ; slot 16 — food (meat)
        .byte $00, $00, $00, $00, $00, $00, $03, $0F
        .byte $3B, $5E, $7E, $6C, $78, $31, $1E, $00
        .byte $00, $18, $1E, $1E, $30, $60, $C0, $60
        .byte $00, $40, $40, $80, $80, $00, $00, $00
undead_skeleton_pat:                            ; slot 20 — skeleton
        ; Bytes shifted up by one row vs. the lib's
        ; undead_skeleton_pat (the Quale extraction left a 2-row blank
        ; top and 0-row blank bottom, so the unshifted sprite floats
        ; one cell low). Same fix as char_paladin2_pat (see the
        ; tools/shift_characters_up.py docstring): each 16-row
        ; half-column is rotated by one — left[0..14] = old left[1..15],
        ; left[15] = 0; mirror for the right half. Only the inline copy
        ; here is shifted; the lib bytes stay raw for other consumers.
        .byte $00, $07, $0F, $0F, $09, $09, $0E, $03
        .byte $03, $18, $27, $31, $33, $04, $04, $00
        .byte $00, $E0, $F0, $F0, $90, $90, $70, $C0
        .byte $40, $18, $E4, $8C, $CC, $20, $20, $00

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
