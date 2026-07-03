#!/usr/bin/env python3
"""Emit a GEN2 HGR program's shipped artefacts from the raw cl65 image.

The gen2-C link config (dev/cc65/apple1_gen2_c.cfg) produces a *headerless*
binary that loads at $6000. POM1 loads either the raw `.bin` (via
`--load 6000:prog.bin`) or the Wozmon-hex `.txt` (self-addressing, with a
trailing `6000R` run marker that boots it through Wozmon's R command). This
copies the freshly linked image to the shipped `.bin` and regenerates the
matching `.txt` so the two never drift apart.

    python3 emit_gen2_txt.py <src.bin> <out.bin> <out.txt> [--start 0x6000]

Used by the gen2 sketch Makefiles (e.g. sketchs/gen2/demo_bounces).
"""
from __future__ import annotations

import argparse
import pathlib
import shutil


def emit_txt(data: bytes, txt: pathlib.Path, start: int, name: str) -> None:
    lines = [
        f"// {name} - GEN2 HGR (cc65). Load at ${start:04X}, run {start:04X}R.",
    ]
    addr = start
    for i in range(0, len(data), 8):
        chunk = data[i : i + 8]
        lines.append(f"{addr:04X}: " + " ".join(f"{b:02X}" for b in chunk))
        addr += len(chunk)
    lines.append(f"{start:04X}R")
    txt.parent.mkdir(parents=True, exist_ok=True)
    txt.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("src_bin", type=pathlib.Path, help="freshly linked cl65 image")
    p.add_argument("out_bin", type=pathlib.Path, help="shipped .bin (copy of src)")
    p.add_argument("out_txt", type=pathlib.Path, help="shipped Wozmon-hex .txt")
    p.add_argument("--start", default="0x6000", help="load/run address (default $6000)")
    args = p.parse_args()

    start = int(args.start, 0)
    data = args.src_bin.read_bytes()

    args.out_bin.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.src_bin, args.out_bin)
    emit_txt(data, args.out_txt, start, args.src_bin.stem)

    print(f"emit_gen2_txt: {len(data)} B @ ${start:04X} -> {args.out_bin} + {args.out_txt}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
