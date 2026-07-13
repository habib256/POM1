#!/usr/bin/env python3
"""build_rogue_hgr_assets.py -- generate the GEN2 HGR asset pack for game_rogue.

Converts the TMS9918 Rogue art (8x8 tileset chars + inline 16x16 sprite
patterns + the 32x32 Hexany boss) into HGR bitmap data for the
sketchs/gen2/game_rogue port:

  - 8 map tiles   : 16x16 TMS (2x2 chars) -> 14x16 HGR (2 bytes x 16 rows)
  - 14 sprites    : 16x16 TMS (32 B)      -> 14x16 HGR (2 bytes x 16 rows)
  - 1 boss        : 32x32 TMS (4 slots)   -> 28x32 HGR (4 bytes x 32 rows)

TMS pixel rows are MSB-leftmost; HGR bytes are bit0-leftmost, 7 px/byte,
bit 7 (palette group) kept clear. Width 16 -> 14 crops one pixel column
off each side (Quale sprites keep their silhouettes well inside the box).

Output: sketchs/gen2/game_rogue/rogue_assets_hgr.inc (ca65 .byte tables).

DO NOT EDIT the output by hand. Re-run this script instead.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TILESET = ROOT / "sketchs/tms9918/game_rogue/tileset_rogue.inc"
MAIN_ASM = ROOT / "sketchs/tms9918/game_rogue/TMS_Rogue.asm"
BOSS_ASM = ROOT / "sketchs/tms9918/game_rogue/sprites_boss.asm"
OUT = ROOT / "sketchs/gen2/game_rogue/rogue_assets_hgr.inc"

BYTE_RE = re.compile(r"^\s*\.byte\s+(.+?)(?:\s*;.*)?$")


def parse_bytes(lines):
    """Collect all .byte payload values from an iterable of source lines."""
    out = []
    for line in lines:
        m = BYTE_RE.match(line)
        if not m:
            continue
        for tok in m.group(1).split(","):
            tok = tok.strip()
            if tok.startswith("$"):
                out.append(int(tok[1:], 16))
            elif tok:
                out.append(int(tok, 0))
    return out


def block_after(text, start_label, end_marker):
    """Source lines between `start_label` and the line containing `end_marker`."""
    lines = text.splitlines()
    try:
        i = next(n for n, l in enumerate(lines) if l.strip().startswith(start_label))
    except StopIteration:
        raise SystemExit(f"label {start_label!r} not found")
    j = next(
        (n for n in range(i + 1, len(lines)) if end_marker in lines[n]), len(lines)
    )
    return lines[i:j]


def tms16_to_rows(data):
    """32 B TMS 16x16 sprite (left col rows 0-15, right col rows 0-15) ->
    16 ints of 16 pixel bits, MSB = leftmost."""
    assert len(data) == 32
    return [(data[r] << 8) | data[16 + r] for r in range(16)]


def chars_to_rows(tl, tr, bl, br):
    """Four 8-byte chars (TL TR BL BR) -> 16 ints of 16 pixel bits."""
    rows = [(tl[r] << 8) | tr[r] for r in range(8)]
    rows += [(bl[r] << 8) | br[r] for r in range(8)]
    return rows


def row16_to_hgr(row):
    """16-pixel row (MSB leftmost) -> 2 HGR bytes covering pixels 1..14."""
    out = []
    for byte_i in range(2):
        b = 0
        for px in range(7):
            src_col = 1 + byte_i * 7 + px  # crop col 0; cols 1..14
            if row & (1 << (15 - src_col)):
                b |= 1 << px  # HGR bit0 = leftmost
        out.append(b)
    return out


def row32_to_hgr(row):
    """32-pixel row (MSB leftmost) -> 4 HGR bytes covering pixels 2..29."""
    out = []
    for byte_i in range(4):
        b = 0
        for px in range(7):
            src_col = 2 + byte_i * 7 + px  # crop 2 cols each side; 2..29
            if row & (1 << (31 - src_col)):
                b |= 1 << px
        out.append(b)
    return out


def emit_bytes(f, data, per_line=8):
    for i in range(0, len(data), per_line):
        chunk = ", ".join(f"${b:02X}" for b in data[i : i + per_line])
        f.write(f"        .byte {chunk}\n")


# ---------------------------------------------------------------------------
# x2 colour pack (game_rogue_x2): every playfield asset doubled to 28xH
# (4 HGR bytes/row) with the colour BAKED IN via pixel-parity masks —
# doubled pixels are 2-px runs, so keeping one parity yields a pure NTSC
# artifact colour with the shape intact (the hgr_sprite16 lesson). The
# blit positions are always 4-byte-aligned (byte cols 4 + 4*vc), so byte
# index i has fixed column parity (even for i%2==0) and masks can be
# applied at generation time. HUD/modal icons keep using the x1 pack.
# ---------------------------------------------------------------------------
# colour -> (mask for even byte cols, mask for odd byte cols, palette bit)
X2_COLORS = {
    "white":  (0x7F, 0x7F, 0x00),
    "green":  (0x2A, 0x55, 0x00),
    "violet": (0x55, 0x2A, 0x00),
    "orange": (0x2A, 0x55, 0x80),
    "blue":   (0x55, 0x2A, 0x80),
}
TILE_HUES = {          # dense-tile visuals (ids 6/7 render as empty)
    "empty": "white", "wall": "white", "stairs_down": "green",
    "door": "orange", "stairs_up": "white", "trap_pit": "violet",
    "gold": "orange", "potion": "violet",
}
SPRITE_HUES = {        # TMS palette -> nearest HGR artifact colour
    "char_guard": "blue",       # hero: light blue on TMS
    "undead_zombie": "green",   # zombie flesh
    "undead_ghost": "violet",
    "undead_reaper": "orange",  # DEATH (dk-yellow on TMS)
    "food_meat": "green",
    "undead_skeleton": "white", # bones (cyan-bright on TMS)
    "item_dagger": "white",     # steel
    "item_potion": "violet",
    "item_scroll": "white",     # paper
    "item_weapon": "blue",      # steel sheen
    "item_armor": "orange",
    "item_ring": "orange",      # gold on TMS
    "troll_goblin": "green",
    "expl_torch": "orange",
}
BOSS_HUE = "orange"            # medium red on TMS

OUT_X2 = ROOT / "sketchs/gen2/game_rogue_x2/rogue_assets_hgr_x2.inc"


def rows16_to_x2_hgr(rows, hue):
    """16 rows of 16 px (MSB-left) -> x2: 32 rows x 4 HGR bytes, coloured.
    Horizontal: source cols 1..14 doubled to 28 px (same crop as the x1
    pack); vertical: each row emitted twice."""
    ev, od, bit = X2_COLORS[hue]
    out = []
    for row in rows:
        rbytes = []
        for byte_i in range(4):
            b = 0
            for px in range(7):
                dx = byte_i * 7 + px        # doubled-image col 0..27
                src_col = 1 + dx // 2       # source cols 1..14
                if row & (1 << (15 - src_col)):
                    b |= 1 << px
            b &= ev if (byte_i % 2 == 0) else od
            if b:
                b |= bit
            rbytes.append(b)
        out.append(rbytes)
        out.append(list(rbytes))
    return [b for r in out for b in r]


def main():
    # --- tiles: chars 0..31 of the tileset pattern table -------------------
    tiles_src = TILESET.read_text()
    pat = parse_bytes(
        block_after(tiles_src, "tileset_rogue:", "tileset_color_table")
    )
    assert len(pat) >= 32 * 8, f"tileset too short: {len(pat)}"
    chars = [pat[i * 8 : (i + 1) * 8] for i in range(32)]
    tile_names = [
        "empty", "wall", "stairs_down", "door",
        "stairs_up", "trap_pit", "gold", "potion",
    ]
    tiles_hgr = []
    for t in range(8):
        rows = chars_to_rows(*chars[t * 4 : t * 4 + 4])
        tiles_hgr.append([b for row in rows for b in row16_to_hgr(row)])

    # --- sprites: the inline sprite_pats block (14 x 32 B) -----------------
    main_src = MAIN_ASM.read_text()
    spr = parse_bytes(block_after(main_src, "sprite_pats:", "prng16 --"))
    assert len(spr) == 14 * 32, f"expected 448 sprite bytes, got {len(spr)}"
    sprite_names = [
        "char_guard", "undead_zombie", "undead_ghost", "undead_reaper",
        "food_meat", "undead_skeleton", "item_dagger", "item_potion",
        "item_scroll", "item_weapon", "item_armor", "item_ring",
        "troll_goblin", "expl_torch",
    ]
    sprites_hgr = []
    for s in range(14):
        rows = tms16_to_rows(spr[s * 32 : (s + 1) * 32])
        sprites_hgr.append([b for row in rows for b in row16_to_hgr(row)])

    # --- boss: 4 x 16x16 quadrants (TL, TR, BL, BR) -> 28x32 ---------------
    boss_src = BOSS_ASM.read_text()
    bossb = parse_bytes(block_after(boss_src, "boss_sprite_pats:", "\x00never"))
    assert len(bossb) == 4 * 32, f"expected 128 boss bytes, got {len(bossb)}"
    q = [tms16_to_rows(bossb[i * 32 : (i + 1) * 32]) for i in range(4)]
    boss_rows = [(q[0][r] << 16) | q[1][r] for r in range(16)]
    boss_rows += [(q[2][r] << 16) | q[3][r] for r in range(16)]
    boss_hgr = [b for row in boss_rows for b in row32_to_hgr(row)]

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w") as f:
        f.write(
            "; ============================================================================\n"
            "; rogue_assets_hgr.inc -- generated by tools/build_rogue_hgr_assets.py\n"
            "; ----------------------------------------------------------------------------\n"
            "; GEN2 HGR asset pack for game_rogue: 8 map tiles + 14 entity/item sprites\n"
            "; (14x16 px, 2 bytes x 16 rows = 32 B each) + the 28x32 boss (4 bytes x 32\n"
            "; rows = 128 B). Converted from the TMS9918 art (Quale's SCROLL-O-SPRITES\n"
            "; CC-BY-3.0 + Hexany Ives' Monster Menagerie CC0). HGR packing: 7 px/byte,\n"
            "; bit 0 = leftmost pixel, bit 7 (palette group) clear.\n"
            ";\n"
            "; CHAR_* keep their TMS values so tile_char_base needs no edit: the HGR\n"
            "; tile bitmap for dense id X lives at rogue_tiles_hgr + tile_char_base[X]*8.\n"
            "; Sprites keep their TMS slot numbers (0,4,8..52): bitmap at\n"
            "; rogue_sprites_hgr + slot*8.\n"
            ";\n"
            "; DO NOT EDIT BY HAND. Re-run the script if the source art changes.\n"
            "; ============================================================================\n\n"
        )
        f.write("CHAR_EMPTY          = 0\n")
        f.write("CHAR_WALL           = 4\n")
        f.write("CHAR_STAIRS_DOWN    = 8\n")
        f.write("CHAR_DOOR           = 12\n")
        f.write("CHAR_STAIRS_UP      = 16\n")
        f.write("CHAR_TRAP_PIT       = 20\n")
        f.write("CHAR_GOLD           = 24\n")
        f.write("CHAR_POTION         = 28\n\n")

        f.write("; --- map tiles: 8 x (16 rows x 2 bytes) --------------------------------\n")
        f.write("rogue_tiles_hgr:\n")
        for name, data in zip(tile_names, tiles_hgr):
            f.write(f"; tile {name}\n")
            emit_bytes(f, data)
        f.write("\n; --- sprites: 14 x (16 rows x 2 bytes), TMS slot = index*4 -------------\n")
        f.write("rogue_sprites_hgr:\n")
        for i, (name, data) in enumerate(zip(sprite_names, sprites_hgr)):
            f.write(f"{name}_hgr:                     ; slot {i * 4}\n")
            emit_bytes(f, data)
        f.write("\n; --- boss: 32 rows x 4 bytes (28x32 px) --------------------------------\n")
        f.write("boss_hgr:\n")
        emit_bytes(f, boss_hgr)

    total = 8 * 32 + 14 * 32 + 128
    print(f"wrote {OUT} ({total} data bytes)")

    # --- x2 colour pack -----------------------------------------------
    if OUT_X2.parent.exists():
        with OUT_X2.open("w") as f:
            f.write(
                "; ============================================================================\n"
                "; rogue_assets_hgr_x2.inc -- generated by tools/build_rogue_hgr_assets.py\n"
                "; ----------------------------------------------------------------------------\n"
                "; x2 COLOUR pack for game_rogue_x2: 8 map tiles + 14 sprites doubled to\n"
                "; 28x32 px (4 HGR bytes x 32 rows = 128 B each) and the boss doubled to\n"
                "; 56x64 px (two 4-byte-wide column halves of 64 rows, 256 B each). The\n"
                "; NTSC artifact colour is BAKED IN (pixel-parity masks + palette bit,\n"
                "; byte-column parity fixed by the 4-aligned blit positions).\n"
                ";\n"
                "; DO NOT EDIT BY HAND. Re-run the script if the art or palette changes.\n"
                "; ============================================================================\n\n"
            )
            f.write("; --- x2 tiles: 8 x (32 rows x 4 bytes), offset = tile_char_base*32 -----\n")
            f.write("rogue_tiles_hgr_x2:\n")
            for name, chs in zip(tile_names, range(8)):
                rows = chars_to_rows(*chars[chs * 4 : chs * 4 + 4])
                f.write(f"; tile {name} ({TILE_HUES[name]})\n")
                emit_bytes(f, rows16_to_x2_hgr(rows, TILE_HUES[name]))
            f.write("\n; --- x2 sprites: 14 x (32 rows x 4 bytes), offset = slot*32 ------------\n")
            f.write("rogue_sprites_hgr_x2:\n")
            for i, name in enumerate(sprite_names):
                rows = tms16_to_rows(spr[i * 32 : (i + 1) * 32])
                f.write(f"{name}_hgr_x2:                  ; slot {i * 4} ({SPRITE_HUES[name]})\n")
                emit_bytes(f, rows16_to_x2_hgr(rows, SPRITE_HUES[name]))
            # boss: 32x32 -> 56x64, two 4-byte column halves
            ev, od, bit = X2_COLORS[BOSS_HUE]
            halves = ([], [])
            for row in boss_rows:               # 32 rows of 32 px
                hb = [[], []]
                for byte_i in range(8):
                    b = 0
                    for px in range(7):
                        dx = byte_i * 7 + px    # doubled col 0..55
                        src_col = 2 + dx // 2   # source cols 2..29 (x1 crop)
                        if row & (1 << (31 - src_col)):
                            b |= 1 << px
                    b &= ev if (byte_i % 2 == 0) else od
                    if b:
                        b |= bit
                    hb[byte_i // 4].append(b)
                for h in range(2):
                    halves[h].extend(hb[h])     # row for this half...
                    halves[h].extend(hb[h])     # ...emitted twice (vertical x2)
            f.write("\n; --- x2 boss: two column halves, 64 rows x 4 bytes each ----------------\n")
            f.write(f"boss_hgr_x2_l:                 ; left 28 px ({BOSS_HUE})\n")
            emit_bytes(f, halves[0])
            f.write("boss_hgr_x2_r:                 ; right 28 px\n")
            emit_bytes(f, halves[1])
        print(f"wrote {OUT_X2} ({8*128 + 14*128 + 512} data bytes)")


if __name__ == "__main__":
    main()
