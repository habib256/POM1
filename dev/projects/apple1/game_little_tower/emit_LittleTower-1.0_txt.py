#!/usr/bin/env python3
"""Assemble Little Tower and write LittleTower-1.0.txt — wraps emit_woz.py."""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[2] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=["LittleTower-1.0.asm"],
        lib_dirs=["apple1"],
        cfg="apple1_little_tower.cfg",
        out_dir_software="Apple-1 games",
        start_addr=0x0280,
        bytes_per_line=16,
        header_lines=[
            "// Little Tower v1.0 - Apple 1 adventure game",
            "// Built from dev/projects/apple1/game_little_tower/ - load + run at $0280",
        ],
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
