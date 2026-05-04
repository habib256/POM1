#!/usr/bin/env python3
"""
Build a 32 kB P-LAB CodeTank ROM image populated with the second wave
of TMS9918 games (GAMES2). Sister script to build_codetank_rom.py
which carries the original four-game lineup (Codetank_GAME1.rom).

Default layout (single ROM file, both halves used):

  Lower 16 kB ($4000-$7FFF when CodeTank board jumper = Lower):
    $4000-$xxxx  TMS_Rogue (linked at $4000, run-in-place from ROM)
    rest         padded with $FF.

  Upper 16 kB ($4000-$7FFF when CodeTank board jumper = Upper):
    Reserved for future MVPs (variant overworld, second roguelike).
    Currently $FF-padded.

The cartridge runs from a P-LAB Apple-1 with the standard Parmigiani
dual-bank RAM (low $0000-$0FFF + high $E000-$EFFF) — Rogue's map
buffer (768 B) lives in the high bank at $E000-$E2FF.

Usage:
    python3 tools/build_games2_rom.py
    python3 tools/build_games2_rom.py -o roms/codetank/Codetank_GAMES2_dev.rom
"""
from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys

ROOT  = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "codetank_games2"

ROM_SIZE  = 0x8000   # 32 kB (28c256 EEPROM)
HALF_SIZE = 0x4000   # 16 kB

DEV = ROOT / "dev" / "projects"
ROGUE_ASM = DEV / "tms9918_rogue" / "TMS_Rogue.asm"
ROGUE_CFG = DEV / "tms9918_rogue" / "apple1_rogue.cfg"

LIB_APPLE1 = ROOT / "dev" / "lib" / "apple1"
LIB_M6502  = ROOT / "dev" / "lib" / "m6502"
LIB_TMS    = ROOT / "dev" / "lib" / "tms9918"
TMS_M1_ASM  = LIB_TMS / "tms9918m1.asm"
TMS_PAD_ASM = LIB_TMS / "tms9918_pad.asm"

DEFAULT_OUT = ROOT / "roms" / "codetank" / "Codetank_GAMES2.rom"


def need(tool: str) -> None:
    if shutil.which(tool) is None:
        raise SystemExit(
            f"{tool} not found in PATH. Install cc65 (Debian/Ubuntu: "
            f"`sudo apt install cc65`)."
        )


def assemble_multi(asms: list[pathlib.Path], cfg: pathlib.Path,
                   name: str, max_size: int) -> bytes:
    """Assemble each asm file separately, link the bundle with `cfg`."""
    BUILD.mkdir(parents=True, exist_ok=True)
    objs: list[pathlib.Path] = []
    for asm in asms:
        obj = BUILD / f"{name}_{asm.stem}.o"
        subprocess.run(
            ["ca65",
             "-I", str(LIB_APPLE1),
             "-I", str(LIB_M6502),
             "-I", str(LIB_TMS),
             "-I", str(asm.parent),
             "-o", str(obj), str(asm)],
            check=True, cwd=str(ROOT),
        )
        objs.append(obj)
    binp = BUILD / f"{name}.bin"
    subprocess.run(
        ["ld65", "-C", str(cfg), "-o", str(binp), *map(str, objs)],
        check=True, cwd=str(ROOT),
    )
    data = binp.read_bytes()
    if not data:
        raise SystemExit(f"{name}: ld65 produced an empty binary")
    if len(data) > max_size:
        raise SystemExit(
            f"{name}: code is {len(data)} B, exceeds slot of {max_size} B"
        )
    return data


def build_lower_bank() -> bytes:
    """Lower 16 kB: TMS_Rogue at $4000, $FF-padded."""
    print("[GAMES2] Lower bank (TMS_Rogue, run-in-place):", file=sys.stderr)
    rogue = assemble_multi(
        [ROGUE_ASM, TMS_M1_ASM, TMS_PAD_ASM],
        ROGUE_CFG, "TMS_Rogue", HALF_SIZE,
    )
    bank = bytearray(b"\xFF" * HALF_SIZE)
    bank[: len(rogue)] = rogue
    pct = len(rogue) * 100.0 / HALF_SIZE
    print(f"  TMS_Rogue        {len(rogue):5d} B / {HALF_SIZE:5d} B slot "
          f"({pct:5.1f}%, {HALF_SIZE - len(rogue):5d} B padding)",
          file=sys.stderr)
    return bytes(bank)


def build_upper_bank() -> bytes:
    """Upper 16 kB: reserved for future MVPs, currently $FF-padded."""
    print("\n[GAMES2] Upper bank (reserved, $FF-padded):", file=sys.stderr)
    print(f"  (empty)              0 B / {HALF_SIZE:5d} B slot "
          f"(  0.0%, {HALF_SIZE:5d} B padding)",
          file=sys.stderr)
    return b"\xFF" * HALF_SIZE


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-o", "--output", type=pathlib.Path,
                        default=DEFAULT_OUT,
                        help=f"output ROM path (default: {DEFAULT_OUT})")
    args = parser.parse_args()

    need("ca65")
    need("ld65")

    lower = build_lower_bank()
    upper = build_upper_bank()
    rom = lower + upper
    if len(rom) != ROM_SIZE:
        raise SystemExit(f"ROM size mismatch: {len(rom)} B != {ROM_SIZE}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(rom)
    print(f"\n[GAMES2] Wrote {args.output} ({len(rom)} B = "
          f"{len(rom) // 1024} KB)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
