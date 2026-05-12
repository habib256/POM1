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

        .import tms9918_pad12  ; silicon-strict pad12-v3 (helper from tms9918_pad.asm)
        .import tms9918_pad40  ; silicon-strict pad40 (hot upload loops)
        .import vdp_display_off ; lib helper — blank display for burst windows
.include "apple1.inc"
.include "tms9918.inc"

; --- Lib (tms9918m1.asm) ---
.import init_vdp_g1, clear_name_table, vdp_set_write, name_at_rc
.import disable_sprites
.importzp vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col
.import boss_sprite_pats        ; sprites_boss.asm — 4x16x16 boss tile

; --- Geometry: logical 16x10 tile grid in the top 20 char rows ---
; Sprite-mode colour indices (TMS9918 palette: 4 dk-blue, 5 lt-blue,
; 14 gray, 15 white). The hero sprite uses lt-blue so it stands out
; against the gray-on-black wall pattern.
COL_LT_BLUE    = 5
COL_PLAYER     = COL_LT_BLUE
LOGICAL_COLS   = 16
LOGICAL_ROWS   = 10
; Aliases consumed by lib/m6502/shadowcast.asm (declared before its
; .include at EOF; the lib references SHADOW_COLS / SHADOW_ROWS in its
; OOR check inside apply_octant).
SHADOW_COLS    = LOGICAL_COLS
SHADOW_ROWS    = LOGICAL_ROWS
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
PIT_DMG        = 3              ; HP lost on stepping into a TILE_TRAP_PIT.
                                ; Tuned to match a hard DEATH bite so a
                                ; pit + monster combo can kill an
                                ; unhealed player. Pits stay visible
                                ; after first hit — repeated steps
                                ; deal damage every time.
