#!/usr/bin/env python3
"""
Mirror dev/lib/tms9918/sprites_*.asm into dev/lib/hgr/sprites/sprites_*_hgr.asm
so the same SCROLL-O-SPRITES bank can be linked into either a TMS9918 or a
GEN2 HGR project with identical ergonomics.

Convention. Both libraries export the same per-sprite label set (e.g.
`fauna_dog_pat`, `sym_at_pat`). The PAYLOAD differs because the two video
chips speak completely different bitmap formats:

    TMS9918 sprite mode (32 B/sprite):
        bytes  0..7  -> top-left   quarter (rows 0..7,  cols 0..7),
        bytes  8..15 -> bottom-left            (rows 8..15, cols 0..7),
        bytes 16..23 -> top-right              (rows 0..7,  cols 8..15),
        bytes 24..31 -> bottom-right           (rows 8..15, cols 8..15).
        Within each byte, bit 7 is the LEFTMOST pixel.

    GEN2 HGR (48 B/sprite, 16 rows x 3 bytes per row):
        byte 0: cols 0..6 in bits 0..6  (bit 0 = leftmost),
        byte 1: cols 7..13,
        byte 2: cols 14..15 in bits 0..1.
        Bit 7 of every byte stays clear (NTSC group selector, not a pixel).

A project mixes the two libraries at its peril (link-time duplicate symbol
collision) — pick one variant per build. If you ever need both, alias the
labels in your project's local copy.

Sources scanned: every dev/lib/tms9918/sprites_*.asm. Each becomes
dev/lib/hgr/sprites/<basename>_hgr.asm with matching labels and a trailing
collective `<category>_hgr_data` base label for index-based loops.

Usage:
    python3 tools/build_hgr_sprites.py            # rebuild all 14 categories
    python3 tools/build_hgr_sprites.py --only fauna
    python3 tools/build_hgr_sprites.py --check    # diff against existing files
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys
from typing import List, Tuple

REPO = pathlib.Path(__file__).resolve().parents[1]
TMS_DIR = REPO / "dev" / "lib" / "tms9918"
HGR_DIR = REPO / "dev" / "lib" / "hgr" / "sprites"

LABEL_RE = re.compile(r"^([a-z_][a-z0-9_]*):\s*$")
BYTE_RE = re.compile(r"\.byte\s+(.*)")
COMMENT_RE = re.compile(r"^;\s*slot\s+(\d+)/\d+\s+of\s+\"([^\"]+)\"\s+row\s+--\s+(.+)$")


def parse_tms_file(path: pathlib.Path):
    """Return (title, [(label, [32 bytes], slot_comment), ...])."""
    text = path.read_text()
    title_m = re.search(r'\bSCROLL-O-SPRITES\s+"([^"]+)"', text)
    title = title_m.group(1) if title_m else path.stem.replace("sprites_", "")

    sprites: List[Tuple[str, List[int], str]] = []
    cur_label: str | None = None
    cur_bytes: List[int] = []
    cur_slot_comment: str = ""
    pending_slot_comment: str = ""

    for line in text.splitlines():
        stripped = line.strip()
        m = COMMENT_RE.match(stripped)
        if m:
            pending_slot_comment = stripped[2:]  # drop the "; "
            continue
        m = LABEL_RE.match(stripped)
        if m and (m.group(1).endswith("_pat") or m.group(1).endswith("_data")):
            if cur_label is not None and len(cur_bytes) == 32:
                sprites.append((cur_label, cur_bytes, cur_slot_comment))
            cur_label = m.group(1)
            cur_bytes = []
            cur_slot_comment = pending_slot_comment
            pending_slot_comment = ""
            continue
        bm = BYTE_RE.match(stripped)
        if bm and cur_label is not None and len(cur_bytes) < 32:
            for tok in bm.group(1).split(","):
                tok = tok.strip()
                if tok.startswith("$"):
                    cur_bytes.append(int(tok[1:], 16))
    if cur_label is not None and len(cur_bytes) == 32:
        sprites.append((cur_label, cur_bytes, cur_slot_comment))
    return title, sprites


def tms_to_pixels(sprite_bytes: List[int]) -> List[List[int]]:
    """32-byte TMS quarter-block sprite -> 16x16 pixel matrix."""
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
    """16x16 pixel matrix -> 16 rows of 3 HGR bytes (bit 0 = leftmost,
    bit 7 stays clear for the NTSC group selector)."""
    rows = []
    for r in range(16):
        b0 = sum((1 << c) for c in range(7) if pixels[r][c])
        b1 = sum((1 << (c - 7)) for c in range(7, 14) if pixels[r][c])
        b2 = sum((1 << (c - 14)) for c in range(14, 16) if pixels[r][c])
        rows.append((b0, b1, b2))
    return rows


def emit_hgr_asm(category: str, title: str,
                 sprites: List[Tuple[str, List[int], str]]) -> str:
    out: List[str] = []
    n = len(sprites)
    out.append("; " + "=" * 76)
    out.append(f"; sprites_{category}_hgr.asm  --  {n} sprites in GEN2 HGR format")
    out.append("; " + "-" * 76)
    out.append(f"; SCROLL-O-SPRITES \"{title}\" by Quale, May 2013, CC-BY-3.0.")
    out.append("; Auto-generated by tools/build_hgr_sprites.py from")
    out.append(f"; dev/lib/tms9918/sprites_{category}.asm. Do not edit by hand —")
    out.append("; rerun the tool to refresh after changing the TMS source.")
    out.append(";")
    out.append("; Layout: each sprite = 16 rows x 3 bytes = 48 B.")
    out.append("; HGR encoding: bit 0 = leftmost pixel within byte; bit 7 stays")
    out.append("; clear (NTSC group selector). 16-px sprite occupies cols 0..6")
    out.append("; in byte 0, cols 7..13 in byte 1, cols 14..15 in bits 0..1 of")
    out.append("; byte 2.")
    out.append(";")
    out.append("; Same per-sprite labels as the TMS variant — pick ONE library")
    out.append("; per build (cc65 errors out on duplicate symbols if both are")
    out.append("; linked). The format is implicit in which .o the project")
    out.append("; pulls in.")
    out.append("; " + "=" * 76)

    labels = [label for (label, _bs, _c) in sprites]
    chunk = 5
    for i in range(0, len(labels), chunk):
        out.append(".export " + ", ".join(labels[i:i + chunk]))
    out.append(f".export {category}_hgr_data")
    out.append("")
    out.append(f"; Constants like {category.upper()}_HGR_COUNT live in the sister")
    out.append(f"; sprites_{category}_hgr.inc — projects `.include` it to get the")
    out.append("; sprite count for loop bounds, `CMP #N` immediate forms, etc.")
    out.append("; (cc65 cannot resolve `.import`ed symbols in immediate mode at")
    out.append("; assembly time — the .inc keeps that path clean.)")
    out.append("")
    out.append('.segment "CODE"')
    out.append("")
    out.append(f"{category}_hgr_data:")
    for label, sprite_bytes, slot_comment in sprites:
        pixels = tms_to_pixels(sprite_bytes)
        hgr_rows = pixels_to_hgr(pixels)
        if slot_comment:
            out.append(f"; {slot_comment}")
        out.append(f"{label}:")
        for r0 in (0, 4, 8, 12):
            line = "        .byte "
            parts = []
            for r in range(r0, r0 + 4):
                b0, b1, b2 = hgr_rows[r]
                parts.append(f"${b0:02X}, ${b1:02X}, ${b2:02X}")
            out.append(line + ", ".join(parts) +
                       f"  ; rows {r0:02d}..{r0 + 3:02d}")
    out.append("")
    return "\n".join(out)


def emit_hgr_inc(category: str, title: str, n: int) -> str:
    cap = category.upper()
    return (
        f"; ============================================================================\n"
        f"; sprites_{category}_hgr.inc  --  constants for the {cap} HGR sprite library\n"
        f"; ----------------------------------------------------------------------------\n"
        f"; Auto-generated by tools/build_hgr_sprites.py. Project rule: `.include`\n"
        f"; this header to get the count constants in immediate-mode form\n"
        f"; (e.g. `LDA #{cap}_HGR_COUNT`), then `.import` the data labels from the\n"
        f"; sister sprites_{category}_hgr.asm linked in via EXTRA_ASM. Same\n"
        f"; convention applies to every other category in dev/lib/hgr/sprites/.\n"
        f"; ============================================================================\n"
        f"\n"
        f"{cap}_HGR_COUNT             = {n}\n"
        f"{cap}_HGR_BYTES_PER_SPRITE  = 48\n"
    )


def category_from_filename(path: pathlib.Path) -> str:
    return path.stem.replace("sprites_", "")


def discover_sources(only: str | None) -> List[pathlib.Path]:
    candidates = sorted(TMS_DIR.glob("sprites_*.asm"))
    if only:
        candidates = [p for p in candidates
                      if category_from_filename(p) == only]
        if not candidates:
            raise SystemExit(f"no source matches --only={only}")
    return candidates


def main(argv: List[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--only", default=None,
                   help="rebuild a single category (e.g. fauna)")
    p.add_argument("--check", action="store_true",
                   help="diff regenerated output against on-disk files")
    args = p.parse_args(argv)

    HGR_DIR.mkdir(parents=True, exist_ok=True)
    sources = discover_sources(args.only)

    diff_count = 0
    for src in sources:
        category = category_from_filename(src)
        title, sprites = parse_tms_file(src)
        if not sprites:
            print(f"[skip] {src.name}: no sprites parsed", file=sys.stderr)
            continue
        out_path = HGR_DIR / f"sprites_{category}_hgr.asm"
        inc_path = HGR_DIR / f"sprites_{category}_hgr.inc"
        new_text = emit_hgr_asm(category, title, sprites)
        new_inc = emit_hgr_inc(category, title, len(sprites))
        if args.check:
            old_text = out_path.read_text() if out_path.exists() else ""
            old_inc = inc_path.read_text() if inc_path.exists() else ""
            if old_text != new_text:
                print(f"[diff] {out_path.relative_to(REPO)} differs",
                      file=sys.stderr)
                diff_count += 1
            if old_inc != new_inc:
                print(f"[diff] {inc_path.relative_to(REPO)} differs",
                      file=sys.stderr)
                diff_count += 1
            continue
        out_path.write_text(new_text)
        inc_path.write_text(new_inc)
        print(f"[write] {out_path.relative_to(REPO)}  "
              f"+ {inc_path.name}  ({len(sprites)} sprites, "
              f"{len(sprites) * 48} B)",
              file=sys.stderr)

    if args.check:
        return 0 if diff_count == 0 else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
