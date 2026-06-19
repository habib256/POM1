#!/usr/bin/env python3
"""Render the current TMS_Rogue sprite inventory as a labeled PNG.

Each entry shows the 16x16 Quale sprite scaled 4x, the TILE_* equate,
the source label in dev/lib/tms9918/sprites_*.asm, and the role / colour
group as it appears in-game. Run after editing the PALETTE in
tools/extract_quale_8x8_tiles.py to preview the result without booting
POM1.

Output: dev/projects/tms9918/game_rogue/sprite_inventory.png
"""
from __future__ import annotations

import pathlib
import re

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIB_TMS = ROOT / "dev" / "lib" / "tms9918"
PROJECT = ROOT / "dev" / "projects" / "tms9918_rogue"
OUT_PNG = PROJECT / "sprite_inventory.png"

# Well-known TMS9918 palette → RGB approximation.
TMS_PALETTE = {
    0:  (0, 0, 0),
    1:  (0, 0, 0),
    2:  (33, 200, 66),
    3:  (94, 220, 120),
    4:  (84, 85, 237),
    5:  (125, 118, 252),
    6:  (212, 82, 77),
    7:  (66, 235, 245),
    8:  (252, 85, 84),
    9:  (255, 121, 120),
    10: (212, 193, 84),
    11: (230, 206, 128),
    12: (33, 176, 59),
    13: (201, 91, 186),
    14: (204, 204, 204),
    15: (255, 255, 255),
}

# Tiles currently used by TMS_Rogue (rendered into the name table via
# the pattern table at $0000). Mirrors the PALETTE dict in
# tools/extract_quale_8x8_tiles.py — keep in sync by hand. fg/bg from
# the tileset_color_table groups (one fg/bg per group of 8 chars).
#   group 0 (chars  0.. 7): $E1 — gray on black
#   group 1 (chars  8..15): $A1 — dk-yellow on black
#   group 2 (chars 16..23): $B1 — lt-yellow on black
#   group 3 (chars 24..31): $71 — cyan on black
TILES = [
    # tile_id, sprite_label,            name,         fg, bg, role
    # TILE_EMPTY (id 0) intentionally omitted — char 0 is all-zero by
    # default, no sprite to display.
    (4,  "bldg_brick_wall_pat",         "WALL",       14, 1,
     "stone wall (everything outside dug area)"),
    (8,  "bldg_stairs_down_pat",        "STAIRS_DOWN", 10, 1,
     "stairs down — descent path; advances depth"),
    (12, "bldg_door_wood_pat",          "DOOR",       10, 1,
     "wooden door — passable for player + monsters"),
    (16, "bldg_cobble3_pat",            "STAIRS_UP",  11, 1,
     "rubble — 'the way up just collapsed' (one-way descent)"),
    (20, "expl_torch_pat",              "TORCH",      11, 1,
     "torch — DEFINED in tileset, NOT USED in code yet"),
    (24, "expl_coin_pat",               "GOLD",        7, 1,
     "gold — NOT USED yet (item pool is food-only for now)"),
    (28, "expl_flask_pat",              "POTION",      7, 1,
     "potion — NOT USED yet (item pool is food-only for now)"),
]

# Hardware sprites (16x16 SAT entries, pattern table at $3800). Inlined
# into sprite_pats in TMS_Rogue.asm rather than .imported from the lib
# to fit the cartridge layout cleanly.
#   slot  0 ($3800) : player paladin
#   slot  4 ($3820) : skull   (MON_TYPE 1)
#   slot  8 ($3840) : ghost   (MON_TYPE 2)
#   slot 12 ($3860) : mummy   (MON_TYPE 3)
#   slot 16 ($3880) : food meat (ITEM_TYPE 1, food drop)
HW_SPRITES = [
    # (slot, sprite_label, display_name, fg_idx, bg_idx, role)
    (0,  "char_paladin2_pat",  "PLAYER",   5,  1,
     "lt-blue — flashes red (col 8) when hit by a monster"),
    (4,  "undead_undead_pat",  "UNDEAD",   15, 1,
     "MON_TYPE 1 — 1 HP, 1 dmg; weakest undead (Quale's 'skull' artwork)"),
    (8,  "undead_ghost_pat",   "GHOST",    14, 1,
     "MON_TYPE 2 — 2 HP, 1 dmg; mid-tier"),
    (12, "undead_death_pat",   "DEATH",    10, 1,
     "MON_TYPE 4 — 3 HP, 2 dmg; strongest undead, gated to depth 4 (Quale's 'mummy' artwork)"),
    (16, "food_meat_pat",      "FOOD",     11, 1,
     "drumstick dropped by dead monster (50% rate) — heals FOOD_HEAL=2 HP"),
    (20, "undead_skeleton_pat", "SKELETON", 7, 1,
     "MON_TYPE 3 — 2 HP, 2 dmg; warrior tier, unlocks at depth 3 (Quale's 'crossbones' artwork)"),
]


LABEL_RE = re.compile(r"^([a-zA-Z][a-zA-Z0-9_]*):\s*$")
BYTE_RE = re.compile(r"^\s*\.byte\s+([^;]+?)(?:\s*;.*)?$")


