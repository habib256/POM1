#!/usr/bin/env python3
"""Assemble Sokoban (text mode, 4K variant) and write Sokoban.txt — wraps emit_woz.py."""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[2] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=["Sokoban.asm"],
        lib_dirs=["apple1", "games/sokoban"],
        cfg="apple1_sok_4k.cfg",
        out_dir_software="Apple-1 games",
        start_addr=0x0280,
        bytes_per_line=16,
        header_lines=[
            "// Sokoban (text mode) for Apple 1 - stock 4K compatible (apple1_sok_4k.cfg)",
            "// Built from dev/projects/apple1/game_sokoban/ - load + run at $0280",
        ],
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
