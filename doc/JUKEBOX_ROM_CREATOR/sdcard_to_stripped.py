#!/usr/bin/env python3
"""sdcard_to_stripped.py -- Generate JukeBox .bin/.pat pairs from raw sdcard
files tagged with the NAME#TTAAAA convention.

Only handles MC binaries (TT == "06"). BAS (F1) requires the slicer logic in
1-stripper.sh; not implemented here.

Usage:
    python3 sdcard_to_stripped.py <sdcard_path> <8char_name>

The new pair lands in `2-stripped/<8char_name>.bin` + `.pat`.
"""
from __future__ import annotations

import pathlib
import sys

SCRIPT_DIR  = pathlib.Path(__file__).resolve().parent
STRIPPED_DIR = SCRIPT_DIR / "2-stripped"


def parse_tag(filename: str) -> tuple[str, int]:
    """Parse NAME#TTAAAA -> (name, load_addr). Returns ("", 0) on failure."""
    if "#" not in filename:
        return "", 0
    name, tag = filename.split("#", 1)
    if len(tag) < 6:
        return "", 0
    type_byte = tag[:2]
    addr_hex  = tag[2:6]
    if type_byte.lower() != "06":
        return "", 0
    try:
        addr = int(addr_hex, 16)
    except ValueError:
        return "", 0
    return name, addr


def make_pat(name8: str, load_addr: int, length: int, type_byte: int = 0xFE) -> bytes:
    """Build the 15-byte PAT entry for the JukeBox firmware.

    Dashes in the basename are pad characters (used in filenames so they reach
    8 chars); the firmware shows them as spaces. Mirrors 1-stripper.sh which
    runs `sed 's/-/ /g'` on the name before writing it into the PAT.
    """
    if len(name8) > 8:
        raise ValueError(f"name '{name8}' is {len(name8)} chars, max 8")
    name_padded = name8.upper().replace("-", " ").ljust(8, " ").encode("ascii")
    return bytes([type_byte]) + name_padded + bytes([
        0xFF, 0xFF,                    # EPROM_ADDR placeholder (build_jukebox_rom patches)
        load_addr & 0xFF, (load_addr >> 8) & 0xFF,  # LOAD_ADDR LE
        length    & 0xFF, (length    >> 8) & 0xFF,  # LENGTH LE
    ])


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__.strip())
        return 2
    sdcard_path = pathlib.Path(argv[1])
    name8       = argv[2]
    if not sdcard_path.is_file():
        sys.exit(f"error: {sdcard_path} not found")
    if len(name8) > 8 or not name8:
        sys.exit(f"error: target name '{name8}' must be 1..8 chars")

    _, load_addr = parse_tag(sdcard_path.name)
    if load_addr == 0:
        sys.exit(f"error: cannot parse #TT/AAAA tag from filename '{sdcard_path.name}'")

    data = sdcard_path.read_bytes()
    length = len(data)

    bin_out = STRIPPED_DIR / f"{name8}.bin"
    pat_out = STRIPPED_DIR / f"{name8}.pat"
    bin_out.write_bytes(data)
    pat_out.write_bytes(make_pat(name8, load_addr, length))

    print(f"  {name8:8s}  load=${load_addr:04X}  len={length:5d}  -> {bin_out.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
