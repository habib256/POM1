#!/usr/bin/env python3
"""Assemble both maze generators and write their canonical .txt — wraps emit_woz.py.

Multi-binary project (Sidewinder + Recursive Backtracker), so this emits two
Wozmon-hex sidecars under software/Apple-1 games/. Both load + run at $0280.
"""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[2] / "cc65"))
from emit_woz import emit  # noqa: E402

MAZES = [
    ("Maze_Sidewinder.asm", [
        "// Maze Generator for Apple 1",
        "// Sidewinder Algorithm",
        "// 19x11 cells -> 40x24 display",
        "// Title screen + S=Start / E=End markers",
    ]),
    ("Maze2_Backtracker.asm", [
        "// Maze 2 - Generator for Apple 1",
        "// Recursive Backtracker (DFS) Algorithm",
        "// 19x11 cells -> 40x24 display",
        "// Title screen + S=Start / E=End markers",
    ]),
]


def main() -> int:
    for asm, header in MAZES:
        emit(
            asm_files=[asm],
            lib_dirs=["apple1", "m6502"],
            cfg="apple1_4k.cfg",
            out_dir_software="Apple-1 games",
            start_addr=0x0280,
            bytes_per_line=16,
            header_lines=header,
            project_dir=PROJ,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
