#!/usr/bin/env python3
"""Shift every sprite in dev/lib/tms9918/sprites_characters.asm UP by
one row. The Quale extraction left two blank rows at the top of each
character sprite and none at the bottom — sprites float too high in
their 16x16 cell. This rewrites each 32-byte block so:

    new_left[0..14]  = old_left[1..15];  new_left[15]  = 0
    new_right[0..14] = old_right[1..15]; new_right[15] = 0

After running this, regenerate the HGR variant with
tools/build_hgr_sprites.py so dev/lib/hgr/sprites/sprites_characters_hgr.asm
stays in sync.

Destructive in-place edit. Idempotent only after re-extraction from the
PNG — running twice eats a non-blank row.
"""
from __future__ import annotations

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "dev" / "lib" / "tms9918" / "sprites_characters.asm"

LABEL_RE = re.compile(r"^([a-zA-Z][a-zA-Z0-9_]*):\s*$")
BYTE_LINE_RE = re.compile(r"^(\s*\.byte\s+)([^;]*?)(\s*;.*)?$")


def parse_byte_line(line: str) -> tuple[str, list[int], str]:
    m = BYTE_LINE_RE.match(line.rstrip("\n"))
    if not m:
        raise ValueError(f"not a .byte line: {line!r}")
    prefix = m.group(1)
    body = m.group(2)
    trailing = m.group(3) or ""
    bytes_out: list[int] = []
    for tok in body.split(","):
        tok = tok.strip()
        if not tok:
            continue
        if tok.startswith("$"):
            bytes_out.append(int(tok[1:], 16))
        elif tok.lower().startswith("0x"):
            bytes_out.append(int(tok[2:], 16))
        else:
            bytes_out.append(int(tok))
    return prefix, bytes_out, trailing


def fmt_byte_line(prefix: str, bytes_in: list[int], trailing: str) -> str:
    body = ", ".join(f"${b:02X}" for b in bytes_in)
    return f"{prefix}{body}{trailing}\n"


def main() -> int:
    text = SRC.read_text().splitlines(keepends=True)
    out_lines: list[str] = []
    i = 0
    shifted = 0
    while i < len(text):
        line = text[i]
        m = LABEL_RE.match(line.rstrip("\n"))
        if not m or not m.group(1).startswith("char_"):
            out_lines.append(line)
            i += 1
            continue
        out_lines.append(line)
        i += 1

        sprite_bytes: list[int] = []
        line_metas: list[tuple[str, str]] = []
        while len(line_metas) < 4 and i < len(text):
            cur = text[i]
            if cur.lstrip().startswith(".byte"):
                prefix, bs, trailing = parse_byte_line(cur)
                line_metas.append((prefix, trailing))
                sprite_bytes.extend(bs)
                i += 1
            else:
                out_lines.append(cur)
                i += 1
        if len(sprite_bytes) != 32:
            raise SystemExit(
                f"expected 32 bytes for {m.group(1)}, "
                f"got {len(sprite_bytes)}"
            )
        left = sprite_bytes[1:16] + [0]
        right = sprite_bytes[17:32] + [0]
        new_bytes = left + right
        for k in range(4):
            prefix, trailing = line_metas[k]
            out_lines.append(
                fmt_byte_line(prefix, new_bytes[k*8:(k+1)*8], trailing)
            )
        shifted += 1

    SRC.write_text("".join(out_lines))
    print(f"Shifted {shifted} sprites in {SRC}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
