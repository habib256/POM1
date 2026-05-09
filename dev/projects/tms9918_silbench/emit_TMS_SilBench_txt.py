#!/usr/bin/env python3
"""Assemble TMS_SilBench and write TMS_SilBench.txt (Wozmon hex).

Modules linked (in this order so the entry point lands at $0280 or $4000):
  TMS_SilBench.asm          -- main: 29-test silicon benchmark runner
  lib/tms9918/tms9918m1.asm -- Mode-1 driver (init, clear_name_table, vdp_set_write)
  lib/tms9918/tms9918_5strigger.asm -- 5S mid-frame raster trap primitive
  lib/tms9918/tms9918_pad.asm       -- silicon-strict pad12 helper
                                       (auto-linked by emit_woz when source
                                        references tms9918_pad12)

Default build: stock 4 KB Apple-1 (apple1_silbench.cfg, load $0280).
For CodeTank ROM image: re-run `make codetank` — emits the same .bin
into software/tms9918/, the operator points build_codetank_rom.py at
it. See README.md.

Thin wrapper around dev/cc65/emit_woz.py.
"""
import os
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[1] / "cc65"))
from emit_woz import emit  # noqa: E402


def main() -> int:
    # Default to CodeTank cfg — silbench's 29-test scope overflows the
    # stock 4 KB Apple-1 layout. Override with SILBENCH_CFG env var if
    # the binary shrinks below 4 KB in the future.
    cfg = os.environ.get("SILBENCH_CFG", "apple1_silbench_codetank.cfg")
    if cfg.endswith("codetank.cfg"):
        start_addr = 0x4000
    else:
        start_addr = 0x0280
    emit(
        asm_files=[
            "TMS_SilBench.asm",
            "../../lib/tms9918/tms9918m1.asm",
            "../../lib/tms9918/tms9918_5strigger.asm",
        ],
        lib_dirs=["apple1", "tms9918"],
        cfg=cfg,
        out_dir_software="Graphic TMS9918",
        start_addr=start_addr,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
