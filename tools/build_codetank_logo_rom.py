#!/usr/bin/env python3
"""
Build a 32 kB CodeTank ROM that pairs the existing lower-bank menu
(Galaga / Sokoban / Snake / Life) with a new upper-bank dispatcher
menu that lets the user pick between Tetris and LOGO V1.8.

Layout:

  Lower 16 kB ($0000-$3FFF in the file, CPU $4000-$7FFF when CodeTank
  jumper = Lower):
    -- copied verbatim from the existing galaga_sokoban_menu.rom --
    Menu (4 games) at $4000, Galaga / Sokoban / Snake / Life behind it.

  Upper 16 kB ($4000-$7FFF in the file, CPU $4000-$7FFF when jumper =
  Upper):
    $4000-$407F  UPPER MENU (1 = Tetris, 2 = LOGO)
    $4080-$5DAB  Tetris raw payload (unchanged, 7 308 B)
    $5DAC-$5DFF  $FF padding (84 B)
    $5E00-$xxxx  LOGO V1.8 (built with apple1_logo_codetank_bank.cfg)
    $xxxx-$7FFF  $FF padding

Wozmon entry (Upper jumper): 4000R → menu prints "1=TETRIS  2=LOGO"
and dispatches based on the keypress.

LOGO's variables sit in RAM right after the zero page+stack:
    $0000-$001F  ZP scalars (32 B)
    $0200-$027F  LBUF: line buffer + parser scratch (128 B)
    $0280-$06CF  PROC: proc table + save bufs + var_table + params (1 088 B)

Usage:
    python3 tools/build_codetank_logo_rom.py
    python3 tools/build_codetank_logo_rom.py -o roms/codetank/my.rom
"""
from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOGO_DIR = ROOT / "dev" / "projects" / "tms9918_logo"
LOGO_CFG = LOGO_DIR / "apple1_logo_codetank_bank.cfg"
MENU_DIR = ROOT / "dev" / "projects" / "tms9918_codetank_menu_upper"
MENU_CFG = MENU_DIR / "apple1_codetank_menu_upper.cfg"
LIB_APPLE1 = ROOT / "dev" / "lib" / "apple1"
LIB_M6502  = ROOT / "dev" / "lib" / "m6502"
LIB_TMS    = ROOT / "dev" / "lib" / "tms9918"

EXISTING_ROM = ROOT / "roms" / "codetank" / "galaga_sokoban_menu.rom"
DEFAULT_OUT  = ROOT / "roms" / "codetank" / "tetris_logo.rom"

ROM_SIZE  = 0x8000   # 32 kB (28c256)
HALF_SIZE = 0x4000   # 16 kB
MENU_OFFSET_IN_BANK = 0x0000  # CPU $4000 = upper-bank file offset $0000
MENU_FILE_OFFSET    = HALF_SIZE + MENU_OFFSET_IN_BANK   # = $4000
MENU_SLOT_SIZE      = 0x0080  # 128 B (Tetris payload starts at $4080)
LOGO_OFFSET_IN_BANK = 0x1E00  # CPU $5E00 = upper-bank file offset $1E00
LOGO_FILE_OFFSET    = HALF_SIZE + LOGO_OFFSET_IN_BANK  # = $5E00 in the 32 kB file


def need(tool: str) -> None:
    if shutil.which(tool) is None:
        sys.exit(f"{tool} not found in PATH (apt install cc65)")


def run(cmd: list[str], cwd: pathlib.Path) -> None:
    subprocess.run(cmd, check=True, cwd=str(cwd))


def build_upper_menu() -> bytes:
    """Assemble the upper-bank dispatcher menu (≤128 B at $4000)."""
    print("[CodeTank] Assembling upper-bank menu (CODE @ $4000)...",
          file=sys.stderr)
    obj = MENU_DIR / "menu_upper.o"
    out = MENU_DIR / "codetank_menu_upper.bin"
    run(["ca65", "-o", str(obj), str(MENU_DIR / "codetank_menu_upper.asm")],
        cwd=ROOT)
    run(["ld65", "-C", str(MENU_CFG), "-o", str(out), str(obj)], cwd=ROOT)
    data = out.read_bytes()
    print(f"  upper menu: {len(data)} bytes / {MENU_SLOT_SIZE} B slot",
          file=sys.stderr)
    if len(data) > MENU_SLOT_SIZE:
        sys.exit(
            f"upper menu is {len(data)} B, exceeds the 128 B slot before "
            f"the Tetris payload at $4080"
        )
    try:
        obj.unlink()
    except FileNotFoundError:
        pass
    return data


