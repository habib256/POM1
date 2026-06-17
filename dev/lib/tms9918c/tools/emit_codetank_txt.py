#!/usr/bin/env python3
"""Emit Wozmon hex .txt for a flat binary loaded at $4000 (CodeTank)."""
from __future__ import annotations

import argparse
import pathlib


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--bin", required=True, type=pathlib.Path)
    p.add_argument("--txt", required=True, type=pathlib.Path)
    p.add_argument("--start", default="0x4000")
    args = p.parse_args()
    start = int(args.start, 0)
    data = args.bin.read_bytes()
    lines: list[str] = []
    addr = start
    for i in range(0, len(data), 8):
        chunk = data[i : i + 8]
        lines.append(f"{addr:04X}: " + " ".join(f"{b:02X}" for b in chunk))
        addr += len(chunk)
    lines.append(f"{start:04X}R")
    args.txt.parent.mkdir(parents=True, exist_ok=True)
    args.txt.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"Wrote {args.txt} ({len(data)} bytes @ ${start:04X})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
