#!/usr/bin/env python3
"""Assemble A1-IO RTC Clock and write RtcClock.txt — wraps emit_woz.py."""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[2] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=["RtcClock.asm"],
        lib_dirs=["apple1", "a1io"],
        cfg="apple1_4k.cfg",
        out_dir_software="a1io_rtc",
        start_addr=0x0280,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
