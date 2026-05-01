#!/usr/bin/env python3
"""
Extract the 12 emoticon sprites from the "Expression" section of
Quale's SCROLL-O-SPRITES sheet (May 2013, CC-BY-3.0) at
pic/undefined - Imgur.png.

Section layout (verified pixel-by-pixel):
    Cell origin : (col=32, row=176)
    Cell pitch  : 16 px wide x 16 px tall
    Count       : 12 sprites in a single horizontal row
    Content     : ~14 cols x 12 rows of cream pixels per cell, centred

Each cell is converted to a 16x16 TMS9918 sprite pattern (32 bytes,
TL/BL/TR/BR quarter-block layout). Names map to the visible emotion.

Outputs:
    screenshots/scroll_expressions.png  -- preview grid (zoomed)
    stdout                              -- ca65 .byte directives

Usage:
    python3 tools/extract_scroll_expressions.py
    python3 tools/extract_scroll_expressions.py --no-stdout
"""
from __future__ import annotations

import argparse
import pathlib
import sys
from typing import Dict, List

import numpy as np
from PIL import Image, ImageDraw, ImageFont


REPO = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_SRC = REPO / "pic" / "undefined - Imgur.png"
DEFAULT_OUT = REPO / "screenshots" / "scroll_expressions.png"

CELL_ORIGIN = (32, 176)
CELL_PITCH = 16
N_CELLS = 12

# Emotion name + LOGO shape symbol (max 6 chars to fit shape_table padding).
# Reading the sprites left-to-right (names from the sprite designer's eye --
# corrected by the user 2026-05-01):
EXPRESSIONS = [
    ("NORMAL", "normal_pat", "neutral / default expression"),
    ("HAPPY",  "happy_pat",  "happy"),
    ("SUPER",  "super_pat",  "super happy, big open mouth"),
    ("SAD",    "sad_pat",    "sad / frown"),
    ("UPSET",  "upset_pat",  "upset / disappointed"),
    ("ANGRY",  "angry_pat",  "angry, frowning brows"),
    ("GRUMPY", "grumpy_pat", "grumpy, tongue out"),
    ("PERV",   "perv_pat",   "pervy / lewd"),
    ("SICK",   "sick_pat",   "queasy / about to throw up (X eyes)"),
    ("SLEEP",  "sleep_pat",  "asleep"),
    ("PIRATE", "pirate_pat", "pirate (one eye shut)"),
    ("SHADES", "shades_pat", "wearing shades / sunglasses"),
]


def load_bw(src: pathlib.Path) -> np.ndarray:
    """Load image, return uint8 H*W array (1 = cream/sprite, 0 = bg)."""
    img = Image.open(src).convert("RGB")
    arr = np.array(img)
    cream = ((arr[:, :, 0] > 200) &
             (arr[:, :, 1] > 200) &
             (arr[:, :, 2] > 150)).astype(np.uint8)
    return cream


def extract_cell(bw: np.ndarray, idx: int) -> np.ndarray:
    """Return 16x16 cell for sprite #idx (0..N_CELLS-1)."""
    x0 = CELL_ORIGIN[0] + idx * CELL_PITCH
    y0 = CELL_ORIGIN[1]
    return bw[y0:y0 + CELL_PITCH, x0:x0 + CELL_PITCH]


def to_tms9918_bytes(g: np.ndarray) -> List[int]:
    """16x16 -> 32 bytes in TL, BL, TR, BR quarter-block order."""
    assert g.shape == (16, 16), g.shape
    out = []
    for y0, x0 in ((0, 0), (8, 0), (0, 8), (8, 8)):
        for r in range(8):
            byte = 0
            for x in range(8):
                if g[y0 + r, x0 + x]:
                    byte |= 1 << (7 - x)
            out.append(byte)
    return out