def build_logo_codetank() -> bytes:
    """Assemble + link LOGO with CODE at $5E00. Returns the raw bytes."""
    print("[CodeTank] Assembling LOGO (CODE @ $5E00)...", file=sys.stderr)
    objs = []
    for asm_path, name in [
        (LOGO_DIR / "TMS_Logo.asm", "TMS_Logo_ct"),
        (LIB_M6502 / "math.asm",    "math_ct"),
        (LIB_TMS   / "tms9918m2.asm", "tms9918m2_ct"),
    ]:
        obj = LOGO_DIR / f"{name}.o"
        run(
            ["ca65",
             "-I", str(LIB_APPLE1),
             "-I", str(LIB_M6502),
             "-I", str(LIB_TMS),
             "-o", str(obj),
             str(asm_path)],
            cwd=ROOT,
        )
        objs.append(obj)
    out_bin = LOGO_DIR / "TMS_Logo_codetank.bin"
    run(
        ["ld65", "-C", str(LOGO_CFG), "-o", str(out_bin)] +
        [str(o) for o in objs],
        cwd=ROOT,
    )
    data = out_bin.read_bytes()
    print(f"  LOGO ROM image: {len(data)} bytes", file=sys.stderr)
    if len(data) > HALF_SIZE - LOGO_OFFSET_IN_BANK:
        sys.exit(
            f"LOGO is {len(data)} B, exceeds the upper-bank LOGO slot of "
            f"{HALF_SIZE - LOGO_OFFSET_IN_BANK} B starting at $5E00"
        )
    # cleanup obj files
    for o in objs:
        try:
            o.unlink()
        except FileNotFoundError:
            pass
    return data


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-o", "--output", type=pathlib.Path, default=DEFAULT_OUT)
    ap.add_argument(
        "--base", type=pathlib.Path, default=EXISTING_ROM,
        help=f"32 kB base ROM to splice LOGO into (default: {EXISTING_ROM})",
    )
    args = ap.parse_args()
    need("ca65")
    need("ld65")

    if not args.base.is_file():
        sys.exit(
            f"Base ROM {args.base} not found. Run "
            f"`python3 tools/build_codetank_rom.py` first."
        )
    base = bytearray(args.base.read_bytes())
    if len(base) != ROM_SIZE:
        sys.exit(f"{args.base}: expected {ROM_SIZE} bytes, got {len(base)}")

    menu = build_upper_menu()
    logo = build_logo_codetank()

    # Splice the new upper menu over the old Tetris auto-loader at $4000.
    # Pad the unused portion of the 128 B slot with $FF so older content
    # doesn't leak through.
    base[MENU_FILE_OFFSET:MENU_FILE_OFFSET + MENU_SLOT_SIZE] = (
        b"\xFF" * MENU_SLOT_SIZE
    )
    base[MENU_FILE_OFFSET:MENU_FILE_OFFSET + len(menu)] = menu

    # Splice LOGO bytes at file offset $5E00 (upper bank, $5E00 CPU).
    end = LOGO_FILE_OFFSET + len(logo)
    if end > ROM_SIZE:
        sys.exit("LOGO would overflow the ROM; abort")
    base[LOGO_FILE_OFFSET:end] = logo
    # Pad after LOGO with $FF up to end of bank (fresh, no stale bytes
    # from a previous-image variant).
    base[end:ROM_SIZE] = b"\xFF" * (ROM_SIZE - end)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(bytes(base))
    print(
        f"\n[CodeTank] Wrote {args.output} ({len(base)} bytes)",
        file=sys.stderr,
    )
    print(
        "  Wozmon entry (Upper jumper): 4000R → menu (1=Tetris, 2=LOGO)",
        file=sys.stderr,
    )

    sidecar = args.output.with_suffix(".txt")
    sidecar.write_text(
        "TMS9918: lower jumper = games menu (Galaga/Sokoban/Snake/Life). "
        "Upper jumper = upper menu at 4000R (1=Tetris, 2=LOGO V1.8).\n",
        encoding="ascii",
    )
    print(f"  Sidecar: {sidecar}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
