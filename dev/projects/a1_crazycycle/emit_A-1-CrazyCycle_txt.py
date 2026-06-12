#!/usr/bin/env python3
"""Assemble A-1-CrazyCycle and write A-1-CrazyCycle.txt — wraps emit_woz.py."""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[1] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=["A-1-CrazyCycle.asm"],
        lib_dirs=["apple1", "hgr"],
        cfg="apple1_gen2.cfg",
        out_dir_software="Graphic HGR",
        start_addr=0xE000,
        project_dir=PROJ,
        # Bundle the UBERNIE HGR picture as a second hex zone at $2000 so a
        # single .txt load fills the framebuffer AND installs the code (the
        # demo shows the picture instead of drawing a test card).
        extra_zones=[(0x2000, "sdcard/NONO/HGR/UBERNIE#062000")],
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
