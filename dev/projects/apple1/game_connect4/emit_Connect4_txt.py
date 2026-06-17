#!/usr/bin/env python3
"""Assemble Connect4 (text mode) and write Connect4.txt — wraps emit_woz.py."""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[2] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=["Connect4.asm"],
        lib_dirs=["apple1"],
        cfg="apple1_4k.cfg",
        out_dir_software="Apple-1 games",
        start_addr=0x0280,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
