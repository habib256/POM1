#!/usr/bin/env python3
"""Assemble TMS_Snake and write TMS_Snake.txt — wraps emit_woz.py."""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[1] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=["TMS_Snake.asm"],
        lib_dirs=["apple1", "m6502"],
        cfg="apple1_snake.cfg",
        out_dir_software="tms9918",
        start_addr=0x0280,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