LAST_ATTACKER_PIT = 7           ; sentinel last_attacker value for pits;
                                ; mon_name_lo/hi[7] = "A PIT". Slot 6
                                ; is owned by MON_TYPE_BOSS (= "THE
                                ; DEMON" in the death-screen lookup).
; Per-buff durations after activation. Each tier is tuned to its role:
; the offensive buff is short (force aggressive timing), defense is
; medium (covers a couple of encounters), the ring is between the two
; (long enough for the regen to actually pulse), and the torch is the
; longest by far (exploration aid, spans most of a floor).
WEAPON_DURATION = 20            ; sword window — covers 1-2 fights at
                                ; depth 1 (3-monster pool ~6-9 turns)
                                ; or one prolonged encounter mid-game.
ARMOR_DURATION  = 30            ; tunic spans multiple combat windows
                                ; per floor — you can't "save" damage
                                ; already taken, so the buffer needs
                                ; to cover the trip between heals.
RING_DURATION   = 15            ; amulet REGEN — needs a few cycles
                                ; to actually heal (RING_REGEN_PERIOD
                                ; = 5, so 15 turns = up to 3 pulses).
TORCH_DURATION  = 50            ; exploration tool, not a fight
                                ; window. 50 turns ≈ a full floor's
                                ; worth of moves at the 14×8 interior,
                                ; so a torch carries you most of a
                                ; level if you light it on entry.
TORCH_RADIUS   = 7              ; FOV during a torch buff. Doubles the
                                ; default FOV_RADIUS=3 (3→7 covers
                                ; nearly the full 14×8 playable
                                ; interior — see one room edge to the
                                ; other plus most of an adjacent
                                ; corridor).

; --- Monster pool ------------------------------------------------------
; 16-slot pool at $E300 (high bank, after the 768-byte map_buffer).
; Per-slot layout (8 bytes):
;
;   +0 MON_TYPE   0=dead/empty, 1=undead, 2=ghost, 3=troll,
;                 4=skeleton, 5=death
;   +1 MON_HP     remaining hit points (0 → mark dead)
;   +2 MON_COL    logical 0..15
;   +3 MON_ROW    logical 0..9
;   +4 MON_NAME   sprite-name (4/8/12/20/48 → VRAM $3820/$3840/$3860/
;                 $38A0/$3980)
;   +5 MON_COLOR  TMS9918 palette index for the SAT colour byte
;   +6 MON_DMG    damage dealt to the player on a successful hit
;   +7 MON_HURT   non-zero if the sprite should render in COL_HURT this
;                 frame; for trolls, doubles as the persistent
;                 "provoked" flag (kept across turns by clear_hurt_flags
;                 so ai_troll switches from flee to chase).
;
; SAT budget worst case after the HUD dagger counter:
;   player + dagger HUD + 16 monsters + 8 floor items + 4 buff icons
;   + 1 thrown dagger = 31 live entries, leaving entry 31 for Y=$D0.
; The sharper HUD constraint is the TMS9918's 4-sprites-per-scanline
; limit: timer icons share y=176, so only four SAT entries that cover
; rows 22-23 are visible there. The dagger HUD icon lives at y=160
; (rows 20-21) to stay off that timer scanline group.
; Sprite-pattern slot 0 ($3800-$381F) is the player; slots 4/8/12 are
; the three undead types, all uploaded once at start.
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
; Numeric ids ARE the spawn-progression order: random rolls in
; spawn_monsters draw a type in [1..mon_type_pool], so monsters with
; lower ids appear earlier. Tier breakdown:
;   tier 1 (depth 1+)   ids 1..3 = UNDEAD, GHOST, TROLL
;     - UNDEAD: 1 HP / 1 dmg, greedy chase. The bread-and-butter mob.
;     - GHOST : 2 HP / 1 dmg, random walk. Soft early threat.
;     - TROLL : 3 HP / 1 dmg, flees. Loot piñata for daggers.
;   tier 2 (depth 5+)   id 4 = SKELETON
;     - 2 HP / 2 dmg, anti-greedy flank. First serious melee threat.
;   tier 3 (depth 10+)  id 5 = DEATH
;     - 3 HP / 2 dmg, sloth-chase. Late-game lethality spike.
MON_TYPE_UNDEAD   = 1
MON_TYPE_GHOST    = 2
MON_TYPE_TROLL    = 3           ; flees the player (ai_troll); shipped
                                ; in the depth-1 tier so the player
                                ; meets the dagger-target archetype
                                ; from the start.
MON_TYPE_SKELETON = 4           ; gated to depth 5+ — mon_type_pool
                                ; widens from 3 to 4 there.
MON_TYPE_DEATH    = 5           ; gated to depth 10+ — mon_type_pool
                                ; widens from 4 to 5 there. Latest
                                ; random-spawn tier so the player has
                                ; serious gear before facing 2-dmg/3-HP
                                ; brutes.
MON_TYPE_BOSS     = 6           ; depth-13 boss. Single instance,
                                ; spawned by gen_boss_room. 32x32
                                ; visual = 4 sprite slots tiled around
                                ; a single MON_ROW/MON_COL anchor;
                                ; logically occupies the 2x2 footprint
                                ; (anchor, anchor+E, anchor+S,
                                ; anchor+SE). Killing it triggers
                                ; win_screen.
SPRITE_NAME_PLAYER   = 0
SPRITE_NAME_UNDEAD   = 4
SPRITE_NAME_GHOST    = 8
SPRITE_NAME_DEATH    = 12
SPRITE_NAME_FOOD     = 16
SPRITE_NAME_SKELETON = 20
SPRITE_NAME_DAGGER   = 24       ; thrown projectile + on-floor inventory drop
SPRITE_NAME_POTION   = 28
SPRITE_NAME_SCROLL   = 32
SPRITE_NAME_WEAPON   = 36       ; generic sword for mace / sword / axe
SPRITE_NAME_ARMOR    = 40       ; generic chest plate for leather / chain / plate
SPRITE_NAME_RING     = 44
SPRITE_NAME_TROLL    = 48       ; troll grunt — flees the player
SPRITE_NAME_TORCH    = 52       ; consumable torch (FOV buff item)
SPRITE_NAME_BOSS_TL  = 56       ; depth-13 boss — top-left 16x16 tile
SPRITE_NAME_BOSS_TR  = 60       ; top-right
SPRITE_NAME_BOSS_BL  = 64       ; bottom-left
SPRITE_NAME_BOSS_BR  = 68       ; bottom-right

; TMS9918 palette indices used for monster sprites (0=transp, 2=med green,
; 7=cyan, 14=gray, 15=white, 10=dk yellow). UNDEAD stays bright white,
; GHOST gets the pale gray, DEATH ages into dark yellow, SKELETON rides
; on cyan to stand out as the warrior-class undead, TROLL gets med green
; for the classic D&D troll silhouette — all visible on the gray-on-black
; wall pattern.
MON_COL_UNDEAD   = 15
MON_COL_GHOST    = 14
MON_COL_DEATH    = 10
MON_COL_BOSS     = 8            ; medium red — boss demon silhouette
                                ; reads as "this thing wants you dead"
                                ; against the gray-on-black walls
MON_COL_SKELETON = 7
MON_COL_TROLL    = 2
COL_HURT       = 8              ; medium red — shared "took a hit" colour
                                ; for the player and any wounded monster
                                ; that survived its damage tick.
COL_FOOD       = 11             ; light yellow — food drumstick reads as
                                ; "edible" against the gray walls.
COL_DAGGER     = 14             ; medium gray — steel
COL_POTION     = 13             ; magenta — alchemy
COL_SCROLL     = 7              ; cyan — paper
COL_WEAPON     = 15             ; white — polished steel
COL_ARMOR      = 6              ; dark red — leather/iron
COL_RING       = 10             ; dark yellow — gold
COL_TORCH      = 9              ; light red / orange — flame.
                                ;   The TMS9918 palette has no pure
                                ;   orange; idx 9 is the warmest red
                                ;   that still reads as fire against
                                ;   the gray dungeon walls.

; --- World item pool (drops on the dungeon floor) ----------------------
; 8-slot pool at $E380, immediately after the monster pool ($E300-$E37F)
; — placing them contiguous lets one clear loop reset both at level
; transitions. Per-slot layout (4 bytes):
;
;   +0 ITEM_TYPE     0=empty, 1..7 = ITEM_T_* (see below)
;   +1 ITEM_COL      logical 0..15
;   +2 ITEM_ROW      logical 0..9
;   +3 ITEM_SUBTYPE  category-specific id (e.g. SUB_WEAPON_MACE=0,
;                    SUB_WEAPON_SWORD=1). Index into name + value tables.
;
; MVP3 only spawned food drops (post-kill). MVP4 also generates 1..3
; random typed items per level (spawn_level_items). Walking onto a
; cell auto-picks the item into the inventory pool (try_pickup_item).
ITEM_COUNT     = 8
ITEM_SIZE      = 4
ITEM_TYPE      = 0
ITEM_COL       = 1
ITEM_ROW       = 2
ITEM_SUBTYPE   = 3

; Item categories (shared with INV_TYPE). 0 = empty / dead slot.
ITEM_T_WEAPON  = 1
ITEM_T_ARMOR   = 2
ITEM_T_RING    = 3
ITEM_T_POTION  = 4
ITEM_T_SCROLL  = 5
ITEM_T_FOOD    = 6
ITEM_T_DAGGER  = 7              ; throwable stackable projectile
ITEM_T_TORCH   = 8              ; consumable FOV boost (TORCH_DURATION
                                ; turns of TORCH_RADIUS). No range
                                ; weapon / no equip — pure exploration
                                ; tool. Spawned by spawn_level_items.
ITEM_T_MAX     = 8

; Sub-type ids (per category). After the MVP4 simplification each
; category has a single sub-type that matches its on-screen sprite —
; the SCROLL-O-SPRITES inventory pictograms (sword / tunic / amulet /
; bottle / scroll / ration / dagger). Each sub-type has one entry in
; item_name_table + category value table, indexed by sub-type id.
SUB_WEAPON_SWORD  = 0           ; dmg 2
SUB_WEAPON_COUNT  = 1
SUB_ARMOR_TUNIC   = 0           ; def 2
SUB_ARMOR_COUNT   = 1
SUB_RING_AMULET   = 0           ; +1 HP every RING_REGEN_PERIOD turns
SUB_RING_COUNT    = 1
SUB_POT_HEAL      = 0           ; HP += 8 (cap)
SUB_POT_COUNT     = 1
SUB_SCROLL_MAP    = 0           ; reveal entire map (one-shot modal)
SUB_SCROLL_COUNT  = 1
SUB_FOOD_RATION   = 0           ; HP += FOOD_HEAL (3)
SUB_FOOD_COUNT    = 1
SUB_DAGGER_PLAIN  = 0           ; thrown dmg 2
SUB_DAGGER_COUNT  = 1
SUB_TORCH_PLAIN   = 0           ; FOV boost to TORCH_RADIUS
SUB_TORCH_COUNT   = 1

; Ring effect bit flag packed into ring_flags ZP. After MVP4 simplification
; the only ring is the regen amulet; the bit-mask form is kept because the
; AND test in finish_turn is identical regardless of how many flags exist.
RING_F_REGEN  = $01
RING_REGEN_PERIOD = 5           ; HP++ every 5 turns when amulet worn

; --- Inventory pool (the player's bag) ---------------------------------
; 26-slot pool at $E3A0..$E46F, one slot per letter a..z. Each slot is
; 8 bytes (matches MON_SIZE so a single SBC #INV_SIZE / ADC #INV_SIZE
; pattern is reusable). Per-slot layout:
;
;   +0 INV_TYPE     ITEM_T_* (0 = empty)
;   +1 INV_SUBTYPE  category-specific id
;   +2 INV_QTY      stack count (1 for unique items, ≥1 for
;                   daggers/food). Drops to 0 → slot wiped to empty.
;   +3 INV_VALUE    derived from sub-type lookup at pickup time
;                   (weapon dmg / armor def / heal / ring flag, etc.).
;                   Cached so the equip/throw paths don't re-look-up.
;   +4..7 reserved (future identification flags, charges, etc.)
;
; INV_COUNT == 26 because the on-screen letter is "the slot index".
; The free slot search is linear from 0 — first hole gets the new item.
; Active gear is no longer tracked by an equipped_* slot index — the
; buff-timer rework made every gear category (weapon / armor / ring /
; torch) a CONSUMABLE: activation drains the slot and starts a
; per-category ZP timer countdown. Durations live in the WEAPON_DURATION
; / ARMOR_DURATION / RING_DURATION / TORCH_DURATION constants; the
; cached boost value lives next to each timer.
INV_COUNT      = 26
INV_SIZE       = 8
INV_TYPE       = 0
INV_SUBTYPE    = 1
INV_QTY        = 2
INV_VALUE      = 3
INV_NONE       = $FF            ; sentinel for "no equipped slot"

; --- Procedural dungeon tuning (gen_dungeon) ---
N_ROOMS        = 2              ; "two-rooms" mode: first in left half,
                                ; second in right half, separated by a
                                ; ≥1-column gap so they never overlap.
                                ; The L-corridor between centres is the
                                ; only inter-room connection.
                                ; "big-room" mode generates a single
                                ; full-screen room with edge doors
                                ; (no corridor).

; --- Dense TILE_* ids (4 bits per cell in the bit-packed map_buffer) ---
; map_buffer stores 2 cells per byte (low nibble = even col, high nibble
; = odd col). Each nibble holds: bits 0..2 = tile id below, bit 3 =
; pit-reveal flag (only meaningful for TILE_TRAP_PIT). render_map's
; `tile_char_base[id]` LUT turns a dense id into a `CHAR_*` base from
; tileset_rogue.inc when streaming to the name table.
;
; TILE_CORR / TILE_CORR_DROP are transient markers used during corridor
; carving: carve_corridor_marker plants TILE_CORR on freshly-dug cells,
; finalize_doors pass 1 promotes boundary markers to TILE_DOOR and
; flags mid-corridor markers as TILE_CORR_DROP, pass 2 demotes the
; drops to TILE_EMPTY. Neither marker survives finalize_doors, but
; render_map maps both to CHAR_EMPTY anyway so a half-finalised buffer
; still draws cleanly.
TILE_EMPTY       = 0
TILE_WALL        = 1
TILE_DOOR        = 2
TILE_STAIRS_DOWN = 3
TILE_STAIRS_UP   = 4
TILE_TRAP_PIT    = 5
TILE_CORR        = 6
TILE_CORR_DROP   = 7
TILE_REVEAL_BIT  = $08          ; OR into nibble for revealed pit

; --- Field of view (compute_fov) ---------------------------------------
; vis_buffer is a bit-packed visibility map: 16x10 = 160 cells → 20
; bytes, one bit per cell. Layout: row R uses bytes 2R and 2R+1; bit
; (col & 7) of byte (R*2 + col>>3) is the cell's lit flag. compute_fov
; wipes the buffer entirely on every call and re-paints the lit cells —
; pure "torchlight" mode, no persistent memory of explored terrain.
; render_map renders cells whose bit is set (everything else is blacked
; out); place_all_sprites uses the same gate to drop monsters / items
; in dark cells from the SAT. The VIS_VISIBLE constant survives as the
; "any bit set" sentinel returned by vis_test_a — callers do BNE/BEQ
; on the helper's A return without caring about the specific mask.
;
; The earlier VIS_SEEN bit (persistent "remembered" terrain) was retired
; once compute_fov needed to wipe the buffer at every step for the
; door-crossing reset to work — VIS_SEEN was always equal to VIS_VISIBLE
; in practice, so the duplicate bit was removed.
;
; compute_fov uses Björn Bergström's recursive shadowcasting (RogueBasin
; 2002). The plane around the player is partitioned into 8 octants;
; each is processed by `cast_octant`, which scans rows of increasing
; depth and columns from outer (slope ≈ 1) to inner (slope = 0). For
; every cell it computes leftSlope = (2c+1)/(2d-1) and rightSlope =
; max(0, 2c-1)/(2d+1) and tests them against the live cone (start, end)
; via cross-multiplication (mul_4bit). On floor→wall transitions it
; recurses one row deeper with the cone narrowed to (start, leftSlope);
; the wall's rightSlope becomes the new start when the row crosses back
; to floor. Result: mathematically symmetric FOV with no slope
; artifacts and no coverage holes — the previous Bresenham pass
; occasionally let the player peek past a corner into the next room.
;
; FOV_RADIUS tuning: the playable interior is only 14 wide × 8 tall, so
; a radius ≥ 5 lights most of the grid from the centre and movement
; barely changes what's visible. Radius 3 keeps "torchlight in a dark
; dungeon" pacing — half a small room at any time. TORCH_RADIUS = 7
; doubles that during the torch buff.
;
; All slope numerators/denominators stay within {0..15} (max 2*7+1),
; so the cross-products fit in a single byte (max 15*15 = 225 < 256)
; and we use a tiny 4-bit×4-bit multiply (mul_4bit) instead of a full
; 8x8 → 16-bit routine.
FOV_RADIUS      = 3
VIS_VISIBLE     = $01
VIS_BUFFER_SIZE = 20            ; (LOGICAL_COLS * LOGICAL_ROWS + 7) / 8

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
                                ; Edge doors warp horizontally /
                                ; vertically into a sibling big-room
                                ; on the same floor.
trans_mode:     .res 1          ; level-transition kind (set by
                                ;   main_loop, consumed by new_level):
                                ;   0 = initial level    — random gen, no INC
                                ;   1 = descent (stairs) — random gen, depth++
                                ;   2 = exited E (col 15) — force big-room, spawn at W
                                ;   3 = exited W (col 0)  — force big-room, spawn at E
                                ;   4 = exited N (row 0)  — force big-room, spawn at S
                                ;   5 = exited S (row 9)  — force big-room, spawn at N
wrap_anchor:    .res 1          ; row (modes 2/3) or col (modes 4/5)
                                ; the player exited at — preserved as
                                ; the spawn anchor in the wrapped room.
boss_cheat:     .res 1          ; 0 = normal start at depth 1.
                                ; 1 = hidden code 'B' picked at title:
                                ; spawn directly in depth-13 boss room.
hp:             .res 1          ; current HP (0..HP_MAX). 0 = death.
player_hurt:    .res 1          ; non-zero if the player sprite should
                                ; render in COL_HURT this frame.
                                ; clear_hurt_flags resets it at the top
                                ; of every main_loop iteration; set by
                                ; step_monster_toward_player when a
                                ; monster lands its bump-attack.
last_attacker:  .res 1          ; MON_TYPE of the last monster that bit
                                ; the player. 0 = none yet. Surfaced as
                                ; "KILLED BY <NAME>" on the death screen.
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

; --- MVP4 inventory + equipment state ----------------------------------
; equipped_* hold an INV slot index (0..25) or INV_NONE ($FF) for "no
; item in this slot". player_dmg / player_def are recomputed each time
; an equip/remove happens (recompute_player_stats); apply_step (player
; → monster bump) reads player_dmg, the monster→player bite path reads
; player_def. ring_flags is an OR of RING_F_* bits — only RING_F_REGEN
; matters after MVP4 simplification, but the bit-mask form is preserved
; for cheap toggling. regen_tick counts down from RING_REGEN_PERIOD to 1
; each turn; on hitting 0 it adds +1 HP and reloads.
ring_flags:     .res 1
regen_tick:     .res 1
player_dmg:     .res 1          ; computed: weapon_value (or 1 bare-handed)
                                ; + xp_atk_bonus
player_def:     .res 1          ; computed: armor_value (or 0 unarmored)
                                ; + xp_def_bonus
player_xp:      .res 1          ; kill counter, +1 per slain monster
                                ; (saturates at 255). Painted on the
                                ; row-21 HUD as XP:NNN.

; --- XP-driven progression ---------------------------------------------
; Three countdowns gate the level-up effects. Each award_xp tick
; decrements all three; whichever hits 0 fires its bonus and reloads.
; That keeps the stats in sync with player_xp without per-frame
; division. Bonuses are additive on top of equipment values:
;   +1 HP_MAX every 5 kills (also +1 current HP — small heal-on-up)
;   +1 ATK    every 10 kills (in recompute_player_stats)
;   +1 DEF    every 20 kills (in recompute_player_stats)
; All three reset in init_inventory (cold-start covers death restart).
hp_max:         .res 1          ; runtime HP cap (replaces HP_MAX literal
                                ; in heal sites + regen + HUD). Init'd
                                ; to HP_MAX, grows with player_xp/5.
xp_atk_bonus:   .res 1          ; player_xp / 10, folded into player_dmg
xp_def_bonus:   .res 1          ; player_xp / 20, folded into player_def
hp_tick:        .res 1          ; reload 5  → +1 hp_max on rollover
atk_tick:       .res 1          ; reload 10 → +1 xp_atk_bonus on rollover
def_tick:       .res 1          ; reload 20 → +1 xp_def_bonus on rollover

; --- Timed buff state --------------------------------------------------
; Weapons / armors / rings / torches are CONSUMABLE buffs: using one
; wipes the slot and starts a per-category turn countdown (durations
; in WEAPON_DURATION / ARMOR_DURATION / RING_DURATION / TORCH_DURATION).
; While the timer is non-zero the boost stacks into player_dmg /
; player_def (above the XP base) or the FOV radius (torch).
; Reactivating the same kind replaces the timer (no stacking; latest
; activation wins). Each turn finish_turn ticks all four; on
; weapon/armor expiry it calls
; recompute_player_stats so the bonus drops cleanly.
weapon_timer:   .res 1          ; remaining turns of weapon buff (0 = bare)
weapon_boost:   .res 1          ; cached weapon INV_VALUE (= ATK boost)
armor_timer:    .res 1          ; remaining turns of armor buff
armor_boost:    .res 1          ; cached armor INV_VALUE (= DEF boost)
torch_timer:    .res 1          ; remaining turns of torch FOV boost
ring_timer:     .res 1          ; remaining turns of ring buff (REGEN
                                ; passive). Expiry clears ring_flags so
                                ; finish_turn's regen tick stops firing.
ring_boost:     .res 1          ; cached ring INV_VALUE (= RING_F_*
                                ; bit). On expiry we AND ring_flags
                                ; with ~ring_boost — keeps the path
                                ; future-proof if a second ring sub-
                                ; type lands and only one of two bits
                                ; should be cleared.
; --- Cached FOV radius for the current compute_fov call ---------------
; compute_fov picks fov_r at entry (FOV_RADIUS by default, TORCH_RADIUS
; while torch_timer > 0). cast_octant + apply_octant read it as the
; row-loop terminator.
fov_r:          .res 1

; --- Bit-packed vis_buffer scratch -------------------------------------
; vismask holds the bit mask while vis_test_a decomposes a linear
; (0..159) cell index into byte index + bit position. vis_pak_lo /
; vis_pak_hi are render_map's per-row 16-bit shift register: the row's
; two visibility bytes get loaded once per logical row, then the
; LSR/ROR pair extracts one cell-visibility bit per inner-loop
; iteration straight into the carry flag.
vismask:        .res 1
vis_pak_lo:     .res 1
vis_pak_hi:     .res 1

; --- Bit-packed map_buffer scratch -------------------------------------
; tmp_packed[0] caches the new-nibble argument across the
; read-modify-write inside map_set_x / set_tile / reveal_pit_at_target;
; tmp_packed[1] caches the surviving "other nibble" of the byte while
; the active nibble gets rebuilt. pak_byte holds render_map's current
; packed map byte while two cells are emitted from it.
tmp_packed:     .res 2
pak_byte:       .res 2          ; [0] = current packed byte; [1] = nibble
                                ; cache for emit_tl_cell / emit_bl_cell

; --- MVP4 thrown projectile ZP -----------------------------------------
; throw_active = 1 while a dagger is mid-flight: place_all_sprites
; appends one extra SAT entry (dagger sprite at cur_x/cur_y * 16) before
; writing the chain terminator. Cleared at the start of the game and
; after each throw resolves. throw_dx/dy are the per-step direction
; (-1, 0, +1 each — only cardinals), set by parse_direction.
throw_active:   .res 1
throw_dx:       .res 1
throw_dy:       .res 1
throw_dmg:      .res 1          ; cached damage of the dagger being
                                ; thrown — applied to the monster on
                                ; hit. Loaded from DAGGER_DMG constant
                                ; before consuming a charge from
                                ; dagger_qty (daggers no longer ride
                                ; the inventory pool, see below).
throw_step:     .res 1          ; remaining flight cells (0..THROW_RANGE).
                                ; Was previously aliased to fov_step from
                                ; the Bresenham FOV; the shadowcaster
                                ; doesn't need a step counter, so a
                                ; dedicated byte is clearer.

; --- FOV scratch (compute_fov / shadowcaster lib) ---------------------
; oct_*, cast_* and the cross-product scratch (cast_xprod) live in
; `lib/m6502/shadowcast.asm` — its `.ifndef cast_depth` guard allocates
; them inside this project's ZP segment when the file is .include'd at
; the bottom. cur_x/cur_y are pre-declared here (instead of letting the
; lib do it) so every site that uses them — including the dagger-throw
; animation, mark_visible_at_cur, and is_opaque_at_cur — sees them as
; ZP at assembly time and gets the short-form addressing. The lib's
; second .ifndef guard (`cur_x`) skips its own .res for them.
; The 4-bit×4-bit slope multiplies route through `umul4` (lib/m6502/
; multiply.asm), which owns its own mul_tmp / mul_res0.
cur_x:          .res 1
cur_y:          .res 1

; --- Dagger ammo counter ----------------------------------------------
; Daggers behave like a quiver, NOT like inventory items: they never
; take a bag letter, never appear in the inventory modal, and they
; have a dedicated HUD slot at the bottom-right of the buff-timer area
; (sprite + 2-digit count). Pickup increments dagger_qty, throw
; decrements it. Capped at 99 for the 2-digit display; extra pickups
; past the cap are silently dropped (matches "you can't carry any more
; daggers" feedback).
dagger_qty:     .res 1


; --- Map buffer (160 B, 16x10 logical tiles, one byte = tile base id) ---
; The 2-pass corridor algorithm (mark every dug cell as TILE_DOOR, then
; finalize_doors converts non-boundary markers back to TILE_EMPTY) means
; we don't need to remember room rects after carving — the marker tile
; itself is the bookkeeping.
.segment "MAPSEG"
; Bit-packed map_buffer: 2 cells per byte (low nibble = even col, high
; nibble = odd col), 16 cols × 10 rows = 160 cells → 80 bytes. Each
; nibble carries dense TILE_* (bits 0..2) + pit-reveal bit (bit 3).
; The map_get / set / reveal helpers in this file own the packing math.
map_buffer:     .res (LOGICAL_COLS * LOGICAL_ROWS) / 2
; Bit-packed parallel visibility buffer: one BIT per cell (16x10 = 160
; cells → 20 bytes). Layout: row R uses bytes 2R and 2R+1; bit (col & 7)
; of byte (R*2 + col>>3) is the cell's VIS_VISIBLE flag. The vis_test_a
; helper does the linear-index → byte+bit decomposition; render_map
; loads both row bytes into vis_pak_lo / vis_pak_hi and consumes them
; as a 16-bit shift register. clear_vis_buffer wipes it on every
; new_level.
vis_buffer:     .res 20

; --- Monster + item pools in unused high-bank RAM ----------------------
; Both pools live outside the linker-managed MAPSEG (which ends at
; $E064 after the 80-byte map_buffer + 20-byte vis_buffer); the rest
; of the high bank up to $EFFF is free RAM and we address it
; absolutely. Keeping the two pools contiguous (monsters then items)
; lets spawn_monsters wipe both with a single loop covering 160 B
; starting at `monsters`.
;
;   monsters  $E300-$E37F  16 slots × 8 B = 128 B
;   items     $E380-$E39F   8 slots × 4 B =  32 B
;   inventory $E3A0-$E46F  26 slots × 8 B = 208 B (a..z, INV_SIZE each)
monsters  = $E300
items     = $E380
inventory = $E3A0


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

        JSR init_vdp_g1         ; 8 regs + defensive SAT init via lib
                                ; disable_sprites (full 128-byte $D0+$D1 fill,
                                ; mirrors tms9918m2 — May 2026 symmetrization)
        ; init_vdp_g1 exits with R1 = $C0 (display ON). Blank again for the
        ; upload phase — masks the visible noise frame on real silicon AND
        ; widens CPU↔VDP slots to the 2c ScreenOff gate.
        JSR     vdp_display_off ; lib helper (tms9918_pad.asm)

        ; All VRAM uploads happen with display OFF — masks the visible noise
        ; frame AND gives the VDP its widest CPU access window.
        JSR upload_tileset      ; 2048 B pattern table -> VRAM $0000
        JSR upload_colour_table ; 32 B colour table   -> VRAM $2000
        JSR upload_sprite_pats  ; 576 B sprite patterns -> VRAM $3800
                                ; (moved up from the late init slot; sprite
                                ;  patterns are level-independent for MVP)
        JSR clear_name_table    ; whole 32x24 -> char 0 (black). Display
                                ; still OFF, so the silicon's CPU slot is
                                ; the fast 2c gate for all 768 writes.

        ; Display ON + sprite 16x16 mode. Boot frame now lands on a fully
        ; populated VDP state — no garbage flash even at NMOS phase corners.
        JSR override_r1_16x16

        JSR draw_title          ; ROGUE banner + key-layout prompt
        JSR wait_kb_choice      ; bind keys + seed prng_lo/hi from key timing

        JSR clear_name_table    ; second title page = mission briefing
        JSR draw_briefing       ; lore + mechanics primer
        LDA KBD                 ; drain layout-choice strobe ('1' / '2')
        JSR wait_key            ; explicit ack before the dungeon appears

        LDA #1                  ; first level — new_level INCs from here
        STA depth
        LDA #0                  ; trans_mode 0 = initial random gen, no wrap
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
        STA last_attacker       ; no killer yet → death screen renders
                                ; "KILLED BY UNKNOWN" if hp ever hits 0
                                ; before any monster lands a bite.
        JSR init_inventory      ; wipes the 26-slot bag + clears equipped_*
                                ; + zeros ring_flags
                                ; + recomputes derived player_dmg/def

        JSR clear_name_table    ; wipe title before the game view appears
        JSR gen_dungeon         ; procedural rooms + corridors + stairs;
                                ; sets player_col/row to first room centre.
        JSR clear_vis_buffer    ; fresh dungeon -> nothing remembered yet
        JSR compute_fov         ; light up the player's spawn neighbourhood
        JSR render_map          ; expand 16x10 logical map to 32x20 char block
                                ; (now FOV-gated: dark cells render blank)

        ; upload_sprite_pats moved to the display-OFF init window — sprite
        ; patterns are level-independent, no per-level re-upload needed.
        JSR spawn_monsters      ; populate the pool for the first level
        JSR spawn_level_items   ; 1..3 typed items scattered on TILE_EMPTY
        JSR spawn_level_pits    ; 0..2 pits scattered on TILE_EMPTY
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR place_all_sprites   ; SAT: player slot 0 + each live monster
        JSR update_hud          ; HUD rows 20..23: stats + timers/ammo

main_loop:
        ; Reset hurt flags so any flash from last turn doesn't leak
        ; into THIS turn's place_all_sprites repaint. The SAT still
        ; holds the previous frame's red-flash colour bytes until the
        ; next place_all_sprites overwrites them, so the flash is
        ; visible during wait_key.
        JSR clear_hurt_flags
        JSR wait_key            ; A = raw key (high bit still set)
        ; --- Commands (each via BNE-skip-then-JMP trampoline; handlers
        ; sit past 127 bytes from this dispatch point). Item use happens
        ; from the inventory modal (I + slot letter); E was removed as a
        ; redundant playfield prompt.
        CMP     #('I' | $80)
        BNE     @nx_i
        JMP     @do_inv
@nx_i:  CMP     #('T' | $80)
        BNE     @nx_t
        JMP     @do_throw
@nx_t:  CMP     #('?' | $80)
        BEQ     @help_jmp       ; both keys land on the same trampoline
        CMP     #('H' | $80)    ; H is free in both layouts (WASD / ZQSD)
        BNE     @nx_h
@help_jmp:
        JMP     @do_help
@nx_h:  CMP     #('.' | $80)    ; rest one turn (regen tick, no movement)
        BNE     @nx_dot
        JMP     @do_rest
@nx_dot:
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
        BNE @nx_descent
        JMP @do_descent
@nx_descent:
        ; Edge-door classifiers: BNE-trampoline pattern (BEQ targets are
        ; >127 B forward now that MVP4 grew main_loop with the I/W/R
        ; commands). Each BNE jumps over a JMP that covers the long range.
        LDA tgt_col
        BNE @w_skip             ; col 0
        JMP @exit_w
@w_skip:
        CMP #15
        BNE @e_skip             ; col 15
        JMP @exit_e
@e_skip:
        LDA tgt_row
        BNE @n_skip             ; row 0
        JMP @exit_n
@n_skip:
        CMP #9
        BNE @s_skip             ; row 9
        JMP @exit_s
@s_skip:
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
        LDA tmp                 ; pit trap fires every step (no consume)
        CMP #TILE_TRAP_PIT
        BNE @no_pit
        JSR trigger_pit
@no_pit:
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
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR place_all_sprites
        JSR finish_turn
        JMP main_loop

@do_inv:
        JSR show_inventory      ; A=1 if the player tapped a letter that
                                ; consumed a food/potion/scroll, A=0 if
                                ; they just dismissed (or toggled equip)
        TAX
        BNE @inv_turn
        JMP main_loop           ; free action — no monster turn
@inv_turn:
        JSR move_monsters
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR place_all_sprites
        JSR finish_turn
        JMP main_loop

@do_throw:
        JSR handle_throw        ; A=1 → turn consumed, A=0 → free / error
        TAX
        BNE @throw_turn
        JMP main_loop
@throw_turn:
        JSR move_monsters
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR place_all_sprites
        JSR finish_turn
        JMP main_loop

@do_help:
        JSR show_help           ; modal — always a free action
        JMP main_loop

@do_rest:
        ; '.' rest — burn one turn without moving. Monsters take their
        ; turn, the regen amulet pulses, all buff timers tick down. Same
        ; cleanup chain as a regular move minus compute_fov / render_map
        ; (player didn't move, so visibility is unchanged).
        JSR move_monsters
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR place_all_sprites
        JSR finish_turn
        JMP main_loop

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
;   trans_mode = 0           : initial random gen (big-room or two-rooms),
;                              no depth change.
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
        ; Depth only advances on stairs-down descent. Reaching depth 13
        ; routes into the boss room — a fixed big-room layout with one
        ; MON_TYPE_BOSS instance (the 32x32 demon). Edge-door wraps and
        ; non-stairs paths skip the depth check entirely.
        LDA trans_mode
        CMP #1
        BNE @no_inc
        INC depth
        LDA depth
        CMP #13
        BCC @no_inc
        ; depth >= 13 → boss room takeover.
        JMP @boss
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
        JSR spawn_level_items   ; fresh items scattered for this floor
        JSR spawn_level_pits    ; 0..2 fresh pits for this floor
        JMP @repaint
@boss:
        ; Boss room: fixed big-room geometry, no doors / no stairs / no
        ; items / no pits / no random monster pool. gen_boss_room hand-
        ; carves the arena and stamps the player spawn. spawn_boss then
        ; writes the single MON_TYPE_BOSS entry into monsters[0]; the
        ; usual spawn_monsters wipe path is bypassed so its monster-
        ; pool wipe doesn't clobber the boss the moment it lands.
        JSR fill_with_walls
        JSR gen_boss_room
        JSR spawn_boss
@repaint:
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
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #$81            ; $01 | $80 -> register 1
        STA     VDP_CTRL
        RTS


; clear_sat_y removed May 2026 — the defensive SAT.Y init is now part of
; tms9918m1.asm::disable_sprites (called internally by init_vdp_g1). Same
; $D0+127×$D1 pattern, in lib so all Mode I consumers benefit (Rogue, Chess,
; Snake, OrbitalPool). See doc/TMS9918-SPRITE_INIT.md §4.2.


; ----------------------------------------------------------------------------
; upload_tileset: stream tileset_rogue (2048 bytes, 256 chars * 8) into
; the pattern table at VRAM $0000. Auto-increment write — one big block.
;
; Real-silicon adaptation (May 2026): the inner write loop uses pad40
; instead of the macro's pad12 to satisfy the hardened 40c slot contract
; documented in dev/lib/tms9918/tms9918_pad.asm. Caller runs this with
; display OFF so the slot is the wide 2c gate, but the pad40 gives us
; headroom even if a future caller forgets the OFF discipline.
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
        STA     VDP_DATA
        JSR     tms9918_pad40   ; 40c silicon-strict gap (was WRT_DATA_REG/pad12)
        INY
        BNE     @byte
        INC     vdp_src_hi
        DEX
        BNE     @page
        RTS


; ----------------------------------------------------------------------------
; upload_colour_table: stream 32 colour bytes to VRAM $2000. Inner loop
; uses pad40 — same rationale as upload_tileset.
; ----------------------------------------------------------------------------
upload_colour_table:
        LDA     #$00
        STA     vdp_lo
        LDA     #$20
        STA     vdp_hi
        JSR     vdp_set_write   ; addr = $2000

        LDX     #0
@lp:    LDA     tileset_color_table,X
        STA     VDP_DATA
        JSR     tms9918_pad40   ; 40c silicon-strict gap (was WRT_DATA_REG/pad12)
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
; gen_boss_room: depth-13 arena. Same big-room geometry as gen_big_room
; (cols 1..14 × rows 1..8) but stripped of stairs / perimeter doors —
; the player is committed once they descend, the only way out is to
; kill the boss (or die). Boss anchor at (7, 4) so its 32x32 visual
; footprint covers cells (7,4)/(8,4)/(7,5)/(8,5); player spawns at
; (7, 8) directly south of the boss with a clear approach.
; ----------------------------------------------------------------------------
gen_boss_room:
        LDA     #1
        STA     room_x
        STA     room_y
        LDA     #14
        STA     room_w
        LDA     #8
        STA     room_h
        JSR     carve_room

        ; Player spawn — south of the boss, walking distance to engage.
        LDA     #7
        STA     player_col
        LDA     #8
        STA     player_row
        RTS


; ----------------------------------------------------------------------------
; spawn_boss: write the single MON_TYPE_BOSS instance into monsters[0]
; and zero the rest of the monster + item pools (same wipe spawn_monsters
; uses, but no random-spawn loop afterwards). HP = mon_init_hp[BOSS] +
; mon_hp_bonus (depth / 3); DMG = mon_init_dmg[BOSS] + mon_dmg_bonus
; (depth / 6). At depth 13 that lands the boss at 19 HP / 6 dmg, well
; clear of normal-tier endgame monsters.
;
; Anchor at (7, 4): the place_all_sprites boss path tiles three more
; SAT entries at +16 px on each axis so the 32x32 visual covers cells
; (7,4)/(8,4)/(7,5)/(8,5).
; ----------------------------------------------------------------------------
spawn_boss:
        ; Wipe the monster + item pools (160 B starting at `monsters`)
        ; so a previous level's leftovers don't render alongside the
        ; boss. Same loop spawn_monsters uses at the top of every
        ; level — keep it inline so the boss path doesn't drag in the
        ; whole random-spawn machinery.
        LDA     #0
        LDX     #(MON_COUNT * MON_SIZE) + (ITEM_COUNT * ITEM_SIZE)
@wipe_lp:
        DEX
        STA     monsters,X
        BNE     @wipe_lp
        STA     monsters,X              ; final write at offset 0

        ; Compute the depth-scaled HP / DMG bonuses (same shape as
        ; spawn_monsters): /3 for HP, /6 for DMG. Subtract-3 loop
        ; tracks the quotient in X; mon_dmg_bonus is just X >> 1.
        LDA     depth
        LDX     #0
@hp_q:  CMP     #3
        BCC     @hp_q_done
        SEC
        SBC     #3
        INX
        JMP     @hp_q
@hp_q_done:
        STX     mon_hp_bonus            ; depth / 3
        TXA
        LSR                             ; depth / 6
        STA     mon_dmg_bonus

        ; Stamp monsters[0] = MON_TYPE_BOSS at (col=7, row=4).
        LDX     #0
        LDA     #MON_TYPE_BOSS
        STA     monsters+MON_TYPE,X
        ; HP = mon_init_hp[6] + mon_hp_bonus
        LDY     #MON_TYPE_BOSS
        LDA     mon_init_hp,Y
        CLC
        ADC     mon_hp_bonus
        STA     monsters+MON_HP,X
        ; DMG = mon_init_dmg[6] + mon_dmg_bonus
        LDA     mon_init_dmg,Y
        CLC
        ADC     mon_dmg_bonus
        STA     monsters+MON_DMG,X
        ; Sprite name = TL slot (the other three quadrants are
        ; emitted by place_all_sprites at +16 px offsets).
        LDA     mon_init_name,Y
        STA     monsters+MON_NAME,X
        LDA     mon_init_color,Y
        STA     monsters+MON_COLOR,X
        ; Anchor (col, row) = (7, 4).
        LDA     #7
        STA     monsters+MON_COL,X
        LDA     #4
        STA     monsters+MON_ROW,X
        LDA     #0
        STA     monsters+MON_HURT,X
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
        TXA
        JSR     map_get_a               ; A = nibble; X preserved
        AND     #7                      ; strip reveal bit (none here, but cheap)
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
        JSR     map_set_x
        JMP     @p1_next
@p1_keep:
        ; Boundary cell — promote marker to a real door.
        LDA     #TILE_DOOR
        JSR     map_set_x
@p1_next:
        TXA
        BNE     @p1

        ; ---- Pass 2: convert flagged drops to TILE_EMPTY ----
        LDX     #LOGICAL_COLS * LOGICAL_ROWS
@p2:
        DEX
        TXA
        JSR     map_get_a
        AND     #7
        CMP     #TILE_CORR_DROP
        BNE     @p2_next
        LDA     #TILE_EMPTY
        JSR     map_set_x
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
        TXA
        JSR     map_get_a
        AND     #7
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
        JSR     map_set_x
@p3_next:
        TXA
        BNE     @p3
        RTS


; ----------------------------------------------------------------------------
; fill_with_walls: stamp TILE_WALL onto every cell of the bit-packed
; map_buffer. Each byte holds 2 cells, so we write
; (TILE_WALL << 4) | TILE_WALL into all 80 bytes — one byte = both
; nibbles set to TILE_WALL. Sentinel-BNE: LDX #80, DEX-then-store,
; exit when X reaches 0 after writing index 0.
; ----------------------------------------------------------------------------
fill_with_walls:
        LDA     #(TILE_WALL << 4) | TILE_WALL
        LDX     #80
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


; rand_mod -- uniform [0, A) pseudorandom — promoted to lib/m6502/dungeon.asm
; (.include'd at EOF). It depends on prng16, hence the include order at the
; bottom of the file (prng16 first, dungeon next).


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
; tile_char_base: dense TILE_* (0..7) → CHAR_* base id used by render_map.
; Slots 6/7 (TILE_CORR / TILE_CORR_DROP) are transient markers and
; should never reach render_map after finalize_doors; we still map them
; to CHAR_EMPTY so a half-finalised buffer draws cleanly instead of
; flashing garbage chars.
; ----------------------------------------------------------------------------
tile_char_base:
        .byte CHAR_EMPTY        ; 0 TILE_EMPTY
        .byte CHAR_WALL         ; 1 TILE_WALL
        .byte CHAR_DOOR         ; 2 TILE_DOOR
        .byte CHAR_STAIRS_DOWN  ; 3 TILE_STAIRS_DOWN
        .byte CHAR_STAIRS_UP    ; 4 TILE_STAIRS_UP
        .byte CHAR_TRAP_PIT     ; 5 TILE_TRAP_PIT
        .byte CHAR_EMPTY        ; 6 TILE_CORR    (transient → blank)
        .byte CHAR_EMPTY        ; 7 TILE_CORR_DROP (transient → blank)


; ----------------------------------------------------------------------------
; map_get_a: read the packed cell nibble at linear cell index A (0..159).
; Returns A = nibble (0..15): bits 0..2 = dense tile id, bit 3 = pit
; reveal flag. Caller does `AND #7` to drop the reveal bit when checking
; tile identity. Clobbers Y, tmp_packed; preserves X.
;
; Decomposition: byte = idx >> 1; nibble select = idx & 1 (low for even
; col, high for odd col).
; ----------------------------------------------------------------------------
map_get_a:
        LSR                     ; A = byte index, carry = old idx & 1
        TAY
        LDA     map_buffer,Y
        BCS     @hi
        AND     #$0F
        RTS
@hi:
        LSR
        LSR
        LSR
        LSR
        RTS


; ----------------------------------------------------------------------------
; map_set_x: write nibble A (0..15) to packed cell at linear index X.
; Preserves X. Clobbers A, Y, tmp_packed.
;
; Read-modify-write: keep the surviving nibble of the byte and merge
; the new one in.
; ----------------------------------------------------------------------------
map_set_x:
        AND     #$0F            ; sanitize new nibble
        STA     tmp_packed      ; new nibble in low 4 bits
        TXA
        LSR                     ; A = byte index, carry = X & 1
        TAY
        LDA     map_buffer,Y
        BCS     @set_hi
        AND     #$F0            ; keep high nibble, clear low
        ORA     tmp_packed      ; merge new low nibble
        STA     map_buffer,Y
        RTS
@set_hi:
        AND     #$0F            ; keep low nibble, clear high
        STA     tmp_packed+1
        LDA     tmp_packed
        ASL
        ASL
        ASL
        ASL                     ; new nibble shifted to bits 4..7
        ORA     tmp_packed+1
        STA     map_buffer,Y
        RTS


; ----------------------------------------------------------------------------
; set_tile: write A as a dense tile id (0..15) to map_buffer at
; (tgt_col, tgt_row). Preserves X — place_perimeter_doors and other
; loop-driven callers (`JSR set_tile ; preserves X`) lean on this.
; Clobbers A, Y, tmp_packed.
;
; Byte index = tgt_row * 8 + tgt_col / 2; nibble select = tgt_col & 1.
; ----------------------------------------------------------------------------
set_tile:
        AND     #$0F                    ; sanitize new nibble
        STA     tmp_packed
        LDA     tgt_col
        LSR                             ; A = tgt_col / 2 (carry discarded)
        STA     tmp_packed+1
        LDA     tgt_row
        ASL
        ASL
        ASL                             ; row * 8
        CLC
        ADC     tmp_packed+1            ; + col / 2
        TAY                             ; Y = byte index
        LDA     tgt_col
        AND     #1
        BNE     @set_hi
        ; even col → low nibble
        LDA     map_buffer,Y
        AND     #$F0                    ; keep high nibble
        ORA     tmp_packed              ; merge new low nibble
        STA     map_buffer,Y
        RTS
@set_hi:
        ; odd col → high nibble
        LDA     map_buffer,Y
        AND     #$0F                    ; keep low nibble
        STA     tmp_packed+1
        LDA     tmp_packed
        ASL
        ASL
        ASL
        ASL                             ; new nibble shifted to bits 4..7
        ORA     tmp_packed+1
        STA     map_buffer,Y
        RTS


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

        ; --- mon_type_pool: 3 (depth 1..4) → 4 (depth 5..9) → 5 (depth 10+).
        ; Two step-ups instead of the old per-depth ramp: the player
        ; meets UNDEAD/GHOST/TROLL from the first floor (full AI variety
        ; out of the gate), SKELETON joins at depth 5 (first 2-dmg
        ; melee threat), DEATH waits for depth 10 (3-HP / 2-dmg sloth
        ; chase, the lethality spike). ---
        LDA     depth
        CMP     #10
        BCC     @lt10
        LDA     #5
        JMP     @pool_done
@lt10:  CMP     #5
        BCC     @lt5
        LDA     #4
        JMP     @pool_done
@lt5:   LDA     #3
@pool_done:
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
; spawn_level_items: scatter 1..3 typed items across the freshly-carved
; map. Called from start: + new_level after spawn_monsters has wiped
; the world item pool. Each spawn:
;   1. find_empty_cell (TILE_EMPTY, no monster, no other item, ≠ player)
;   2. roll a type via cumulative thresholds in `level_item_thresh`
;   3. roll a subtype uniformly in [0, level_item_subc[type])
;   4. spawn_typed_item — silently lost if the world pool is full
;      (8 simultaneous slots; a level rarely fills it from gen alone).
;
; Thresholds use a 32-entry roll (matches the LFSR's natural granularity
; better than 100 and lets rand_mod #32 short-circuit after one iteration
; for 0..31 results). Weights (post buff-timer rework):
;   food 19%, dagger 25%, potion 9%, scroll 6%, weapon 13%, armor 9%,
;   ring 6%, torch 13%.
; Weapons + armours bumped (8→13%, 7→9%) since they're now CONSUMABLE
; on use (10-turn buff) — must drop more often or the player runs out
; of buffs after one floor. Torches added at 6% to seed exploration.
; ----------------------------------------------------------------------------
spawn_level_items:
        LDA     #3
        JSR     rand_mod
        CLC
        ADC     #1                      ; 1..3
        TAX                             ; X = remaining count
@outer:
        TXA
        PHA                             ; save count across find/spawn
        JSR     find_empty_cell
        BCS     @done_pop
        ; Roll a type via the threshold table (32-roll).
        LDA     #32
        JSR     rand_mod
        LDX     #0
@thr_lp:
        CMP     level_item_thresh,X
        BCC     @thr_hit
        INX
        JMP     @thr_lp
@thr_hit:
        LDA     level_item_type,X
        STA     map_ptr+1               ; cache type across rand_mod
        LDA     level_item_subc,X
        JSR     rand_mod                ; A = subtype 0..count-1
        TAY
        LDA     map_ptr+1               ; A = type
        JSR     spawn_typed_item        ; (tgt_col, tgt_row) → world slot
        PLA                             ; restore remaining count
        TAX
        DEX
        BNE     @outer
        RTS
@done_pop:
        PLA                             ; discard count (find_empty_cell failed)
        RTS

; Per-category tables — parallel arrays. Index = category id.
level_item_thresh:
        ; cumulative roll thresholds; first index whose threshold > roll
        ; wins. Last entry must equal the rand_mod modulus (32). Per-
        ; category mass (out of 32):
        ;   FOOD 6  DAGGER 8  POTION 3  SCROLL 2
        ;   WEAPON 4  ARMOR 3  RING 2  TORCH 4
        ; Daggers were boosted from 5 → 8 now that they are quiver ammo
        ; with their own HUD counter, not inventory clutter. Food, scrolls
        ; and torches give back one roll each to keep the total at 32.
        .byte   6, 14, 17, 19, 23, 26, 28, 32
level_item_type:
        .byte   ITEM_T_FOOD, ITEM_T_DAGGER, ITEM_T_POTION, ITEM_T_SCROLL
        .byte   ITEM_T_WEAPON, ITEM_T_ARMOR, ITEM_T_RING, ITEM_T_TORCH
level_item_subc:
        .byte   SUB_FOOD_COUNT, SUB_DAGGER_COUNT, SUB_POT_COUNT, SUB_SCROLL_COUNT
        .byte   SUB_WEAPON_COUNT, SUB_ARMOR_COUNT, SUB_RING_COUNT, SUB_TORCH_COUNT


; ----------------------------------------------------------------------------
; spawn_level_pits: scatter 0..2 TILE_TRAP_PIT cells on EMPTY floor squares
; (1d3 roll). Pits stay visible AND keep dealing damage on every step —
; players have to remember their position across the FOV-wipe. Called from
; new_level + start: right after spawn_level_items so the PRNG advances at
; the same point each turn, keeping seeds reproducible.
; ----------------------------------------------------------------------------
spawn_level_pits:
        LDA     #3
        JSR     rand_mod                ; A = 0..2
@lp:    CMP     #0
        BEQ     @done
        PHA                             ; save remaining count across find/set
        JSR     find_empty_cell
        BCS     @done_pop               ; no empty cell left → bail
        LDA     #TILE_TRAP_PIT
        JSR     set_tile
        PLA
        SEC
        SBC     #1
        JMP     @lp
@done:  RTS
@done_pop:
        PLA
        RTS


; ----------------------------------------------------------------------------
; trigger_pit: subtract PIT_DMG from hp (saturating at 0), flag the player
; sprite for the next-frame hurt flash, and tag last_attacker so the death
; screen renders "KILLED BY A PIT" instead of a monster name. Pit tile is
; NOT consumed — repeat steps cost HP every time.
; ----------------------------------------------------------------------------
trigger_pit:
        LDA     hp
        SEC
        SBC     #PIT_DMG
        BCS     @hp_ok
        LDA     #0
@hp_ok:
        STA     hp
        LDA     #1
        STA     player_hurt
        LDA     #LAST_ATTACKER_PIT
        STA     last_attacker
        ; Reveal the pit on the cell we just stepped onto. main_loop's
        ; regular-move path arrived here with tgt_col/tgt_row still
        ; holding the destination (= player's new position), so the
        ; helper writes the right cell.
        JMP     reveal_pit_at_target    ; tail-call (RTS from helper)


; ----------------------------------------------------------------------------
; find_empty_cell: pick an unoccupied TILE_EMPTY cell that is neither
; the player's nor held by a live monster / floor item.
;   On success: tgt_col / tgt_row set, carry CLEAR.
;   On failure: carry SET (only when literally every cell is ineligible).
;
; Strategy: random START offset in [0, 160) followed by a deterministic
; linear sweep of all 160 cells (wrapping past 159 → 0). Guarantees an
; eligible cell is returned if one exists — the previous "32 random
; tries then bail" form had a real ~1-2 % silent-fail rate on small
; two-rooms layouts (P(empty)≈12 % → P(all 32 miss)≈0.875^32≈1.4 %)
; and would silently strip a monster / item / pit from the level.
; Worst case here is a full 160-cell scan, well under one frame.
;
; tmp = current cursor offset (rand_mod stomps tmp, so we only call it
; ONCE up-front and never inside the loop). monster_at_target /
; item_at_target clobber X but leave tmp alone, so the cursor survives.
; The PHA/PLA around the pool scans preserves X = remaining-cell counter.
; ----------------------------------------------------------------------------
find_empty_cell:
        LDA     #LOGICAL_COLS * LOGICAL_ROWS
        JSR     rand_mod                ; A in [0, 160)
        STA     tmp                     ; cursor offset
        LDX     #LOGICAL_COLS * LOGICAL_ROWS  ; remaining cells to inspect
@try:
        ; Decode tmp = row*16 + col → (tgt_col, tgt_row).
        LDA     tmp
        AND     #$0F
        STA     tgt_col
        LDA     tmp
        LSR
        LSR
        LSR
        LSR
        STA     tgt_row
        JSR     tile_at_target          ; preserves X (uses Y, map_ptr)
        CMP     #TILE_EMPTY
        BNE     @advance
        LDA     tgt_col
        CMP     player_col
        BNE     @ok_player
        LDA     tgt_row
        CMP     player_row
        BEQ     @advance
@ok_player:
        TXA
        PHA                             ; save remaining counter
        JSR     monster_at_target       ; clobbers X
        BCC     @advance_pop            ; monster present → next cell
        JSR     item_at_target          ; clobbers X
        PLA
        TAX                             ; restore remaining counter
        BCC     @advance                ; item present → next cell
        CLC
        RTS
@advance_pop:
        PLA
        TAX
@advance:
        INC     tmp
        LDA     tmp
        CMP     #LOGICAL_COLS * LOGICAL_ROWS
        BCC     @no_wrap
        LDA     #0
        STA     tmp
@no_wrap:
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
        CMP     #MON_TYPE_BOSS
        BEQ     @boss
        ; Standard 1-cell monsters: exact (col, row) match.
        LDA     monsters+MON_COL,X
        CMP     tgt_col
        BNE     @next
        LDA     monsters+MON_ROW,X
        CMP     tgt_row
        BNE     @next
        CLC                             ; hit
        RTS
@boss:
        ; Boss occupies a 2x2 footprint anchored at MON_COL/MON_ROW.
        ; Cell (tgt_col, tgt_row) hits the boss iff:
        ;   tgt_col in {anchor_col, anchor_col+1}
        ;   tgt_row in {anchor_row, anchor_row+1}
        LDA     tgt_col
        SEC
        SBC     monsters+MON_COL,X
        CMP     #2
        BCS     @next                   ; >= 2 → outside footprint
        LDA     tgt_row
        SEC
        SBC     monsters+MON_ROW,X
        CMP     #2
        BCS     @next
        CLC                             ; bump hits the boss
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
; (1=undead, 2=ghost, 3=troll, 4=skeleton, 5=death). Index 0 is the
; dead/empty marker and never read here, so it sits as a placeholder $00.
; Array order matches MON_TYPE id order — spawn_monsters' random roll
; in [1..mon_type_pool] indexes directly into these arrays. Tier
; breakdown: ids 1..3 (UNDEAD/GHOST/TROLL) ship at depth 1 so the
; player meets all three behaviours (greedy chase, random walk, flee)
; immediately. SKELETON (id 4) gates to depth 5 — first 2-dmg melee
; threat — and DEATH (id 5) waits until depth 10 so the player has
; serious gear before facing the 3-HP / 2-dmg sloth-chase brutes.
; ----------------------------------------------------------------------------
; The 7th slot (index 6 = MON_TYPE_BOSS) is reserved for the depth-13
; encounter; it is never picked by spawn_monsters' rand_mod path
; (mon_type_pool caps at 5) — gen_boss_room hand-spawns it instead.
; HP/dmg are intentionally high: bare HP 15 with depth-13 mon_hp_bonus
; (depth/3 = 4) lands the boss at 19 HP, mon_dmg_bonus (depth/6 = 2) on
; top of base 4 dmg lands a 6-dmg bite. With expected player gear
; (TUNIC armour for DEF +1 plus XP-driven DEF) the boss bites the
; player for ~3 HP per turn, gating the win behind real positioning
; rather than face-tanking.
mon_init_hp:
        .byte   0, 1, 2, 3, 2, 3, 15
mon_init_name:
        .byte   0, SPRITE_NAME_UNDEAD, SPRITE_NAME_GHOST, SPRITE_NAME_TROLL, SPRITE_NAME_SKELETON, SPRITE_NAME_DEATH, SPRITE_NAME_BOSS_TL
mon_init_color:
        .byte   0, MON_COL_UNDEAD, MON_COL_GHOST, MON_COL_TROLL, MON_COL_SKELETON, MON_COL_DEATH, MON_COL_BOSS
mon_init_dmg:
        .byte   0, 1, 1, 1, 2, 2, 4


; ----------------------------------------------------------------------------
; spawn_item: drop a food ration at (tgt_col, tgt_row). Caller has
; copied the dying monster's MON_COL/MON_ROW into tgt_col/tgt_row.
; Picks the first empty pool slot (ITEM_TYPE == 0); if every slot is
; taken the food is silently lost — 8 simultaneous items is a generous
; cap, and the player would have already eaten several times to clear
; the pool. SUB_FOOD_RATION is the only food sub-type for now; a thin
; helper `spawn_typed_item` (below) lets the level generator drop
; arbitrary (type, subtype) at any cell.
; ----------------------------------------------------------------------------
spawn_item:
        LDA     #ITEM_T_FOOD
        LDY     #SUB_FOOD_RATION
        ; Fall through into spawn_typed_item.

; ----------------------------------------------------------------------------
; spawn_typed_item: A = ITEM_T_*, Y = subtype, (tgt_col, tgt_row) =
; cell. Picks the first empty world slot. No-op if the pool is full.
; ----------------------------------------------------------------------------
spawn_typed_item:
        STA     tmp                     ; cache type while we hunt for a slot
        LDX     #0
@lp:    LDA     items+ITEM_TYPE,X
        BEQ     @found
        TXA
        CLC
        ADC     #ITEM_SIZE
        TAX
        CPX     #(ITEM_COUNT * ITEM_SIZE)
        BCC     @lp
        RTS                             ; pool full → silently lost
@found:
        LDA     tmp
        STA     items+ITEM_TYPE,X
        LDA     tgt_col
        STA     items+ITEM_COL,X
        LDA     tgt_row
        STA     items+ITEM_ROW,X
        TYA
        STA     items+ITEM_SUBTYPE,X
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
; after the player's coordinates are updated. If an item sits on the
; player's new cell, push it into the inventory pool. Daggers are the
; one exception: they increment dagger_qty and never enter the bag.
; Every inventory item merges with an existing slot of the same sub-type
; by bumping INV_QTY; otherwise it takes the first empty slot. If the
; bag is full, the item stays on the ground and the player has to come
; back (no message yet — eventual P10 polish).
;
; map_ptr is reused as scratch for the cached (type, subtype) of the
; world item — it's dead between turns (only render_map writes to it).
; ----------------------------------------------------------------------------
try_pickup_item:
        LDA     player_col
        STA     tgt_col
        LDA     player_row
        STA     tgt_row
        JSR     item_at_target
        BCS     @done
        ; Cache (type, subtype) of the world item; keep its slot offset
        ; on the stack so the consume-slot step at the end is cheap.
        LDA     items+ITEM_TYPE,X
        STA     map_ptr                 ; scratch[0] = type
        LDA     items+ITEM_SUBTYPE,X
        STA     map_ptr+1               ; scratch[1] = subtype
        TXA
        PHA                             ; preserve world slot offset
        LDA     map_ptr
        CMP     #ITEM_T_DAGGER
        BEQ     @pickup_dagger
        ; Every item type now stacks on (type, subtype) match — the
        ; buff-timer rework made weapons / armours / torches each into
        ; a per-use charge, so picking up a second sword should grow
        ; the existing pile (1xSWORD → 2xSWORD), not eat a new bag
        ; letter. Rings + scrolls never *had* to be unique either, so
        ; folding them in tightens the inventory display too.
        JSR     find_inv_stack          ; C clear → matching slot in X
        BCS     @find_empty
        INC     inventory+INV_QTY,X
        JMP     @consume
@find_empty:
        JSR     find_inv_empty          ; C clear → empty slot in X
        BCS     @bag_full
        LDA     map_ptr
        STA     inventory+INV_TYPE,X
        LDA     map_ptr+1
        STA     inventory+INV_SUBTYPE,X
        LDA     #1
        STA     inventory+INV_QTY,X
        LDA     map_ptr                 ; A = type
        LDY     map_ptr+1               ; Y = subtype
        JSR     lookup_item_value
        STA     inventory+INV_VALUE,X
@consume:
        PLA
        TAX                             ; restore world slot offset
        LDA     #0
        STA     items+ITEM_TYPE,X       ; free the world slot
        RTS
@bag_full:
        PLA                             ; discard saved world offset
        ; Bag full → leave the world item where it sits. The player can
        ; come back, eat / equip something, then walk over the cell again.
@done:
        RTS
@pickup_dagger:
        ; Daggers are ammo, not inventory. Cap to the 2-digit HUD range;
        ; extra pickups past 99 are consumed but do not increase the count.
        LDA     dagger_qty
        CMP     #DAGGER_QTY_MAX
        BCS     @consume
        INC     dagger_qty
        JMP     @consume


; ----------------------------------------------------------------------------
; find_inv_empty: linear scan of the 26-slot bag for the first slot
; with INV_TYPE = 0. On hit: C clear, X = slot byte offset (0, 8, ...).
; On miss: C set, X clobbered. Preserves Y, A clobbered.
; ----------------------------------------------------------------------------
find_inv_empty:
        LDX     #0
@lp:    LDA     inventory+INV_TYPE,X
        BEQ     @hit
        TXA
        CLC
        ADC     #INV_SIZE
        TAX
        CPX     #(INV_COUNT * INV_SIZE)
        BCC     @lp
        SEC
        RTS
@hit:
        CLC
        RTS


; ----------------------------------------------------------------------------
; find_inv_stack: linear scan of the bag for a slot whose
; (INV_TYPE, INV_SUBTYPE) matches (map_ptr, map_ptr+1). Used to merge
; stackable pickups (food, daggers) so the player isn't punished by a
; cluttered bag for fighting through monsters that drop the same ration.
; On hit: C clear, X = slot offset. On miss: C set.
; ----------------------------------------------------------------------------
find_inv_stack:
        LDX     #0
@lp:    LDA     inventory+INV_TYPE,X
        CMP     map_ptr
        BNE     @next
        LDA     inventory+INV_SUBTYPE,X
        CMP     map_ptr+1
        BNE     @next
        CLC
        RTS
@next:
        TXA
        CLC
        ADC     #INV_SIZE
        TAX
        CPX     #(INV_COUNT * INV_SIZE)
        BCC     @lp
        SEC
        RTS


; ----------------------------------------------------------------------------
; lookup_item_value: dispatch to a per-category byte table. On entry:
;   A = ITEM_T_* (1..7),  Y = sub-type (0..N).
; Returns:
;   A = the canonical INV_VALUE for that item (weapon dmg / armor def /
;       ring flag bit / heal amount / etc.). Defaults to 0 for unknown
;       (type, subtype) pairs — a defensive fallback that keeps a
;       garbled pickup from corrupting player_dmg later.
; Preserves X — try_pickup_item passes the inventory slot offset in X
; across the call to land INV_VALUE on the right slot. Earlier code
; here did TAX + CPX, which trashed X and made every fresh weapon /
; armour / potion / ration land its value at the WRONG byte (often
; corrupting the previous slot's INV_SUBTYPE — the "AMULET name turns
; into garbage" symptom). Dispatch via CMP on A directly avoids the
; TAX entirely; A survives across each BNE because BNE doesn't touch
; the accumulator, only Z/C.
; ----------------------------------------------------------------------------
lookup_item_value:
        CMP     #ITEM_T_WEAPON
        BNE     @nw
        LDA     weapon_value_table,Y
        RTS
@nw:    CMP     #ITEM_T_ARMOR
        BNE     @na
        LDA     armor_value_table,Y
        RTS
@na:    CMP     #ITEM_T_RING
        BNE     @nr
        LDA     ring_value_table,Y
        RTS
@nr:    CMP     #ITEM_T_POTION
        BNE     @np
        LDA     potion_value_table,Y
        RTS
@np:    CMP     #ITEM_T_SCROLL
        BNE     @ns
        LDA     scroll_value_table,Y
        RTS
@ns:    CMP     #ITEM_T_FOOD
        BNE     @nf
        LDA     food_value_table,Y
        RTS
@nf:    CMP     #ITEM_T_DAGGER
        BNE     @nd
        LDA     dagger_value_table,Y
        RTS
@nd:    CMP     #ITEM_T_TORCH
        BNE     @nt
        LDA     torch_value_table,Y
        RTS
@nt:    LDA     #0
        RTS

; Per-category value tables. Sub-type id = byte index (always 0 after
; MVP4 simplification — one sub-type per category). INV_VALUE is the
; cached "what does this item do" payload — looked up once at pickup,
; stored in the slot, never re-fetched by the combat / equip / quaff
; paths. Weapons + armour are the player's bump damage / hit defense;
; the amulet stores the RING_F_REGEN bit it ORs into ring_flags when
; worn; potions + food store their HP heal amount; scrolls store the
; sub-type id again as a generic "effect tag" the read-scroll handler
; switches on.
weapon_value_table:
        .byte 2                         ; sword: dmg 2
armor_value_table:
        .byte 1                         ; tunic: def 1 (immune-armor fix:
                                        ; with def 2 every dmg-1 monster
                                        ; was reduced to 0 via apply_step's
                                        ; floor-at-0, trivialising depths
                                        ; 1-5; def 1 keeps a 1-dmg punch
                                        ; alive for UNDEAD/GHOST/TROLL.
ring_value_table:
        .byte RING_F_REGEN              ; amulet: regen flag
potion_value_table:
        .byte 5                         ; potion: heal +5 HP (tuned down
                                        ; from 8 — at HP_MAX 14 a +8 heal
                                        ; was 57% of max, making potions
                                        ; a "free run reset"; +5 is still
                                        ; meaningful but forces planning).
scroll_value_table:
        .byte SUB_SCROLL_MAP            ; scroll: full-map reveal
food_value_table:
        .byte FOOD_HEAL                 ; ration: +3 HP
dagger_value_table:
        .byte 2                         ; thrown damage
torch_value_table:
        .byte SUB_TORCH_PLAIN           ; torch: 1 sub-type, FOV boost
                                        ; magnitude is hardcoded via
                                        ; TORCH_RADIUS in compute_fov;
                                        ; the value byte is just a tag.

; --- Item → SAT name / palette tables (indexed by ITEM_T_*) ------------
; Index 0 (ITEM_T_NONE) entries are unreachable from place_all_sprites
; (the BEQ on items+ITEM_TYPE,X skips empty slots before the lookup),
; but stay 0/0 so a stray index doesn't read past the end of the table.
item_sprite_table:
        .byte 0                         ; 0 unused
        .byte SPRITE_NAME_WEAPON        ; 1 ITEM_T_WEAPON
        .byte SPRITE_NAME_ARMOR         ; 2 ITEM_T_ARMOR
        .byte SPRITE_NAME_RING          ; 3 ITEM_T_RING
        .byte SPRITE_NAME_POTION        ; 4 ITEM_T_POTION
        .byte SPRITE_NAME_SCROLL        ; 5 ITEM_T_SCROLL
        .byte SPRITE_NAME_FOOD          ; 6 ITEM_T_FOOD
        .byte SPRITE_NAME_DAGGER        ; 7 ITEM_T_DAGGER
        .byte SPRITE_NAME_TORCH         ; 8 ITEM_T_TORCH
item_color_table:
        .byte 0
        .byte COL_WEAPON
        .byte COL_ARMOR
        .byte COL_RING
        .byte COL_POTION
        .byte COL_SCROLL
        .byte COL_FOOD
        .byte COL_DAGGER
        .byte COL_TORCH

; --- Item display names ($FF-terminated ASCII) -------------------------
; One name per category after MVP4 simplification — matches the on-screen
; sprite. The inventory modal walks the .byte list with draw_text.
; Names are kept under 12 chars so the "[a] *NAME"-style line fits in a
; 32-col row alongside the quantity and a "(equipped)" marker.
name_sword:     .byte "SWORD",     $FF
name_tunic:     .byte "TUNIC",     $FF
name_amulet:    .byte "AMULET",    $FF
name_potion:    .byte "POTION",    $FF
name_scroll:    .byte "SCROLL",    $FF
name_ration:    .byte "RATION",    $FF
name_dagger:    .byte "DAGGER",    $FF
name_torch:     .byte "TORCH",     $FF
name_unknown:   .byte "?",         $FF

; Per-category pointer tables. Sub-type id = byte index (always 0 now).
weapon_name_table:
        .byte <name_sword
weapon_name_table_h:
        .byte >name_sword
armor_name_table:
        .byte <name_tunic
armor_name_table_h:
        .byte >name_tunic
ring_name_table:
        .byte <name_amulet
ring_name_table_h:
        .byte >name_amulet
potion_name_table:
        .byte <name_potion
potion_name_table_h:
        .byte >name_potion
scroll_name_table:
        .byte <name_scroll
scroll_name_table_h:
        .byte >name_scroll
food_name_table:
        .byte <name_ration
food_name_table_h:
        .byte >name_ration
dagger_name_table:
        .byte <name_dagger
dagger_name_table_h:
        .byte >name_dagger
torch_name_table:
        .byte <name_torch
torch_name_table_h:
        .byte >name_torch


; ----------------------------------------------------------------------------
; lookup_item_name: dispatch (type, subtype) → (vdp_src_lo, vdp_src_hi)
; pointing at the $FF-terminated display string. Used by show_inventory
; and prompt-message routines. On entry: A = ITEM_T_*, Y = subtype.
; Falls back to "?" for unknown (type, subtype) so the inventory modal
; never explodes on a corrupted slot. Clobbers A, X.
; ----------------------------------------------------------------------------
lookup_item_name:
        TAX                             ; X = type; Y already holds subtype
        CPX     #ITEM_T_WEAPON
        BNE     @na
        LDA     weapon_name_table,Y
        STA     vdp_src_lo
        LDA     weapon_name_table_h,Y
        STA     vdp_src_hi
        RTS
@na:    CPX     #ITEM_T_ARMOR
        BNE     @nr
        LDA     armor_name_table,Y
        STA     vdp_src_lo
        LDA     armor_name_table_h,Y
        STA     vdp_src_hi
        RTS
@nr:    CPX     #ITEM_T_RING
        BNE     @np
        LDA     ring_name_table,Y
        STA     vdp_src_lo
        LDA     ring_name_table_h,Y
        STA     vdp_src_hi
        RTS
@np:    CPX     #ITEM_T_POTION
        BNE     @ns
        LDA     potion_name_table,Y
        STA     vdp_src_lo
        LDA     potion_name_table_h,Y
        STA     vdp_src_hi
        RTS
@ns:    CPX     #ITEM_T_SCROLL
        BNE     @nf
        LDA     scroll_name_table,Y
        STA     vdp_src_lo
        LDA     scroll_name_table_h,Y
        STA     vdp_src_hi
        RTS
@nf:    CPX     #ITEM_T_FOOD
        BNE     @nd
        LDA     food_name_table,Y
        STA     vdp_src_lo
        LDA     food_name_table_h,Y
        STA     vdp_src_hi
        RTS
@nd:    CPX     #ITEM_T_DAGGER
        BNE     @nt
        LDA     dagger_name_table,Y
        STA     vdp_src_lo
        LDA     dagger_name_table_h,Y
        STA     vdp_src_hi
        RTS
@nt:    CPX     #ITEM_T_TORCH
        BNE     @unknown
        LDA     torch_name_table,Y
        STA     vdp_src_lo
        LDA     torch_name_table_h,Y
        STA     vdp_src_hi
        RTS
@unknown:
        LDA     #<name_unknown
        STA     vdp_src_lo
        LDA     #>name_unknown
        STA     vdp_src_hi
        RTS


; ----------------------------------------------------------------------------
; init_inventory: wipe the 26-slot bag (208 B at $E3A0..$E46F), clear
; the three equipped_* slots to INV_NONE, zero ring_flags / regen_tick,
; then prime player_dmg + player_def via recompute_player_stats.
; Called once at start (after the title-screen choice) and never again
; — death = JMP $4000 cold-start, which re-runs start: from the top.
; ----------------------------------------------------------------------------
init_inventory:
        ; Wipe 208 bytes. INV_COUNT * INV_SIZE = 26 * 8 = 208 = $D0,
        ; bit 7 set so the sentinel-BNE pattern (DEX-then-store-then-BNE)
        ; covers the full range.
        LDA     #0
        LDX     #(INV_COUNT * INV_SIZE)
@lp:    DEX
        STA     inventory,X
        BNE     @lp
        STA     inventory,X             ; final write at offset 0
        ; Clear the live-state ZP bytes that the pickup / equip / combat
        ; paths read directly. ring_flags must stay 0 until a ring is
        ; worn (used as an OR mask in move_monsters / apply_step).
        STA     ring_flags
        STA     regen_tick
        STA     player_xp               ; fresh run starts at 0 kills
        STA     xp_atk_bonus            ; no XP-driven ATK bonus yet
        STA     xp_def_bonus            ; no XP-driven DEF bonus yet
        STA     throw_active            ; no projectile in flight at boot
        STA     dagger_qty              ; dagger ammo is a HUD counter,
                                        ; not an inventory stack
        STA     weapon_timer            ; no weapon buff active
        STA     weapon_boost
        STA     armor_timer             ; no armor buff active
        STA     armor_boost
        STA     torch_timer             ; no torch buff active
        STA     ring_timer              ; no ring buff active
        STA     ring_boost
        LDA     #HP_MAX
        STA     hp_max                  ; runtime HP cap, XP-bumpable
        LDA     #5
        STA     hp_tick
        LDA     #10
        STA     atk_tick
        LDA     #20
        STA     def_tick
        ; equipped_* bookkeeping is gone — every gear category is now
        ; a consumable buff (weapon / armor / ring / torch). The bag
        ; letter visible in the inventory IS the activation key, no
        ; "equipped slot" indirection needed.
        JSR     recompute_player_stats  ; sets player_dmg = 1, player_def = 0
        RTS


; ----------------------------------------------------------------------------
; recompute_player_stats: rebuild player_dmg + player_def from the
; live timer-based buff state, then fold in the XP-driven progression
; bonuses. Called when:
;   - a buff is activated (@do_weapon / @do_armor consume + start timer)
;   - a buff timer rolls over to 0 in finish_turn
;   - an XP threshold fires in award_xp
;
; Formula:
;   player_dmg = (weapon_timer ? weapon_boost : 1) + xp_atk_bonus
;   player_def = (armor_timer  ? armor_boost  : 0) + xp_def_bonus
; The "1 if no weapon" base preserves MVP3's bare-hands PLAYER_DMG=1.
; The amulet's regen happens in finish_turn, not here. Clobbers A.
; ----------------------------------------------------------------------------
recompute_player_stats:
        ; --- player_dmg ---
        LDA     weapon_timer
        BEQ     @no_wpn                 ; expired → bare-handed
        LDA     weapon_boost
        JMP     @wpn_save
@no_wpn:
        LDA     #1                      ; bare-handed base
@wpn_save:
        CLC
        ADC     xp_atk_bonus            ; XP progression bonus
        STA     player_dmg

        ; --- player_def ---
        LDA     armor_timer
        BEQ     @no_arm                 ; expired → unarmored
        LDA     armor_boost
        JMP     @arm_save
@no_arm:
        LDA     #0
@arm_save:
        CLC
        ADC     xp_def_bonus            ; XP progression bonus
        STA     player_def
        RTS


; ----------------------------------------------------------------------------
; player_attack_monster: deal PLAYER_DMG damage to the monster at pool
; offset X. Saturates HP at 0. Survivors get MON_HURT set so they
; render in COL_HURT this frame; killed monsters free their slot AND
; drop a food item at their last position 1-in-4 times (rand_mod #4
; → BNE no_drop). 25% rate (tuned down from 50%) keeps the heal econ
; tight — combined with the +35% level-spawn food and +1 HP every 5
; kills from XP, the player still sustains combat over a long run but
; can no longer thesaurise enough heal to ignore positioning.
; ----------------------------------------------------------------------------
player_attack_monster:
        LDA     monsters+MON_HP,X
        SEC
        SBC     player_dmg              ; weapon value or 1 bare-handed
        BCS     @store
        LDA     #0
@store:
        STA     monsters+MON_HP,X
        BNE     @hurt_flash
        ; --- Killed: stash the corpse cell, free the slot, bump XP,
        ; then maybe drop. ---
        LDA     monsters+MON_COL,X
        STA     tgt_col
        LDA     monsters+MON_ROW,X
        STA     tgt_row
        LDA     monsters+MON_TYPE,X
        CMP     #MON_TYPE_BOSS
        BEQ     @boss_killed
        LDA     #0
        STA     monsters+MON_TYPE,X
        JSR     award_xp
        ; 25% drop rate. rand_mod #4 returns {0..3}; only 0 fires the
        ; drop. rand_mod preserves X (it touches A + tmp only).
        LDA     #4
        JSR     rand_mod
        BNE     @no_drop
        JMP     spawn_item              ; tail-call (RTS from spawn_item)
@no_drop:
        RTS
@boss_killed:
        ; Boss kill = run win. Free the slot so the in-progress redraw
        ; doesn't paint a 32x32 corpse, count the kill, and JMP into
        ; the victory takeover. win_screen handles its own stack reset
        ; (resets SP, paints CONGRATULATIONS, waits for any key, then
        ; cold-starts via JMP $4000) so we don't need to RTS through
        ; the apply_step / main_loop chain.
        LDA     #0
        STA     monsters+MON_TYPE,X
        JSR     award_xp                ; one final +1 for the trophy kill
        JMP     win_screen
@hurt_flash:
        LDA     #1
        STA     monsters+MON_HURT,X
        RTS


; ----------------------------------------------------------------------------
; award_xp: +1 to player_xp, saturating at 255, then ticks the three
; level-up countdowns (hp_tick, atk_tick, def_tick). Each tick that
; rolls over to 0 fires its bonus and reloads; the level-up curve is:
;   hp_max +1 every 5 kills (+ 1 current HP, capped to new hp_max)
;   xp_atk_bonus +1 every 10 kills
;   xp_def_bonus +1 every 20 kills
; The two ATK/DEF events JSR recompute_player_stats so player_dmg and
; player_def pick up the new bonuses immediately — the next bump or
; bite uses the level-up'd value. Saved across the JSR via X-on-stack
; because callers (player_attack_monster, throw kill) hold X = monster
; pool offset and need it preserved.
; ----------------------------------------------------------------------------
award_xp:
        LDA     player_xp
        CMP     #$FF
        BEQ     @done
        INC     player_xp
        ; --- HP_MAX tick ---
        DEC     hp_tick
        BNE     @no_hp
        LDA     #5
        STA     hp_tick
        INC     hp_max
        ; +1 current HP, but never above the freshly-bumped cap.
        LDA     hp
        CMP     hp_max
        BCS     @no_hp                  ; already at or above (defensive)
        INC     hp
@no_hp:
        ; --- ATK tick ---
        DEC     atk_tick
        BNE     @no_atk
        LDA     #10
        STA     atk_tick
        INC     xp_atk_bonus
        TXA
        PHA
        JSR     recompute_player_stats
        PLA
        TAX
@no_atk:
        ; --- DEF tick ---
        DEC     def_tick
        BNE     @no_def
        LDA     #20
        STA     def_tick
        INC     xp_def_bonus
        TXA
        PHA
        JSR     recompute_player_stats
        PLA
        TAX
@no_def:
@done:
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
        CMP     #MON_TYPE_TROLL
        BEQ     ai_troll
        CMP     #MON_TYPE_BOSS
        BNE     @undead_default
        JMP     ai_boss                 ; out-of-range branch — trampoline
@undead_default:
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
; ai_troll: peaceable until provoked. The default is anti-greedy flee
; — step AWAY from the player on the longer axis, falling back to the
; other if the flee direction is blocked (out of bounds, wall, monster,
; item). Cornered trolls don't move this turn; since the flee step
; always grows the player→monster distance, apply_step never lands on
; the player cell, so unprovoked trolls never accidentally bite.
;
; Once the player lands a hit (bump or thrown dagger),
; player_attack_monster sets MON_HURT, and clear_hurt_flags is patched
; to KEEP MON_HURT live across turns for trolls only — so the flag
; doubles as a permanent "provoked" state. From that point on the
; troll switches to greedy chase (ai_undead) and hunts the player
; until killed. The COL_HURT red tint that comes for free with
; MON_HURT acts as a visual "this troll is angry" cue.
; ----------------------------------------------------------------------------
ai_troll:
        LDA     monsters+MON_HURT,X
        BEQ     @flee
        JMP     ai_undead               ; provoked → greedy chase, forever
@flee:
        JSR     compute_abs_deltas
        BEQ     @bail
        LDA     mon_abs_dx
        CMP     mon_abs_dy
        BCC     @y_first                ; |dx| < |dy| → flee y first
        JSR     try_flee_x
        BCC     @bail
        JSR     try_flee_y
        RTS
@y_first:
        JSR     try_flee_y
        BCC     @bail
        JSR     try_flee_x
@bail:
        RTS


; ----------------------------------------------------------------------------
; try_flee_x / try_flee_y: mirror try_step_x / _y but step AWAY from
; the player (mon.col < player.col → flee west; else flee east; ditto
; for rows). If abs delta on this axis is 0 there's no clear flee
; direction, bail with C set so the caller tries the other axis.
; apply_step still vets bounds, tile passability and pool collisions.
; ----------------------------------------------------------------------------
try_flee_x:
        LDA     mon_abs_dx
        BNE     @x_compute
        SEC
        RTS
@x_compute:
        LDA     monsters+MON_ROW,X
        STA     tgt_row
        LDA     monsters+MON_COL,X
        CMP     player_col
        BCC     @x_west                 ; mon.col < player.col → flee west (-1)
        CLC
        ADC     #1                      ; mon.col >= player.col → flee east (+1)
        STA     tgt_col
        JMP     apply_step
@x_west:
        SEC
        SBC     #1
        STA     tgt_col
        JMP     apply_step

try_flee_y:
        LDA     mon_abs_dy
        BNE     @y_compute
        SEC
        RTS
@y_compute:
        LDA     monsters+MON_COL,X
        STA     tgt_col
        LDA     monsters+MON_ROW,X
        CMP     player_row
        BCC     @y_north                ; mon.row < player.row → flee north (-1)
        CLC
        ADC     #1                      ; mon.row >= player.row → flee south (+1)
        STA     tgt_row
        JMP     apply_step
@y_north:
        SEC
        SBC     #1
        STA     tgt_row
        JMP     apply_step


; ----------------------------------------------------------------------------
; ai_boss: greedy chase for the depth-13 boss. The boss occupies a 2x2
; footprint anchored at MON_COL/MON_ROW; movement on either axis shifts
; the whole footprint by one cell. apply_boss_step does the 4-cell
; passability sweep AND the player-overlap bite (player inside the new
; footprint = bump-attack, no actual move).
;
; Compute deltas relative to the anchor cell (top-left of the
; footprint). With the anchor as the reference, |dx| / |dy| keep their
; meaning as "axis priority", and the "step toward player" sign is
; recovered via CMP just like ai_undead.
; ----------------------------------------------------------------------------
ai_boss:
        JSR     compute_abs_deltas
        BEQ     @bail
        LDA     mon_abs_dx
        CMP     mon_abs_dy
        BCC     @y_first                ; |dx| < |dy| → y-axis first
        JSR     try_boss_step_x
        BCC     @bail
        JSR     try_boss_step_y
        RTS
@y_first:
        JSR     try_boss_step_y
        BCC     @bail
        JSR     try_boss_step_x
@bail:
        RTS


; ----------------------------------------------------------------------------
; try_boss_step_x / try_boss_step_y: 2x2 sibling of try_step_x / _y for
; the boss. Sets tgt_col / tgt_row to the prospective new anchor and
; tail-calls apply_boss_step. If |delta| on this axis is 0 there is no
; signed direction to take, bail with C set so the caller tries the
; other axis. Both routines preserve X (the boss's pool offset = 0).
; ----------------------------------------------------------------------------
try_boss_step_x:
        LDA     mon_abs_dx
        BNE     @compute
        SEC
        RTS
@compute:
        LDA     monsters+MON_ROW,X
        STA     tgt_row
        LDA     monsters+MON_COL,X
        CMP     player_col
        BCC     @east                   ; anchor.col < player.col → +1
        SEC
        SBC     #1
        STA     tgt_col
        JMP     apply_boss_step
@east:
        CLC
        ADC     #1
        STA     tgt_col
        JMP     apply_boss_step

try_boss_step_y:
        LDA     mon_abs_dy
        BNE     @compute
        SEC
        RTS
@compute:
        LDA     monsters+MON_COL,X
        STA     tgt_col
        LDA     monsters+MON_ROW,X
        CMP     player_row
        BCC     @south                  ; anchor.row < player.row → +1
        SEC
        SBC     #1
        STA     tgt_row
        JMP     apply_boss_step
@south:
        CLC
        ADC     #1
        STA     tgt_row
        ; fall through to apply_boss_step


; ----------------------------------------------------------------------------
; apply_boss_step: shared tail for try_boss_step_x / _y. (tgt_col,
; tgt_row) carries the prospective new anchor. Returns:
;   - C clear if the action consumed the boss's turn (either bit
;     the player or moved into the new anchor cell);
;   - C set if blocked (caller may try the other axis).
;
; Player overlap test uses the same "tgt_col..+1, tgt_row..+1" 2x2
; window as monster_at_target's boss path. If the player sits in the
; new footprint, the boss bites: hp -= max(MON_DMG - player_def, 0),
; saturated at 0; player_hurt + last_attacker get set; the boss does
; NOT advance (the player blocks the step).
;
; Otherwise we sweep the four prospective cells: in-bounds of the
; playable rectangle AND tile == TILE_EMPTY. Any failure short-circuits
; to @blocked. tgt_col / tgt_row get DEC'd back to the new-anchor
; coordinates before commit.
; ----------------------------------------------------------------------------
apply_boss_step:
        ; --- Player overlap test (player inside new 2x2 footprint?) ---
        LDA     player_col
        SEC
        SBC     tgt_col
        CMP     #2
        BCS     @sweep
        LDA     player_row
        SEC
        SBC     tgt_row
        CMP     #2
        BCS     @sweep
        ; Player is in the new footprint → bite, no move.
        LDA     monsters+MON_DMG,X
        SEC
        SBC     player_def
        BCS     @dmg_ok
        LDA     #0
@dmg_ok:
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
        LDA     #MON_TYPE_BOSS
        STA     last_attacker
        CLC                             ; turn used
        RTS

@sweep:
        ; Cell A = (tgt_col, tgt_row) — already in tgt_*.
        JSR     boss_cell_passable
        BCS     @blocked
        ; Cell B = (tgt_col+1, tgt_row) — INC col, restore on exit.
        INC     tgt_col
        JSR     boss_cell_passable
        BCS     @blk_dec_col
        ; Cell D = (tgt_col+1, tgt_row+1).
        INC     tgt_row
        JSR     boss_cell_passable
        BCS     @blk_dec_col_row
        ; Cell C = (tgt_col, tgt_row+1).
        DEC     tgt_col
        JSR     boss_cell_passable
        BCS     @blk_dec_row
        ; All four passable. Restore tgt_row to new-anchor row, commit.
        DEC     tgt_row
        LDA     tgt_col
        STA     monsters+MON_COL,X
        LDA     tgt_row
        STA     monsters+MON_ROW,X
        CLC
        RTS

@blk_dec_col_row:
        DEC     tgt_row
@blk_dec_col:
        DEC     tgt_col
        SEC
        RTS
@blk_dec_row:
        DEC     tgt_row
        SEC
        RTS
@blocked:
        SEC
        RTS


; ----------------------------------------------------------------------------
; boss_cell_passable: is the cell at (tgt_col, tgt_row) a valid
; destination for one of the boss's four footprint cells?
;   - In bounds (PLAY_LEFT_COL .. PLAY_RIGHT_COL, PLAY_TOP_ROW ..
;     PLAY_BOT_ROW); the boss room has no perimeter doors, so the
;     bounds rectangle is the wall ring.
;   - Tile == TILE_EMPTY (the boss room has no stairs, doors, items
;     or pits — a wall is the only blocker that can show up).
; Returns C clear if passable, C set if blocked. Preserves X (boss
; pool offset). Clobbers A, Y, tmp_packed.
; ----------------------------------------------------------------------------
boss_cell_passable:
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
        BNE     @blocked
        CLC
        RTS
@blocked:
        SEC
        RTS


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
        ; --- Player overlap → bite. Effective damage = max(0,
        ; MON_DMG - player_def). Armor / ring of protection thus
        ; reduce every hit by their summed value, with a hard floor
        ; at 0 so heavy armor against a weak undead means "no harm". ---
        LDA     monsters+MON_DMG,X
        SEC
        SBC     player_def
        BCS     @dmg_ok
        LDA     #0                      ; floor: no negative damage
@dmg_ok:
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
        LDA     monsters+MON_TYPE,X     ; cache attacker for death screen
        STA     last_attacker
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
        CMP     #TILE_TRAP_PIT
        BEQ     @move_pit               ; pit is walkable → damage + reveal
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
@move_pit:
        ; Monster stepped onto a pit. Subtract PIT_DMG from MON_HP
        ; (saturating), reveal the cell so the player sees the trap,
        ; then either commit the move (survived) or free the slot
        ; (killed — no XP, no drop, the player didn't earn it).
        LDA     monsters+MON_HP,X
        SEC
        SBC     #PIT_DMG
        BCS     @pit_hp_ok
        LDA     #0
@pit_hp_ok:
        STA     monsters+MON_HP,X
        JSR     reveal_pit_at_target    ; preserves X
        LDA     monsters+MON_HP,X
        BEQ     @pit_kill
        LDA     #1
        STA     monsters+MON_HURT,X
        JMP     @move_ok                ; survived → commit step
@pit_kill:
        LDA     #0
        STA     monsters+MON_TYPE,X
        CLC                             ; counts as the monster's action
        RTS


; ----------------------------------------------------------------------------
; render_map: expand the bit-packed map_buffer (80 B, 2 cells/byte)
; into the name table at VRAM $1800. Each logical tile becomes a 2x2
; block of 4 chars (base+0=TL, base+1=TR, base+2=BL, base+3=BR), with
; `base` looked up in tile_char_base[] keyed by the cell's dense
; TILE_* id. Auto-increment streams data row-by-row: one logical row
; produces 32 TL/TR chars (name-table row N) followed by 32 BL/BR
; chars (row N+1).
;
; FOV gate: each cell consults vis_buffer's bit-packed flag before
; emitting. A dark cell writes 4 zeros (blank black) — so un-lit
; corridors stay black even after gen_dungeon paints them in the
; map_buffer. The two vis-row bytes load once per logical row into the
; vis_pak_lo / vis_pak_hi shift register; LSR vis_pak_hi / ROR
; vis_pak_lo extracts the next column's bit straight into the carry
; flag, in column-iteration order. We re-load before the BL pass since
; the TL pass drains the register.
;
; Inner loop iterates Y = 0..7 (8 packed map bytes per logical row),
; processing 2 cells per iteration: low nibble first (col 2Y), then
; high nibble (col 2Y+1). Row counter lives in tmp_packed (not X) so
; emit_tl_cell / emit_bl_cell can use X freely as the tile-id index
; into tile_char_base.
; ----------------------------------------------------------------------------
render_map:
        ; Sync to VBlank before the full map rebuild burst.
        WAIT_VBLANK
        LDA     #$00
        STA     vdp_lo
        LDA     #$18
        STA     vdp_hi
        JSR     vdp_set_write   ; addr = $1800 (name table top)

        ; vdp_src walks 8 packed bytes per logical row.
        LDA     #<map_buffer
        STA     vdp_src_lo
        LDA     #>map_buffer
        STA     vdp_src_hi

        ; map_ptr walks the bit-packed vis_buffer (2 bytes per row).
        LDA     #<vis_buffer
        STA     map_ptr
        LDA     #>vis_buffer
        STA     map_ptr+1

        LDA     #LOGICAL_ROWS
        STA     tmp_packed      ; row counter — frees X for emit helpers
@row_lp:
        ; Load the row's two visibility bytes into the shift register.
        LDY     #0
        LDA     (map_ptr),Y
        STA     vis_pak_lo
        INY
        LDA     (map_ptr),Y
        STA     vis_pak_hi

        ; --- TL / TR pass: 16 cols = 8 packed map bytes ---
        LDY     #0
@tl_byte_lp:
        LDA     (vdp_src_lo),Y
        STA     pak_byte
        ; --- low nibble (col 2Y) ---
        LSR     vis_pak_hi
        ROR     vis_pak_lo
        BCC     @tl_lo_dark
        LDA     pak_byte
        AND     #$0F
        JSR     emit_tl_cell
        JMP     @tl_hi
@tl_lo_dark:
        LDA     #0
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        WRT_DATA_REG
        WRT_DATA_REG
@tl_hi: ; --- high nibble (col 2Y+1) ---
        LSR     vis_pak_hi
        ROR     vis_pak_lo
        BCC     @tl_hi_dark
        LDA     pak_byte
        LSR
        LSR
        LSR
        LSR
        JSR     emit_tl_cell
        JMP     @tl_byte_next
@tl_hi_dark:
        LDA     #0
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        WRT_DATA_REG
        WRT_DATA_REG
@tl_byte_next:
        INY
        CPY     #(LOGICAL_COLS / 2)
        BNE     @tl_byte_lp

        ; Reload the row's vis bytes — the TL pass drained the register.
        LDY     #0
        LDA     (map_ptr),Y
        STA     vis_pak_lo
        INY
        LDA     (map_ptr),Y
        STA     vis_pak_hi

        ; --- BL / BR pass: same iteration, base+2/base+3 chars. ---
        LDY     #0
@bl_byte_lp:
        LDA     (vdp_src_lo),Y
        STA     pak_byte
        LSR     vis_pak_hi
        ROR     vis_pak_lo
        BCC     @bl_lo_dark
        LDA     pak_byte
        AND     #$0F
        JSR     emit_bl_cell
        JMP     @bl_hi
@bl_lo_dark:
        LDA     #0
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        WRT_DATA_REG
        WRT_DATA_REG
@bl_hi: LSR     vis_pak_hi
        ROR     vis_pak_lo
        BCC     @bl_hi_dark
        LDA     pak_byte
        LSR
        LSR
        LSR
        LSR
        JSR     emit_bl_cell
        JMP     @bl_byte_next
@bl_hi_dark:
        LDA     #0
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        WRT_DATA_REG
        WRT_DATA_REG
@bl_byte_next:
        INY
        CPY     #(LOGICAL_COLS / 2)
        BNE     @bl_byte_lp
        JMP     @row_advance


@row_advance:
        ; Advance source pointer 8 bytes (packed map row) and vis ptr 2.
        CLC
        LDA     vdp_src_lo
        ADC     #(LOGICAL_COLS / 2)
        STA     vdp_src_lo
        BCC     @noinc_src
        INC     vdp_src_hi
@noinc_src:
        CLC
        LDA     map_ptr
        ADC     #2
        STA     map_ptr
        BCC     @noinc_vis
        INC     map_ptr+1
@noinc_vis:
        DEC     tmp_packed              ; row counter
        BEQ     @done
        JMP     @row_lp                 ; long-jump trampoline — render_map
                                        ; grew past the BNE range when the
                                        ; pit reveal/hide branches landed.
@done:
        RTS


; ----------------------------------------------------------------------------
; emit_tl_cell / emit_bl_cell: render one cell's 2 chars (TL+TR or
; BL+BR) given the 4-bit cell nibble in A. Bits 0..2 = dense TILE_*
; id, bit 3 = pit-reveal flag (only meaningful for TILE_TRAP_PIT).
; A hidden pit (TILE_TRAP_PIT without reveal) renders as blank — same
; as out-of-FOV cells. Clobbers A, X. Caller's X is dead inside
; render_map (row counter lives in tmp_packed); pak_byte+1 holds the
; cached nibble across the reveal-bit test.
; ----------------------------------------------------------------------------
emit_tl_cell:
        STA     pak_byte+1              ; cache full nibble
        AND     #7
        TAX                             ; X = tile id 0..7
        LDA     pak_byte+1
        AND     #TILE_REVEAL_BIT
        BNE     @show                   ; revealed pit → render normally
        CPX     #TILE_TRAP_PIT
        BEQ     @dark                   ; hidden pit → blank
@show:
        LDA     tile_char_base,X
        WRT_DATA_REG                    ; TL = base+0
        CLC
        ADC     #1
        WRT_DATA_REG                    ; TR = base+1
        RTS
@dark:
        LDA     #0
        WRT_DATA_REG
        WRT_DATA_REG
        RTS

emit_bl_cell:
        STA     pak_byte+1
        AND     #7
        TAX
        LDA     pak_byte+1
        AND     #TILE_REVEAL_BIT
        BNE     @show
        CPX     #TILE_TRAP_PIT
        BEQ     @dark
@show:
        LDA     tile_char_base,X
        CLC
        ADC     #2
        WRT_DATA_REG                    ; BL = base+2
        CLC
        ADC     #1
        WRT_DATA_REG                    ; BR = base+3
        RTS
@dark:
        LDA     #0
        WRT_DATA_REG
        WRT_DATA_REG
        RTS


; ----------------------------------------------------------------------------
; clear_vis_buffer: zero all 20 visibility bytes (bit-packed, 8 cells
; per byte). Called by new_level (and once at start) so a fresh dungeon
; shows nothing until the player lights it up. DEX-then-store-then-BNE
; writes indices 19..0 inclusive, exiting cleanly when X reaches 0
; after the final STA.
; ----------------------------------------------------------------------------
clear_vis_buffer:
        LDA     #0
        LDX     #VIS_BUFFER_SIZE
@lp:    DEX
        STA     vis_buffer,X
        BNE     @lp
        RTS


; ----------------------------------------------------------------------------
; compute_fov: re-flood the player's field of view via recursive
; shadowcasting (Björn Bergström, RogueBasin 2002). Four phases:
;
;   0. Pick fov_r — FOV_RADIUS by default, TORCH_RADIUS while a torch
;      buff is active.
;   1. Wipe vis_buffer entirely. Pure torchlight, no remembered terrain.
;   2. Light the player's own cell (cast_octant starts at depth=1, so
;      depth=0 needs a separate seed).
;   3. Loop the 8 octants. For each octant, set the (xx, xy, yx, yy)
;      transform and call cast_octant with the full cone start=1/1,
;      end=0/1 starting at depth=1.
;   4. Tail: strip_invisible_pit_reveals re-hides any revealed pit
;      that's outside the new FOV (fall-through, unchanged).
;
; Cost analysis (R=7, torch on): up to 4*R*(R+1) = 224 cell visits
; across the 8 octants; each costs ~250 cycles (slope mul + compare +
; mark + opacity test). ~56 k cycles, ~56 ms at 1 MHz, ~3 frames.
; Default R=3: 4*R*(R+1) = 48 visits, ~12 k cycles, well under one
; frame. The slower torch path is acceptable — torch is a buff with
; finite duration and the radius doubling already implies "deeper
; thinking time per turn".
; ----------------------------------------------------------------------------
compute_fov:
        ; --- Phase 0: pick the active radius. ---
        LDA     torch_timer
        BEQ     @r_default
        LDA     #TORCH_RADIUS
        JMP     @r_save
@r_default:
        LDA     #FOV_RADIUS
@r_save:
        STA     fov_r

        ; --- Phase 1: wipe vis_buffer entirely (20 bit-packed bytes,
        ; one bit per cell). Same DEX-then-store-then-BNE pattern as
        ; clear_vis_buffer; writes indices 19..0 inclusive.
        LDA     #0
        LDX     #VIS_BUFFER_SIZE
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

        ; --- Phase 3: 8 octants via the shared shadowcaster lib. ---
        JSR     shadowcast_octants
        ; Fall through into strip_invisible_pit_reveals.

; ----------------------------------------------------------------------------
; strip_invisible_pit_reveals: scan map_buffer for cells with the high
; bit set (= revealed pit) and clear the bit on cells whose vis_buffer
; entry is dark this turn. Pits that drift out of the player's torchlight
; re-hide so the player has to remember (or re-trigger) them.
;
; Called as the tail of compute_fov so every FOV recomputation propagates
; the rule "revealed iff currently lit".
; ----------------------------------------------------------------------------
strip_invisible_pit_reveals:
        LDX     #(LOGICAL_COLS * LOGICAL_ROWS) - 1
@lp:    TXA
        JSR     map_get_a               ; A = nibble (raw, reveal bit kept)
        AND     #TILE_REVEAL_BIT        ; bit 3 of the nibble
        BEQ     @next                   ; not a revealed pit
        TXA
        JSR     vis_test_a              ; A nonzero if cell in FOV
        BNE     @next                   ; lit → keep reveal
        ; Out of FOV → clear the reveal bit on this cell. Drop straight
        ; into the byte/nibble layer to skip a second map_get_a/map_set_x
        ; round-trip — pit cells are sparse so this hot path stays tight.
        TXA
        LSR                             ; A = byte index, carry = X & 1
        TAY
        BCS     @clr_hi
        LDA     map_buffer,Y
        AND     #$F7                    ; clear low-nibble reveal bit
        STA     map_buffer,Y
        JMP     @next
@clr_hi:
        LDA     map_buffer,Y
        AND     #$7F                    ; clear high-nibble reveal bit
        STA     map_buffer,Y
@next:
        DEX
        BPL     @lp
        RTS


; ----------------------------------------------------------------------------
; vis_bitmask: 1 << bit. Indexed by (linear cell index & 7) to recover
; the per-cell mask inside vis_buffer's bit-packed bytes.
; ----------------------------------------------------------------------------
vis_bitmask:
        .byte $01, $02, $04, $08, $10, $20, $40, $80


; ----------------------------------------------------------------------------
; vis_test_a: read the visibility bit at linear cell index A (0..159).
; Returns A = 0 if the cell is dark, A != 0 (the bit mask) if lit.
; Caller branches via BNE (lit) / BEQ (dark). Clobbers A, Y; preserves X.
;
; Decomposition: byte = idx >> 3 (0..19); bit = idx & 7. The bit mask
; comes from vis_bitmask[bit].
; ----------------------------------------------------------------------------
vis_test_a:
        PHA                     ; save linear index
        AND     #7              ; bit position 0..7
        TAY
        LDA     vis_bitmask,Y
        STA     vismask
        PLA                     ; A = linear index again
        LSR
        LSR
        LSR                     ; A = byte index 0..19
        TAY
        LDA     vis_buffer,Y
        AND     vismask
        RTS


; ----------------------------------------------------------------------------
; mark_visible_at_cur: set the bit-packed visibility bit at
; (cur_x, cur_y). Caller has already verified cur_x / cur_y are
; in-range. Clobbers A, Y; preserves X (cast_ray uses X as the step
; countdown).
;
; idx = cur_y * 16 + cur_x → byte = cur_y * 2 + (cur_x >> 3), bit = cur_x & 7.
; ----------------------------------------------------------------------------
mark_visible_at_cur:
        LDA     cur_x
        AND     #7
        TAY
        LDA     vis_bitmask,Y           ; mask = 1 << (cur_x & 7)
        STA     vismask
        LDA     cur_x
        LSR
        LSR
        LSR                             ; cur_x >> 3 (0 or 1)
        CLC
        ADC     cur_y
        ADC     cur_y                   ; cur_y * 2 + (cur_x >> 3)
        TAY
        LDA     vis_buffer,Y
        ORA     vismask
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
        ADC     cur_x                   ; A = linear cell index
        JSR     map_get_a               ; A = nibble; preserves X
        AND     #7                      ; strip reveal bit, A = dense tile id
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
; upload_sprite_pats: stream all sprite patterns as one 448-byte block
; to VRAM $3800. The patterns sit back-to-back in source order
; (sprite_pats), so a single auto-increment loop covers every slot.
; In 16x16 mode each name slot is 32 B (4 char patterns), so 448 B
; covers names 0..52 inclusive (14 sprites).
;   slot  0 ($3800-$381F) : player paladin
;   slot  4 ($3820-$383F) : undead         (MON_TYPE_UNDEAD)
;   slot  8 ($3840-$385F) : ghost          (MON_TYPE_GHOST)
;   slot 12 ($3860-$387F) : death          (MON_TYPE_DEATH)
;   slot 16 ($3880-$389F) : food meat      (ITEM_T_FOOD on the floor)
;   slot 20 ($38A0-$38BF) : skeleton       (MON_TYPE_SKELETON)
;   slot 24 ($38C0-$38DF) : dagger         (ITEM_T_DAGGER + thrown)
;   slot 28 ($38E0-$38FF) : potion         (ITEM_T_POTION)
;   slot 32 ($3900-$391F) : scroll         (ITEM_T_SCROLL)
;   slot 36 ($3920-$393F) : weapon-generic (ITEM_T_WEAPON)
;   slot 40 ($3940-$395F) : armor-generic  (ITEM_T_ARMOR)
;   slot 44 ($3960-$397F) : ring (amulet)  (ITEM_T_RING)
;   slot 48 ($3980-$399F) : troll grunt    (MON_TYPE_TROLL)
;   slot 52 ($39A0-$39BF) : torch          (ITEM_T_TORCH)
;   slot 56 ($39C0-$39DF) : boss top-left  (MON_TYPE_BOSS Q_TL)
;   slot 60 ($39E0-$39FF) : boss top-right (MON_TYPE_BOSS Q_TR)
;   slot 64 ($3A00-$3A1F) : boss bottom-left  (MON_TYPE_BOSS Q_BL)
;   slot 68 ($3A20-$3A3F) : boss bottom-right (MON_TYPE_BOSS Q_BR)
;
; Total = 448 B (sprite_pats) + 128 B (boss_sprite_pats) = 576 B.
; INX/BNE wraps after 256, so we stride in three loops: 256, 192, 128.
; ----------------------------------------------------------------------------
upload_sprite_pats:
        JSR     tms9918_pad12   ; MANUAL caller-gap cushion
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12   ; addr-low → addr-cmd bridge
        LDA     #$78            ; $38 | $40 -> write at $3800
        STA     VDP_CTRL
        JSR     tms9918_pad12   ; cmd → first STA VDP_DATA cushion
        ; First 256 bytes (slots 0..31, offsets 0..255 of sprite_pats).
        ; Inner loops use pad40 (hardened 40c silicon contract) instead
        ; of WRT_DATA_REG/pad12 — see upload_tileset rationale.
        LDX     #0
@lp1:   LDA     sprite_pats,X
        STA     VDP_DATA
        JSR     tms9918_pad40
        INX
        BNE     @lp1
        ; Next 192 bytes (slots 32..55, offsets 256..447 of sprite_pats)
        LDX     #0
@lp2:   LDA     sprite_pats+256,X
        STA     VDP_DATA
        JSR     tms9918_pad40
        INX
        CPX     #192
        BNE     @lp2
        ; Boss tile: 128 bytes from boss_sprite_pats → slots 56..68
        ; (VRAM $39C0..$3A3F). The auto-increment cursor is already
        ; sitting at $39C0, so we just keep streaming.
        LDX     #0
@lp3:   LDA     boss_sprite_pats,X
        STA     VDP_DATA
        JSR     tms9918_pad40
        INX
        CPX     #128
        BNE     @lp3
        RTS


; ----------------------------------------------------------------------------
; clear_hurt_flags: zero player_hurt and every MON_HURT byte EXCEPT
; on TROLL slots — for trolls, MON_HURT doubles as a "provoked" flag
; that ai_troll reads to flip from flee to chase, and that flag must
; persist until the troll dies. Every other type uses MON_HURT only
; as a one-frame red flash, so we wipe it each turn.
;
; Killed trolls have MON_TYPE=0 (not MON_TYPE_TROLL), so the CMP below
; falls through and the dead slot's MON_HURT is properly zeroed before
; the slot gets reused by the next spawn pass.
; ----------------------------------------------------------------------------
clear_hurt_flags:
        LDA     #0
        STA     player_hurt
        LDX     #0
@lp:    LDA     monsters+MON_TYPE,X
        CMP     #MON_TYPE_TROLL
        BEQ     @skip                   ; provoked-troll flag stays sticky
        LDA     #0
        STA     monsters+MON_HURT,X
@skip:
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
; Slot 1 is the always-visible dagger ammo HUD icon. Slots after that
; are one entry per LIVE monster (MON_TYPE != 0), in pool order — each
; pulls Y/X from MON_ROW/MON_COL × 16 and (name, color) straight from
; the per-monster fields. The next-after-last slot gets Y=$D0, the chip's
; chain-terminator sentinel — every later SAT entry is ignored regardless
; of contents.
;
; Logical (lcol, lrow) → pixel (lcol*16, lrow*16) so a 16x16 sprite
; sits exactly on its 16x16 logical tile. We don't bother sub-pixel-
; centring monsters into the 4-rendered-quads tile (they ride the same
; cell grid as the player, which already aligns).
; ----------------------------------------------------------------------------
place_all_sprites:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #$5B            ; $1B | $40 -> write at $1B00
        STA     VDP_CTRL
        JSR     tms9918_pad12   ; addr-cmd → first STA VDP_DATA: only 11c (now bumped to 40c via pad24)
                                ; without this (LDA zp + 4*ASL + STA = 15c)

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

        ; --- Slot 1: dagger ammo HUD. It lives on rows 20-21, above the
        ;     timer icons, so it never enters the same 4-sprites-per-
        ;     scanline group. The 2-digit count is painted by
        ;     update_hud_equip at row 21 cols 30-31.
        LDA     #160
        WRT_DATA_REG
        LDA     #224                    ; pixel x=224 (cols 28-29)
        WRT_DATA_REG
        WRT_DATA_VAL SPRITE_NAME_DAGGER
        WRT_DATA_VAL COL_DAGGER

        ; --- Slots 2..N: live monsters (one SAT entry per non-zero
        ;     MON_TYPE). FOV gate: a monster on a cell whose
        ;     VIS_VISIBLE bit is clear is dropped from the SAT entirely
        ;     — same effect as if the slot were dead this frame, no
        ;     halo/leak through fog. Monsters in seen-only cells stay
        ;     hidden too (Rogue convention: you remember the layout,
        ;     not the bestiary that walked through it).
        LDX     #0
@mon_lp:
        LDA     monsters+MON_TYPE,X
        BNE     @mon_alive
        JMP     @mon_skip               ; out-of-range branch — trampoline
@mon_alive:
        CMP     #MON_TYPE_BOSS
        BNE     @one_cell
        JMP     @boss_paint             ; out-of-range branch — trampoline
@one_cell:
        ; FOV gate: vis_test_a (linear index in A → bit-packed lookup).
        LDA     monsters+MON_ROW,X
        ASL
        ASL
        ASL
        ASL                     ; row * 16
        CLC
        ADC     monsters+MON_COL,X
        JSR     vis_test_a
        BNE     @mon_lit
        JMP     @mon_skip               ; out-of-range — trampoline
@mon_lit:
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
        JMP     @mon_skip
@boss_paint:
        ; FOV gate on the anchor cell: if the top-left of the
        ; footprint is dark, drop the whole boss from the SAT (avoids
        ; half-painted demons floating through fog). pak_byte+1 caches
        ; the boss colour across the four quadrant emissions; pak_byte
        ; caches the row * 16 base so the SBC math doesn't have to
        ; redo the shift.
        LDA     monsters+MON_ROW,X
        ASL
        ASL
        ASL
        ASL                     ; row * 16
        CLC
        ADC     monsters+MON_COL,X
        JSR     vis_test_a
        BNE     @boss_lit
        JMP     @mon_skip               ; dark anchor → drop the whole tile
@boss_lit:
        ; Pre-resolve the colour (COL_HURT during the bump-frame, else
        ; MON_COLOR). One MON_HURT load → 4 quadrants → consistent
        ; flash across the whole 32x32 silhouette.
        LDA     monsters+MON_HURT,X
        BEQ     @boss_normal
        LDA     #COL_HURT
        JMP     @boss_color_done
@boss_normal:
        LDA     monsters+MON_COLOR,X
@boss_color_done:
        STA     pak_byte+1              ; cached boss colour byte
        ; Cache row * 16 (top y) and col * 16 (left x) — used by all
        ; four quadrant SAT writes plus the +16 px sibling rows.
        LDA     monsters+MON_ROW,X
        ASL
        ASL
        ASL
        ASL
        STA     pak_byte                ; pak_byte = top y
        ; --- TL quadrant: y = top, x = left, name = TL ---
        LDA     pak_byte
        WRT_DATA_REG
        LDA     monsters+MON_COL,X
        ASL
        ASL
        ASL
        ASL
        WRT_DATA_REG                    ; left x
        WRT_DATA_VAL SPRITE_NAME_BOSS_TL
        LDA     pak_byte+1
        WRT_DATA_REG
        ; --- TR quadrant: y = top, x = left + 16, name = TR ---
        LDA     pak_byte
        WRT_DATA_REG
        LDA     monsters+MON_COL,X
        ASL
        ASL
        ASL
        ASL
        CLC
        ADC     #16
        WRT_DATA_REG                    ; left x + 16
        WRT_DATA_VAL SPRITE_NAME_BOSS_TR
        LDA     pak_byte+1
        WRT_DATA_REG
        ; --- BL quadrant: y = top + 16, x = left, name = BL ---
        LDA     pak_byte
        CLC
        ADC     #16
        WRT_DATA_REG                    ; top y + 16
        LDA     monsters+MON_COL,X
        ASL
        ASL
        ASL
        ASL
        WRT_DATA_REG
        WRT_DATA_VAL SPRITE_NAME_BOSS_BL
        LDA     pak_byte+1
        WRT_DATA_REG
        ; --- BR quadrant: y = top + 16, x = left + 16, name = BR ---
        LDA     pak_byte
        CLC
        ADC     #16
        WRT_DATA_REG
        LDA     monsters+MON_COL,X
        ASL
        ASL
        ASL
        ASL
        CLC
        ADC     #16
        WRT_DATA_REG
        WRT_DATA_VAL SPRITE_NAME_BOSS_BR
        LDA     pak_byte+1
        WRT_DATA_REG
@mon_skip:
        TXA
        CLC
        ADC     #MON_SIZE
        TAX
        CPX     #(MON_COUNT * MON_SIZE)
        BCS     @mon_done
        JMP     @mon_lp                 ; out-of-range backward branch
@mon_done:

        ; --- Slots after monsters: live items (one entry per non-zero
        ; ITEM_TYPE). Items render LAST (highest SAT slot index) which
        ; gives them the LOWEST display priority on TMS9918 — a player
        ; or monster sprite on the same cell covers the item, exactly
        ; the "loot under feet" visual we want. Items also FOV-gate:
        ; a drop in a dark cell is invisible until the player lights it.
        ; sprite name + color come from the item_sprite_table /
        ; item_color_table indexed by ITEM_TYPE.
        LDX     #0
@item_lp:
        LDA     items+ITEM_TYPE,X
        BEQ     @item_skip
        ; FOV gate: vis_test_a (linear index in A → bit-packed lookup).
        LDA     items+ITEM_ROW,X
        ASL
        ASL
        ASL
        ASL
        CLC
        ADC     items+ITEM_COL,X
        JSR     vis_test_a
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
        ; Sprite name byte: dispatch on ITEM_TYPE (1..7).
        LDA     items+ITEM_TYPE,X
        TAY
        LDA     item_sprite_table,Y
        WRT_DATA_REG
        LDA     item_color_table,Y
        WRT_DATA_REG
@item_skip:
        TXA
        CLC
        ADC     #ITEM_SIZE
        TAX
        CPX     #(ITEM_COUNT * ITEM_SIZE)
        BCC     @item_lp

        ; --- Buff-timer sprites at the bottom of the screen (y=176,
        ;     covering rows 22-23). One SAT entry per ACTIVE timer
        ;     (zero timer → no entry, no pixel cost). Companion 2-digit
        ;     countdowns are painted to the immediately-right name-table
        ;     cells by update_hud_timers — the alternating SPR / DIG
        ;     layout reads as "icon NN  icon NN  icon NN  icon NN".
        ;     Layout (left-packed against col 0, digits sit on row 23
        ;     immediately right of each sprite — sprites span rows 22-23,
        ;     digits live in the SPRITE-FREE column to the right):
        ;       WPN: sprite px=  0 (cols  0-1)   digits row 23 cols  2-3
        ;       ARM: sprite px= 32 (cols  4-5)   digits row 23 cols  6-7
        ;       RNG: sprite px= 64 (cols  8-9)   digits row 23 cols 10-11
        ;       TRC: sprite px= 96 (cols 12-13)  digits row 23 cols 14-15
        ;     Dagger ammo uses y=160 (rows 20-21) so it doesn't compete
        ;     with the timer icons on rows 22-23.
        ;     Nothing gameplay-side ever lives at y >= 160 (logical row 9
        ;     ends at y=159).
        LDA     weapon_timer
        BEQ     @no_wpn_spr
        LDA     #176
        WRT_DATA_REG
        LDA     #0                      ; pixel x=0 — leftmost slot
        WRT_DATA_REG
        WRT_DATA_VAL SPRITE_NAME_WEAPON
        WRT_DATA_VAL COL_WEAPON
@no_wpn_spr:
        LDA     armor_timer
        BEQ     @no_arm_spr
        LDA     #176
        WRT_DATA_REG
        LDA     #32                     ; pixel x=32 (cols 4-5)
        WRT_DATA_REG
        WRT_DATA_VAL SPRITE_NAME_ARMOR
        WRT_DATA_VAL COL_ARMOR
@no_arm_spr:
        LDA     ring_timer
        BEQ     @no_rng_spr
        LDA     #176
        WRT_DATA_REG
        LDA     #64                     ; pixel x=64 (cols 8-9)
        WRT_DATA_REG
        WRT_DATA_VAL SPRITE_NAME_RING
        WRT_DATA_VAL COL_RING
@no_rng_spr:
        LDA     torch_timer
        BEQ     @no_trc_spr
        LDA     #176
        WRT_DATA_REG
        LDA     #96                     ; pixel x=96 (cols 12-13)
        WRT_DATA_REG
        WRT_DATA_VAL SPRITE_NAME_TORCH
        WRT_DATA_VAL COL_TORCH
@no_trc_spr:

        ; --- Optional thrown-projectile slot (handle_throw sets this
        ;     while a dagger is mid-flight). Inserted BEFORE the chain
        ;     terminator so the chip actually scans + draws it. cur_x /
        ;     cur_y carry the current frame's logical position; pixel
        ;     coords come from <<4 just like every other entity. ---
        LDA     throw_active
        BEQ     @term
        LDA     cur_y
        ASL
        ASL
        ASL
        ASL
        WRT_DATA_REG
        LDA     cur_x
        ASL
        ASL
        ASL
        ASL
        WRT_DATA_REG
        WRT_DATA_VAL SPRITE_NAME_DAGGER
        WRT_DATA_VAL COL_DAGGER
@term:
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
        ; way you came is forbidden (treated as a hard wall).
        ; TILE_TRAP_PIT IS allowed — the player walks in (visible or not)
        ; and trigger_pit deals damage in main_loop after the move.
        LDA     tmp
        CMP     #TILE_EMPTY
        BEQ     @pass
        CMP     #TILE_STAIRS_DOWN
        BEQ     @pass
        CMP     #TILE_TRAP_PIT
        BEQ     @pass
@blocked:
        SEC
        RTS
@pass:
        CLC
        RTS


; ----------------------------------------------------------------------------
; tile_at_target: read the dense tile id at (tgt_col, tgt_row).
; Returns A = tile id (bits 0..2 only — pit-reveal bit stripped) so
; callers compare to TILE_* (dense) without caring whether a pit is
; revealed or hidden. Preserves X. Clobbers A, Y, tmp_packed.
;
; Byte index = tgt_row * 8 + tgt_col / 2; nibble select = tgt_col & 1.
; ----------------------------------------------------------------------------
tile_at_target:
        LDA     tgt_col
        LSR                             ; A = tgt_col / 2
        STA     tmp_packed
        LDA     tgt_row
        ASL
        ASL
        ASL                             ; row * 8
        CLC
        ADC     tmp_packed              ; + col / 2
        TAY                             ; Y = byte index
        LDA     tgt_col
        AND     #1
        BNE     @tg_hi
        LDA     map_buffer,Y
        AND     #7                      ; low nibble, reveal stripped
        RTS
@tg_hi:
        LDA     map_buffer,Y
        LSR
        LSR
        LSR
        LSR                             ; high nibble down to low
        AND     #7                      ; reveal stripped
        RTS


; ----------------------------------------------------------------------------
; reveal_pit_at_target: set the pit-reveal bit (bit 3 of the cell
; nibble) at (tgt_col, tgt_row). Used by trigger_pit (player) and
; apply_step's @move_pit branch (monster) so render_map switches the
; cell from blank-floor to visible pit graphic. The bit is cleared
; again by strip_invisible_pit_reveals once the cell drifts out of FOV.
; Preserves X. Clobbers A, Y, tmp_packed.
;
; Low nibble: OR #$08. High nibble: OR #$80 (bit 3 of the high nibble
; = bit 7 of the byte).
; ----------------------------------------------------------------------------
reveal_pit_at_target:
        LDA     tgt_col
        LSR
        STA     tmp_packed
        LDA     tgt_row
        ASL
        ASL
        ASL
        CLC
        ADC     tmp_packed
        TAY
        LDA     tgt_col
        AND     #1
        BNE     @rv_hi
        LDA     map_buffer,Y
        ORA     #$08
        STA     map_buffer,Y
        RTS
@rv_hi:
        LDA     map_buffer,Y
        ORA     #$80
        STA     map_buffer,Y
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
        JSR     tms9918_pad12   ; MANUAL caller-gap cushion (12c, was 40c pre-openMSX-port)
        STA     VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STX     VDP_CTRL
                                ; raw gap is LDY+LDA(zp,Y)+CMP+BEQ+STA = 15c)
        LDY     #0
@lp:    LDA     (vdp_src_lo),Y
        CMP     #$FF
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BEQ     @done
        WRT_DATA_REG
        INY
        BNE     @lp
@done:  RTS


; ----------------------------------------------------------------------------
; update_hud: paint the 4-row HUD below the 32x20 playfield. Layout:
;
;   row 20: DEPTH NNN                         [dagger sprite top]
;   row 21: ATK:NN DEF:NN                     [dagger sprite] NN
;   row 22: timers' sprite tops                         HP HH/HM
;   row 23: timer digits                         XP:NNN
;
; Dagger ammo now lives above the timer row to avoid the TMS9918
; 4-sprites-per-scanline limit; HP and XP stay right-anchored on the
; last two HUD text rows.
; ----------------------------------------------------------------------------
update_hud:
        JSR     tms9918_pad12   ; MANUAL caller-gap cushion (12c, was 40c pre-openMSX-port)
        LDA     #$80                    ; row 20 col 0 = $1A80
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA     VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #$5A                    ; $1A | $40 -> write
        STA     VDP_CTRL
        JSR     tms9918_pad12           ; gap addr-cmd → first data STA
        ; --- "DEPTH " (6 chars) ---
        LDX     #0
@d_lp:  LDA     hud_depth,X
        WRT_DATA_REG
        INX
        CPX     #6
        BNE     @d_lp
        LDA     depth
        JSR     print_byte_3digits
        ; --- 23 spaces of right-padding (cols 9..31). The dagger sprite
        ;     covers cols 28-29, so the name-table below it must be blank. ---
        LDA     #' '
        LDX     #23
@sp_lp: WRT_DATA_REG
        DEX
        BNE     @sp_lp
        ; Fall through into update_hud_equip — paints row 21 with the
        ; computed ATK/DEF and dagger ammo count.

; ----------------------------------------------------------------------------
; update_hud_equip: paint row 21 with combat stats + dagger ammo.
;   "ATK:NN DEF:NN               [dagger]NN"
;    cols 0..12                         cols 30..31
; ATK = computed player_dmg (weapon + str_bonus + ring_str),
; DEF = computed player_def (armor + ring_prot),
; dagger_qty = ammo counter (0..99, zero-padded). The 17 spaces between
; DEF and the count keep cols 28-29 blank under the 16x16 dagger sprite.
; ----------------------------------------------------------------------------
update_hud_equip:
        LDA     #21
        STA     vdp_row
        LDA     #0
        STA     vdp_col
        JSR     name_at_rc
        JSR     vdp_set_write
        WRT_DATA_VAL 'A'
        WRT_DATA_VAL 'T'
        WRT_DATA_VAL 'K'
        WRT_DATA_VAL ':'
        LDA     player_dmg
        JSR     print_byte_2digits
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        WRT_DATA_VAL ' '
        WRT_DATA_VAL 'D'
        WRT_DATA_VAL 'E'
        WRT_DATA_VAL 'F'
        WRT_DATA_VAL ':'
        LDA     player_def
        JSR     print_byte_2digits
        ; 17 spaces of right-padding (cols 13..29).
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #' '
        LDX     #17
@sp_lp: WRT_DATA_REG
        DEX
        BNE     @sp_lp
        LDA     dagger_qty
        JSR     print_byte_2digits
        ; Fall through into update_hud_hp — paints row 22 with right-
        ; anchored HP before row 23's timers + XP.

; ----------------------------------------------------------------------------
; update_hud_hp: paint HP on row 22 cols 24..31.
; ----------------------------------------------------------------------------
update_hud_hp:
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #22
        STA     vdp_row
        LDA     #24
        STA     vdp_col
        JSR     name_at_rc
        JSR     vdp_set_write
        LDX     #0
@h_lp:  LDA     hud_hp,X
        WRT_DATA_REG
        INX
        CPX     #3
        BNE     @h_lp
        LDA     hp
        JSR     print_byte_2digits
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #'/'
        WRT_DATA_REG
        LDA     hp_max                  ; XP-bumpable cap, not literal
        JSR     print_byte_2digits
        ; Fall through into update_hud_timers — paints row 23 with
        ; the four buff countdowns (weapon / armor / ring / torch),
        ; flush against the left edge, plus XP on the right.

; ----------------------------------------------------------------------------
; update_hud_timers: paint the digit countdowns next to each active
; buff sprite at the very bottom of the screen, plus XP at the far
; right. The matching item sprites are
; emitted by place_all_sprites at y=176 (rows 22-23); this routine writes
; the 2-digit text on row 23 immediately to the right of each sprite
; footprint.
;
; Stream-paint row 23 cols 2..31 — leading sprite footprint
; at cols 0-1 is left blank by clear_msg_rows. Inactive timers paint
; spaces; the auto-increment cursor still has to advance through every
; cell, so the gap-space writes interleaved with the digit pairs are
; mandatory even for "no buff" runs.
;
;   col:  2-3   4-5    6-7   8-9    10-11  12-13  14-15 ... 26-31
;         WPN n [ARM]  ARM n [RNG]  RNG n  [TRC]  TRC n      XP:NNN
;         (sprite footprints for ARM/RNG/TRC sit on the gap cols;
;          the WPN sprite footprint sits at cols 0-1 to the LEFT of
;          the digits stream, untouched by this routine.)
; ----------------------------------------------------------------------------
update_hud_timers:
        JSR     tms9918_pad12   ; MANUAL caller-gap cushion (12c, was 40c pre-openMSX-port)
        LDA     #$E2                    ; row 23 col 2 = $1AE2
        STA     VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #$5A                    ; $1A | $40
        STA     VDP_CTRL
        JSR     tms9918_pad12           ; gap addr-cmd → first WRT_DATA_VAL

        ; Slot 0: WPN digits (cols 2-3)
        LDA     weapon_timer
        BEQ     @wpn_blank
        JSR     print_byte_2digits
        JMP     @gap_arm
@wpn_blank:
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        WRT_DATA_VAL ' '
        WRT_DATA_VAL ' '
@gap_arm:
        WRT_DATA_VAL ' '                ; cols 4-5: ARM sprite footprint
        WRT_DATA_VAL ' '

        ; Slot 1: ARM digits (cols 6-7)
        LDA     armor_timer
        BEQ     @arm_blank
        JSR     print_byte_2digits
        JMP     @gap_rng
@arm_blank:
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        WRT_DATA_VAL ' '
        WRT_DATA_VAL ' '
@gap_rng:
        WRT_DATA_VAL ' '                ; cols 8-9: RNG sprite footprint
        WRT_DATA_VAL ' '

        ; Slot 2: RNG digits (cols 10-11)
        LDA     ring_timer
        BEQ     @rng_blank
        JSR     print_byte_2digits
        JMP     @gap_trc
@rng_blank:
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        WRT_DATA_VAL ' '
        WRT_DATA_VAL ' '
@gap_trc:
        WRT_DATA_VAL ' '                ; cols 12-13: TRC sprite footprint
        WRT_DATA_VAL ' '

        ; Slot 3: TRC digits (cols 14-15)
        LDA     torch_timer
        BEQ     @trc_blank
        JSR     print_byte_2digits
        JMP     @xp_gap
@trc_blank:
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        WRT_DATA_VAL ' '
        WRT_DATA_VAL ' '
@xp_gap:
        ; Clear cols 16-25, then write XP at cols 26-31.
        LDA     #' '
        LDX     #10
@xp_sp_lp:
        WRT_DATA_REG
        DEX
        BNE     @xp_sp_lp
        WRT_DATA_VAL 'X'
        WRT_DATA_VAL 'P'
        WRT_DATA_VAL ':'
        LDA     player_xp
        JSR     print_byte_3digits
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
; Order: regen tick (RING_F_REGEN heals a slow drip) → HUD repaint →
; death check. Regen runs FIRST so a tick that brings HP to >=1 can
; rescue the player from a fatal bump in the same turn — matches
; classic Rogue's "ring of regeneration absorbs the killing blow"
; feel.
; ----------------------------------------------------------------------------
finish_turn:
        ; --- Regen tick (RING_F_REGEN). regen_tick counts down from
        ; RING_REGEN_PERIOD; on 0 it fires +1 HP and reloads. The
        ; init-state regen_tick = 0 means the first turn after equipping
        ; the ring fires immediately (a tiny grace bonus).
        LDA     ring_flags
        AND     #RING_F_REGEN
        BEQ     @no_regen
        LDA     regen_tick
        BNE     @regen_dec
        LDA     #RING_REGEN_PERIOD
        STA     regen_tick
        LDA     hp
        CMP     hp_max                  ; runtime cap, not literal
        BCS     @no_regen
        INC     hp
        JMP     @no_regen
@regen_dec:
        DEC     regen_tick
@no_regen:
        ; --- Buff timers: weapon / armor / torch ---
        ; Each ticks down by 1 per turn. Weapon + armor expiries call
        ; recompute_player_stats so player_dmg / player_def drop the
        ; boost cleanly. Torch expiry doesn't need a recompute (FOV is
        ; recomputed on the next move from compute_fov).
        LDA     weapon_timer
        BEQ     @no_weapon_tick
        DEC     weapon_timer
        BNE     @no_weapon_tick
        JSR     recompute_player_stats
@no_weapon_tick:
        LDA     armor_timer
        BEQ     @no_armor_tick
        DEC     armor_timer
        BNE     @no_armor_tick
        JSR     recompute_player_stats
@no_armor_tick:
        LDA     torch_timer
        BEQ     @no_torch_tick
        DEC     torch_timer
@no_torch_tick:
        ; Ring expiry clears the active RING_F_* bit out of ring_flags.
        ; AND-with-complement of ring_boost so a future second-bit
        ; ring (e.g. RING_F_PROT) coexists if both are activated and
        ; only the expired one drops.
        LDA     ring_timer
        BEQ     @no_ring_tick
        DEC     ring_timer
        BNE     @no_ring_tick
        LDA     ring_boost
        EOR     #$FF
        AND     ring_flags
        STA     ring_flags
        LDA     #0
        STA     ring_boost
@no_ring_tick:
        ; Order matters: clear_msg_rows wipes rows 22-23 FIRST (any
        ; transient "INV FULL" / "NOT WEARABLE" prompt left from an
        ; earlier free-action handler), then update_hud repaints rows
        ; 20-23 — including HP/XP and the buff timer digits, which would
        ; otherwise be zapped if clear_msg_rows ran second.
        JSR     clear_msg_rows
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR     update_hud
        LDA     hp
        BNE     @alive
        JMP     death_screen            ; tail-call (never returns)
@alive:
        RTS


; ----------------------------------------------------------------------------
; clear_msg_rows: blank rows 22 and 23 (64 chars total) by writing
; ' ' across them via auto-increment from VRAM $1AC0. render_map only
; touches rows 0-19, so transient messages parked on row 23 by
; print_msg_row would otherwise persist forever — finish_turn now
; calls this on every accepted action, which is the natural moment to
; clear "stale prompt residue" before the new turn's HUD repaints.
; ----------------------------------------------------------------------------
clear_msg_rows:
        JSR     tms9918_pad12   ; MANUAL caller-gap cushion (12c, was 40c pre-openMSX-port)
        LDA     #$C0
        STA     VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #$5A                    ; $1A | $40
        STA     VDP_CTRL
        JSR     tms9918_pad12           ; gap addr-cmd → first WRT_DATA_VAL
        LDX     #64
@lp:    WRT_DATA_VAL ' '
        DEX
        BNE     @lp
        RTS

; ----------------------------------------------------------------------------
; clear_msg_row23: blank just row 23 (32 chars) — used by transient-prompt
; cancellers (handle_throw cancel) that need to wipe the on-screen prompt
; without disturbing row 22's HP line. Same VDP-write recipe as
; clear_msg_rows but starts at $1AE0 (row 23 col 0) and emits 32 spaces.
; ----------------------------------------------------------------------------
clear_msg_row23:
        JSR     tms9918_pad12   ; MANUAL caller-gap cushion (12c, was 40c pre-openMSX-port)
        LDA     #$E0
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDA     #$5A                    ; $1A | $40
        STA     VDP_CTRL
        JSR     tms9918_pad12
        LDX     #32
@lp:    WRT_DATA_VAL ' '
        DEX
        BNE     @lp
        RTS


; ----------------------------------------------------------------------------
; death_screen: full GAME OVER takeover screen. Wipes the playfield,
; paints the banner + cause-of-death + detailed stats block, then waits
; for an acknowledging keypress before cold-starting the cartridge
; (JMP $4000). Reached from finish_turn when hp == 0.
;
; Layout (32x24):
;   row  3:           GAME OVER
;   row  6:      YOU DIED ON LEVEL NNN
;   row  7:        KILLED BY <NAME>
;   row  9..13: stats block (DEPTH / KILLS / HP MAX / ATK / DEF)
;   row 16:           PRESS ANY KEY
; ----------------------------------------------------------------------------
death_screen:
        JSR     disable_sprites         ; hide hero + monsters under the splash
        JSR     clear_name_table
        ; "GAME OVER" (9 chars) — row 3 col 11 → $186B
        LDA     #<msg_game_over
        STA     vdp_src_lo
        LDA     #>msg_game_over
        STA     vdp_src_hi
        LDA     #$6B
        LDX     #$58                    ; $18 | $40
        JSR     draw_text
        ; "YOU DIED ON LEVEL " (18 chars) — row 6 col 5 → $18C5
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #<msg_died
        STA     vdp_src_lo
        LDA     #>msg_died
        STA     vdp_src_hi
        LDA     #$C5
        LDX     #$58
        JSR     draw_text
        LDA     depth
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR     print_byte_3digits
        ; "KILLED BY " (10 chars) — row 7 col 7 → $18E7. Killer name
        ; appended right after at $18F1 via a second draw_text call;
        ; the name comes from the mon_name_lo/hi table indexed by
        ; last_attacker (0..7; clamps to UNKNOWN if out of range).
        ; Slots: 0=UNKNOWN, 1..5=MON_TYPE_*, 6=MON_TYPE_BOSS,
        ; 7=LAST_ATTACKER_PIT.
        LDA     #<msg_killed_by
        STA     vdp_src_lo
        LDA     #>msg_killed_by
        STA     vdp_src_hi
        LDA     #$E7
        LDX     #$58
        JSR     draw_text
        LDX     last_attacker
        CPX     #8                      ; valid range: 0..7
        BCC     @attacker_ok
        LDX     #0                      ; out-of-range → UNKNOWN slot
@attacker_ok:
        LDA     mon_name_lo,X
        STA     vdp_src_lo
        LDA     mon_name_hi,X
        STA     vdp_src_hi
        LDA     #$F1                    ; row 7 col 17 (right after "KILLED BY ")
        LDX     #$58
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR     draw_text
        ; Detailed stats + footer
        JSR     paint_scores
        JMP     wait_for_restart


; ----------------------------------------------------------------------------
; win_screen: full CONGRATULATIONS takeover screen for the player who
; reaches the bottom of the dungeon (depth 13 — see the new_level
; depth-cap branch). Same layout shape as death_screen so the two
; endings feel like sister ceremonies.
;
; Layout:
;   row  3:        CONGRATULATIONS
;   row  6:         DUNGEON CONQUERED
;   row  9..13: stats block (DEPTH / KILLS / HP MAX / ATK / DEF)
;   row 16:           PRESS ANY KEY
; ----------------------------------------------------------------------------
win_screen:
        JSR     clear_name_table
        ; "CONGRATULATIONS" (15 chars) — row 3 col 8 → $1868
        LDA     #<msg_congrats
        STA     vdp_src_lo
        LDA     #>msg_congrats
        STA     vdp_src_hi
        LDA     #$68
        LDX     #$58
        JSR     draw_text
        ; "DUNGEON CONQUERED" (17 chars) — row 6 col 7 → $18C7
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #<msg_won
        STA     vdp_src_lo
        LDA     #>msg_won
        STA     vdp_src_hi
        LDA     #$C7
        LDX     #$58
        JSR     draw_text
        ; Detailed stats + footer
        JSR     paint_scores
        JMP     wait_for_restart


; ----------------------------------------------------------------------------
; paint_scores: fill rows 9..13 with the end-of-game stats block at
; col 10, then "PRESS ANY KEY" at row 16 col 9. Shared by death_screen
; and win_screen.
;
; Stats sourced from current ZP at end-of-game:
;   DEPTH      = depth                       (3 digits)
;   KILLS      = player_xp                   (3 digits)
;   HP MAX     = hp_max (XP-bumpable cap)    (2 digits)
;   ATK BASE   = 1 + xp_atk_bonus            (2 digits, includes
;                                             bare-handed +1 base)
;   DEF BASE   = xp_def_bonus                (2 digits)
; "BASE" suffixes flag these as the player's permanent (XP-driven)
; growth — buff timers are gone from the picture by the time we paint.
; ----------------------------------------------------------------------------
paint_scores:
        ; Sync to VBlank before the score-screen rebuild burst.
        WAIT_VBLANK
        ; row 9 col 10 → $192A
        LDA     #<msg_score_depth
        STA     vdp_src_lo
        LDA     #>msg_score_depth
        STA     vdp_src_hi
        LDA     #$2A
        LDX     #$59                    ; $19 | $40
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR     draw_text
        LDA     depth
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR     print_byte_3digits
        ; row 10 col 10 → $194A
        LDA     #<msg_kills
        STA     vdp_src_lo
        LDA     #>msg_kills
        STA     vdp_src_hi
        LDA     #$4A
        LDX     #$59
        JSR     draw_text
        LDA     player_xp
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR     print_byte_3digits
        ; row 11 col 10 → $196A
        LDA     #<msg_score_hp
        STA     vdp_src_lo
        LDA     #>msg_score_hp
        STA     vdp_src_hi
        LDA     #$6A
        LDX     #$59
        JSR     draw_text
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA     hp_max
        JSR     print_byte_2digits
        ; row 12 col 10 → $198A
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #<msg_score_atk
        STA     vdp_src_lo
        LDA     #>msg_score_atk
        STA     vdp_src_hi
        LDA     #$8A
        LDX     #$59
        JSR     draw_text
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA     xp_atk_bonus
        CLC
        ADC     #1                      ; bare-handed +1 base
        JSR     print_byte_2digits
        ; row 13 col 10 → $19AA
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #<msg_score_def
        STA     vdp_src_lo
        LDA     #>msg_score_def
        STA     vdp_src_hi
        LDA     #$AA
        LDX     #$59
        JSR     draw_text
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA     xp_def_bonus
        JSR     print_byte_2digits
        ; "PRESS ANY KEY" — row 16 col 9 → $1A09
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #<msg_press_key
        STA     vdp_src_lo
        LDA     #>msg_press_key
        STA     vdp_src_hi
        LDA     #$09
        LDX     #$5A
        JSR     draw_text
        RTS


; ----------------------------------------------------------------------------
; wait_for_restart: ~1.3 s deaf-time (busy loop, ignores the KBD) so a
; held movement key from the killing turn can't insta-restart, then
; wait_key for an explicit acknowledgment, then JMP $4000 to cold-start
; the cartridge (re-seeds PRNG, re-runs init_inventory). Shared by
; death_screen and win_screen.
;
; Loop budget @ 1.022 MHz:
;   inner  : INY/BNE = 5 c × 256 iters = 1280 c
;   middle : (1280 + INX/BNE) × 256 = ~329 k c
;   outer  : ~329 k × 4 = ~1.3 M c ≈ 1.3 s
; mon_abs_dx is dead state on the end-of-game path so we reuse it as
; the outer counter rather than burn another ZP byte.
; ----------------------------------------------------------------------------
wait_for_restart:
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
        LDA     KBD                     ; drain latched key
        JSR     wait_key                ; explicit acknowledge press
        JMP     $4000                   ; cartridge cold-start


hud_depth:      .byte "DEPTH "                  ; 6 chars (left-anchored)
hud_hp:         .byte "HP "                     ; 3 chars (right-anchored;
                                                ;   no leading space — the
                                                ;   15-char gap lives in
                                                ;   update_hud directly)
msg_died:       .byte "YOU DIED ON LEVEL ", $FF
msg_killed_by:  .byte "KILLED BY ", $FF       ; 10 chars; lookup table below
                                              ; supplies the trailing name
                                              ; (longest = "SKELETON", 8 chars).
mon_name_unknown: .byte "UNKNOWN",  $FF
mon_name_undead:  .byte "UNDEAD",   $FF
mon_name_ghost:   .byte "GHOST",    $FF
mon_name_troll:   .byte "TROLL",    $FF
mon_name_skel:    .byte "SKELETON", $FF
mon_name_death:   .byte "DEATH",    $FF
mon_name_boss:    .byte "THE DEMON", $FF      ; MON_TYPE_BOSS
mon_name_pit:     .byte "A PIT",    $FF       ; sentinel index 7
; Indexed by last_attacker (0 = no killer yet → UNKNOWN, 1..5 = MON_TYPE_*
; for the random tiers, 6 = MON_TYPE_BOSS, 7 = LAST_ATTACKER_PIT).
mon_name_lo:
        .byte <mon_name_unknown, <mon_name_undead, <mon_name_ghost
        .byte <mon_name_troll,   <mon_name_skel,   <mon_name_death
        .byte <mon_name_boss,    <mon_name_pit
mon_name_hi:
        .byte >mon_name_unknown, >mon_name_undead, >mon_name_ghost
        .byte >mon_name_troll,   >mon_name_skel,   >mon_name_death
        .byte >mon_name_boss,    >mon_name_pit
msg_kills:      .byte "KILLS: ", $FF
msg_game_over:  .byte "GAME OVER", $FF
msg_congrats:   .byte "CONGRATULATIONS", $FF
msg_won:        .byte "DUNGEON CONQUERED", $FF
msg_score_depth:.byte "DEPTH: ", $FF
msg_score_hp:   .byte "HP MAX: ", $FF
msg_score_atk:  .byte "ATK BASE: ", $FF
msg_score_def:  .byte "DEF BASE: ", $FF
title_inv:      .byte "INVENTORY", $FF
msg_press_key:  .byte "PRESS ANY KEY", $FF
msg_empty_inv:  .byte "-NOTHING-", $FF       ; same length as "(NOTHING)" so
                                              ; centring at col 11 still
                                              ; lands cleanly. Quale font's
                                              ; '(' / ')' glyphs are broken
                                              ; (extracted as plain vertical
                                              ; bars), so dashes here.
msg_dir_q:      .byte "DIRECTION? ", $FF
msg_not_use:    .byte "NOT USABLE", $FF
msg_no_dagger:  .byte "NO DAGGER", $FF        ; throw with empty bag
msg_no_room:    .byte "NO ROOM", $FF


; ----------------------------------------------------------------------------
; redraw_game: full repaint pipeline used by modal exits (inventory,
; equip prompts, death-screen-precursor). clear_name_table wipes the
; old modal text, render_map redraws the playfield from map_buffer +
; vis_buffer, place_all_sprites rebuilds the SAT, update_hud paints
; the bottom HUD strip. compute_fov is NOT re-run here — visibility
; hasn't changed (no movement happened during the modal).
; ----------------------------------------------------------------------------
redraw_game:
        JSR     clear_name_table
        JSR     render_map
        JSR     place_all_sprites
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR     update_hud
        RTS


; ----------------------------------------------------------------------------
; show_inventory: full-screen modal listing every non-empty bag slot.
; Daggers are deliberately absent: they live in dagger_qty and are shown
; on the HUD, not as lettered inventory entries.
; Blocks until any key is pressed, then redraws the game view. Each
; slot occupies TWO display rows: the item's 16x16 sprite at (col 1,
; row N) sits to the left of "[L] *NAME UTILITY" on row N. The utility
; tag spells out what the item DOES so the player can read its effect
; on ATK / DEF / HP without leaving the modal. Row N+1 is reserved for
; the "(xQ)" stack count on stackables (food / dagger ≥ 2) and stays
; blank otherwise. The gameplay SAT is overwritten so the player +
; visible monsters + floor items disappear for the modal's duration.
; Format:
;
;   [SPRITE] [A] *SWORD ATK+2       ← weapon: '*' = equipped
;   [SPRITE] [B]  TUNIC DEF+2       ← armor : '*' = worn
;   [SPRITE] [C] *AMULET REGEN      ← ring  : '*' = on
;   [SPRITE] [D]  POTION HP+8       ← potion: heal on use
;   [SPRITE] [E]  SCROLL REVEAL     ← scroll: full-map reveal
;   [SPRITE] [F]  RATION HP+3       ← food
;                 (X3)                  ← stack count on row N+1
;
; 9 slots fit (rows 3..20). Empty inventory shows "(NOTHING)" centred.
; The footer is constant "PRESS ANY KEY" at row 22, col 9.
;
; Layout uses lib's name_at_rc(vdp_row, vdp_col) → vdp_lo/hi → drawn
; via WRT_DATA_REG (auto-increment within the row keeps every char on
; the same name-table line). The slot-letter math:
;   X = byte offset 0..200, slot index = X >> 3 (each slot is 8 B),
;   letter = 'A' + (X >> 3).
; map_ptr is reused as scratch for "current display row" so the slot
; loop can survive the JSRs that touch tmp.
; ----------------------------------------------------------------------------
show_inventory:
        ; Sync to VBlank before the inventory-screen rebuild burst.
        WAIT_VBLANK
        ; Rebuild the SAT from scratch — every gameplay sprite (player +
        ; visible monsters + floor items) is gone, replaced by one
        ; pictogram per non-empty bag slot at (col 1, row N) so each
        ; item's icon sits to the left of its "[L] *NAME" line. A
        ; Y=$D0 chain terminator follows the last sprite, so anything
        ; that was on the SAT past our writes is ignored. redraw_game
        ; on exit calls place_all_sprites which rebuilds the full SAT.
        ; map_ptr tracks the same display row used by the text pass
        ; below so the sprite row matches the slot's text row.
        LDA     #$00
        STA     VDP_CTRL
        NOP
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #$5B                    ; $1B | $40 → write at $1B00
        STA     VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #3
        STA     map_ptr
        LDX     #0
@spr_lp:
        LDA     inventory+INV_TYPE,X
        BEQ     @spr_next
        CMP     #ITEM_T_DAGGER
        BEQ     @spr_next               ; legacy/corrupt slots stay hidden
        LDA     map_ptr
        CMP     #21                     ; same cap as text pass — leaves
        BCS     @spr_next               ; rows 21/22 free for footer
        LDA     map_ptr
        SEC
        SBC     #1                      ; shift sprite up by half its height
        ASL
        ASL
        ASL                             ; (row - 1) * 8 = pixel Y
        WRT_DATA_REG
        WRT_DATA_VAL 80                 ; pixel X = col 10 (16x16 sprite
                                        ; occupies tile cols 10..11 — sits
                                        ; between "[A] qtyx" prefix and
                                        ; the item NAME on the same row)
        LDA     inventory+INV_TYPE,X
        TAY
        LDA     item_sprite_table,Y
        WRT_DATA_REG
        LDA     item_color_table,Y
        WRT_DATA_REG
        INC     map_ptr                 ; sprite Y stride matches the
        INC     map_ptr                 ; 2-row text stride (16 px → no
                                        ; scanline overlap, no 4-per-line
                                        ; pressure)
@spr_next:
        TXA
        CLC
        ADC     #INV_SIZE
        TAX
        CPX     #(INV_COUNT * INV_SIZE)
        BCC     @spr_lp
        WRT_DATA_VAL $D0                ; chain terminator
        JSR     clear_name_table
        ; Header "INVENTORY" centred on row 1.
        LDA     #1
        STA     vdp_row
        LDA     #11
        STA     vdp_col
        JSR     name_at_rc
        LDA     #<title_inv
        STA     vdp_src_lo
        LDA     #>title_inv
        STA     vdp_src_hi
        LDA     vdp_lo
        LDX     vdp_hi
        JSR     draw_text
        ; Walk the bag. map_ptr[0] = next display row (starts at 3),
        ; X = current slot byte offset. Each slot now occupies TWO
        ; display rows because the 16x16 inline sprite at col 10..11
        ; spans 2 tile rows; row N carries "[L] QxSS NAME UTIL" text,
        ; row N+1 is blank text (sprite bottom half is rendered there).
        ; Cap at row 21 leaves row 22 for the footer.
        LDA     #3
        STA     map_ptr
        LDX     #0
@slot_lp:
        LDA     inventory+INV_TYPE,X
        BEQ     @slot_next
        CMP     #ITEM_T_DAGGER
        BEQ     @slot_next              ; daggers are HUD ammo, not bag UI
        LDA     map_ptr
        CMP     #21
        BCS     @slot_next              ; bag is huge → silently truncate
        STX     tgt_col                 ; preserve slot offset across helpers
                                        ; (tgt_col is dead during modal — set
                                        ; only by handle_input, consumed by
                                        ; main_loop already)
        JSR     draw_inv_line
        LDX     tgt_col
        INC     map_ptr                 ; +1 row for the sprite bottom half
        INC     map_ptr                 ; +1 row before next entry start
@slot_next:
        TXA
        CLC
        ADC     #INV_SIZE
        TAX
        CPX     #(INV_COUNT * INV_SIZE)
        BCC     @slot_lp
        ; If nothing was drawn (map_ptr still 3), show "(NOTHING)".
        LDA     map_ptr
        CMP     #3
        BNE     @footer
        LDA     #4
        STA     vdp_row
        LDA     #11
        STA     vdp_col
        JSR     name_at_rc
        LDA     #<msg_empty_inv
        STA     vdp_src_lo
        LDA     #>msg_empty_inv
        STA     vdp_src_hi
        LDA     vdp_lo
        LDX     vdp_hi
        JSR     draw_text
@footer:
        ; "PRESS ANY KEY" at row 22 col 9.
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA     #22
        STA     vdp_row
        LDA     #9
        STA     vdp_col
        JSR     name_at_rc
        LDA     #<msg_press_key
        STA     vdp_src_lo
        LDA     #>msg_press_key
        STA     vdp_src_hi
        LDA     vdp_lo
        LDX     vdp_hi
        JSR     draw_text
        ; Drain any pending key from the 'I' press, then wait for a
        ; fresh keypress (drain mirrors death_screen's pattern so a held
        ; 'I' doesn't insta-close the modal).
        LDA     KBD
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR     wait_key
        ; If the player typed an in-range letter pointing at a non-empty
        ; slot, jump straight into the use/equip/throw dispatch — no
        ; need to close the modal and re-prompt with 'E'. Anything else
        ; (non-letter, empty slot, out of range) just dismisses.
        AND     #$7F
        CMP     #'A'
        BCC     @inv_dismiss
        CMP     #('Z'+1)
        BCS     @inv_dismiss
        SEC
        SBC     #'A'
        CMP     #INV_COUNT
        BCS     @inv_dismiss
        ASL
        ASL
        ASL                             ; slot index → byte offset
        TAX
        LDA     inventory+INV_TYPE,X
        BEQ     @inv_dismiss            ; empty slot → no-op
        CMP     #ITEM_T_DAGGER
        BEQ     @inv_dismiss            ; hidden legacy slot → no-op
@inv_dispatch:
        ; Valid slot — dispatch via the shared use-slot routine. It
        ; redraws on success paths. We always trail with a PHA /
        ; redraw_game / PLA so the modal is wiped uniformly and the
        ; turn-cost flag survives back to main_loop's @do_inv.
        JSR     dispatch_use_slot
        PHA
        JSR     redraw_game
        PLA
        RTS
@inv_dismiss:
        JSR     redraw_game
        LDA     #0                      ; no turn consumed
        RTS


; ----------------------------------------------------------------------------
; draw_inv_line: paint one bag slot's row.
; Inputs:
;   X       = slot byte offset (0, 8, 16, ..., 200) — preserved on RTS.
;   map_ptr = display row 0..23 (caller passes via vdp_row indirectly).
; Layout (col 4..n):
;   "[L] QxSS NAME UTIL"
; where:
;   L  = 'A' + (X >> 3)              slot letter
;   Q  = INV_QTY ('1'..'9')          stack count, always shown
;   x  = literal 'x'                 multiplier
;   SS = blanks at cols 10-11        the SAT sprite renders here over
;                                    the blank tiles (sprite X = px 80)
;   NAME = type/sub-type display string from lookup_item_name
;   UTIL = "ATK+N" / "DEF+N" / "HP+N" / "REGEN" / "REVEAL" / "THROW"
;          / "FOV+N", appended by draw_inv_utility (tail-call).
; The legacy '*' equipped marker was dropped: weapons + armours are now
; consumable buffs, so the only candidate was the ring, and an "is the
; ring on?" indicator is more readable on the HUD than buried in the
; bag list.
; ----------------------------------------------------------------------------
draw_inv_line:
        LDA     map_ptr
        STA     vdp_row
        LDA     #4
        STA     vdp_col
        JSR     name_at_rc
        JSR     vdp_set_write
        WRT_DATA_VAL '['
        TXA
        LSR
        LSR
        LSR                             ; A = slot index 0..25
        CLC
        ADC     #'A'
        WRT_DATA_REG                    ; the letter
        WRT_DATA_VAL ']'
        WRT_DATA_VAL ' '
        ; Stack count: INV_QTY reflects the pile size (every type
        ; stacks now). Capped at 9 for display so qty=10..255 doesn't
        ; spill into ASCII glyphs ('0' + 10 = ':' otherwise) — silent
        ; clamp keeps the layout tidy. The actual stack size is still
        ; tracked correctly in INV_QTY for the consume / decrement path.
        LDA     inventory+INV_QTY,X
        CMP     #10
        BCC     @qty_ok
        LDA     #9
@qty_ok:
        CLC
        ADC     #'0'
        WRT_DATA_REG
        WRT_DATA_VAL 'x'
        WRT_DATA_VAL ' '                ; col 10 — sprite tile (left half)
        WRT_DATA_VAL ' '                ; col 11 — sprite tile (right half)
        WRT_DATA_VAL ' '                ; col 12 — gap before NAME
        ; Look up display string → vdp_src_lo/hi. lookup_item_name
        ; clobbers A/X — preserve slot offset in map_ptr+1.
        STX     map_ptr+1
        LDA     inventory+INV_TYPE,X
        LDY     inventory+INV_SUBTYPE,X
        JSR     lookup_item_name
        LDX     map_ptr+1               ; X back to slot offset
        ; Stream the string until $FF (auto-increment continues at the
        ; current name-table cell).
        LDY     #0
@nm_lp:
        LDA     (vdp_src_lo),Y
        CMP     #$FF
        BEQ     @nm_done
        WRT_DATA_REG
        INY
        BNE     @nm_lp
@nm_done:
        ; Tail-call into draw_inv_utility so the utility tag (ATK+N,
        ; DEF+N, HP+N, REGEN, REVEAL, THROW, FOV+N) lands on the same
        ; row, in the auto-increment stream right after the name. X
        ; still holds the slot offset (the @nm_lp loop only touches
        ; A and Y).
        JMP     draw_inv_utility


; ----------------------------------------------------------------------------
; draw_inv_utility: append " <UTILITY>" to draw_inv_line's stream so
; the player sees what the slot DOES next to its name. One-space gap
; from the last name char keeps the column ragged but readable (item
; names are 5–6 chars and end at col 14 or 15). Strings:
;   WEAPON  → ATK+N    (N = INV_VALUE)
;   ARMOR   → DEF+N
;   RING    → REGEN    (only ring sub-type after MVP4)
;   POTION  → HP+N     (N = 8)
;   SCROLL  → REVEAL   (full-map reveal effect)
;   FOOD    → HP+N     (N = FOOD_HEAL = 3)
;   DAGGER  → hidden legacy fallback; live daggers are HUD ammo
;
; Inputs:
;   X = slot byte offset (preserved on RTS so callers can re-use it).
; Assumes the VDP write address is armed and pointing at the next free
; cell on the slot's row — this function is called as a tail of
; draw_inv_line, never standalone.
; ----------------------------------------------------------------------------
draw_inv_utility:
        WRT_DATA_VAL ' '                ; gap after the name
        LDA     inventory+INV_TYPE,X
        CMP     #ITEM_T_WEAPON
        BEQ     @atk
        CMP     #ITEM_T_ARMOR
        BEQ     @def
        ; The remaining types live past WRT_DATA_VAL macro expansions
        ; that overflow the BEQ +/-128 window — bounce through BNE
        ; skips + JMP trampolines.
        CMP     #ITEM_T_RING
        BNE     @n_ring
        JMP     @ring
@n_ring:
        CMP     #ITEM_T_POTION
        BNE     @n_pot
        JMP     @hp
@n_pot:
        CMP     #ITEM_T_FOOD
        BNE     @n_food
        JMP     @hp
@n_food:
        CMP     #ITEM_T_SCROLL
        BNE     @n_scr
        JMP     @scroll
@n_scr:
        CMP     #ITEM_T_TORCH
        BNE     @n_torch
        JMP     @torch
@n_torch:
        ; ITEM_T_DAGGER is unreachable in normal play (daggers are HUD ammo),
        ; but keep a legacy/corrupt-slot label instead of falling into
        ; random bytes if an old slot survives in RAM.
        WRT_DATA_VAL 'T'
        WRT_DATA_VAL 'H'
        WRT_DATA_VAL 'R'
        WRT_DATA_VAL 'O'
        WRT_DATA_VAL 'W'
        RTS
@atk:
        WRT_DATA_VAL 'A'
        WRT_DATA_VAL 'T'
        WRT_DATA_VAL 'K'
        WRT_DATA_VAL '+'
        LDA     inventory+INV_VALUE,X
        CLC
        ADC     #'0'                    ; assumes value < 10
        WRT_DATA_REG
        RTS
@def:
        WRT_DATA_VAL 'D'
        WRT_DATA_VAL 'E'
        WRT_DATA_VAL 'F'
        WRT_DATA_VAL '+'
        LDA     inventory+INV_VALUE,X
        CLC
        ADC     #'0'
        WRT_DATA_REG
        RTS
@hp:
        WRT_DATA_VAL 'H'
        WRT_DATA_VAL 'P'
        WRT_DATA_VAL '+'
        LDA     inventory+INV_VALUE,X
        CLC
        ADC     #'0'                    ; potion = 8, ration = 3
        WRT_DATA_REG
        RTS
@ring:
        WRT_DATA_VAL 'R'
        WRT_DATA_VAL 'E'
        WRT_DATA_VAL 'G'
        WRT_DATA_VAL 'E'
        WRT_DATA_VAL 'N'
        RTS
@scroll:
        WRT_DATA_VAL 'R'
        WRT_DATA_VAL 'E'
        WRT_DATA_VAL 'V'
        WRT_DATA_VAL 'E'
        WRT_DATA_VAL 'A'
        WRT_DATA_VAL 'L'
        RTS
@torch:
        WRT_DATA_VAL 'F'
        WRT_DATA_VAL 'O'
        WRT_DATA_VAL 'V'
        WRT_DATA_VAL '+'
        LDA     #TORCH_RADIUS
        SEC
        SBC     #FOV_RADIUS             ; A = boost magnitude (4 by default)
        CLC
        ADC     #'0'                    ; assumes magnitude < 10
        WRT_DATA_REG
        RTS


; ----------------------------------------------------------------------------
; print_msg_row: render the $FF-terminated string at (vdp_src_lo, hi)
; on row A, col 0. Used by the equip / remove / use / throw handlers
; to flash a one-line prompt or error on row 23 (or row 22 for results
; that should survive the next redraw_game). Clobbers A, X.
; ----------------------------------------------------------------------------
print_msg_row:
        STA     vdp_row
        LDA     #0
        STA     vdp_col
        JSR     name_at_rc
        LDA     vdp_lo
        LDX     vdp_hi
        JSR     draw_text
        RTS


; ----------------------------------------------------------------------------
; consume_inv_slot: decrement INV_QTY at byte offset X. If QTY drops
; to 0, free the slot (clear INV_TYPE). All item types stack on
; (type, subtype) match now (see try_pickup_item), so a "stack of 1"
; folds naturally into the "single item" case. No equipped_* checks
; either — every gear category is a consumable buff.
; ----------------------------------------------------------------------------
consume_inv_slot:
        DEC     inventory+INV_QTY,X
        BNE     @done
        LDA     #0
        STA     inventory+INV_TYPE,X
@done:
        RTS


; ----------------------------------------------------------------------------
; dispatch_use_slot: shared dispatch body. On entry X = slot byte offset
; (caller has validated the slot is non-empty). Returns A=1 on a turn-
; consuming use (food/potion/scroll), A=0 on a free action (equipment
; toggle, dagger no-op). map_ptr is reused as scratch and clobbered.
; ----------------------------------------------------------------------------
dispatch_use_slot:
        STX     map_ptr                 ; cache slot offset
        LDA     inventory+INV_TYPE,X
        ; Three first dispatches reach their @do_* labels with a short
        ; BEQ; the consumable branches (food/potion/scroll) sit past
        ; the equipment block and need BNE-skip-then-JMP trampolines.
        CMP     #ITEM_T_WEAPON
        BEQ     @do_weapon
        CMP     #ITEM_T_ARMOR
        BEQ     @do_armor
        CMP     #ITEM_T_RING
        BEQ     @do_ring
        CMP     #ITEM_T_FOOD
        BNE     @nxd_food
        JMP     @do_food
@nxd_food:
        CMP     #ITEM_T_POTION
        BNE     @nxd_pot
        JMP     @do_potion
@nxd_pot:
        CMP     #ITEM_T_SCROLL
        BNE     @nxd_scr
        JMP     @do_scroll
@nxd_scr:
        CMP     #ITEM_T_TORCH
        BNE     @nxd_torch
        JMP     @do_torch
@nxd_torch:
        ; ITEM_T_DAGGER lands here — not "used", only "thrown".
        LDA     #<msg_not_use
        STA     vdp_src_lo
        LDA     #>msg_not_use
        STA     vdp_src_hi
        LDA     #23
        JSR     print_msg_row
        LDA     #0
        RTS

; --- Buff activation (free actions — no turn cost) ---
; Weapons / armours / torches are CONSUMABLE buffs. Activating one:
;   1. caches the item's INV_VALUE into weapon_boost / armor_boost
;      (or just sets torch_timer for torches — no value to cache,
;      compute_fov uses the constant TORCH_RADIUS),
;   2. sets the matching timer to its per-category duration constant,
;   3. consumes the inventory slot,
;   4. recomputes player_dmg / player_def (weapon / armor only),
;   5. redraws.
; Reactivating any same-class item simply overwrites the timer (no
; stacking). Free action so the player can pop a buff and still move
; the same turn — turning timing into a real lever.
@do_weapon:
        LDX     map_ptr
        LDA     inventory+INV_VALUE,X
        STA     weapon_boost
        LDA     #WEAPON_DURATION
        STA     weapon_timer
        JSR     consume_inv_slot
        JSR     recompute_player_stats
        JSR     redraw_game
        LDA     #0                      ; free action
        RTS

@do_armor:
        LDX     map_ptr
        LDA     inventory+INV_VALUE,X
        STA     armor_boost
        LDA     #ARMOR_DURATION
        STA     armor_timer
        JSR     consume_inv_slot
        JSR     recompute_player_stats
        JSR     redraw_game
        LDA     #0
        RTS

@do_torch:
        ; Torch lights up the FOV to TORCH_RADIUS for TORCH_DURATION
        ; turns (50 — far longer than the combat buffs since this is
        ; an exploration tool, not a fight window). compute_fov reads
        ; torch_timer and switches radii on its own — no
        ; recompute_player_stats call needed (FOV doesn't touch
        ; player_dmg / player_def). The redraw_game call below pipes
        ; the freshly-lit cells into the playfield right away.
        LDX     map_ptr
        LDA     #TORCH_DURATION
        STA     torch_timer
        JSR     consume_inv_slot
        JSR     compute_fov             ; widen the lit cells immediately
        JSR     redraw_game
        LDA     #0
        RTS

@do_ring:
        ; Ring activation is now a CONSUMABLE buff like weapon / armor.
        ; Cache the INV_VALUE bit (RING_F_*) into ring_boost, OR it
        ; into ring_flags so finish_turn's regen tick fires, set
        ; ring_timer = RING_DURATION, and reset regen_tick to 0 so the
        ; first +1 HP fires next turn (small grace bonus, mirrors the
        ; pre-rework behaviour). On expiry, finish_turn AND-clears the
        ; bit out of ring_flags.
        LDX     map_ptr
        LDA     inventory+INV_VALUE,X
        STA     ring_boost
        ORA     ring_flags
        STA     ring_flags
        LDA     #RING_DURATION
        STA     ring_timer
        LDA     #0
        STA     regen_tick              ; first regen tick fires next turn
        JSR     consume_inv_slot
        JSR     redraw_game
        LDA     #0                      ; free action
        RTS

@do_food:
        ; Heal HP += INV_VALUE (= FOOD_HEAL), capped at HP_MAX.
        LDX     map_ptr
        LDA     hp
        CLC
        ADC     inventory+INV_VALUE,X
        CMP     hp_max                  ; runtime cap (HP_MAX + xp/5)
        BCC     @food_save
        LDA     hp_max
@food_save:
        STA     hp
        LDX     map_ptr
        JSR     consume_inv_slot
        JSR     redraw_game
        LDA     #1
        RTS

@do_potion:
        ; Heal HP += INV_VALUE, capped at runtime hp_max. After MVP4
        ; simplification there's only one potion sub-type, so no
        ; sub-type dispatch is needed.
        LDX     map_ptr
        LDA     hp
        CLC
        ADC     inventory+INV_VALUE,X
        CMP     hp_max
        BCC     @pot_save
        LDA     hp_max
@pot_save:
        STA     hp
        LDX     map_ptr
        JSR     consume_inv_slot
        JSR     redraw_game
        LDA     #1
        RTS

@do_scroll:
        ; One-shot full-map reveal. After MVP4 simplification scrolls
        ; have a single effect (SUB_SCROLL_MAP), so no sub-type dispatch.
        ; render the now-fully-lit map, wait for a keypress (modal
        ; "look at this"), consume the scroll, redraw under normal FOV.
        ; The reveal does NOT persist — compute_fov on the next move
        ; wipes vis_buffer and the player drops back to torchlight.
        ; Bit-packed: 8 cells per byte, so $FF lights 8 cells at once.
        LDA     #$FF
        LDX     #VIS_BUFFER_SIZE
@map_lp:
        DEX
        STA     vis_buffer,X
        BNE     @map_lp
        JSR     clear_name_table
        JSR     render_map
        JSR     place_all_sprites
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        JSR     update_hud
        ; Wait for any key — drain first so the killing 'E' press doesn't
        ; insta-close the modal.
        LDA     KBD
        JSR     wait_key
        LDX     map_ptr
        JSR     consume_inv_slot
        JSR     compute_fov             ; restore torchlight
        JSR     redraw_game
        LDA     #1
        RTS


; ----------------------------------------------------------------------------
; parse_direction: convert a movement-key press into a signed (dx, dy)
; cardinal step.
;   Inputs:  A = raw KBD value (high bit set), one of the runtime-bound
;            key_west / key_east / key_north / key_south codes.
;   Returns: C clear → throw_dx / throw_dy set
;            C set   → unrecognised key; throw_dx/dy unchanged.
; Diagonal throws not supported (Rogue's classic four-direction rule).
; ----------------------------------------------------------------------------
parse_direction:
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
@west:
        LDA     #$FF
        STA     throw_dx
        LDA     #0
        STA     throw_dy
        CLC
        RTS
@east:
        LDA     #$01
        STA     throw_dx
        LDA     #0
        STA     throw_dy
        CLC
        RTS
@north:
        LDA     #0
        STA     throw_dx
        LDA     #$FF
        STA     throw_dy
        CLC
        RTS
@south:
        LDA     #0
        STA     throw_dx
        LDA     #$01
        STA     throw_dy
        CLC
        RTS


; ----------------------------------------------------------------------------
; delay_throw_frame: ~80 ms busy loop at 1.022 MHz. Used between
; animation steps so the dagger flight is visible. Inner = INY/BNE
; ≈ 5 c × 256 = 1280 c. Outer = (1280 + INX/BNE ≈ 5 c) × 64 ≈ 82 k c
; → ~80 ms. No keyboard polling — the player can't abort a throw
; mid-flight. Clobbers A, X, Y.
; ----------------------------------------------------------------------------
delay_throw_frame:
        LDX     #64
@d1:    LDY     #0
@d2:    INY
        BNE     @d2
        DEX
        BNE     @d1
        RTS


; ----------------------------------------------------------------------------
; handle_throw: 'T' command — hurl a dagger in a chosen direction.
; Streamlined fire-and-forget design (per project spec):
;   1. Check dagger_qty — daggers are ammo, not inventory slots.
;      Silent-fail with "NO DAGGER" + free action if the quiver is empty.
;   2. Print "DIRECTION? " on row 23, wait for a movement key.
;   3. parse_direction → throw_dx/dy. Cancel returns silently with the
;      dagger count unchanged (free action, prompt persists until the
;      next turn — visual reminder that the throw didn't happen).
;   4. Decrement dagger_qty.
;   5. Animate flight cell-by-cell at ~80 ms/frame, range THROW_RANGE.
;      The dagger ALWAYS vanishes on stop — monster hit (deal throw_dmg,
;      kill if HP→0), wall / door / stairs / pit, OOB, or end of range.
;      No floor drops; matches the player-facing model "fire and forget".
;
; Returns A = 1 (turn consumed) on a successful throw, A = 0 on error
; (no daggers in bag / cancelled direction).
; ----------------------------------------------------------------------------
THROW_RANGE = 8
DAGGER_DMG  = 2                 ; per-throw damage (dagger is now a
                                ; quiver-stat ammo, not an inventory
                                ; item, so the dmg lives as a constant
                                ; rather than INV_VALUE on a slot).
DAGGER_QTY_MAX = 99             ; ammo cap (2-digit HUD display)

handle_throw:
        ; --- Quiver empty → "NO DAGGER", free action. ---
        LDA     dagger_qty
        BNE     @have_dagger
        LDA     #<msg_no_dagger
        STA     vdp_src_lo
        LDA     #>msg_no_dagger
        STA     vdp_src_hi
        LDA     #23
        JSR     print_msg_row
        LDA     #0
        RTS
@have_dagger:
        LDA     #DAGGER_DMG
        STA     throw_dmg               ; cache dmg before consume

        ; --- Pick the direction ---
        LDA     #<msg_dir_q
        STA     vdp_src_lo
        LDA     #>msg_dir_q
        STA     vdp_src_hi
        LDA     #23
        JSR     print_msg_row
        LDA     KBD                     ; drain T strobe
        JSR     wait_key
        JSR     parse_direction
        BCC     @have_dir
        ; Cancelled — non-direction key. Wipe row 23 so the prompt
        ; doesn't linger ("DIRECTION? " confused players into thinking
        ; the game was still waiting), bag stays intact, free action.
        JSR     clear_msg_row23
        LDA     #0
        RTS
@have_dir:
        ; --- Consume one dagger charge from the quiver. ---
        DEC     dagger_qty

        ; --- Animate the flight. cur_x/cur_y walk from the player's
        ; cell outward by (throw_dx, throw_dy) up to THROW_RANGE steps. ---
        LDA     player_col
        STA     cur_x
        LDA     player_row
        STA     cur_y
        LDA     #1
        STA     throw_active            ; place_all_sprites now appends
                                        ; an extra dagger SAT entry
        LDA     #THROW_RANGE
        STA     throw_step
@flight_lp:
        ; Step (cur_x, cur_y) by (throw_dx, throw_dy).
        CLC
        LDA     cur_x
        ADC     throw_dx
        STA     cur_x
        CLC
        LDA     cur_y
        ADC     throw_dy
        STA     cur_y
        ; Bounds: stay strictly inside the playable interior — off-grid
        ; means the dagger sailed off the playfield, vanish.
        LDA     cur_y
        CMP     #PLAY_TOP_ROW
        BCC     @vanish
        CMP     #(PLAY_BOT_ROW + 1)
        BCS     @vanish
        LDA     cur_x
        CMP     #PLAY_LEFT_COL
        BCC     @vanish
        CMP     #(PLAY_RIGHT_COL + 1)
        BCS     @vanish
        ; Tile check — non-empty (wall/door/stairs/pit) stops the dagger.
        LDA     cur_y
        ASL
        ASL
        ASL
        ASL
        CLC
        ADC     cur_x                   ; A = linear cell index
        JSR     map_get_a               ; A = nibble (4 bits)
        AND     #7                      ; strip reveal bit, A = dense tile id
        BNE     @vanish
        ; Monster check — first hit absorbs the dagger.
        LDA     cur_x
        STA     tgt_col
        LDA     cur_y
        STA     tgt_row
        JSR     monster_at_target       ; X = pool offset on hit
        BCC     @hit_mon
        ; Empty cell — render this frame and step again.
        JSR     redraw_game             ; rebuilds SAT with throw_active=1
        JSR     delay_throw_frame
        DEC     throw_step
        BNE     @flight_lp
        ; End of range — vanish.
        JMP     @vanish

@hit_mon:
        ; X = monster pool offset. Apply throw_dmg, set MON_HURT, kill
        ; if HP hits 0. No food drop (only bump kills give food).
        LDA     monsters+MON_HP,X
        SEC
        SBC     throw_dmg
        BCS     @hp_save
        LDA     #0
@hp_save:
        STA     monsters+MON_HP,X
        BNE     @hurt
        LDA     #0
        STA     monsters+MON_TYPE,X
        JSR     award_xp
        JMP     @vanish
@hurt:
        LDA     #1
        STA     monsters+MON_HURT,X
        ; Fall through to vanish.
@vanish:
        ; Dagger ends here — clear the in-flight sprite, repaint once
        ; so the projectile disappears from the SAT, charge a turn.
        LDA     #0
        STA     throw_active
        JSR     redraw_game
        LDA     #1
        RTS


; ----------------------------------------------------------------------------
; show_help: full-screen help modal reached from main_loop's '?'
; dispatch. Hides every gameplay sprite (Y=$D0 chain terminator at
; SAT[0] via disable_sprites), wipes the name table, paints a static
; reference card from `help_table` (same str_lo/hi + vram_lo/hi tuple
; layout as draw_title), drains the '?' strobe, waits for any key,
; then redraws the playfield. Always a free action — caller does NOT
; tick monsters or call finish_turn.
;
; The reference card lives in cartridge ROM as 16 short $FF-terminated
; strings (~250 B), keeping the player from needing the README on real
; hardware. Both keyboard layouts (QWERTY-WASD / AZERTY-ZQSD) are
; printed because the title-screen choice is forgotten by the time
; the player needs the reminder.
; ----------------------------------------------------------------------------
show_help:
        ; Sync to VBlank before the help-screen rebuild burst.
        WAIT_VBLANK
        JSR     disable_sprites
        JSR     clear_name_table
        ; Walk help_table — same 4-byte tuple loop as draw_title (tmp
        ; survives draw_text). Each tuple = (str_lo, str_hi, vram_lo,
        ; vram_hi_with_write_bit).
        LDX     #0
@lp:    LDA     help_table+0,X
        STA     vdp_src_lo
        LDA     help_table+1,X
        STA     vdp_src_hi
        LDA     help_table+2,X
        PHA
        STX     tmp                     ; preserve loop index
        LDA     help_table+3,X
        TAX
        PLA                             ; A = vram_lo, X = vram_hi
        JSR     draw_text
        LDA     tmp
        CLC
        ADC     #4
        TAX
        CPX     #(help_table_end - help_table)
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BCC     @lp
        ; Drain the '?' strobe so a held key can't insta-close, then
        ; wait for an explicit ack press.
        LDA     KBD
        JSR     wait_key
        JSR     redraw_game
        RTS

help_table:
        ;       str_lo,         str_hi,         vram_lo, vram_hi
        .byte   <hlp_title,     >hlp_title,     $2E, $58  ; row  1 col 14
        .byte   <hlp_move_hdr,  >hlp_move_hdr,  $62, $58  ; row  3 col  2
        .byte   <hlp_move_qw,   >hlp_move_qw,   $84, $58  ; row  4 col  4
        .byte   <hlp_move_az,   >hlp_move_az,   $A4, $58  ; row  5 col  4
        .byte   <hlp_cmds_hdr,  >hlp_cmds_hdr,  $E2, $58  ; row  7 col  2
        .byte   <hlp_cmd_i,     >hlp_cmd_i,     $04, $59  ; row  8 col  4
        .byte   <hlp_cmd_t,     >hlp_cmd_t,     $24, $59  ; row  9 col  4
        .byte   <hlp_cmd_q,     >hlp_cmd_q,     $44, $59  ; row 10 col  4
        .byte   <hlp_cmd_rest,  >hlp_cmd_rest,  $64, $59  ; row 11 col  4
        .byte   <hlp_tips_hdr,  >hlp_tips_hdr,  $C2, $59  ; row 14 col  2
        .byte   <hlp_tip_bump,  >hlp_tip_bump,  $E4, $59  ; row 15 col  4
        .byte   <hlp_tip_pick,  >hlp_tip_pick,  $04, $5A  ; row 16 col  4
        .byte   <hlp_tip_down,  >hlp_tip_down,  $24, $5A  ; row 17 col  4
        .byte   <hlp_tip_warp,  >hlp_tip_warp,  $44, $5A  ; row 18 col  4
        .byte   <hlp_tip_win,   >hlp_tip_win,   $64, $5A  ; row 19 col  4
        .byte   <msg_press_key, >msg_press_key, $C9, $5A  ; row 22 col  9
help_table_end:

; --- Help-screen strings ($FF-terminated; reuses msg_press_key from
; the death/win shared pool to avoid a duplicate). ---
hlp_title:      .byte "HELP", $FF
hlp_move_hdr:   .byte "MOVEMENT KEYS", $FF
hlp_move_qw:    .byte "1 QWERTY  W A S D", $FF
hlp_move_az:    .byte "2 AZERTY  Z Q S D", $FF
hlp_cmds_hdr:   .byte "COMMANDS", $FF
hlp_cmd_i:      .byte "I  INVENTORY OPEN/USE", $FF
hlp_cmd_t:      .byte "T  THROW DAGGER", $FF
hlp_cmd_q:      .byte "H/?  THIS HELP SCREEN", $FF
hlp_cmd_rest:   .byte ".  REST ONE TURN", $FF
hlp_tips_hdr:   .byte "TIPS", $FF
hlp_tip_bump:   .byte "BUMP MONSTER TO ATTACK", $FF
hlp_tip_pick:   .byte "PICKUP IS AUTOMATIC", $FF
hlp_tip_down:   .byte "STAIRS DOWN: DESCEND", $FF
hlp_tip_warp:   .byte "EDGE DOOR: WARP LEVEL", $FF
hlp_tip_win:    .byte "REACH DEPTH 13 TO WIN", $FF


; ----------------------------------------------------------------------------
; draw_title: paint the title screen on the cleared name table. Each
; entry in title_table is a 4-byte tuple (str_lo, str_hi, vram_lo,
; vram_hi_with_write_bit) describing one centred line. The VRAM addr
; is precomputed as $1800 + row * 32 + col, then ORed with $40 (write
; bit) for draw_text's STX VDP_CTRL second-byte. tmp holds the table
; index across the JSR (draw_text leaves zp untouched but clobbers X).
; ----------------------------------------------------------------------------
draw_title:
        ; Sync to VBlank before the title-screen rebuild burst.
        WAIT_VBLANK
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
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BCC     @lp
        RTS

title_table:
        .byte   <title_rogue,     >title_rogue,     $8D, $58  ; row  4 col 13
        .byte   <title_subtitle,  >title_subtitle,  $E4, $58  ; row  7 col  4
        .byte   <title_author,    >title_author,    $27, $59  ; row  9 col  7
        .byte   <title_select_kb, >title_select_kb, $A8, $59  ; row 13 col  8
        .byte   <title_qwerty,    >title_qwerty,    $E8, $59  ; row 15 col  8
        .byte   <title_azerty,    >title_azerty,    $28, $5A  ; row 17 col  8
        .byte   <title_help_hint, >title_help_hint, $86, $5A  ; row 20 col  6
title_table_end:


; ----------------------------------------------------------------------------
; draw_briefing: second title page — compact mission cards plus the win
; condition. Painted right after the keyboard-layout choice so the
; player gets the "why am I doing this" pitch BEFORE the dungeon view
; appears. start: drains KBD then
; wait_keys for an explicit acknowledgment, then continues into level 1.
; Same table-driven loop as draw_title — separate table so each page
; can be edited independently. tmp survives draw_text.
; ----------------------------------------------------------------------------
draw_briefing:
        ; Sync to VBlank before the briefing-screen rebuild burst.
        WAIT_VBLANK
        LDX     #0
@lp:    LDA     briefing_table+0,X
        STA     vdp_src_lo
        LDA     briefing_table+1,X
        STA     vdp_src_hi
        LDA     briefing_table+2,X
        PHA
        STX     tmp
        LDA     briefing_table+3,X
        TAX
        PLA
        JSR     draw_text
        LDA     tmp
        CLC
        ADC     #4
        TAX
        CPX     #(briefing_table_end - briefing_table)
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BCC     @lp
        RTS

briefing_table:
        ;       str_lo,         str_hi,         vram_lo, vram_hi
        .byte   <brf_title,     >brf_title,     $4D, $58  ; row  2 col 13
        .byte   <brf_goal,      >brf_goal,      $84, $58  ; row  4 col  4
        .byte   <brf_stairs,    >brf_stairs,    $E4, $58  ; row  7 col  4
        .byte   <brf_sword,     >brf_sword,     $44, $59  ; row 10 col  4
        .byte   <brf_bag,       >brf_bag,       $84, $59  ; row 12 col  4
        .byte   <brf_torch,     >brf_torch,     $C4, $59  ; row 14 col  4
        .byte   <brf_pit,       >brf_pit,       $04, $5A  ; row 16 col  4
        .byte   <brf_skull,     >brf_skull,     $44, $5A  ; row 18 col  4
        .byte   <brf_win,       >brf_win,       $84, $5A  ; row 20 col  4
        .byte   <msg_press_key, >msg_press_key, $C9, $5A  ; row 22 col  9
briefing_table_end:

; --- Mission-briefing strings ($FF-terminated). Apostrophes / parens
; avoided (broken Quale glyphs); brackets + dashes render
; cleanly. Reuses msg_press_key (death/win/help shared pool). ---
brf_title:      .byte "MISSION", $FF
brf_goal:       .byte "[AMULET] FIND IT", $FF
brf_stairs:     .byte "[STAIRS] GO DOWN 12 FLOORS", $FF
brf_sword:      .byte "[SWORD] BUMP MONSTERS", $FF
brf_bag:        .byte "[BAG] PICK UP ITEMS", $FF
brf_torch:      .byte "[TORCH] LIGHT THE DARK", $FF
brf_pit:        .byte "[PIT] WATCH YOUR STEP", $FF
brf_skull:      .byte "[SKULL] ONE LIFE", $FF
brf_win:        .byte "DEPTH 13 = ESCAPE", $FF


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
; Hint line so a first-time player knows the in-game help is one
; keypress away. Brackets render fine in the Quale font (the bracket
; glyphs survived the extraction; only the parens were broken).
title_help_hint:
        .byte   "[H] FOR HELP IN GAME", $FF


; ============================================================================
; sprite_pats -- 13 sprite patterns (16x16, 32 B each = 416 B), uploaded
; as a single contiguous block to VRAM $3800 by upload_sprite_pats.
; Inlined here rather than .imported from the lib .asm files because
; pulling in the full 33-sprite character lib + 8-sprite undead lib +
; 27-sprite outfit lib + 4-sprite trollkind lib would blow the 3 456 B
; low-bank CODE budget on the DRAM build.
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

; --- MVP4 item sprites (slots 24..44) ----------------------------------
; All 16x16 sprites laid out as 4 quadrants of 8 rows each:
;   bytes  0..7  = left top half   (TL)
;   bytes  8..15 = left bottom half (BL)
;   bytes 16..23 = right top half  (TR)
;   bytes 24..31 = right bottom half (BR)
; Each row byte = 8 horizontal pixels, MSB = leftmost column.
;
; The patterns are intentionally simple — readable at the 256x192
; resolution from a few feet, and cheap on VRAM (32 B each). Colour
; comes from the SAT entry, so the same dagger pattern can flash hurt
; (COL_HURT) when it lands a hit.
item_dagger_pat:                                ; slot 24 — dagger / throw
        ; Quale's outfit_dagger_pat (sprites_outfit.asm slot 01/27 of
        ; "Outfit"), inlined to keep the cartridge link self-contained.
        .byte $00, $00, $00, $00, $03, $03, $00, $03
        .byte $07, $0E, $1D, $1A, $1C, $00, $00, $00
        .byte $00, $00, $00, $18, $18, $A0, $C0, $60
        .byte $B0, $B0, $00, $00, $00, $00, $00, $00
item_potion_pat:                                ; slot 28 — potion
        ; Quale's food_bottle_pat (sprites_food_drink.asm slot 20/22 of
        ; "Food & Drink") — slim corked bottle. Game still calls the
        ; category POTION (INV_T_POTION); only the visual changed.
        .byte $00, $00, $00, $00, $03, $06, $07, $03
        .byte $04, $0E, $0E, $0E, $0E, $04, $03, $00
        .byte $00, $00, $00, $00, $C0, $60, $E0, $C0
        .byte $20, $10, $10, $10, $10, $20, $C0, $00
item_scroll_pat:                                ; slot 32 — scroll
        ; Quale's magic_scroll_pat (sprites_magick.asm slot 07/15 of
        ; "Magick") — rolled scroll with text lines.
        .byte $00, $07, $09, $0B, $08, $0F, $08, $0F
        .byte $0C, $0F, $08, $0F, $6C, $4F, $3F, $00
        .byte $00, $FC, $FE, $FE, $00, $F8, $08, $F8
        .byte $18, $F8, $08, $F8, $18, $F8, $F0, $00
item_weapon_pat:                                ; slot 36 — weapon (sword)
        ; Quale's outfit_sword_pat (sprites_outfit.asm slot 02/27 of
        ; "Outfit") — a hilt-and-blade sword silhouette.
        .byte $00, $00, $00, $00, $00, $01, $03, $07
        .byte $37, $36, $1B, $0C, $17, $63, $60, $00
        .byte $00, $1E, $3E, $7A, $F2, $E4, $C8, $90
        .byte $20, $40, $80, $00, $00, $00, $00, $00
