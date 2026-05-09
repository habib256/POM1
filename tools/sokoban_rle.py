#!/usr/bin/env python3
"""RLE-encode Sokoban level data and emit ca65 .byte declarations.

RLE format:
  - byte < $80 : literal char (bit 7 always clear for Sokoban ASCII).
  - byte >= $80: (byte & $7F) = run length (2..127); next byte = char to repeat.
"""

import re
import sys
from pathlib import Path

ROOT = Path("/Users/factory/src/POM1/software")

def parse_levels(asm_path, start_n, end_n):
    """Return {n: (w, h, row_off, col_off, [rows])} for level<start>..level<end>."""
    text = asm_path.read_text()
    levels = {}
    for n in range(start_n, end_n + 1):
        # Match "levelN:\n    .byte W, H, RO, CO\n    .byte \"row0\"\n ..."
        pat = re.compile(
            rf'^level{n}:\s*\n'
            rf'\s*\.byte\s+(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\n'
            rf'((?:\s*\.byte\s+"[^"]*"\s*\n)+)',
            re.M,
        )
        m = pat.search(text)
        if not m:
            sys.exit(f"level{n} not found in {asm_path}")
        w, h, ro, co = (int(x) for x in m.group(1, 2, 3, 4))
        rows = re.findall(r'\.byte\s+"([^"]*)"', m.group(5))
        assert len(rows) == h, (asm_path, n, len(rows), h)
        for r in rows:
            assert len(r) == w, (asm_path, n, r, w)
        levels[n] = (w, h, ro, co, rows)
    return levels

def rle_encode(data: bytes) -> bytes:
    """Encode `data` into RLE form. Single-byte runs stay as literals."""
    out = bytearray()
    i = 0
    while i < len(data):
        c = data[i]
        # Count the run of identical bytes starting at i.
        j = i
        while j < len(data) and data[j] == c and (j - i) < 127:
            j += 1
        run = j - i
        if run >= 2:
            out.append(0x80 | run)
            out.append(c)
        else:
            out.append(c)
        i = j
    return bytes(out)

def byte_lines(data: bytes, indent: str = "        ") -> str:
    lines = []
    for k in range(0, len(data), 12):
        chunk = data[k:k+12]
        parts = []
        for b in chunk:
            if 0x20 <= b < 0x80 and chr(b) not in "\\'\"":
                parts.append(f"'{chr(b)}'")
            else:
                parts.append(f"${b:02X}")
        lines.append(f"{indent}.byte {','.join(parts)}")
    return "\n".join(lines)

def emit_level(n, lvl):
    w, h, ro, co, rows = lvl
    ascii_data = "".join(rows).encode("ascii")
    rle = rle_encode(ascii_data)
    header = bytes([w, h, ro, co])
    original_size = 4 + len(ascii_data)
    new_size = 4 + len(rle)
    comment = f"; level{n}  {w}x{h}  raw={len(ascii_data)}B  rle={len(rle)}B  (save {len(ascii_data)-len(rle)}B)"
    out = [comment]
    out.append(f"level{n}:")
    out.append(f"        .byte {w},{h},{ro},{co}")
    out.append(byte_lines(rle))
    return "\n".join(out), original_size, new_size

BASE_LAST = 45  # Last level included in sokoban_levels.inc (text-safe set).

def rle_decode(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        b = data[i]; i += 1
        if b < 0x80:
            out.append(b)
        else:
            out.extend([data[i]] * (b & 0x7F)); i += 1
    return bytes(out)

def parse_levels_from_inc(inc_path, start_n, end_n):
    """Decode RLE level data back into (w, h, ro, co, rows) tuples."""
    text = inc_path.read_text()
    levels = {}
    for n in range(start_n, end_n + 1):
        pat = re.compile(
            rf'^level{n}:\s*\n'
            rf'\s*\.byte\s+(\d+),(\d+),(\d+),(\d+)\s*\n'
            rf'((?:\s*\.byte\s+[^\n]*\n)+)',
            re.M,
        )
        m = pat.search(text)
        if not m:
            continue
        w, h, ro, co = (int(x) for x in m.group(1, 2, 3, 4))
        toks = re.findall(r"\$([0-9A-Fa-f]{2})|'(.)'", m.group(5))
        comp = bytes((int(a, 16) if a else ord(b)) for a, b in toks)
        raw = rle_decode(comp)
        assert len(raw) == w * h, (n, len(raw), w, h)
        rows = [raw[i*w:(i+1)*w].decode("ascii") for i in range(h)]
        levels[n] = (w, h, ro, co, rows)
    return levels

def main():
    # Prefer re-parsing from the existing generated .inc files (round-trip
    # safe via RLE decoder). Falls back to the legacy .asm format if the
    # .inc files haven't been generated yet.
    base_inc = ROOT / "games" / "sokoban_levels.inc"
    ext_inc = ROOT / "games" / "sokoban_levels_ext.inc"
    full = {}
    if base_inc.exists():
        full.update(parse_levels_from_inc(base_inc, 1, 72))
    if ext_inc.exists():
        full.update(parse_levels_from_inc(ext_inc, 1, 72))
    if len(full) < 72:
        # Bootstrap path: read from the .asm sources that still hold raw data.
        for path, hi in [(ROOT / "games" / "Sokoban.asm", 47),
                         (ROOT / "hgr" / "HGR_Sokoban.asm", 72)]:
            if path.exists():
                try:
                    full.update(parse_levels(path, 1, hi))
                except SystemExit:
                    pass
    missing = [n for n in range(1, 73) if n not in full]
    if missing:
        sys.exit(f"missing levels: {missing}")

    total_old = total_new = 0
    base_body = []
    for n in range(1, BASE_LAST + 1):
        blob, old, new = emit_level(n, full[n])
        base_body.append(blob)
        total_old += old
        total_new += new
    base_text = (
        "; =============================================\n"
        f"; sokoban_levels.inc - RLE-compressed levels 1..{BASE_LAST}\n"
        "; Auto-generated by tools/sokoban_rle.py. Do not edit.\n"
        "; Format: header (w, h, row_offset, col_offset) + RLE byte stream.\n"
        "; RLE: byte<$80 = literal; byte>=$80 -> (byte & $7F) copies of next byte.\n"
        "; Shared by text + TMS variants; HGR also pulls sokoban_levels_ext.inc.\n"
        "; =============================================\n\n"
        + "\n\n".join(base_body) + "\n"
    )
    base_inc.write_text(base_text)

    total_old_e = total_new_e = 0
    ext_body = []
    for n in range(BASE_LAST + 1, 73):
        blob, old, new = emit_level(n, full[n])
        ext_body.append(blob)
        total_old_e += old
        total_new_e += new
    ext_text = (
        "; =============================================\n"
        f"; sokoban_levels_ext.inc - RLE-compressed levels {BASE_LAST+1}..72\n"
        "; Auto-generated by tools/sokoban_rle.py. Do not edit.\n"
        "; Format identical to sokoban_levels.inc. HGR variant only.\n"
        "; =============================================\n\n"
        + "\n\n".join(ext_body) + "\n"
    )
    ext_inc.write_text(ext_text)

    print(f"Base 1..{BASE_LAST:<2} : {total_old:5} -> {total_new:5} bytes  (save {total_old-total_new})")
    print(f"Ext  {BASE_LAST+1}..72: {total_old_e:5} -> {total_new_e:5} bytes  (save {total_old_e-total_new_e})")
    print(f"Combined   : {total_old+total_old_e:5} -> {total_new+total_new_e:5} bytes")

if __name__ == "__main__":
    main()
