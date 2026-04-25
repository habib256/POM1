#!/usr/bin/env python3
"""wozmon_to_stripped.py -- Convert a Wozmon hex paste (.txt) into a JukeBox
.bin/.pat pair.

Format accepted (matches Memory::loadHexDump):
  - Comment lines: lines starting with '//', '#', or ';'
  - Inline comments after a '//', '#', or ';' marker
  - Data lines: 'XXXX: BB BB BB ...'  (or 'XXXX:BB BB BB' compact)
  - Multiple ':' inside a data line are separators (treated like spaces)
  - Continuation lines (no address prefix) append to the running address

Usage:
    python3 wozmon_to_stripped.py <wozmon.txt> <8char_name>
"""
from __future__ import annotations

import pathlib
import re
import sys

SCRIPT_DIR  = pathlib.Path(__file__).resolve().parent
STRIPPED_DIR = SCRIPT_DIR / "2-stripped"

ADDR_RE = re.compile(r"^([0-9A-Fa-f]{4}):\s*(.*)$")


def strip_comment(line: str) -> str:
    for marker in ("//", "#", ";"):
        idx = line.find(marker)
        if idx >= 0:
            line = line[:idx]
    return line.strip()


def parse_wozmon(path: pathlib.Path) -> tuple[int, bytes]:
    """Return (start_addr, contiguous bytes). Raises on gaps or overlap."""
    sparse: dict[int, int] = {}
    cur_addr: int | None = None
    start_addr: int | None = None
    for raw in path.read_text(errors="replace").splitlines():
        line = strip_comment(raw)
        if not line:
            continue
        m = ADDR_RE.match(line)
        if m:
            cur_addr = int(m.group(1), 16)
            payload = m.group(2)
            if start_addr is None:
                start_addr = cur_addr
        else:
            payload = line
            if cur_addr is None:
                # Data before first address — skip
                continue
        # Tokenize: replace ':' with space, split, parse hex bytes
        for tok in payload.replace(":", " ").split():
            try:
                b = int(tok, 16)
            except ValueError:
                continue
            if not 0 <= b <= 0xFF:
                continue
            if cur_addr in sparse:
                raise SystemExit(f"error: duplicate write at ${cur_addr:04X} in {path}")
            sparse[cur_addr] = b
            cur_addr += 1
    if start_addr is None or not sparse:
        raise SystemExit(f"error: no data parsed from {path}")
    end_addr = max(sparse.keys())
    # Build contiguous buffer; fill gaps with $00 (programs typically don't have gaps).
    out = bytearray(end_addr - start_addr + 1)
    for addr, b in sparse.items():
        out[addr - start_addr] = b
    return start_addr, bytes(out)


def make_pat(name8: str, load_addr: int, length: int, type_byte: int = 0xFE) -> bytes:
    """Dashes in the basename are pad chars; mirror 1-stripper.sh and convert
    them to spaces before writing the PAT NAME field."""
    if len(name8) > 8:
        raise SystemExit(f"error: name '{name8}' is {len(name8)} chars, max 8")
    name_padded = name8.upper().replace("-", " ").ljust(8, " ").encode("ascii")
    return bytes([type_byte]) + name_padded + bytes([
        0xFF, 0xFF,
        load_addr & 0xFF, (load_addr >> 8) & 0xFF,
        length    & 0xFF, (length    >> 8) & 0xFF,
    ])


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__.strip())
        return 2
    src   = pathlib.Path(argv[1])
    name8 = argv[2]
    if not src.is_file():
        sys.exit(f"error: {src} not found")
    if not 1 <= len(name8) <= 8:
        sys.exit(f"error: name '{name8}' must be 1..8 chars")

    start, data = parse_wozmon(src)
    bin_out = STRIPPED_DIR / f"{name8}.bin"
    pat_out = STRIPPED_DIR / f"{name8}.pat"
    bin_out.write_bytes(data)
    pat_out.write_bytes(make_pat(name8, start, len(data)))
    print(f"  {name8:8s}  load=${start:04X}  len={len(data):5d}  -> {bin_out.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