def render_preview(bw: np.ndarray, out_path: pathlib.Path,
                   scale: int = 8) -> None:
    cell_px = 16 * scale
    pad = 12
    label_h = 16
    title_h = 22
    bg = (28, 28, 36)
    fg = (255, 255, 255)
    grid_color = (60, 60, 76)
    text = (200, 210, 230)
    title_color = (255, 220, 120)

    cols = N_CELLS // 2          # 6 per row -> 2 rows x 6 cols
    rows = (N_CELLS + cols - 1) // cols
    sheet_w = pad + cols * (cell_px + pad)
    sheet_h = pad + title_h + rows * (cell_px + label_h + pad)

    img = Image.new("RGB", (sheet_w, sheet_h), bg)
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 12)
        title_font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 14)
    except OSError:
        font = ImageFont.load_default()
        title_font = font

    draw.text((pad, pad),
              "SCROLL-O-SPRITES \"Expression\" (12 emotes, 16x16, CC-BY-3.0 Quale)",
              fill=title_color, font=title_font)
    y = pad + title_h
    for i, (name, sym, _desc) in enumerate(EXPRESSIONS):
        r = i // cols
        c = i % cols
        x = pad + c * (cell_px + pad)
        cy = y + r * (cell_px + label_h + pad)
        cell16 = extract_cell(bw, i)
        for ry in range(16):
            for rx in range(16):
                if cell16[ry, rx]:
                    draw.rectangle(
                        [x + rx * scale, cy + ry * scale,
                         x + (rx + 1) * scale - 1,
                         cy + (ry + 1) * scale - 1],
                        fill=fg,
                    )
        mid = x + 8 * scale
        draw.line([(mid, cy), (mid, cy + cell_px)], fill=grid_color)
        draw.line([(x, cy + 8 * scale),
                   (x + cell_px, cy + 8 * scale)], fill=grid_color)
        draw.rectangle(
            [x, cy, x + cell_px - 1, cy + cell_px - 1],
            outline=grid_color,
        )
        tw = draw.textlength(name, font=font)
        draw.text((x + (cell_px - tw) // 2, cy + cell_px + 2),
                  name, fill=text, font=font)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(out_path)
    print(f"[preview] {out_path}  ({img.width}x{img.height} px)",
          file=sys.stderr)


def emit_asm(bw: np.ndarray) -> str:
    """Return ca65 .byte directives + a shape_table snippet ready to paste
    into TMS_Logo_16k.asm."""
    out = []
    out.append("; ===== Expression emotes (12x 16x16, from SCROLL-O-SPRITES "
               "by Quale, CC-BY-3.0) =====")
    out.append("; Source: pic/undefined - Imgur.png  -- "
               "tools/extract_scroll_expressions.py")
    out.append(";")
    # shape_table snippet
    out.append("; --- shape_table entries (paste before the $FF terminator) ---")
    for name, sym, desc in EXPRESSIONS:
        padded = (name + "      ")[:6]
        out.append(f'        .byte "{padded}"')
        out.append("        .byte 32")
        out.append(f"        .word {sym}")
    out.append("")
    out.append("; --- pattern data (place near the other 16x16 patterns) ---")
    for i, (name, sym, desc) in enumerate(EXPRESSIONS):
        cell16 = extract_cell(bw, i)
        bs = to_tms9918_bytes(cell16)
        out.append(f"; {name} -- {desc}")
        out.append(f"{sym}:")
        for off in (0, 8, 16, 24):
            out.append("        .byte " +
                       ", ".join(f"${b:02X}" for b in bs[off:off + 8]))
    return "\n".join(out)


def main(argv):
    p = argparse.ArgumentParser()
    p.add_argument("--src", type=pathlib.Path, default=DEFAULT_SRC)
    p.add_argument("--out", type=pathlib.Path, default=DEFAULT_OUT)
    p.add_argument("--scale", type=int, default=8)
    p.add_argument("--no-stdout", action="store_true")
    args = p.parse_args(argv)

    bw = load_bw(args.src)
    render_preview(bw, args.out, args.scale)
    if not args.no_stdout:
        print(emit_asm(bw))


if __name__ == "__main__":
    main(sys.argv[1:])
