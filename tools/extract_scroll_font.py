#!/usr/bin/env python3
"""
Extract the Quale SCROLL-O-SPRITES font (May 2013, CC-BY-3.0) as 8x8 glyphs
from pic/undefined - Imgur.png. Emits dev/lib/tms9918/font_quale.asm + a
preview PNG under screenshots/.

The original sheet uses variable-height cells: lowercase has descenders
(g, j, p, q, y, ç go below the baseline) and several rows carry accents
above the body. Standard TMS9918 character patterns are 8x8, so we cannot
preserve everything. Tradeoffs taken (documented in the emitted .asm):

  - Digits 0-9                  -> 8x8, full glyph captured.
  - Uppercase A-Z              -> 8x8, full glyph captured.
  - Uppercase accented (~6)    -> 8x8 body only, accent dropped.
  - Uppercase accented row 2   -> SKIPPED (10-row glyphs don't fit 8x8).
  - Lowercase a-z              -> 8x8, body + descenders captured.
  - Lowercase accented (a..a)  -> 8x8 body only, accent dropped (in lower
                                  row 1).
  - Lowercase accented row 2   -> 8x8 with accent + body together (these
                                  glyphs were drawn to fit 8 rows).
  - Punctuation                -> 8x8.
  - Decorative symbols         -> 8x8 (some cells use full 8-row art).

Usage:
    python3 tools/extract_scroll_font.py --write
    python3 tools/extract_scroll_font.py             # dry-run + preview
"""
from __future__ import annotations

import argparse
import pathlib
import sys
from typing import List, Tuple

import numpy as np
from PIL import Image, ImageDraw, ImageFont


REPO = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_SRC = REPO / "pic" / "undefined - Imgur.png"
DEFAULT_OUT = REPO / "dev" / "lib" / "tms9918" / "font_quale.asm"
PREVIEW = REPO / "screenshots" / "scroll_font.png"

CELL_X0 = 32
CELL_W = 8
CELL_H = 8

# (block_label, cell_y, n_chars, ascii_or_label_per_char, group_comment)
# Names are written as ASCII tokens; non-ASCII chars use a short label like
# "A_grave", "a_circumflex" so the assembler stays clean.
BLOCKS: List[dict] = [
    {
        "label": "digits",
        "cy": 948,
        "n": 10,
        "names": list("0123456789"),
        "comment": "ASCII digits 0-9",
    },
    {
        "label": "upper",
        "cy": 964,
        "n": 32,
        "names": list("ABCDEFGHIJKLMNOPQRSTUVWXYZ") + [
            "A_grave", "A_acute", "A_circ", "A_tilde", "AE_or_C", "E_grave",
        ],
        "comment": "Uppercase A-Z + 6 accented (accent dropped: 8x8 limit)",
    },
    # Band 3 (uppercase accented row 2) is intentionally skipped: glyphs span
    # 10 rows and cannot be losslessly fit into 8x8.
    {
        "label": "lower",
        "cy": 998,
        "n": 32,
        "names": list("abcdefghijklmnopqrstuvwxyz") + [
            "a_grave", "a_acute", "a_circ", "a_tilde", "ae_or_c", "e_grave",
        ],
        "comment": ("Lowercase a-z + 6 accented "
                    "(body+descenders preserved; accent dropped)"),
    },
    {
        "label": "lower_acc2",
        "cy": 1012,
        "n": 11,
        # Best-effort: matches Quale's typical layout (e/i/o/u variants).
        "names": [
            "e_acute", "e_circ", "e_diaer", "i_grave", "i_acute", "i_circ",
            "i_diaer", "o_grave", "o_acute", "o_circ", "o_tilde",
        ],
        "comment": "Lowercase accented row 2 (full 8x8 fit: accent + body)",
    },
    {
        "label": "punct",
        "cy": 1027,
        "n": 25,
        # Quale ordering best-guess: # % @ $ . , ! ? : ; ' " ( ) [ ] x / \ + - < = >
        "names": [
            "hash", "percent", "at", "dollar", "period", "comma",
            "excl", "quest", "colon", "semi", "apos", "quote",
            "lparen", "rparen", "lbrack", "rbrack",
            "times", "slash", "bslash",
            "plus", "minus", "lt", "eq", "gt", "tilde",
        ],
        "comment": "Punctuation",
    },
    {
        "label": "decor",
        "cy": 1039,
        "n": 19,
        "names": [f"deco{i + 1:02d}" for i in range(19)],
        "comment": "Decorative symbols (best-effort labels; rename as needed)",
    },
]


def load_bw(src: pathlib.Path) -> np.ndarray:
    img = Image.open(src).convert("RGB")
    arr = np.array(img)
    cream = ((arr[:, :, 0] > 200) &
             (arr[:, :, 1] > 200) &
             (arr[:, :, 2] > 150)).astype(np.uint8)
    return cream


def extract_glyph(bw: np.ndarray, cy: int, idx: int) -> np.ndarray:
    x = CELL_X0 + idx * CELL_W
    return bw[cy:cy + CELL_H, x:x + CELL_W]


def to_bytes_8x8(g: np.ndarray) -> List[int]:
    """8x8 -> 8 bytes (one per row, MSB = column 0)."""
    assert g.shape == (CELL_H, CELL_W), g.shape
    return [
        sum((1 << (7 - x)) for x in range(CELL_W) if g[r, x])
        for r in range(CELL_H)
    ]


