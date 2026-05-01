#!/usr/bin/env python3
"""
Extract the 4 character x 3 animation-frame grid from the top-left of
pic/arduboy_graphics_v1.png and emit them as LOGO sprite patterns.

Source layout (verified pixel-by-pixel):
    Cell pitch   : 16 px wide x 16 px tall
    Origin       : (col=0, row=16)         -- cells aligned to col 0,16,32,48
    Frame pitch  : row 16, 32, 48          -- 3 frames per character (top-to-bottom)
    Character art: ~12 wide x ~16 tall (centred in the 16x16 cell, ~3-px
                   left padding, hair tufts can poke up to col 13 within cell)

Each cell is treated as one 8x16-character sprite split into:
  HEAD = top 8 rows, central 8 cols of the cell (rows 0-7, cols 4-11)
  BODY = bottom 8 rows, central 8 cols       (rows 8-15, cols 4-11)
  FULL = entire 16x16 cell                   (rows 0-15, cols 0-15)

The HEAD-only crop is the most useful 8x8 LOGO sprite (faces are
clearly distinct between the 4 characters and across animation frames).
The FULL 16x16 keeps the character entire (head + body) and matches
LOGO's existing 16x16 mode.

Outputs:
    screenshots/arduboy_chars_proposed.png  -- preview grid
    stdout                                  -- ca65 .byte directives

Usage:
    python3 tools/extract_arduboy_topleft.py
    python3 tools/extract_arduboy_topleft.py --threshold 100
    python3 tools/extract_arduboy_topleft.py --crop head   # only emit 8x8 heads
    python3 tools/extract_arduboy_topleft.py --crop full   # only emit 16x16 chars
    python3 tools/extract_arduboy_topleft.py --crop body   # only emit 8x8 bodies
"""
from __future__ import annotations

import argparse
import pathlib
import sys
from typing import Dict, List, Tuple

import numpy as np
from PIL import Image, ImageDraw, ImageFont


REPO = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_SRC = REPO / "pic" / "arduboy_graphics_v1.png"
DEFAULT_OUT = REPO / "screenshots" / "arduboy_chars_proposed.png"

# Cell grid (col, row), each 16x16 in source.
CELL_W = 16
CELL_H = 16
ORIGIN_COL = 0
ORIGIN_ROW = 16
N_CHARS = 4       # columns
N_FRAMES = 3      # rows

# Crop windows inside a 16x16 cell:
#   HEAD: rows 0-7, cols 4-11 (centre-8)
#   BODY: rows 8-15, cols 4-11
HEAD_BOX = (4, 0, 12, 8)    # (x0, y0, x1, y1) within cell
BODY_BOX = (4, 8, 12, 16)


def load_bw(src: pathlib.Path, threshold: int) -> np.ndarray:
    """Load image, return uint8 H*W array (1 = ink, 0 = paper)."""
    img = Image.open(src).convert("L")
    arr = np.array(img)
    return (arr < threshold).astype(np.uint8)


def cell(bw: np.ndarray, char_idx: int, frame_idx: int) -> np.ndarray:
    """16x16 cell for char_idx (0..3) at frame_idx (0..2)."""
    x0 = ORIGIN_COL + char_idx * CELL_W
    y0 = ORIGIN_ROW + frame_idx * CELL_H
    return bw[y0:y0 + CELL_H, x0:x0 + CELL_W]


def crop(cell16: np.ndarray, box: Tuple[int, int, int, int]) -> np.ndarray:
    x0, y0, x1, y1 = box
    return cell16[y0:y1, x0:x1]


def to_8x8_bytes(g: np.ndarray) -> List[int]:
    assert g.shape == (8, 8), g.shape
    out = []
    for r in range(8):
        byte = 0
        for x in range(8):
            if g[r, x]:
                byte |= 1 << (7 - x)
        out.append(byte)
    return out


def to_16x16_bytes(g: np.ndarray) -> List[int]:
    """16x16 -> 32 bytes in TL, BL, TR, BR quarter-block order
       (TMS9918 sprite-pattern layout)."""
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


CHAR_NAMES = ["1: BRUN", "2: BLOND", "3: CAPUCHE", "4: COURONNE"]


