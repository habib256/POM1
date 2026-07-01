#!/usr/bin/env python3
"""Rebuild the standalone $0280 TMS9918 programs' Wozmon-hex .txt artefacts.

These are the load-at-$0280 "run from the Woz Monitor (280R)" builds shipped
under software/Graphic TMS9918/ — distinct from the CodeTank ROM builds (which
run in place at $4000 and are produced by tools/build_codetank_rom.py from the
same sources).

Each program is assembled from its current source under sketchs/tms9918/<dir>/
with the current dev/lib/tms9918 (pad18) libraries and linked with the $0280
linker cfg co-located next to the source. tms9918_pad.asm is auto-linked by
emit_woz whenever a source references tms9918_pad18/24/40.

Usage:
    python3 sketchs/tms9918/build_standalone_txt.py

Requires cc65 (ca65/ld65) on PATH. Outputs land in software/Graphic TMS9918/.

NOTE: TMS_Galaga is CodeTank-only ($4100). TMS_Maze3D / TMS_OrbitalPool /
TMS_SilBench / TMS_Stars / TMS_Nyan_Fantasy have no in-tree source any more
(their dev/projects/ sources were removed in commit 686fe03 and never migrated
to sketchs/), so their shipped .txt cannot be regenerated here.
"""
from __future__ import annotations
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SK = ROOT / "sketchs" / "tms9918"
sys.path.insert(0, str(ROOT / "dev" / "cc65"))
from emit_woz import emit  # noqa: E402

SOFT = ROOT / "software" / "Graphic TMS9918"

# (project_subdir, [asm sources], [lib include dirs], cfg, start, txt stem)
JOBS = [
    ("game_snake",  ["TMS_Snake.asm"],
     ["apple1", "m6502", "tms9918"], "apple1_snake.cfg", 0x0280, "TMS_Snake"),
    ("demo_mandel", ["TMS_Mandel.asm", "lib/tms9918/tms9918m2.asm"],
     ["apple1", "tms9918"], "apple1_mandel.cfg", 0x0280, "TMS_Mandel"),
    ("demo_plasma", ["TMS_Plasma.asm", "lib/tms9918/tms9918m1.asm"],
     ["apple1", "tms9918"], "apple1_plasma.cfg", 0x0280, "TMS_Plasma"),
    ("demo_life",   ["TMS_Life.asm"],
     ["apple1", "tms9918"], "apple1_life.cfg", 0x0280, "TMS_Life"),
    ("demo_vague",  ["TMS_Vague.asm", "lib/tms9918/tms9918m1.asm"],
     ["apple1", "tms9918"], "apple1_vague.cfg", 0x0280, "TMS_Vague"),
    ("demo_split",  ["TMS_Split.asm", "lib/tms9918/tms9918m1.asm",
                     "lib/tms9918/tms9918_5strigger.asm"],
     ["apple1", "tms9918"], "apple1_split.cfg", 0x0280, "TMS_Split"),
    ("tool_logo",   ["TMS_Logo_16k.asm", "lib/m6502/math.asm",
                     "lib/tms9918/tms9918m2.asm", "lib/tms9918/sprites_emotes.asm",
                     "lib/tms9918/text_bitmap.asm", "lib/tms9918/bubble.asm",
                     "lib/tms9918/buffer_editor.asm", "lib/tms9918/sprite_helpers.asm"],
     ["apple1", "m6502", "tms9918"], "apple1_logo_16k.cfg", 0x0280, "TMS_Logo_16k"),
    ("game_sokoban", ["TMS_Sokoban.asm"],
     ["apple1", "games/sokoban", "tms9918"], "apple1_sokoban_8k.cfg", 0x0280,
     "TMS_Sokoban"),
]


def _existing_header(stem: str) -> list[str]:
    """Preserve any leading // comment header from the committed .txt."""
    p = SOFT / (stem + ".txt")
    out: list[str] = []
    if not p.exists():
        return out
    for line in p.read_text().splitlines():
        if line.startswith("//"):
            out.append(line)
        else:
            break
    return out


def main() -> int:
    for sub, asms, libs, cfg, start, stem in JOBS:
        emit(
            asm_files=asms,
            lib_dirs=libs,
            cfg=cfg,
            out_dir_software="Graphic TMS9918",
            start_addr=start,
            header_lines=_existing_header(stem),
            project_dir=SK / sub,
            quiet=False,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
