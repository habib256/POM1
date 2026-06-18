#!/usr/bin/env python3
"""Assemble GT-6144 Life and write GT1_Life.txt — wraps emit_woz.py."""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[2] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=["GT1_Life.asm"],
        lib_dirs=["apple1", "gt6144"],
        cfg="gt6144.cfg",
        out_dir_software="Graphic gt-6144",
        start_addr=0x0300,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
