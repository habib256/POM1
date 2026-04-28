#!/usr/bin/env python3
"""Assemble HGR10_Life and write HGR10_Life.txt — wraps emit_woz.py."""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[1] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=["HGR10_Life.asm"],
        lib_dirs=["apple1", "hgr"],
        cfg="apple1_gen2.cfg",
        out_dir_software="hgr",
        start_addr=0x0280,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
