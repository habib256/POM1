#!/usr/bin/env python3
"""Assemble HGR_SymbolsShow and write the Wozmon hex dump."""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[1] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=["HGR_SymbolsShow.asm"],
        lib_dirs=["apple1", "hgr"],
        cfg="apple1_gen2.cfg",
        out_dir_software="hgr",
        start_addr=0xE000,
        header_lines=[
            "// HGR Symbols showcase (sprites_symbols_hgr.inc, 23 sprites)",
        ],
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
