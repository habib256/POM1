#!/usr/bin/env python3
"""Render an annotated grid overlay on a region of the Arduboy sheet.

Each 16x16 cell is labelled with its source-pixel origin (col,row) so
you can point at specific cells when discussing what's where. 8-pixel
grid lines (blue) and 16-pixel grid lines (red) overlap so any sprite
boundary is unambiguous.

Default region: the walking-sprites zone around cols 215-310, rows 0-110.
Pass --region X0,Y0,X1,Y1 to scan another part of the sheet.

Usage:
  python3 tools/annotate_arduboy_grid.py
  python3 tools/annotate_arduboy_grid.py --region 0,0,408,200
  python3 tools/annotate_arduboy_grid.py --region 200,0,310,110 \
      --out screenshots/arduboy_walking_grid.png
"""
from __future__ import annotations

import argparse
import pathlib

from PIL import Image, ImageDraw, ImageFont


REPO = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_SRC = REPO / "pic" / "arduboy_graphics_v1.png"
DEFAULT_OUT = REPO / "screenshots" / "arduboy_walking_grid.png"
DEFAULT_REGION = (215, 0, 310, 110)


def render(src: pathlib.Path, out: pathlib.Path,
           region: tuple, scale: int = 8) -> None:
    img = Image.open(src).convert("RGB")
    x0, y0, x1, y1 = region
    crop = img.crop((x0, y0, x1, y1))
    W, H = crop.size

    big = crop.resize((W * scale, H * scale), Image.NEAREST)
    draw = ImageDraw.Draw(big)
    try:
        font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 13)
    except OSError:
        font = ImageFont.load_default()

    GRID_8 = (90, 110, 160)
    GRID_16 = (220, 60, 60)
    LBL = (255, 255, 80)

    for x in range(0, W + 1, 8):
        color = GRID_16 if x % 16 == 0 else GRID_8
        width = 2 if x % 16 == 0 else 1
        draw.line([(x * scale, 0), (x * scale, H * scale)],
                  fill=color, width=width)
    for y in range(0, H + 1, 8):
        color = GRID_16 if y % 16 == 0 else GRID_8
        width = 2 if y % 16 == 0 else 1
        draw.line([(0, y * scale), (W * scale, y * scale)],
                  fill=color, width=width)

    for ry in range(0, H, 16):
        for rx in range(0, W, 16):
            lbl = f"{x0 + rx},{y0 + ry}"
            draw.text((rx * scale + 4, ry * scale + 2),
                      lbl, fill=LBL, font=font)

    out.parent.mkdir(parents=True, exist_ok=True)
    big.save(out)
    print(f"Wrote {out} ({big.size[0]}x{big.size[1]} px, "
          f"region={region}, scale={scale}x)")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--src", type=pathlib.Path, default=DEFAULT_SRC)
    p.add_argument("--out", type=pathlib.Path, default=DEFAULT_OUT)
    p.add_argument("--region", type=str,
                   default=",".join(str(v) for v in DEFAULT_REGION),
                   help="X0,Y0,X1,Y1 (default: walking-sprites zone)")
    p.add_argument("--scale", type=int, default=8)
    args = p.parse_args()
    region = tuple(int(v) for v in args.region.split(","))
    render(args.src, args.out, region, args.scale)


if __name__ == "__main__":
    main()