def parse_byte_blocks(asm_path: pathlib.Path,
                      block_size: int) -> dict[str, list[int]]:
    out: dict[str, list[int]] = {}
    label = None
    bytes_collected: list[int] = []
    for raw in asm_path.read_text().splitlines():
        m = LABEL_RE.match(raw)
        if m:
            if label and len(bytes_collected) == block_size:
                out[label] = bytes_collected
            label = m.group(1)
            bytes_collected = []
            continue
        if label is None:
            continue
        if len(bytes_collected) >= block_size:
            continue
        m = BYTE_RE.match(raw)
        if not m:
            continue
        for tok in m.group(1).split(","):
            tok = tok.strip()
            if not tok:
                continue
            try:
                if tok.startswith("$"):
                    bytes_collected.append(int(tok[1:], 16))
                elif tok.lower().startswith("0x"):
                    bytes_collected.append(int(tok[2:], 16))
                else:
                    bytes_collected.append(int(tok))
            except ValueError:
                bytes_collected = []
                label = None
                break
    if label and len(bytes_collected) == block_size:
        out[label] = bytes_collected
    return out


def load_all_sprites() -> dict[str, list[int]]:
    merged: dict[str, list[int]] = {}
    for asm in sorted(LIB_TMS.glob("sprites_*.asm")):
        merged.update(parse_byte_blocks(asm, 32))
    # Player sprite (char_adventurer_pat) is inlined in TMS_Rogue.asm.
    merged.update(parse_byte_blocks(PROJECT / "TMS_Rogue.asm", 32))
    return merged


def render_sprite_16x16(data: list[int],
                        fg: tuple[int, int, int],
                        bg: tuple[int, int, int]) -> Image.Image:
    """Quale 32-byte 16x16 layout: bytes 0..7 = left top, 8..15 = left
    bottom, 16..23 = right top, 24..31 = right bottom (each 8 bytes is
    8 rows of one 8x8 quadrant)."""
    img = Image.new("RGB", (16, 16), bg)
    px = img.load()
    for byte_idx, byte_val in enumerate(data):
        if byte_idx < 8:
            row, col_off = byte_idx, 0
        elif byte_idx < 16:
            row, col_off = byte_idx, 0  # rows 8..15 of left half
        elif byte_idx < 24:
            row, col_off = byte_idx - 16, 8
        else:
            row, col_off = byte_idx - 16, 8  # rows 8..15 of right half
        for bit in range(8):
            if byte_val & (0x80 >> bit):
                px[col_off + bit, row] = fg
    return img


def load_font(size: int = 14) -> ImageFont.ImageFont:
    for cand in (
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    ):
        try:
            return ImageFont.truetype(cand, size)
        except OSError:
            continue
    return ImageFont.load_default()


def main() -> int:
    sprites = load_all_sprites()

    SCALE = 5
    SPR_PX = 16 * SCALE          # 80 px per sprite
    ROW_H = SPR_PX + 28
    PAD = 24
    TEXT_X = PAD + SPR_PX + 28
    WIDTH = 880
    SECTION_H = 32               # vertical room for each section header
    HEIGHT = (PAD * 2
              + SECTION_H + ROW_H * len(TILES)
              + SECTION_H + ROW_H * len(HW_SPRITES)
              + 40)

    img = Image.new("RGB", (WIDTH, HEIGHT), (24, 24, 28))
    draw = ImageDraw.Draw(img)
    title_font = load_font(20)
    section_font = load_font(17)
    big = load_font(16)
    small = load_font(13)

    draw.text((PAD, 8),
              "TMS_Rogue — sprite inventory (currently used)",
              fill=(240, 240, 240), font=title_font)

    y = PAD + 24

    def emit(label: str | None, name: str, head_prefix: str,
             tile_id: int | None, fg_idx: int, bg_idx: int,
             role: str, y: int) -> None:
        fg = TMS_PALETTE[fg_idx]
        bg = TMS_PALETTE[bg_idx]
        if label is None:
            spr = Image.new("RGB", (16, 16), bg)
        elif label not in sprites:
            spr = Image.new("RGB", (16, 16), (200, 0, 0))
        else:
            spr = render_sprite_16x16(sprites[label], fg, bg)
        spr = spr.resize((SPR_PX, SPR_PX), Image.NEAREST)
        # Border so black-on-black sprites stay visible
        bordered = Image.new("RGB", (SPR_PX + 2, SPR_PX + 2), (90, 90, 90))
        bordered.paste(spr, (1, 1))
        img.paste(bordered, (PAD - 1, y - 1))

        head = (f"{head_prefix}{name}  (id={tile_id})"
                if tile_id is not None else f"{head_prefix}{name}")
        draw.text((TEXT_X, y + 4), head,
                  fill=(255, 255, 255), font=big)
        sub = f"sprite label : {label or '(blank)'}"
        draw.text((TEXT_X, y + 28), sub,
                  fill=(180, 200, 220), font=small)
        col = f"colour       : fg={fg_idx} bg={bg_idx}"
        draw.text((TEXT_X, y + 46), col,
                  fill=(180, 200, 220), font=small)
        draw.text((TEXT_X, y + 64), role,
                  fill=(220, 220, 160), font=small)

    # --- Section 1: name-table tiles (8x8 quads in pattern table $0000) ---
    draw.text((PAD, y),
              "Tiles  (pattern table $0000, name table $1800)",
              fill=(160, 220, 255), font=section_font)
    y += SECTION_H
    for tile_id, label, name, fg_idx, bg_idx, role in TILES:
        emit(label, name, "TILE_", tile_id, fg_idx, bg_idx, role, y)
        y += ROW_H

    # --- Section 2: hardware sprites (16x16 in sprite-pattern table $3800) ---
    draw.text((PAD, y),
              "Hardware sprites  (sprite pattern table $3800, SAT $1B00)",
              fill=(160, 220, 255), font=section_font)
    y += SECTION_H
    for slot, label, name, fg_idx, bg_idx, role in HW_SPRITES:
        emit(label, name, f"slot {slot:>2} — ", None,
             fg_idx, bg_idx, role, y)
        y += ROW_H

    img.save(OUT_PNG)
    print(f"Wrote {OUT_PNG} ({WIDTH}x{HEIGHT})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
