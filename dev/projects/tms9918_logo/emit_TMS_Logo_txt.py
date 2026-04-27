#!/usr/bin/env python3
"""Assemble TMS_Logo (3 modules) and write TMS_Logo.txt (Woz Monitor hex paste).

Modules linked together (in order so main entry stays at $0280):
  TMS_Logo.asm   -- LOGO interpreter, REPL, turtle, errors, banner, main.
  math.asm       -- fixed-point trig, LFSR, decimal output, mod360.
                    Lives in dev/lib/m6502/.
  tms9918m2.asm  -- TMS9918 Mode-2 bitmap driver (init, plot, line_xy).
                    Lives in dev/lib/tms9918/.
"""
import pathlib
import subprocess
import sys

PROJ = pathlib.Path(__file__).resolve().parent
ROOT = PROJ.parents[2]
LIB_APPLE1 = ROOT / "dev" / "lib" / "apple1"
LIB_M6502 = ROOT / "dev" / "lib" / "m6502"
LIB_TMS = ROOT / "dev" / "lib" / "tms9918"
TMS_OUT = ROOT / "software" / "tms9918"
BUILD = ROOT / "build"
CFG = PROJ / "apple1_logo.cfg"
BIN = BUILD / "TMS_Logo.bin"
OUT = TMS_OUT / "TMS_Logo.txt"

# Link order matters: the entry point in TMS_Logo.asm must end up at $0280,
# so its .o is listed first.
# Map each module stem to the directory holding its source file.
MODULE_SOURCES = {
    "TMS_Logo": PROJ,
    "math": LIB_M6502,
    "tms9918m2": LIB_TMS,
}
MODULES = ("TMS_Logo", "math", "tms9918m2")

START = 0x280


def main() -> int:
    BUILD.mkdir(parents=True, exist_ok=True)
    objs = []
    for stem in MODULES:
        asm = MODULE_SOURCES[stem] / f"{stem}.asm"
        obj = BUILD / f"{stem}.o"
        subprocess.run(
            [
                "ca65",
                "-I", str(LIB_APPLE1),
                "-I", str(LIB_M6502),
                "-I", str(LIB_TMS),
                "-o", str(obj),
                str(asm),
            ],
            check=True,
            cwd=str(ROOT),
        )
        objs.append(str(obj))
    subprocess.run(
        ["ld65", "-C", str(CFG), "-o", str(BIN), *objs],
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
