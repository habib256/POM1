#!/usr/bin/env python3
"""
Extract 8 directional 16x16 monochrome sprites from a SpriteDatabase
"SpeedboatRip" sheet (or any sheet that lays out 16 directional poses
as a 4x4 grid per color block) and emit them as ca65 .byte directives
in TMS9918 sprite-pattern order (TL, BL, TR, BR -- 32 bytes per sprite).

The shipped boat_*.byte tables in
  sketchs/tms9918/tool_logo/TMS_Logo_16k.asm
were produced by:
  python3 tools/extract_speedboat_sprites.py \\
      --src ~/Téléchargements/SpeedboatRip.png \\
      --color white --silhouette --size 16

Usage:
  python3 tools/extract_speedboat_sprites.py --src PATH [--color NAME]
                                            [--silhouette] [--size N]

Color blocks in the white-background SpeedboatRip sheet (411x260):
  green   (  0,  0,  94, 103)
  yellow  (108,  0, 202, 103)
  white   (219,  0, 313, 103)   <-- cleanest contrast
  red     (  0,112,  94, 215)
  cyan    (108,112, 202, 215)

The grid inside each block is 4x4 = 16 sprites at ~22.5 deg per cell.
Indices 0,2,4,6,8,10,12,14 map to N,NE,E,SE,S,SW,W,NW.
"""
import argparse
import sys

import numpy as np
from PIL import Image
try:
    from scipy.ndimage import binary_fill_holes
except ImportError:
    print("scipy required: pip install scipy", file=sys.stderr); sys.exit(1)


BLOCKS = {
    "green":  (  0,   0,  94, 103),
    "yellow": (108,   0, 202, 103),
    "white":  (219,   0, 313, 103),
    "red":    (  0, 112,  94, 215),
    "cyan":   (108, 112, 202, 215),
}
DIR_NAMES = ["n", "ne", "e", "se", "s", "sw", "w", "nw"]
GRID_INDICES = [0, 2, 4, 6, 8, 10, 12, 14]   # every other of 16


def cell_box(idx, x0, y0, x1, y1, rows=4, cols=4):
    r, c = divmod(idx, cols)
    h = y1 - y0
    w = x1 - x0
    return (x0 + w*c//cols, y0 + h*r//rows,
            x0 + w*(c+1)//cols, y0 + h*(r+1)//rows)


def to_NxN_mono(crop_rgb, N=16, threshold=96, silhouette=True):
    a = np.asarray(crop_rgb).astype(np.int32)
    fg = ~((a[:, :, 0] == 0) & (a[:, :, 1] == 0) & (a[:, :, 2] == 255))
    if silhouette:
        fg = binary_fill_holes(fg)
    ys, xs = np.where(fg)
    if len(ys) == 0:
        return np.zeros((N, N), dtype=np.uint8)
    y0, y1 = ys.min(), ys.max() + 1
    x0, x1 = xs.min(), xs.max() + 1
    fg = fg[y0:y1, x0:x1].astype(np.uint8)
    src_h, src_w = fg.shape
    side = max(src_h, src_w)
    sq = Image.new("L", (side, side), 0)
    sq.paste(Image.fromarray(fg * 255, "L"), ((side - src_w) // 2,
                                              (side - src_h) // 2))
    small = sq.resize((N, N), Image.LANCZOS)
    return (np.asarray(small) >= threshold).astype(np.uint8)


def tms9918_bytes(bits16):
    """16x16 mono -> 32 bytes in TL, BL, TR, BR quarter-block order."""
    tl = bits16[0:8, 0:8]; bl = bits16[8:16, 0:8]
    tr = bits16[0:8, 8:16]; br = bits16[8:16, 8:16]
    out = []
    for q in (tl, bl, tr, br):
        for row in q:
            byte = 0
            for x in range(8):
                if row[x]:
                    byte |= (1 << (7 - x))
            out.append(byte)
    return out


def main(argv):
    p = argparse.ArgumentParser()
    p.add_argument("--src", required=True, help="Path to SpeedboatRip.png")
    p.add_argument("--color", default="white", choices=BLOCKS.keys())
    p.add_argument("--size", type=int, default=16,
                   help="Output sprite side (16=usable on TMS9918, "
                        "8=produces unreadable blobs).")
    p.add_argument("--threshold", type=int, default=96)
    p.add_argument("--silhouette", action="store_true",
                   help="Fill interior holes -- much more legible at small "
                        "size than preserving the white outline pattern.")
    p.add_argument("--prefix", default="boat",
                   help="Symbol prefix in the emitted .byte tables.")
    args = p.parse_args(argv)

    img = Image.open(args.src).convert("RGB")
    block = BLOCKS[args.color]

    print(f"; ----- {args.size}x{args.size} {args.prefix.upper()} sprites "
          f"(8 directional, TMS9918 monochrome) -----")
    print(f"; Source: {args.src} {args.color} block, "
          f"{'filled silhouette' if args.silhouette else 'outline preserved'}")
    print(f"; Lanczos -> {args.size}x{args.size}, threshold={args.threshold}")
    print(";")

    if args.size == 16:
        for name, idx in zip(DIR_NAMES, GRID_INDICES):
            bx = cell_box(idx, *block)
            bits = to_NxN_mono(img.crop(bx), N=16,
                               threshold=args.threshold,
                               silhouette=args.silhouette)
            bs = tms9918_bytes(bits)
            print(f"{args.prefix}_{name}:")
            for off in (0, 8, 16, 24):
                print("        .byte " +
                      ", ".join(f"${b:02X}" for b in bs[off:off + 8]))
    else:
        # Generic NxN: emit row-major bit-packed bytes.
        for name, idx in zip(DIR_NAMES, GRID_INDICES):
            bx = cell_box(idx, *block)
            bits = to_NxN_mono(img.crop(bx), N=args.size,
                               threshold=args.threshold,
                               silhouette=args.silhouette)
            print(f"{args.prefix}_{name}:")
            for r in range(args.size):
                row_bytes = []
                for col in range(0, args.size, 8):
                    byte = 0
                    for x in range(8):
                        if col + x < args.size and bits[r, col + x]:
                            byte |= (1 << (7 - x))
                    row_bytes.append(byte)
                print("        .byte " +
                      ", ".join(f"${b:02X}" for b in row_bytes))


if __name__ == "__main__":
    main(sys.argv[1:])