def emit_asm(bw: np.ndarray) -> str:
    out = []
    out.append("; " + "=" * 76)
    out.append("; font_quale.asm  --  8x8 glyph table from Quale's "
               "SCROLL-O-SPRITES sheet")
    out.append("; " + "-" * 76)
    out.append("; SCROLL-O-SPRITES font by Quale, May 2013, CC-BY-3.0.")
    out.append("; Lifted from pic/undefined - Imgur.png by "
               "tools/extract_scroll_font.py.")
    out.append(";")
    out.append("; Layout: each glyph = 8 bytes (one byte per row, MSB = column 0).")
    out.append("; Compatible with TMS9918 Mode-2 charmap blits "
               "(see lib/tms9918/text_bitmap.asm) and Apple-1 charmap.rom.")
    out.append(";")
    out.append("; Tradeoffs (see header of extract_scroll_font.py):")
    out.append(";   - Uppercase accented chars: accent dropped (body only).")
    out.append(";   - Lowercase first 26: body + descenders captured.")
    out.append(";   - Lowercase accented row 1: accent dropped.")
    out.append(";   - Lowercase accented row 2: full 8x8 glyph fits.")
    out.append(";   - Uppercase accented row 2 (band 3 of original sheet) is")
    out.append(";     OMITTED -- 10-row glyphs cannot fit 8x8.")
    out.append("; " + "=" * 76)

    all_labels: List[str] = []
    body: List[str] = []
    body.append('.segment "CODE"')
    body.append("")
    for block in BLOCKS:
        n = block["n"]
        cy = block["cy"]
        names = block["names"]
        if len(names) != n:
            raise ValueError(
                f"block {block['label']}: {len(names)} names vs {n} cells")
        body.append(f"; ---- {block['label']}: {block['comment']} ----")
        body.append(f"font_quale_{block['label']}_data:")
        for idx, nm in enumerate(names):
            label = f"font_quale_{block['label']}_{nm}"
            all_labels.append(label)
            cell = extract_glyph(bw, cy, idx)
            bs = to_bytes_8x8(cell)
            body.append(f"{label}:")
            body.append("        .byte " +
                        ", ".join(f"${b:02X}" for b in bs))
        body.append("")
    # Block-level data labels and grand total constant
    block_labels = [f"font_quale_{b['label']}_data" for b in BLOCKS]
    total = sum(b["n"] for b in BLOCKS)

    # Wrap exports at 4 per line
    chunk = 4
    for i in range(0, len(all_labels), chunk):
        out.append(".export " + ", ".join(all_labels[i:i + chunk]))
    out.append(".export " + ", ".join(block_labels))
    out.append(f"font_quale_count = {total}")
    out.append(".export font_quale_count")
    out.append("")
    out.extend(body)
    return "\n".join(out)


def render_preview(bw: np.ndarray, out_path: pathlib.Path,
                   scale: int = 6) -> None:
    cell_px = CELL_W * scale
    pad = 8
    label_h = 14
    title_h = 22
    cols = 16
    bg = (28, 28, 36)
    fg = (255, 255, 255)
    grid = (60, 60, 76)
    text = (200, 210, 230)
    title_color = (255, 220, 120)
    block_color = (160, 200, 160)

    # First compute total rows
    rows_total = 0
    for block in BLOCKS:
        rows_total += (block["n"] + cols - 1) // cols

    sheet_w = pad + cols * (cell_px + pad)
    sheet_h = (pad + title_h
               + len(BLOCKS) * (label_h + 4)
               + rows_total * (cell_px + label_h + pad) + pad)

    img = Image.new("RGB", (sheet_w, sheet_h), bg)
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 9)
        title_font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 14)
        block_font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 11)
    except OSError:
        font = ImageFont.load_default()
        title_font = font
        block_font = font

    draw.text((pad, pad),
              "SCROLL-O-SPRITES Quale font (8x8, CC-BY-3.0)",
              fill=title_color, font=title_font)
    y_cursor = pad + title_h

    for block in BLOCKS:
        draw.text((pad, y_cursor),
                  f"[{block['label']}] {block['comment']} "
                  f"-- {block['n']} glyphs",
                  fill=block_color, font=block_font)
        y_cursor += label_h + 4
        # Now render glyphs in this block
        block_rows = (block["n"] + cols - 1) // cols
        for i in range(block["n"]):
            r = i // cols
            c = i % cols
            x = pad + c * (cell_px + pad)
            cy_pix = y_cursor + r * (cell_px + label_h + pad)
            cell = extract_glyph(bw, block["cy"], i)
            for ry in range(CELL_H):
                for rx in range(CELL_W):
                    if cell[ry, rx]:
                        draw.rectangle(
                            [x + rx * scale, cy_pix + ry * scale,
                             x + (rx + 1) * scale - 1,
                             cy_pix + (ry + 1) * scale - 1],
                            fill=fg,
                        )
            draw.rectangle([x, cy_pix, x + cell_px - 1, cy_pix + cell_px - 1],
                           outline=grid)
            nm = block["names"][i]
            draw.text((x, cy_pix + cell_px + 1),
                      nm[:8], fill=text, font=font)
        y_cursor += block_rows * (cell_px + label_h + pad)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(out_path)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--src", type=pathlib.Path, default=DEFAULT_SRC)
    p.add_argument("--out", type=pathlib.Path, default=DEFAULT_OUT)
    p.add_argument("--preview", type=pathlib.Path, default=PREVIEW)
    p.add_argument("--write", action="store_true",
                   help="write font_quale.asm (default: stdout dry-run)")
    p.add_argument("--no-preview", action="store_true")
    args = p.parse_args(argv)

    bw = load_bw(args.src)
    asm = emit_asm(bw)
    if args.write:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(asm)
        n = sum(b["n"] for b in BLOCKS)
        print(f"[write] {args.out}  ({n} glyphs, {n * 8} B)", file=sys.stderr)
    else:
        print(asm)
    if not args.no_preview:
        render_preview(bw, args.preview)
        print(f"[preview] {args.preview}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
