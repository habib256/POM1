#!/usr/bin/env python3
"""Render every Quale sprite from dev/lib/tms9918/sprites_*.asm into a
single labelled PNG, grouped by source file. Used to pick replacement
tiles for TMS_Rogue and to spot mis-labelled entries (the user can
visually verify name → sprite correspondence).

Output: sketchs/tms9918/game_rogue/sprite_sheet_all.png
"""
from __future__ import annotations

import pathlib
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from render_rogue_sprite_inventory import (   # noqa: E402
    LIB_TMS, PROJECT, TMS_PALETTE,
    parse_byte_blocks, render_sprite_16x16, load_font,
)

OUT_PNG = PROJECT / "sprite_sheet_all.png"


def main() -> int:
    sections: list[tuple[str, list[tuple[str, list[int]]]]] = []
    total = 0
    for asm in sorted(LIB_TMS.glob("sprites_*.asm")):
        sprites = parse_byte_blocks(asm, 32)
        if not sprites:
            continue
        items = sorted(sprites.items())
        sections.append((asm.stem, items))
        total += len(items)

    SCALE = 3
    SPR_PX = 16 * SCALE          # 48 px
    COLS = 4
    PAD = 24
    CELL_W = 270
    CELL_H = SPR_PX + 24
    SECTION_GAP = 18
    HEADER_H = 32

    rows_per_section = [
        (len(items) + COLS - 1) // COLS for _, items in sections
    ]
    HEIGHT = (PAD + 40
              + sum(HEADER_H + r * CELL_H + SECTION_GAP
                    for r in rows_per_section)
              + PAD)
    WIDTH = PAD * 2 + COLS * CELL_W

    img = Image.new("RGB", (WIDTH, HEIGHT), (24, 24, 28))
    draw = ImageDraw.Draw(img)
    title_font = load_font(22)
    section_font = load_font(17)
    label_font = load_font(12)

    fg = TMS_PALETTE[15]
    bg = TMS_PALETTE[1]

    draw.text((PAD, 10),
              f"Quale sprite library — full sheet ({total} sprites)",
              fill=(240, 240, 240), font=title_font)

    y = PAD + 40
    for (fname, items), rows in zip(sections, rows_per_section):
        # Section header band
        draw.rectangle([(PAD - 4, y - 4),
                        (WIDTH - PAD + 4, y + HEADER_H - 8)],
                       fill=(50, 50, 70))
        draw.text((PAD, y),
                  f"{fname}  ({len(items)} sprites)",
                  fill=(255, 220, 100), font=section_font)
        y += HEADER_H
        for i, (label, data) in enumerate(items):
            col = i % COLS
            row = i // COLS
            x = PAD + col * CELL_W
            yy = y + row * CELL_H
            spr = render_sprite_16x16(data, fg, bg)
            spr = spr.resize((SPR_PX, SPR_PX), Image.NEAREST)
            bordered = Image.new("RGB", (SPR_PX + 2, SPR_PX + 2),
                                 (90, 90, 90))
            bordered.paste(spr, (1, 1))
            img.paste(bordered, (x - 1, yy - 1))
            tx = x + SPR_PX + 10
            draw.text((tx, yy + 16), label,
                      fill=(255, 255, 255), font=label_font)
        y += rows * CELL_H + SECTION_GAP

    img.save(OUT_PNG)
    print(f"Wrote {OUT_PNG} ({WIDTH}x{HEIGHT}, {total} sprites)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