item_armor_pat:                                 ; slot 40 — armor (chest)
        ; Quale's outfit_tunic_pat (sprites_outfit.asm slot 21/27 of
        ; "Outfit") — a tunic / gambeson silhouette.
        .byte $00, $00, $00, $3C, $3D, $3C, $1C, $0E
        .byte $11, $1F, $1F, $00, $0F, $00, $00, $00
        .byte $00, $00, $00, $3C, $BC, $3C, $38, $70
        .byte $88, $F8, $F8, $00, $F0, $00, $00, $00
item_ring_pat:                                  ; slot 44 — ring
        ; Quale's outfit_amulet_pat (sprites_outfit.asm slot 24/27 of
        ; "Outfit") — pendant on a chain. Game still calls the category
        ; RING (INV_T_RING / SUB_RING_*); only the on-screen pictogram
        ; is the amulet.
        .byte $00, $00, $00, $0E, $1E, $1E, $1D, $03
        .byte $1E, $1C, $1C, $0C, $06, $03, $00, $00
        .byte $00, $00, $00, $E0, $F0, $F8, $8C, $04
        .byte $04, $04, $04, $08, $10, $E0, $00, $00
troll_grunt_pat:                                ; slot 48 — troll grunt
        ; Quale's troll_grunt_pat (sprites_trollkind.asm slot 01/4 of
        ; "Trollkind"). Med-green silhouette via MON_COL_TROLL — flees
        ; the player on its turn (ai_troll).
        .byte $00, $00, $00, $00, $00, $77, $3D, $0F
        .byte $07, $08, $1F, $37, $27, $36, $32, $02
        .byte $00, $00, $00, $00, $00, $EE, $BC, $F0
        .byte $E0, $10, $F8, $EC, $E4, $6C, $4C, $40
