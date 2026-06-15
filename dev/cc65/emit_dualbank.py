#!/usr/bin/env python3
"""Stitch a dual-bank build (lo @ $0280 + hi @ $E000) into one Wozmon-hex .txt.

Shared by the Parmigiani 8 KB dual-bank projects (the Chess trilogy, HGR Sokoban)
whose linker splits the program into a .bin.lo + .bin.hi. Wozmon's "AAAA:"
address-prefix syntax handles the 53 KB jump between the two blocks, and the
run-address line at the end auto-runs the program on File > Load Memory.

Replaces the identical inline `python3 -c "..."` one-liner those Makefiles each
carried. Output is byte-for-byte what the one-liners produced.

Usage (from a project Makefile):
    python3 ../../cc65/emit_dualbank.py LO HI OUT --run 0280 [--header "// ..."]
"""
import argparse
import pathlib


def emit_dualbank(lo_path, hi_path, out_path, run_addr,
                  header=None, lo_addr=0x0280, hi_addr=0xE000):
    lo = pathlib.Path(lo_path).read_bytes()
    hi = pathlib.Path(hi_path).read_bytes()
    out = list(header) if header else []
    for base, data in ((lo_addr, lo), (hi_addr, hi)):
        for i in range(0, len(data), 8):
            row = " ".join(f"{b:02X}" for b in data[i:i + 8])
            out.append(f"{base + i:04X}: {row}")
    out.append(f"{run_addr}R")
    pathlib.Path(out_path).write_text("\n".join(out) + "\n")
    print(f"Wrote {out_path} (lo={len(lo)} B at ${lo_addr:04X}, "
          f"hi={len(hi)} B at ${hi_addr:04X})")


def main() -> int:
    ap = argparse.ArgumentParser(description="dual-bank Wozmon-hex stitcher")
    ap.add_argument("lo", help="path to the low-bank .bin")
    ap.add_argument("hi", help="path to the high-bank .bin")
    ap.add_argument("out", help="path to the .txt to write")
    ap.add_argument("--run", required=True, help="run address (hex), e.g. 0280 or E000")
    ap.add_argument("--header", action="append", default=None,
                    help="optional leading comment line (repeatable)")
    ap.add_argument("--lo-addr", default="0280", help="low-bank load address (hex)")
    ap.add_argument("--hi-addr", default="E000", help="high-bank load address (hex)")
    a = ap.parse_args()
    emit_dualbank(a.lo, a.hi, a.out, a.run, header=a.header,
                  lo_addr=int(a.lo_addr, 16), hi_addr=int(a.hi_addr, 16))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
