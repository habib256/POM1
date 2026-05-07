#!/usr/bin/env python3
"""Assemble TMS_SilTest and write TMS_SilTest.txt (Wozmon hex).

Modules linked (in this order so the entry point lands at $0280):
  TMS_SilTest.asm        -- main: silicon test runner + IRQ handler
  tms9918_text.asm       -- text-mode driver + 1 KB charmap (lib/tms9918/)
  tms9918_pad.asm        -- silicon-strict pad12/pad24/pad40 helpers

Thin wrapper around dev/cc65/emit_woz.py.
"""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[1] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=[
            "TMS_SilTest.asm",
            "lib/tms9918/tms9918_text.asm",
            "lib/tms9918/tms9918_pad.asm",
        ],
        lib_dirs=["apple1", "m6502", "tms9918"],
        cfg="apple1_siltest.cfg",
        out_dir_software="tms9918",
        start_addr=0x0280,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