expl_torch_pat:                                 ; slot 52 — torch
        ; Quale's expl_torch_pat (sprites_exploration.asm slot 01/21
        ; of "Exploration"). Inlined to keep the cartridge link
        ; self-contained. Painted in COL_TORCH (light yellow flame).
        .byte $00, $00, $00, $04, $00, $01, $13, $07
        .byte $04, $02, $07, $0C, $18, $30, $60, $00
        .byte $00, $40, $68, $F0, $F0, $F8, $98, $38
        .byte $78, $F0, $E0, $00, $00, $00, $00, $00

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
; dungeon -- procedural-generation primitives (lib/m6502/dungeon.asm).
; Currently exposes `rand_mod` (uniform [0, A) PRNG wrapper). The bigger
; BSP-light pattern (room placement + L-corridor + door classification)
; stays inline in this file since it bakes in the 16×10 grid layout —
; see the lib's header for what would need parametrising for promotion.
; ============================================================================
.include "dungeon.asm"


; ============================================================================
; multiply -- umul8 (8x8 -> 16-bit) + umul4 (4x4 -> 8-bit) shared library.
; The lib reserves its own mul_tmp / mul_res0 ZP slots via .ifndef guard.
; umul4 is consumed by shadowcast.asm below for slope cross-multiplications.
; ============================================================================
.include "multiply.asm"


; ============================================================================
; shadowcast -- recursive shadowcasting FOV (lib/m6502/shadowcast.asm).
; Expects the caller to provide:
;   - player_col, player_row, fov_r       (ZP — declared above)
;   - SHADOW_COLS, SHADOW_ROWS            (constants — declared above)
;   - mark_visible_at_cur, is_opaque_at_cur (subroutines — defined above)
;   - umul4                               (from multiply.asm above)
; The lib's .ifndef guard allocates cur_x/cur_y/oct_*/cast_* in our
; .zeropage segment.
; ============================================================================
.include "shadowcast.asm"


; ============================================================================
; Generated tileset (pattern table + colour table). Pulled in last so
; the .byte tables sit at the tail of the CODE segment. TILE_* equates
; declared here are visible mid-file thanks to ca65's two-pass assembly.
; ============================================================================
.include "tileset_rogue.inc"
