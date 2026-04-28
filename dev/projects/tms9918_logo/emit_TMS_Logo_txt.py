#!/usr/bin/env python3
"""Assemble TMS_Logo (3 modules) and write TMS_Logo.txt.

Modules linked together (in this order so the entry point ends at $0280):
  TMS_Logo.asm    -- LOGO interpreter, REPL, turtle, errors, banner.
  math.asm        -- fixed-point trig, LFSR, decimal output, mod360.
                     (lib/m6502/)
  tms9918m2.asm   -- TMS9918 Mode-2 bitmap driver (init, plot, line_xy).
                     (lib/tms9918/)

Thin wrapper around dev/cc65/emit_woz.py.
"""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[1] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=[
            "TMS_Logo.asm",
            "lib/m6502/math.asm",
            "lib/tms9918/tms9918m2.asm",
        ],
        lib_dirs=["apple1", "m6502", "tms9918"],
        cfg="apple1_logo.cfg",
        out_dir_software="tms9918",
        start_addr=0x0280,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
