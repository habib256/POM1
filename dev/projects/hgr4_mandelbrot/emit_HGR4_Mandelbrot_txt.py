#!/usr/bin/env python3
"""Assemble HGR4_Mandelbrot and write HGR4_Mandelbrot.txt (Woz Monitor hex)."""
import pathlib
import subprocess
import sys

PROJ = pathlib.Path(__file__).resolve().parent
ROOT = PROJ.parents[2]
LIB_APPLE1 = ROOT / "dev" / "lib" / "apple1"
LIB_HGR = ROOT / "dev" / "lib" / "hgr"
LIB_M6502 = ROOT / "dev" / "lib" / "m6502"
HGR = ROOT / "software" / "hgr"
BUILD = ROOT / "build"
ASM = PROJ / "HGR4_Mandelbrot.asm"
OBJ = BUILD / "HGR4_Mandelbrot.o"
BIN = BUILD / "HGR4_Mandelbrot.bin"
OUT = HGR / "HGR4_Mandelbrot.txt"
CFG = ROOT / "dev" / "cc65" / "apple1_gen2.cfg"

START = 0x280


def main() -> int:
    BUILD.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "ca65",
            "-I", str(LIB_APPLE1),
            "-I", str(LIB_HGR),
            "-I", str(LIB_M6502),
            "-o", str(OBJ),
            str(ASM),
        ],
        check=True,
        cwd=str(ROOT),
    )
    subprocess.run(
        [
            "ld65",
            "-C", str(CFG),
            "-o", str(BIN),
            str(OBJ),
        ],
        check=True,
        cwd=str(ROOT),
    )
    data = BIN.read_bytes()
    lines = []
    addr = START
    for i in range(0, len(data), 8):
        chunk = data[i : i + 8]
        lines.append(f"{addr:04X}: " + " ".join(f"{b:02X}" for b in chunk))
        addr += len(chunk)
    lines.append("0280R")
    OUT.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"Wrote {OUT} ({len(data)} bytes)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
