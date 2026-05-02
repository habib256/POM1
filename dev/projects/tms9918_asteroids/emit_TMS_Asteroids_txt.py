#!/usr/bin/env python3
"""Assemble TMS_Asteroids and write TMS_Asteroids.txt (Wozmon hex).

Modules linked (in this order so the entry point lands at $0280):
  TMS_Asteroids.asm    -- game loop, ship state, input, motion integration
  math.asm             -- fixed-point trig, LFSR, mod360 (lib/m6502/)
  tms9918m2.asm        -- TMS9918 Mode-2 bitmap driver (lib/tms9918/)
  sprite_triangle.asm  -- rotating-triangle sprite rasterizer (lib/tms9918/)

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
            "TMS_Asteroids.asm",
            "lib/m6502/math.asm",
            "lib/tms9918/tms9918m2.asm",
            "lib/tms9918/sprite_triangle.asm",
        ],
        lib_dirs=["apple1", "m6502", "tms9918"],
        cfg="apple1_asteroids.cfg",
        out_dir_software="tms9918",
        start_addr=0x0280,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
