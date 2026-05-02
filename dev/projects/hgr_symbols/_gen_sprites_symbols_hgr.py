#!/usr/bin/env python3
"""
Convert dev/lib/tms9918/sprites_symbols.asm (TMS9918 16x16 quarter-block
sprite layout) into sprites_symbols_hgr.inc -- 16 rows of 3 HGR bytes per
sprite, ready to STA into the GEN2 framebuffer at $2000-$3FFF.

TMS9918 layout (32 B/sprite):
    bytes  0..7  -> top-left   quarter (rows 0..7,  cols 0..7),
    bytes  8..15 -> bottom-left            (rows 8..15, cols 0..7),
    bytes 16..23 -> top-right              (rows 0..7,  cols 8..15),
    bytes 24..31 -> bottom-right           (rows 8..15, cols 8..15).
    Within each byte, bit 7 is the leftmost pixel.

HGR layout (3 B/row):
    byte 0: cols 0..6 in bits 0..6  (bit 0 = leftmost),
    byte 1: cols 7..13,
    byte 2: cols 14..15 in bits 0..1.
    Bit 7 of every byte stays clear (NTSC group selector, not a pixel).

Output: 23 sprites x 16 rows x 3 bytes = 1 104 B of glyph data.
"""
from __future__ import annotations

import pathlib
import re
import sys
from typing import Dict, List, Tuple

REPO = pathlib.Path(__file__).resolve().parents[3]
SRC = REPO / "dev" / "lib" / "tms9918" / "sprites_symbols.asm"
OUT = pathlib.Path(__file__).resolve().parent / "sprites_symbols_hgr.inc"


def parse_tms_sprites(path: pathlib.Path) -> List[Tuple[str, List[int]]]:
    """Return [(label, 32 bytes), ...] in source order."""
    text = path.read_text()
    sprites: List[Tuple[str, List[int]]] = []
    cur_label: str | None = None
    cur_bytes: List[int] = []
    label_re = re.compile(r"^([a-z_][a-z0-9_]*):\s*$")
    byte_re = re.compile(r"\.byte\s+(.*)")
    for line in text.splitlines():
        s = line.strip()
        m = label_re.match(s)
        if m and m.group(1).startswith("sym_"):
            if cur_label is not None and len(cur_bytes) == 32:
                sprites.append((cur_label, cur_bytes))
            cur_label = m.group(1)
            cur_bytes = []
            continue
        m = byte_re.match(s)
        if m and cur_label is not None and len(cur_bytes) < 32:
            for tok in m.group(1).split(","):
                tok = tok.strip()
                if tok.startswith("$"):
                    cur_bytes.append(int(tok[1:], 16))
    if cur_label is not None and len(cur_bytes) == 32:
        sprites.append((cur_label, cur_bytes))
    return sprites


def tms_to_pixels(sprite_bytes: List[int]) -> List[List[int]]:
    pixels = [[0] * 16 for _ in range(16)]
    quarters = [(0, 0, 0), (8, 0, 8), (0, 8, 16), (8, 8, 24)]
    for r0, c0, byte_off in quarters:
        for r in range(8):
            byte = sprite_bytes[byte_off + r]
            for c in range(8):
                if byte & (1 << (7 - c)):
                    pixels[r0 + r][c0 + c] = 1
    return pixels


def pixels_to_hgr(pixels: List[List[int]]) -> List[Tuple[int, int, int]]:
    rows = []
    for r in range(16):
        b0 = sum((1 << c) for c in range(7) if pixels[r][c])
        b1 = sum((1 << (c - 7)) for c in range(7, 14) if pixels[r][c])
        b2 = sum((1 << (c - 14)) for c in range(14, 16) if pixels[r][c])
        rows.append((b0, b1, b2))
    return rows


def emit_inc(sprites: List[Tuple[str, List[int]]]) -> str:
    out = []
    out.append("; " + "=" * 76)
    out.append("; sprites_symbols_hgr.inc  --  23 sprites_symbols renormalised "
               "for GEN2 HGR")
    out.append("; " + "-" * 76)
    out.append("; Auto-generated from dev/lib/tms9918/sprites_symbols.asm by")
    out.append("; dev/projects/hgr_symbols/_gen_sprites_symbols_hgr.py.")
    out.append(";")
    out.append("; Layout: each sprite = 16 rows x 3 bytes = 48 B.")
    out.append("; HGR encoding: bit 0 = leftmost pixel within byte; bit 7 stays")
    out.append("; clear (NTSC group selector). 16-px sprite occupies cols 0..6")
    out.append("; in byte 0, cols 7..13 in byte 1, cols 14..15 in bits 0..1 of")
    out.append("; byte 2. Render by storing the 3 bytes at (hgr_row+X+0..2)")
    out.append("; for each of the 16 rows.")
    out.append(";")
    out.append("; Source: SCROLL-O-SPRITES \"Symbols\" by Quale, May 2013, "
               "CC-BY-3.0.")
    out.append("; " + "=" * 76)
    out.append("")
    out.append(f"HGR_SYMBOL_BYTES_PER_GLYPH = 48")
    out.append(f"HGR_SYMBOL_GLYPH_COUNT     = {len(sprites)}")
    out.append("")
    out.append("hgr_symbols_data:")
    for idx, (label, sprite_bytes) in enumerate(sprites):
        pixels = tms_to_pixels(sprite_bytes)
        hgr_rows = pixels_to_hgr(pixels)
        out.append(f"        ; --- {idx:02d} {label} ---")
        for r, (b0, b1, b2) in enumerate(hgr_rows):
            out.append(f"        .byte ${b0:02X}, ${b1:02X}, ${b2:02X}  "
                       f"; y{r:02d}")
    out.append("")
    return "\n".join(out)


def main() -> int:
    sprites = parse_tms_sprites(SRC)
    if len(sprites) != 23:
        print(f"[gen] WARN: expected 23 sprites, got {len(sprites)}",
              file=sys.stderr)
    OUT.write_text(emit_inc(sprites))
    n = len(sprites)
    print(f"[gen] {OUT}  ({n} sprites, {n * 48} B)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
