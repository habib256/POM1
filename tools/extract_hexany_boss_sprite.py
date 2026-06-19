#!/usr/bin/env python3
"""extract_hexany_boss_sprite.py -- 32x32 1-bit PNG -> TMS9918 sprite block.

Reads a 32x32 1-bit-color-mapped PNG (Hexany's Monster Menagerie creature)
and emits 128 bytes of TMS9918 sprite-pattern data covering 4 consecutive
16x16 sprites (slots N, N+4, N+8, N+12) that tile into a 32x32 boss image
on screen. Layout follows the Quale convention used by sprites_creatures.asm:

  per 16x16 sprite (32 B):
    bytes 0..7   = TL 8x8 (cols 0..7,  rows 0..7)
    bytes 8..15  = BL 8x8 (cols 0..7,  rows 8..15)
    bytes 16..23 = TR 8x8 (cols 8..15, rows 0..7)
    bytes 24..31 = BR 8x8 (cols 8..15, rows 8..15)

  Within each 8x8 chunk: byte i = row i, bit 7 = leftmost pixel.

The 4 emitted 16x16 blocks correspond to the 4 quadrants of the source
32x32 image (top-left / top-right / bottom-left / bottom-right), so
place_all_sprites can paint them at (anchor*16) / (anchor*16+16) for
both the X and Y axes to compose the full 32x32 boss visual.

Usage:
  python3 tools/extract_hexany_boss_sprite.py \\
      "/path/to/creature_024.png" \\
      dev/projects/tms9918/game_rogue/sprites_boss.asm
"""
from __future__ import annotations

import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("ERROR: Pillow not installed. Run: pip install Pillow")

SOURCE_DEFAULT = (
    "/home/gistarcade/Téléchargements/hexanys_monster_menagerie_0.3.0/"
    "Hexany's Monster Menagerie/Tiles/Regular/creature_024.png"
)


def to_bitmap(img: Image.Image) -> list[list[int]]:
    """Convert PIL image to a 2D list of 0/1 (1 = drawn pixel)."""
    if img.size != (32, 32):
        sys.exit(f"ERROR: expected 32x32 source, got {img.size}")
    rgba = img.convert("RGBA")
    bits = [[0] * 32 for _ in range(32)]
    for y in range(32):
        for x in range(32):
            r, g, b, a = rgba.getpixel((x, y))
            # Drawn pixel: opaque AND dark. Hexany's tiles are black on
            # transparent, so treat any non-transparent pixel with low
            # luminance as "lit".
            if a > 127 and (r + g + b) < 128 * 3:
                bits[y][x] = 1
    return bits


def encode_8x8(bits: list[list[int]], x0: int, y0: int) -> list[int]:
    """8x8 block at (x0, y0) → 8 bytes (one byte per row, bit 7 = leftmost)."""
    out = []
    for dy in range(8):
        row_byte = 0
        for dx in range(8):
            if bits[y0 + dy][x0 + dx]:
                row_byte |= 1 << (7 - dx)
        out.append(row_byte)
    return out


def encode_16x16(bits: list[list[int]], x0: int, y0: int) -> list[int]:
    """16x16 block at (x0, y0) → 32 bytes in TL/BL/TR/BR order."""
    return (
        encode_8x8(bits, x0,     y0)        # TL
        + encode_8x8(bits, x0,     y0 + 8)  # BL
        + encode_8x8(bits, x0 + 8, y0)      # TR
        + encode_8x8(bits, x0 + 8, y0 + 8)  # BR
    )


def fmt_bytes(label: str, raw: list[int]) -> str:
    """4 lines × 8 bytes = 32 bytes per 16x16 sprite."""
    lines = [f"; {label}"]
    for i in range(0, 32, 8):
        chunk = raw[i:i + 8]
        lines.append("        .byte " + ", ".join(f"${b:02X}" for b in chunk))
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    src = Path(argv[1]) if len(argv) > 1 else Path(SOURCE_DEFAULT)
    dst = Path(argv[2]) if len(argv) > 2 else Path(
        "dev/projects/tms9918/game_rogue/sprites_boss.asm"
    )

    if not src.is_file():
        sys.exit(f"ERROR: source PNG not found: {src}")
    print(f"[boss] reading {src}")
    bits = to_bitmap(Image.open(src))

    quads = [
        ("Q_TL — top-left  16x16 of the 32x32 boss",  0,  0),
        ("Q_TR — top-right 16x16 of the 32x32 boss", 16,  0),
        ("Q_BL — bottom-left  16x16 of the 32x32 boss", 0, 16),
        ("Q_BR — bottom-right 16x16 of the 32x32 boss", 16, 16),
    ]

    blocks = []
    for label, x0, y0 in quads:
        raw = encode_16x16(bits, x0, y0)
        blocks.append((label, raw))

    header = (
        "; ============================================================================\n"
        "; sprites_boss.asm  --  4 x 16x16 sprite slots tiled into a 32x32 boss\n"
        "; ----------------------------------------------------------------------------\n"
        "; Auto-generated from Hexany's Monster Menagerie creature_024.png (CC0).\n"
        "; Source: https://hexany-ives.itch.io/hexanys-monster-menagerie\n"
        ";\n"
        "; Each 16x16 block is one TMS9918 sprite slot (32 bytes, TL/BL/TR/BR\n"
        "; in 8x8 chunks). place_all_sprites in TMS_Rogue.asm paints them as\n"
        "; a 2x2 tile around the boss anchor (col*16 .. col*16+31 px).\n"
        ";\n"
        "; DO NOT EDIT BY HAND. Re-run tools/extract_hexany_boss_sprite.py\n"
        "; to regenerate from a different source PNG.\n"
        "; ============================================================================\n"
        "\n"
        ".export boss_sprite_pats\n"
        "\n"
        ".segment \"CODE\"\n"
        "\n"
        "boss_sprite_pats:\n"
    )

    out_text = header + "\n".join(fmt_bytes(label, raw) for label, raw in blocks) + "\n"

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(out_text)
    print(f"[boss] wrote {dst}  ({sum(len(raw) for _, raw in blocks)} bytes of sprite data)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