def render_preview(bw: np.ndarray, out_path: pathlib.Path,
                   scale: int = 8) -> None:
    """Per-character section: title, then 3 rows (FULL / HEAD / BODY)
    with the 3 animation frames (A/B/C) horizontally. Vertical
    separators between character groups for clean reading.
    """
    cell_px = 16 * scale
    pad = 10
    sub_label_h = 14
    char_title_h = 28
    section_gap = 18
    bg = (24, 24, 32)
    fg = (255, 255, 255)
    grid_color = (60, 60, 76)
    text = (200, 210, 230)
    char_color = (255, 220, 120)
    mode_color = (140, 220, 200)
    src_color = (255, 255, 255)
    sep_color = (90, 90, 120)

    # 4 chars stacked vertically. Each char block:
    #   - 1 char title row
    #   - 3 sub-blocks (FULL, HEAD, BODY): mode label + 3 cells (A/B/C)
    modes = [
        ("FULL  16x16  (HERO*)",       "full", 16),
        ("HEAD  8x8   (HEAD*, V2.1)",  "head",  8),
        ("BODY  8x8   (BODY*, V2.1)",  "body",  8),
    ]

    # Width: source crop (left) + 3 frames * cell + label space
    src_w = 8 * scale * 2          # source crop column (16-wide x 16 = 256 at 8x)
    src_label_h = 16
    block_h = sub_label_h + cell_px + 6  # one mode block per char section
    char_section_h = char_title_h + len(modes) * block_h + section_gap

    sheet_w = pad + src_w + 2 * pad + N_FRAMES * (cell_px + pad)
    sheet_h = pad + N_CHARS * char_section_h
    img = Image.new("RGB", (sheet_w, sheet_h), bg)
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 11)
        title_font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 16)
        mode_font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 12)
    except OSError:
        font = ImageFont.load_default()
        title_font = font
        mode_font = font

    def paint_grid(g: np.ndarray, x0: int, y0: int, sz: int) -> None:
        inset = (16 - sz) * scale // 2
        for ry in range(sz):
            for rx in range(sz):
                if g[ry, rx]:
                    draw.rectangle(
                        [x0 + inset + rx * scale, y0 + inset + ry * scale,
                         x0 + inset + (rx + 1) * scale - 1,
                         y0 + inset + (ry + 1) * scale - 1],
                        fill=fg,
                    )
        if sz == 16:
            mid = x0 + 8 * scale
            draw.line([(mid, y0), (mid, y0 + cell_px)], fill=grid_color)
            draw.line([(x0, y0 + 8 * scale),
                       (x0 + cell_px, y0 + 8 * scale)], fill=grid_color)
        draw.rectangle(
            [x0, y0, x0 + cell_px - 1, y0 + cell_px - 1],
            outline=grid_color,
        )

    y = pad
    for c in range(N_CHARS):
        # Section divider above (skip for first)
        if c > 0:
            draw.line([(pad, y - section_gap // 2),
                       (sheet_w - pad, y - section_gap // 2)],
                      fill=sep_color)

        # Char title
        draw.text((pad, y), f"HERO {CHAR_NAMES[c]}",
                  fill=char_color, font=title_font)
        y += char_title_h

        # Source crop preview on the left of FULL row only
        # (3 frames of FULL stacked horizontally as the "source" reference)
        # Actually the source IS the full crop; show it twice would be silly.
        # Use the left column for a label "SRC -> 3 FRAMES ->" instead.

        # 3 mode rows (FULL / HEAD / BODY)
        for mode_label, mode, sz in modes:
            # Mode label on the left
            draw.text((pad, y), mode_label, fill=mode_color, font=mode_font)
            x = pad + src_w
            for f in range(N_FRAMES):
                cell16 = cell(bw, c, f)
                if mode == "full":
                    g = cell16
                elif mode == "head":
                    g = crop(cell16, HEAD_BOX)
                else:
                    g = crop(cell16, BODY_BOX)
                paint_grid(g, x, y + sub_label_h - 8, sz)
                # Frame label centred under cell
                lbl = f"{c + 1}{chr(ord('A') + f)}"
                tw = draw.textlength(lbl, font=font)
                draw.text((x + (cell_px - tw) // 2,
                           y + sub_label_h - 8 + cell_px + 2),
                          lbl, fill=text, font=font)
                x += cell_px + pad
            y += block_h
        y += section_gap

    out_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(out_path)
    print(f"[preview] {out_path}  ({img.width}x{img.height} px)",
          file=sys.stderr)


def emit_asm(bw: np.ndarray, modes: List[str]) -> str:
    """Return ca65-ready .byte directives for the requested crops."""
    out = []
    for mode in modes:
        if mode == "full":
            header = "; ===== ARDUBOY chars 16x16 (full body, head + body) ====="
            sym = "hero{c}{f}_pat"
            tobytes = to_16x16_bytes
            box = None
        elif mode == "head":
            header = "; ===== ARDUBOY chars 8x8 (head crop, centre 8x8) ====="
            sym = "head{c}{f}_pat"
            tobytes = to_8x8_bytes
            box = HEAD_BOX
        else:  # body
            header = "; ===== ARDUBOY chars 8x8 (body crop, centre 8x8) ====="
            sym = "body{c}{f}_pat"
            tobytes = to_8x8_bytes
            box = BODY_BOX
        out.append(header)
        for c in range(N_CHARS):
            for f in range(N_FRAMES):
                cell16 = cell(bw, c, f)
                g = cell16 if box is None else crop(cell16, box)
                bs = tobytes(g)
                lbl = sym.format(c=c + 1, f=chr(ord('A') + f))
                out.append(f"{lbl}:")
                if mode == "full":
                    for off in (0, 8, 16, 24):
                        out.append("        .byte " +
                                   ", ".join(f"${b:02X}"
                                             for b in bs[off:off + 8]))
                else:
                    out.append("        .byte " +
                               ", ".join(f"${b:02X}" for b in bs))
        out.append("")
    return "\n".join(out)


def main(argv):
    p = argparse.ArgumentParser()
    p.add_argument("--src", type=pathlib.Path, default=DEFAULT_SRC)
    p.add_argument("--out", type=pathlib.Path, default=DEFAULT_OUT)
    p.add_argument("--threshold", type=int, default=128,
                   help="Pixel < threshold = ink (default 128).")
    p.add_argument("--scale", type=int, default=8)
    p.add_argument("--crop",
                   choices=["all", "full", "head", "body"],
                   default="all",
                   help="Which crop variant to emit on stdout (preview "
                        "always shows all 3).")
    p.add_argument("--no-stdout", action="store_true")
    args = p.parse_args(argv)

    bw = load_bw(args.src, args.threshold)
    render_preview(bw, args.out, scale=args.scale)
    if not args.no_stdout:
        modes = ["full", "head", "body"] if args.crop == "all" else [args.crop]
        print(emit_asm(bw, modes))


if __name__ == "__main__":
    main(sys.argv[1:])
