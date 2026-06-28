#!/usr/bin/env python3
"""Emit a single Wozmon-hex Chess.txt from the two dual-bank link outputs.

The chess link config (apple1_chess_text.cfg) splits the image into two banks
that land in the stock 8 KB Apple-1 RAM map: CODELOW at $0280 (file %O.lo) and
ENGINE at $E000 (file %O.hi). POM1's --load / Wozmon-hex loader reads the
per-line address prefixes, so one .txt with both address zones loads the whole
program in a single step (cf. CliDispatcher's multi-zone note). The trailing
`0280R` boots the splash via Wozmon's R command.
"""
from __future__ import annotations

import argparse
import pathlib


def emit_zone(lines: list[str], data: bytes, start: int) -> None:
    addr = start
    for i in range(0, len(data), 8):
        chunk = data[i : i + 8]
        lines.append(f"{addr:04X}: " + " ".join(f"{b:02X}" for b in chunk))
        addr += len(chunk)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--lo", required=True, type=pathlib.Path, help="CODELOW bank (@ --lo-start)")
    p.add_argument("--hi", required=True, type=pathlib.Path, help="ENGINE bank (@ --hi-start)")
    p.add_argument("--txt", required=True, type=pathlib.Path)
    p.add_argument("--lo-start", default="0x0280")
    p.add_argument("--hi-start", default="0xE000")
    args = p.parse_args()

    lo_start = int(args.lo_start, 0)
    hi_start = int(args.hi_start, 0)
    lo = args.lo.read_bytes()
    hi = args.hi.read_bytes()

    lines: list[str] = []
    emit_zone(lines, lo, lo_start)
    emit_zone(lines, hi, hi_start)
    lines.append(f"{lo_start:04X}R")

    args.txt.parent.mkdir(parents=True, exist_ok=True)
    args.txt.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"Wrote {args.txt} ({len(lo)} B @ ${lo_start:04X} + {len(hi)} B @ ${hi_start:04X})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
