#!/usr/bin/env python3
"""Build the two A-1-CrazyCycle variants into software/Graphic HGR/.

Both bundle the UBERNIE HGR picture as a second hex zone at $2000, so each
single .txt carries code ($E000) + image — load it and `E000R`.

  A-1-CrazyCycle.txt          FANTASY  — GEN2 HGR on a 1:1 / SRAM (Briel) host
                              or POM1 with DRAM refresh OFF. 65 CPU cycles per
                              scanline (1 CPU cycle == 1 video cycle).

  A-1-CrazyCycle-SILICON.txt  SILICON  — GEN2 HGR on a real DRAM Apple-1 or
                              POM1 with DRAM refresh ON. 61 CPU cycles + 4
                              hardware refresh stalls = 65 beam cycles per
                              scanline, so the beam-raced window stays put.
                              Built with `ca65 -D SILICON`.

Run from anywhere: `python3 build.py`.
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]                      # repo root (sketchs/gen2/<demo>/..)
sys.path.insert(0, str(ROOT / "dev" / "cc65"))
from emit_woz import emit                    # noqa: E402

ASSET = ROOT / "sdcard" / "NONO" / "HGR" / "UBERNIE#062000"
OUTDIR = ROOT / "software" / "Graphic HGR"


def variant(out_txt, asflags, label):
    emit(
        asm_files=["A-1-CrazyCycle.asm"],
        lib_dirs=["apple1", "gen2"],
        cfg="apple1_gen2.cfg",
        out_txt=OUTDIR / out_txt,
        start_addr=0xE000,
        extra_zones=[(0x2000, ASSET)],
        project_dir=str(HERE),
        asflags=asflags,
        header_lines=[f"// A-1-CrazyCycle ({label}) - GEN2 HGR beam-race demo"],
    )


variant("A-1-CrazyCycle.txt", (), "FANTASY: 65 CPU cycles/scanline, 1:1 host")
variant("A-1-CrazyCycle-SILICON.txt", ["-D", "SILICON"],
        "SILICON: 61 CPU + 4 refresh = 65 beam, real DRAM / refresh-on")
