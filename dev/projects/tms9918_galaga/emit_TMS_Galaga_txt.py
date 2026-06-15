#!/usr/bin/env python3
"""Assemble TMS_Galaga and write TMS_Galaga.txt (Woz Monitor hex).

Thin wrapper around dev/cc65/emit_woz.py — see that module for shared
build logic. Project-local configuration (asm sources, libs, cfg, output
location, load address) is captured below.
"""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[1] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=["TMS_Galaga.asm"],
        lib_dirs=["apple1", "m6502", "tms9918"],
        cfg="apple1_galaga_codetank_bank.cfg",
        out_dir_software="Graphic TMS9918",
        start_addr=0x4100,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
