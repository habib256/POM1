#!/usr/bin/env python3
"""Assemble HGR_Bestiary and write the Wozmon hex dump."""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[1] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    emit(
        asm_files=["HGR_Bestiary.asm"],
        lib_dirs=["apple1", "hgr", "hgr/sprites"],
        cfg="apple1_gen2.cfg",
        out_dir_software="Graphic HGR",
        start_addr=0xE000,
        header_lines=[
            "// HGR Bestiary browser -- 6 SCROLL-O-SPRITES categories",
            "//   CREATURES TROLLKIND UNLIVING FAUNA MAGICK MUSIC (54 sprites)",
            "//   SPACE = next category, ESC = quit. Run: E000R",
        ],
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
