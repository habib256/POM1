#!/usr/bin/env python3
"""Wrap TMS_Nyan_CodeTank.bin into a 32 KB CodeTank ROM image.

Usage:
    python3 build_rom.py <input_bin> <output_rom>

Input is the linker output (≤ 16 384 B, code at $4000). Output is a
32 KB ROM image: lower 16 KB = padded input, upper 16 KB = $FF fill.

The CodeTank Library scans roms/codetank/*.{rom,bin} on startup, so
just dropping the file in roms/codetank/ makes it appear in the
Hardware → CodeTank Library list with no further configuration.
"""
import pathlib
import sys

HALF = 0x4000   # 16 KB
ROM_SIZE = 0x8000   # 32 KB

def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 1
    src_path = pathlib.Path(argv[1])
    dst_path = pathlib.Path(argv[2])

    src = src_path.read_bytes()
    if len(src) > HALF:
        print(f"ERROR: {src_path} is {len(src)} B, exceeds {HALF} B half-bank.",
              file=sys.stderr)
        return 1

    print(f"  Lower bank: {len(src):5d} B / {HALF:5d} B "
          f"({100 * len(src) / HALF:.1f}%, {HALF - len(src):5d} B free)",
          file=sys.stderr)

    rom = bytearray(b"\xFF" * ROM_SIZE)
    rom[0:len(src)] = src
    # Upper bank stays $FF-filled.

    dst_path.parent.mkdir(parents=True, exist_ok=True)
    dst_path.write_bytes(bytes(rom))
    print(f"  Wrote {dst_path} ({ROM_SIZE} B).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
