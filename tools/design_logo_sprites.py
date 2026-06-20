#!/usr/bin/env python3
"""
Hand-designed clean 16x16 monochrome sprites for the LOGO directional
shapes (TURTL and BOAT, 8 octants each).

Strategy: design ONE cardinal-east master + ONE diagonal-NE master per
family, then rotate mechanically to produce N / S / W and SE / SW / NW.
This guarantees visual consistency across the 8 octants -- the failure
mode of the previous batch (every turtle looking identical because the
hand-tweaked diagonals quietly drifted into hex-blob soup).

Outputs:
  1. screenshots/logo_sprites_proposed.png   -- preview grid
  2. stdout: ca65 .byte directives suitable for pasting over the
     boat_* / turtle_* blocks in TMS_Logo_16k.asm
  3. with --patch: rewrites the .asm in place, replacing the existing
     turtle_* and boat_* `.byte` blocks.

Usage:
  python3 tools/design_logo_sprites.py                 # preview + stdout hex
  python3 tools/design_logo_sprites.py --patch         # rewrite .asm in place
  python3 tools/design_logo_sprites.py --no-stdout     # just the PNG
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys
from typing import Dict, List

from PIL import Image, ImageDraw, ImageFont


REPO = pathlib.Path(__file__).resolve().parents[1]
ASM = REPO / "sketchs" / "tms9918" / "tool_logo" / "TMS_Logo_16k.asm"
PREVIEW = REPO / "screenshots" / "logo_sprites_proposed.png"


# =====================================================================
# Master patterns. Edit these to retune the look.
# '#' = pixel on, '.' = pixel off. Each must be 16 rows x 16 cols.
# =====================================================================

# East-facing turtle. Compact shell (12x10) with 4 distinct corner legs
# (2x2 each, sticking out top and bottom) and a clearly-protruding head
# (4 px wide x 6 px tall lozenge poking past the east edge of the shell).
# The head bulge is what makes the orientation unambiguous at 16x16 --
# the previous design had only 2 px of head, easily lost in the rest of
# the silhouette.
TURTLE_E = """\
................
.##........##...
.##........##...
...########.....
..##########....
.##########.##..
###########.###.
###########.####
###########.####
###########.###.
.##########.##..
..##########....
...########.....
.##........##...
.##........##...
................
"""

# Northeast-facing turtle: tilted teardrop with the head tip at NE,
# wide end at SW. Aspect ratio gives an unmistakable NE silhouette;
# legs are sacrificed for clarity at this size (the rotation gives
# them back at the cardinal directions).
TURTLE_NE = """\
................
............##..
...........####.
..........#####.
.........######.
........#######.
.......########.
......#########.
.....##########.
....##########..
....##########..
....#########...
....########....
.....######.....
......####......
................
"""

# East-facing speedboat: long hull (14x9) with sharp bow at col 15,
# square stern at col 1, and a visible cabin slot mid-hull (col 8) that
# tells the viewer "this is a boat, not a teardrop arrow".
BOAT_E = """\
................
................
................
................
.##########.....
.##############.
.##############.
.#######.######.
.#######.#######
.#######.######.
.##############.
.##############.
.##########.....
................
................
................
"""

# Northeast-facing speedboat: hull tilted 45 deg, sharp bow at the
# top-right corner, cabin slot indicated by a 1-px diagonal break
# mid-hull.
BOAT_NE = """\
................
.............#..
............###.
...........####.
..........#####.
.........######.
........#######.
.......#######..
......######....
.....#####......
....#####.......
...####.........
..####..........
.####...........
.###............
................
"""

MASTERS = {
    "turtle": (TURTLE_E, TURTLE_NE),
    "boat":   (BOAT_E,   BOAT_NE),
}


def parse_grid(s: str) -> List[List[int]]:
    rows = [r for r in s.split("\n") if r]
    assert len(rows) == 16, (len(rows), s)
    grid = []
    for r in rows:
        assert len(r) == 16, (len(r), r)
        grid.append([1 if c == "#" else 0 for c in r])
    return grid


def rot90_ccw(g: List[List[int]]) -> List[List[int]]:
    """East -> North: pixel at (y, x) goes to (15-x, y)."""
    out = [[0] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            out[15 - x][y] = g[y][x]
    return out


def rot90_cw(g: List[List[int]]) -> List[List[int]]:
    """East -> South: pixel at (y, x) goes to (x, 15-y)."""
    out = [[0] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            out[x][15 - y] = g[y][x]
    return out


def rot180(g: List[List[int]]) -> List[List[int]]:
    """East -> West: pixel at (y, x) goes to (15-y, 15-x)."""
    out = [[0] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            out[15 - y][15 - x] = g[y][x]
    return out


def derive_8(card_e: List[List[int]],
             diag_ne: List[List[int]]) -> Dict[str, List[List[int]]]:
    """Build the 8 octants from the East and Northeast masters."""
    return {
        "n":  rot90_ccw(card_e),  # 90 CCW: east -> north
        "ne": diag_ne,
        "e":  card_e,
        "se": rot90_cw(diag_ne),  # 90 CW: NE -> SE
        "s":  rot90_cw(card_e),
        "sw": rot180(diag_ne),
        "w":  rot180(card_e),
        "nw": rot90_ccw(diag_ne),
    }


def to_tms9918_bytes(g: List[List[int]]) -> List[int]:
    """16x16 -> 32 bytes in TL, BL, TR, BR quarter-block order."""
    out = []
    for y0, x0 in ((0, 0), (8, 0), (0, 8), (8, 8)):
        for r in range(8):
            byte = 0
            for c in range(8):
                if g[y0 + r][x0 + c]:
                    byte |= 1 << (7 - c)
            out.append(byte)
    return out


# =====================================================================
# Rendering
# =====================================================================

ROW_LAYOUT = [
    ("TURTL  (8 directions)", "turtle",
     ["n", "ne", "e", "se", "s", "sw", "w", "nw"]),
    ("BOAT   (8 directions)", "boat",
     ["n", "ne", "e", "se", "s", "sw", "w", "nw"]),
]
DISPLAY = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]


def render(all_grids: Dict[str, Dict[str, List[List[int]]]],
           out_path: pathlib.Path, scale: int = 8) -> None:
    cell_px = 16 * scale
    pad = 12
    label_h = 14
    title_h = 22
    bg = (28, 28, 36)
    fg = (255, 255, 255)
    grid_color = (52, 52, 64)
    text = (200, 210, 230)
    title_color = (120, 220, 200)

    sheet_w = pad + 8 * (cell_px + pad)
    sheet_h = pad + len(ROW_LAYOUT) * (title_h + cell_px + label_h + pad)

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

    y = pad
    for title, family, dirs in ROW_LAYOUT:
        draw.text((pad, y), title, fill=title_color, font=title_font)
        y += title_h
        x = pad
        for d, name in zip(dirs, DISPLAY):
            grid = all_grids[family][d]
            for ry in range(16):
                for rx in range(16):
                    if grid[ry][rx]:
                        draw.rectangle(
                            [x + rx * scale, y + ry * scale,
                             x + (rx + 1) * scale - 1,
                             y + (ry + 1) * scale - 1],
                            fill=fg,
                        )
            mid = x + 8 * scale
            draw.line([(mid, y), (mid, y + cell_px)], fill=grid_color)
            draw.line([(x, y + 8 * scale),
                       (x + cell_px, y + 8 * scale)], fill=grid_color)
            draw.rectangle(
                [x, y, x + cell_px - 1, y + cell_px - 1],
                outline=grid_color,
            )
            tw = draw.textlength(name, font=font)
            draw.text((x + (cell_px - tw) // 2, y + cell_px + 2),
                      name, fill=text, font=font)
            x += cell_px + pad
        y += cell_px + label_h + pad

    out_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(out_path)
    print(f"[preview] {out_path}  ({img.width}x{img.height} px)",
          file=sys.stderr)


# =====================================================================
# .asm patching
# =====================================================================

def emit_block(family: str, dirs: Dict[str, List[List[int]]]) -> str:
    """Return the .asm-ready block for one family (`turtle` or `boat`)."""
    lines = []
    for d in ["n", "ne", "e", "se", "s", "sw", "w", "nw"]:
        bs = to_tms9918_bytes(dirs[d])
        lines.append(f"; {family}_{d}")
        lines.append(f"{family}_{d}:")
        for off in (0, 8, 16, 24):
            lines.append("        .byte " +
                         ", ".join(f"${b:02X}" for b in bs[off:off + 8]))
    return "\n".join(lines) + "\n"


PATCH_RE_TEMPLATE = (
    r"(^{prefix}_n:\n(?:        \.byte [^\n]+\n){{4}}"
    r"(?:; ?{prefix}_(?:ne|e|se|s|sw|w|nw)[^\n]*\n)?"
    r"(?:{prefix}_(?:ne|e|se|s|sw|w|nw):\n(?:        \.byte [^\n]+\n){{4}})+)"
)


def replace_block(asm_text: str, family: str,
                  dirs: Dict[str, List[List[int]]]) -> str:
    """Replace the contiguous turtle_* or boat_* `.byte` block (8 sprites)
    in `asm_text` with the freshly-rendered one. Preserves the surrounding
    label structure and comments before each sprite.
    """
    # Match the block starting at <family>_n: through the end of <family>_nw.
    # Conservative: each label has exactly 4 .byte lines.
    pattern = re.compile(
        r"^" + re.escape(family) + r"_n:\n"
        r"(?:        \.byte [^\n]+\n){4}"
        r"(?:[^\n]*\n)*?"
        r"" + re.escape(family) + r"_nw:\n"
        r"(?:        \.byte [^\n]+\n){4}",
        re.MULTILINE,
    )
    new_block = emit_block(family, dirs).rstrip() + "\n"
    new_text, n = pattern.subn(new_block, asm_text)
    if n != 1:
        raise RuntimeError(
            f"Couldn't locate {family}_n..{family}_nw block in asm "
            f"(matched {n} times)")
    return new_text


# =====================================================================
# Main
# =====================================================================

def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--asm", type=pathlib.Path, default=ASM)
    ap.add_argument("--out", type=pathlib.Path, default=PREVIEW)
    ap.add_argument("--scale", type=int, default=8)
    ap.add_argument("--patch", action="store_true",
                    help="rewrite the .asm file in place")
    ap.add_argument("--no-stdout", action="store_true",
                    help="suppress the .byte dump on stdout")
    args = ap.parse_args(argv)

    grids = {}
    for family, (m_card, m_diag) in MASTERS.items():
        grids[family] = derive_8(parse_grid(m_card), parse_grid(m_diag))

    render(grids, args.out, scale=args.scale)

    if not args.no_stdout:
        for family in ("turtle", "boat"):
            print(f"\n; ===== {family.upper()} =====")
            print(emit_block(family, grids[family]))

    if args.patch:
        text = args.asm.read_text()
        for family in ("turtle", "boat"):
            text = replace_block(text, family, grids[family])
        args.asm.write_text(text)
        print(f"[patch] rewrote {args.asm}", file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1:])
