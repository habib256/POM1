#!/usr/bin/env python3
"""Assemble HGR_TestCard and write HGR_TestCard.txt — wraps emit_woz.py."""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[1] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=["HGR_TestCard.asm"],
        lib_dirs=["apple1", "hgr"],
        cfg="apple1_gen2.cfg",
        out_dir_software="Graphic HGR",
        start_addr=0xE000,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
