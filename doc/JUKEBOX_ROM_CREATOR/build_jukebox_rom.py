#!/usr/bin/env python3
"""Build a POM1-compatible jukebox.rom from P-LAB EPROM_CREATOR stripped files.

This is a portable (macOS / Linux / Windows) replacement for the P-LAB shell
pipeline 2-packer.sh + patter.sh. The originals depend on GNU-only tools
(`ascii2binary`, `rename`, `stat --printf`, `bc`) and end with a 16/16 kB
bank flip that POM1 does NOT want: POM1 reads the ROM file with a 1:1 map
(CPU $4000 = file offset 0 with the RAM-16/ROM-32 jumper), so the Program
Manager must sit at file offset $7D00, which is the PRE-flip P-LAB layout.

Final 32 kB file layout produced here:
    $0000 .. $XXXX   Programs, concatenated in pack order
    $XXXX .. $7C00   $FF padding
    $7C00 .. $7C00+15*N   PAT entries (15 bytes each, one per program)
    $7C00+15*N .. $7D00   $FF padding
    $7D00 .. $7FFF   loader_real.bin (768 bytes, starts with $A5)

PAT entry layout (15 bytes, matches the P-LAB stripper output):
    [0]     TYPE        $F1 = BASIC, $FE = binary
    [1..8]  NAME        8 chars, space-padded
    [9..10] EPROM_ADDR  LO, HI -- physical CPU address (filled here)
    [11..12] LOAD_ADDR  LO, HI -- where the loader copies it in RAM
    [13..14] LENGTH     LO, HI -- number of bytes to copy

Usage:
    build_jukebox_rom.py                  # bundle the default classic set
    build_jukebox_rom.py PROG1 PROG2 ...  # bundle a custom selection
        where each PROG is the basename (no extension) of a 2-stripped/*.bin

The script is deterministic: programs are laid out in the order they appear
on the command line (or the default list), largest PAT count capped at 17 to
match the P-LAB loader.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

EPROM_SIZE       = 0x8000          # 32 KB
LOADER_SIZE      = 0x300           # 768 bytes
PAT_AREA_START   = 0x7C00          # first PAT
LOADER_OFFSET    = 0x7D00          # where loader_real.bin goes
PROG_BASE_CPU    = 0x4000          # CPU addr of file offset 0 (RAM-16/ROM-32)
MAX_PROGRAMS     = 17
BASIC_TAIL_BYTES = 0xB6            # pointers appended after the slice for BASIC

SCRIPT_DIR  = Path(__file__).resolve().parent
STRIPPED_DIR = SCRIPT_DIR / "2-stripped"
LOADER_PATH  = SCRIPT_DIR / "loader_real.bin"

DEFAULT_SELECTION = [
    "BASIC---",   # Apple BASIC ROM at $E000
    "WOZF2394",   # Woz floating-point demo (machine code)
    "LIFE----",   # Conway's Life (machine code)
    "MICROCH2",   # MicroChess v2 (machine code)
    "STARTREK",   # Super Star Trek (BASIC)
    "CHECKERS",   # Checkers (BASIC)
    "LUNARLND",   # Lunar Lander (BASIC)
    "HAMURABI",   # Hamurabi (BASIC)
    "AMAZING-",   # Amazing (maze generator, BASIC)
    "BLACKJAK",   # Blackjack (BASIC)
    "BATNUM--",   # Batnum (BASIC)
    "REVERSE-",   # Reverse (BASIC)
]
# Default set totals ~30 kB across 12 programs, leaving headroom under the
# 31.75 kB program budget ($0000-$7BFF) while staying below the 17-entry PAT
# limit.


def load_program(basename: str) -> tuple[bytes, bytearray]:
    bin_path = STRIPPED_DIR / f"{basename}.bin"
    pat_path = STRIPPED_DIR / f"{basename}.pat"
    if not bin_path.is_file():
        sys.exit(f"error: missing {bin_path}")
    if not pat_path.is_file():
        sys.exit(f"error: missing {pat_path}")
    data = bin_path.read_bytes()
    pat  = bytearray(pat_path.read_bytes())
    if len(pat) != 15:
        sys.exit(f"error: {pat_path} is {len(pat)} bytes, expected 15")
    return data, pat


def main(argv: list[str]) -> int:
    names = argv[1:] if len(argv) > 1 else list(DEFAULT_SELECTION)

    if len(names) > MAX_PROGRAMS:
        sys.exit(f"error: {len(names)} programs requested, max is {MAX_PROGRAMS}")

    loaded = [(n, *load_program(n)) for n in names]

    # Budget check: programs must fit below the PAT area.
    total_prog_bytes = sum(len(d) for _, d, _ in loaded)
    if total_prog_bytes > PAT_AREA_START:
        sys.exit(
            f"error: programs total {total_prog_bytes} bytes, "
            f"max {PAT_AREA_START} ($7C00)"
        )

    rom = bytearray(b"\xFF" * EPROM_SIZE)

    # Place programs and record file offsets.
    offsets: list[int] = []
    off = 0
    for name, data, _pat in loaded:
        offsets.append(off)
        rom[off:off + len(data)] = data
        off += len(data)
        print(f"  packed {name:8s}  offset=${offsets[-1]:04X}  len={len(data):5d}")

    # Build PAT entries with EPROM addresses patched in, mimic patter.sh.
    # EPROM address = CPU address where the program data starts. For the
    # RAM-16/ROM-32 jumper, POM1 maps file offset 0 -> CPU $4000, so:
    #     epr_addr = PROG_BASE_CPU + offset
    for i, (name, data, pat) in enumerate(loaded):
        epr_addr = PROG_BASE_CPU + offsets[i]
        pat[9]  = epr_addr & 0xFF
        pat[10] = (epr_addr >> 8) & 0xFF

    # Write PAT area starting at $7C00.
    pat_off = PAT_AREA_START
    for name, _data, pat in loaded:
        rom[pat_off:pat_off + 15] = pat
        pat_off += 15
    # Remaining bytes in PAT area and up to loader stay $FF (already initialised).

    # Append loader_real.bin at $7D00.
    if not LOADER_PATH.is_file():
        sys.exit(f"error: missing {LOADER_PATH}")
    loader = LOADER_PATH.read_bytes()
    if len(loader) != LOADER_SIZE:
        sys.exit(
            f"error: {LOADER_PATH} is {len(loader)} bytes, expected {LOADER_SIZE}"
        )
    rom[LOADER_OFFSET:LOADER_OFFSET + LOADER_SIZE] = loader

    # Firmware signature sanity check.
    sig = rom[0x7D00]
    if sig != 0xA5:
        sys.exit(f"error: byte at $7D00 is ${sig:02X}, expected $A5 (bad loader?)")

    if len(argv) > 1 and argv[1].startswith("--out="):
        out_path = Path(argv[1][len("--out="):])
    else:
        # Default target: repo roms/ dir. Resolve from doc/JUKEBOX_ROM_CREATOR/.
        out_path = SCRIPT_DIR.parent.parent / "roms" / "jukebox.rom"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(rom)
    print(f"\nwrote {out_path}  ({len(rom)} bytes)")
    print(f"  firmware signature at $7D00: ${sig:02X} [ok]")
    print(f"  programs: {len(loaded)}  total body: {total_prog_bytes} bytes "
          f"(free: {PAT_AREA_START - total_prog_bytes} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
