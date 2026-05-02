#!/usr/bin/env python3
"""Assemble TMS_Wozmon_demo and write TMS_Wozmon_demo.txt (Woz Monitor hex).

Thin wrapper around dev/cc65/emit_woz.py -- see that module for the shared
build logic. Multi-module project: the main .asm imports symbols from the
TMS9918 text driver, console runtime, and the silicon-strict helpers
under dev/lib/tms9918/, all of which need to be assembled and linked.
"""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[1] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=[
            "TMS_Wozmon_demo.asm",
            "lib/tms9918/tms9918_text.asm",
            "lib/tms9918/tms9918_console.asm",
            "lib/tms9918/tms9918_helpers.asm",
        ],
        lib_dirs=["apple1", "tms9918"],
        cfg="apple1_tms_wozmon.cfg",
        out_dir_software="tms9918",
        start_addr=0x0300,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
