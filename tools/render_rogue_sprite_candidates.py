#!/usr/bin/env python3
"""Render candidate replacement sprites for the TMS_Rogue door / stairs
tiles. Three columns (door / stairs_down / stairs_up), each showing
4-8 sprites from dev/lib/tms9918/sprites_*.asm at the same colour as
their target tile group, so the user can pick a swap-in quickly.

Output: sketchs/tms9918/game_rogue/sprite_candidates.png
"""
from __future__ import annotations

import pathlib

from PIL import Image, ImageDraw, ImageFont

# Reuse the renderer + parser from the inventory script.
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from render_rogue_sprite_inventory import (   # noqa: E402
    LIB_TMS, PROJECT, TMS_PALETTE,
    load_all_sprites, render_sprite_16x16, load_font,
)

OUT_PNG = PROJECT / "sprite_candidates.png"


# (column_label, target_tile_role, fg_idx, bg_idx, [sprite_label, ...])
COLUMNS = [
    ("DOOR", "TILE_DOOR (id 12, group 1, dk-yellow)", 10, 1, [
        ("bldg_door_closed_pat",    "current (was bldg_battlement_pat)"),
        ("bldg_slot_pat",         "metal door"),
        ("bldg_block_pat",  "locked door"),
        ("bldg_panel_ready_pat",       "window"),
        ("bldg_panel_used_pat", "window grate"),
        ("trap_trapdoor_ready_pat",        "grate"),
        ("expl_bag_pat",         "lock"),
    ]),
    ("STAIRS_DOWN", "TILE_STAIRS_DOWN (id 8, group 1, dk-yellow)", 10, 1, [
        ("bldg_stairs_down_pat", "current (was bldg_house_pat)"),
        ("bldg_stairs_up_pat",  "ascending stairs (slot 12)"),
        ("magic_totem_pat",    "portal"),
        ("magic_emanation_pat",    "vortex"),
        ("trap_trap_used_pat",        "pit trap"),
        ("sym_confusion_pat",      "spiral"),
        ("sym_ex_pat",           "X mark"),
        ("magic_book_pat", "runestone"),
    ]),
    ("STAIRS_UP", "TILE_STAIRS_UP (id 16, group 2, lt-yellow)", 11, 1, [
        ("bldg_stairs_up_pat", "current (was bldg_stairs_pat)"),
        ("bldg_bed_pat",      "arch"),
        ("bldg_wheel_pat",      "dome"),
        ("magic_ankh_pat",     "ankh"),
        ("magic_arcane_pat",      "sun"),
        ("sym_up_pat",   "arrow up"),
        ("sym_ice_pat",    "sparkle"),
        ("sym_star_pat",       "star"),
    ]),
]


def main() -> int:
    sprites = load_all_sprites()

    SCALE = 5
    SPR_PX = 16 * SCALE
    PAD = 24
    COL_W = 280
    HEADER_H = 70
    ROW_H = SPR_PX + 18
    rows = max(len(c[4]) for c in COLUMNS)
    WIDTH = PAD * 2 + COL_W * len(COLUMNS)
    HEIGHT = PAD + HEADER_H + ROW_H * rows + PAD

    img = Image.new("RGB", (WIDTH, HEIGHT), (24, 24, 28))
    draw = ImageDraw.Draw(img)
    title_font = load_font(20)
    head_font = load_font(15)
    label_font = load_font(13)

    draw.text((PAD, 8),
              "TMS_Rogue — sprite candidates (door / stairs)",
              fill=(240, 240, 240), font=title_font)

    for ci, (col_name, sub, fg_idx, bg_idx, items) in enumerate(COLUMNS):
        x_col = PAD + ci * COL_W
        draw.text((x_col, PAD + 24), col_name,
                  fill=(255, 255, 255), font=head_font)
        draw.text((x_col, PAD + 44), sub,
                  fill=(180, 200, 220), font=label_font)
        fg = TMS_PALETTE[fg_idx]
        bg = TMS_PALETTE[bg_idx]
        for ri, (label, descr) in enumerate(items):
            y = PAD + HEADER_H + ri * ROW_H
            if label not in sprites:
                spr = Image.new("RGB", (16, 16), (200, 0, 0))
            else:
                spr = render_sprite_16x16(sprites[label], fg, bg)
            spr = spr.resize((SPR_PX, SPR_PX), Image.NEAREST)
            bordered = Image.new("RGB", (SPR_PX + 2, SPR_PX + 2),
                                 (90, 90, 90))
            bordered.paste(spr, (1, 1))
            img.paste(bordered, (x_col - 1, y - 1))
            tx = x_col + SPR_PX + 12
            draw.text((tx, y + 14), label,
                      fill=(255, 255, 255), font=label_font)
            draw.text((tx, y + 32), descr,
                      fill=(220, 220, 160), font=label_font)

    img.save(OUT_PNG)
    print(f"Wrote {OUT_PNG} ({WIDTH}x{HEIGHT})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
