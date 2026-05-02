#!/usr/bin/env python3
"""Assemble HGR8_BBFontShow and write HGR8_BBFontShow.txt — wraps emit_woz.py."""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[1] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=["HGR8_BBFontShow.asm"],
        lib_dirs=["apple1", "hgr", "m6502"],
        cfg="apple1_gen2.cfg",
        out_dir_software="hgr",
        start_addr=0xE000,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
