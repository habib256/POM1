#!/usr/bin/env python3
"""
Render every 16x16 TMS9918 sprite pattern declared in TMS_Logo_16k.asm
into a single labeled PNG.

Reads the .asm source, finds each sprite label (bird1_pat, bird2_pat,
turtle_n..turtle_nw, boat_n..boat_nw), parses the four `.byte` lines
that follow it (32 bytes total in TL/BL/TR/BR quarter-block order, the
TMS9918 sprite-pattern layout), decodes them to a 16x16 bitmap, and
arranges everything into a grid PNG with names underneath.

This runs **standalone**, with no emulator -- just the .asm file and
PIL. Useful for visually checking new patterns produced by
tools/extract_speedboat_sprites.py before integrating them.

Usage:
  python3 tools/render_logo_sprites.py
  python3 tools/render_logo_sprites.py --src sketchs/tms9918/tool_logo/TMS_Logo_16k.asm
                                        --out screenshots/logo_sprites.png
                                        --scale 8

Default output: screenshots/logo_sprites.png at 8x scale (sprites are
128x128 px each on the sheet).
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys
from typing import Dict, List

from PIL import Image, ImageDraw, ImageFont


REPO = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_ASM = REPO / "sketchs" / "tms9918" / "tool_logo" / "TMS_Logo_16k.asm"
DEFAULT_OUT = REPO / "screenshots" / "logo_sprites.png"

# Render rows: each tuple is (heading-row title, [labels], [display names]).
# Names that match a label exactly in the .asm are pulled from there;
# the display name is what we draw below the cell.
ROWS: List[tuple] = [
    ("Static shapes (16x16)",
     ["bird1_pat", "bird2_pat"],
     ["BIRD1", "BIRD2"]),
    ("Static shapes (8x8, V2.1)",
     ["heart_pat"],
     ["HEART"]),
    ("Expression emotes 1/2 (CC-BY Quale, 16x16)",
     ["serious_pat", "happy_pat", "excited_pat", "sad_pat",
      "hurt_pat", "angry_pat"],
     ["NORMAL", "HAPPY", "SUPER", "SAD", "UPSET", "ANGRY"]),
    ("Expression emotes 2/2 (CC-BY Quale, 16x16)",
     ["upset_pat", "smug_pat", "sick_pat", "sleeping_pat",
      "yarr_pat", "nerd_pat"],
     ["GRUMPY", "PERV", "SICK", "SLEEP", "PIRATE", "SHADES"]),
    ("TURTL  (8 directions, 16x16)",
     ["turtle_n", "turtle_ne", "turtle_e", "turtle_se",
      "turtle_s", "turtle_sw", "turtle_w", "turtle_nw"],
     ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]),
    ("BOAT   (8 directions, 16x16)",
     ["boat_n", "boat_ne", "boat_e", "boat_se",
      "boat_s", "boat_sw", "boat_w", "boat_nw"],
     ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]),
]

BYTE_RE = re.compile(r"\$([0-9A-Fa-f]{2})")
LABEL_RE = re.compile(r"^([a-zA-Z_][a-zA-Z0-9_]*):\s*(?:;.*)?$")


def parse_sprites(asm_path: pathlib.Path) -> Dict[str, List[int]]:
    """Walk the .asm file collecting label -> raw bytes for each sprite
    pattern label. Accepts both 8-byte (8x8) and 32-byte (16x16) blocks:
    a block ends when the next label or non-.byte directive appears, and
    the result is kept if its size is 8 or 32.
    """
    out: Dict[str, List[int]] = {}
    cur_label: str | None = None
    cur_bytes: List[int] = []

    def flush():
        if cur_label and len(cur_bytes) in (8, 32):
            out[cur_label] = list(cur_bytes)

    with asm_path.open() as f:
        for raw in f:
            stripped = raw.lstrip().rstrip("\n")
            if not stripped or stripped.startswith(";"):
                continue
            m = LABEL_RE.match(stripped)
            if m:
                flush()
                cur_label = m.group(1)
                cur_bytes = []
                continue
            if cur_label is None:
                continue
            if ".byte" not in stripped:
                flush()
                cur_label = None
                cur_bytes = []
                continue
            for hx in BYTE_RE.findall(stripped):
                if len(cur_bytes) < 32:
                    cur_bytes.append(int(hx, 16))
        flush()
    return out


def decode_sprite(raw: List[int]) -> List[List[int]]:
    """Decode raw .byte pattern data into a square 0/1 bitmap.
       8 bytes  -> 8x8  bitmap (single 8x8 block).
       32 bytes -> 16x16 bitmap (TL, BL, TR, BR quarter-block layout).
    """
    if len(raw) == 8:
        grid = [[0] * 8 for _ in range(8)]
        for r, byte in enumerate(raw):
            for x in range(8):
                if byte & (1 << (7 - x)):
                    grid[r][x] = 1
        return grid
    assert len(raw) == 32, len(raw)
    grid = [[0] * 16 for _ in range(16)]
    quarters = [
        (raw[0:8],   0, 0),   # TL
        (raw[8:16],  8, 0),   # BL
        (raw[16:24], 0, 8),   # TR
        (raw[24:32], 8, 8),   # BR
    ]
    for octets, y0, x0 in quarters:
        for r, byte in enumerate(octets):
            for x in range(8):
                if byte & (1 << (7 - x)):
                    grid[y0 + r][x0 + x] = 1
    return grid


def render(asm_path: pathlib.Path, out_path: pathlib.Path,
           scale: int = 8) -> None:
    sprites = parse_sprites(asm_path)
    missing = [
        n for _, labels, _ in ROWS for n in labels if n not in sprites
    ]
    if missing:
        sys.exit(f"ERROR: missing sprite labels in {asm_path}: {missing}")

    cell_px = 16 * scale
    pad = 12
    label_h = 14
    title_h = 22
    bg = (28, 28, 36)
    fg = (255, 255, 255)
    grid_color = (52, 52, 64)
    text = (200, 210, 230)
    title_color = (255, 220, 120)

    max_cols = max(len(r[1]) for r in ROWS)
    sheet_w = pad + max_cols * (cell_px + pad)
    sheet_h = pad + sum(title_h + cell_px + label_h + pad
                        for _ in ROWS)

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
    for title, labels, display in ROWS:
        draw.text((pad, y), title, fill=title_color, font=title_font)
        y += title_h
        x = pad
        for label, name in zip(labels, display):
            grid = decode_sprite(sprites[label])
            sz = len(grid)            # 8 or 16 (square)
            # Center the sprite inside a fixed 16x16-equivalent cell so
            # 8x8 and 16x16 share the same column width (8x8 looks half
            # the size, which is how it renders on the real TMS9918 too).
            inset = (16 - sz) * scale // 2
            for ry in range(sz):
                for rx in range(sz):
                    if grid[ry][rx]:
                        draw.rectangle(
                            [x + inset + rx * scale,
                             y + inset + ry * scale,
                             x + inset + (rx + 1) * scale - 1,
                             y + inset + (ry + 1) * scale - 1],
                            fill=fg,
                        )
            # 8x8 quarter divider lines (only meaningful for 16x16 cells
            # since they show the TMS9918 TL/BL/TR/BR layout boundary).
            if sz == 16:
                mid = x + 8 * scale
                draw.line([(mid, y), (mid, y + cell_px)], fill=grid_color)
                draw.line([(x, y + 8 * scale),
                           (x + cell_px, y + 8 * scale)], fill=grid_color)
            # Outer frame
            draw.rectangle(
                [x, y, x + cell_px - 1, y + cell_px - 1],
                outline=grid_color,
            )
            # Name centred underneath
            tw = draw.textlength(name, font=font)
            draw.text((x + (cell_px - tw) // 2, y + cell_px + 2),
                      name, fill=text, font=font)
            x += cell_px + pad
        y += cell_px + label_h + pad

    out_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(out_path)
    print(f"Wrote {out_path}  ({img.width}x{img.height} px, "
          f"{sum(len(r[1]) for r in ROWS)} sprites, scale={scale}x)")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=pathlib.Path, default=DEFAULT_ASM,
                    help=f"Path to TMS_Logo_16k.asm (default: {DEFAULT_ASM})")
    ap.add_argument("--out", type=pathlib.Path, default=DEFAULT_OUT,
                    help=f"Output PNG (default: {DEFAULT_OUT})")
    ap.add_argument("--scale", type=int, default=8,
                    help="Per-pixel zoom factor (default: 8 -> 128px sprites)")
    args = ap.parse_args(argv)
    render(args.src, args.out, scale=args.scale)


if __name__ == "__main__":
    main(sys.argv[1:])
