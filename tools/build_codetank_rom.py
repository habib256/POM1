#!/usr/bin/env python3
"""
Build P-LAB CodeTank ROM images for the TMS9918 graphic card.

Three ROMs are produced (in roms/codetank/):

  Codetank_GAME1.rom (32 kB)
    Lower 16 kB: menu → Galaga, Sokoban, Snake (run-in-place)
                 ($4000 menu / $4100 Galaga / $6000 Sokoban / $7400 Snake)
    Upper 16 kB: TMS_LOGO V2.6 interpreter (run-in-place from $4000)
    Jumper Lower → 4000R brings up the picker; jumper Upper → 4000R boots
    LOGO directly.

  Codetank_GAME2.rom (32 kB)
    Lower 16 kB: TMS_Rogue alone (full bank, run-in-place from $4000)
    Upper 16 kB: TMS_Chess alone (full bank, run-in-place from $4000)
    Jumper Lower → 4000R boots Rogue; jumper Upper → 4000R boots Chess.

  Codetank_GAME3.rom (32 kB)
    Lower 16 kB: TMS_Life alone (run-in-place from $4000)
    Upper 16 kB: empty (reserved for future expansion)

Each game's slot is sized to fit its current assembled binary (see the
.cfg `size = $xxxx` field). `slot()` enforces the boundary and prints a
clear deficit message if a game outgrows its slot.

Usage:
    python3 tools/build_codetank_rom.py            # build all 3
    python3 tools/build_codetank_rom.py --rom=1    # only GAME1
    python3 tools/build_codetank_rom.py --rom=2    # only GAME2
    python3 tools/build_codetank_rom.py --rom=3    # only GAME3
"""
from __future__ import annotations
import argparse
import pathlib
import shutil
import subprocess
import sys

ROOT  = pathlib.Path(__file__).resolve().parents[1]
TMS   = ROOT / "software" / "tms9918"
BUILD = ROOT / "build" / "codetank"

ROM_SIZE  = 0x8000   # 32 kB (28c256)
HALF_SIZE = 0x4000   # 16 kB

DEV               = ROOT / "dev" / "projects"
LIB_APPLE1        = ROOT / "dev" / "lib" / "apple1"
LIB_M6502         = ROOT / "dev" / "lib" / "m6502"
LIB_TMS           = ROOT / "dev" / "lib" / "tms9918"
LIB_SOKOBAN       = ROOT / "dev" / "lib" / "sokoban"
LIB_HGR           = ROOT / "dev" / "lib" / "hgr"
LIB_CHESS         = ROOT / "dev" / "lib" / "chess"

# --- GAME1 sources (menu + Galaga + Sokoban + Snake lower; LOGO upper) -----
MENU_ASM          = DEV / "tms9918_codetank_menu" / "codetank_menu.asm"
MENU_CFG          = DEV / "tms9918_codetank_menu" / "apple1_codetank_menu.cfg"
GALAGA_ASM        = DEV / "tms9918_galaga"        / "TMS_Galaga.asm"
GALAGA_BANK_CFG   = DEV / "tms9918_galaga"        / "apple1_galaga_codetank_bank.cfg"
SOKOBAN_ASM       = DEV / "tms9918_sokoban"       / "TMS_Sokoban.asm"
SOKOBAN_BANK_CFG  = DEV / "tms9918_sokoban"       / "apple1_sokoban_codetank_bank.cfg"
SNAKE_ASM         = DEV / "tms9918_snake"         / "TMS_Snake.asm"
SNAKE_BANK_CFG    = DEV / "tms9918_snake"         / "apple1_snake_codetank_bank.cfg"

# --- GAME3 sources (Life alone in lower bank) ------------------------------
LIFE_ASM          = DEV / "tms9918_life" / "TMS_Life.asm"
LIFE_FULL_CFG     = DEV / "tms9918_life" / "apple1_life_codetank_full.cfg"

# --- LOGO V2 (GAME1 upper) -------------------------------------------------
LOGO_V2_ASM        = DEV / "tms9918_logo" / "TMS_Logo_16k.asm"
LOGO_V2_BANK_CFG   = DEV / "tms9918_logo" / "apple1_logo_v2_codetank_bank.cfg"
LOGO_V2_MATH_ASM   = LIB_M6502   / "math.asm"
LOGO_V2_VDP_ASM    = LIB_TMS     / "tms9918m2.asm"
LOGO_V2_EMOTE_ASM  = LIB_TMS     / "sprites_emotes.asm"
LOGO_V2_TEXT_ASM   = LIB_TMS     / "text_bitmap.asm"
LOGO_V2_BUBBLE_ASM = LIB_TMS     / "bubble.asm"
LOGO_V2_BUFED_ASM  = LIB_TMS     / "buffer_editor.asm"
LOGO_V2_SPRH_ASM   = LIB_TMS     / "sprite_helpers.asm"

# --- GAME2 sources ---------------------------------------------------------
ROGUE_ASM          = DEV / "tms9918_rogue" / "TMS_Rogue.asm"
ROGUE_BOSS_ASM     = DEV / "tms9918_rogue" / "sprites_boss.asm"
ROGUE_CODETANK_CFG = DEV / "tms9918_rogue" / "apple1_rogue.cfg"
ROGUE_M1_ASM       = LIB_TMS / "tms9918m1.asm"

CHESS_ASM          = DEV / "tms9918_chess" / "TMS_Chess.asm"
CHESS_CODETANK_CFG = DEV / "tms9918_chess" / "apple1_chess_codetank.cfg"
CHESS_ENGINE_ASM   = LIB_CHESS / "chess_engine.asm"
CHESS_TEXT_IO_ASM  = LIB_CHESS / "chess_text_io.asm"
CHESS_M1_ASM       = LIB_TMS   / "tms9918m1.asm"


# ---------------------------------------------------------------------------
def need(tool: str) -> None:
    if shutil.which(tool) is None:
        raise SystemExit(
            f"{tool} not found in PATH. Install cc65 (apt: sudo apt install cc65)."
        )


def _common_includes(asm: pathlib.Path) -> list[str]:
    return [
        "-I", str(LIB_APPLE1), "-I", str(LIB_M6502), "-I", str(LIB_TMS),
        "-I", str(LIB_SOKOBAN), "-I", str(LIB_HGR), "-I", str(LIB_CHESS),
        "-I", str(asm.parent),
    ]


def _ca65(asm: pathlib.Path, obj: pathlib.Path,
          extra_args: list[str] | None = None) -> None:
    cmd = ["ca65", *_common_includes(asm), *(extra_args or []),
           "-o", str(obj), str(asm)]
    subprocess.run(cmd, check=True, cwd=str(ROOT))


def assemble(asm: pathlib.Path, cfg: pathlib.Path, name: str,
             max_size: int) -> bytes:
    """Assemble a single-source project, auto-link tms9918_pad.asm if used.
    Raises if the resulting binary exceeds `max_size`."""
    BUILD.mkdir(parents=True, exist_ok=True)
    obj = BUILD / f"{name}.o"
    binp = BUILD / f"{name}.bin"
    _ca65(asm, obj)

    objs = [obj]
    pad_asm = LIB_TMS / "tms9918_pad.asm"
    if pad_asm.is_file():
        try:
            t = asm.read_text(errors="ignore")
        except OSError:
            t = ""
        if "tms9918_pad12" in t or "tms9918_pad24" in t:
            pad_obj = BUILD / f"{name}_pad.o"
            _ca65(pad_asm, pad_obj)
            objs.append(pad_obj)

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


def assemble_multi(asms: list[pathlib.Path], cfg: pathlib.Path, name: str,
                   max_size: int,
                   extra_ca65_args: list[str] | None = None) -> bytes:
    """Multi-module variant for projects like LOGO V2 that link several
    .asm files together, plus auto tms9918_pad.asm linking."""
    BUILD.mkdir(parents=True, exist_ok=True)
    objs: list[pathlib.Path] = []
    needs_pad = False
    for asm in asms:
        obj = BUILD / f"{name}_{asm.stem}.o"
        _ca65(asm, obj, extra_args=extra_ca65_args)
        objs.append(obj)
        try:
            t = asm.read_text(errors="ignore")
        except OSError:
            t = ""
        if "tms9918_pad12" in t or "tms9918_pad24" in t:
            needs_pad = True
    pad_asm = LIB_TMS / "tms9918_pad.asm"
    if needs_pad and pad_asm.is_file() and pad_asm not in asms:
        pad_obj = BUILD / f"{name}_pad.o"
        _ca65(pad_asm, pad_obj)
        objs.append(pad_obj)
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


def slot(bank: bytearray, offset: int, data: bytes, slot_size: int,
         label: str) -> None:
    """Copy `data` into the bank at `offset`, log slot usage."""
    pad = slot_size - len(data)
    print(
        f"  {label:<32} {len(data):5d} B / {slot_size:5d} B slot "
        f"({len(data) * 100 / slot_size:5.1f}%, {pad:5d} B free)",
        file=sys.stderr,
    )
    if len(data) > slot_size:
        raise SystemExit(f"  ERROR: {label} overflows slot by {-pad} bytes")
    bank[offset:offset + len(data)] = data


# ---------------------------------------------------------------------------
# GAME1 — menu + Galaga + Sokoban + Snake (lower) + LOGO V2 (upper)
# ---------------------------------------------------------------------------
def assemble_game1_lower_binaries() -> tuple[bytes, bytes, bytes, bytes]:
    """Assemble the 4 lower-bank artifacts at their fixed bank-cfg offsets.
    Slot sizes come from each cfg's `size =` field; `slot()` enforces the
    boundary and prints a deficit if a game outgrows its slot."""
    print("[GAME1] Assembling lower-bank binaries:", file=sys.stderr)
    menu    = assemble(MENU_ASM,    MENU_CFG,         "G1_menu",    0x0100)
    galaga  = assemble(GALAGA_ASM,  GALAGA_BANK_CFG,  "G1_Galaga",  0x4000)
    sokoban = assemble(SOKOBAN_ASM, SOKOBAN_BANK_CFG, "G1_Sokoban", 0x4000)
    snake   = assemble(SNAKE_ASM,   SNAKE_BANK_CFG,   "G1_Snake",   0x4000)
    return menu, galaga, sokoban, snake


def build_game1_lower_bank() -> bytes:
    """Lower 16 kB layout — menu at $4000 dispatches to 3 games by entry
    address. Each game's bank cfg pins its start address; we copy each
    binary into its slot and verify it fits.

    May 2026 reshuffle: under the 24c silicon-strict contract Galaga grew
    past the old 7 936 B slot. Sokoban shifted +256 B and Snake +512 B to
    absorb the growth; the prior 512 B end-headroom is now part of Snake's
    slot. Update codetank_menu.asm:SOKOBAN_ENTRY/SNAKE_ENTRY and the three
    bank cfgs in lock-step if any of these offsets change again."""
    menu, galaga, sokoban, snake = assemble_game1_lower_binaries()
    print("[GAME1] Lower bank layout (menu + 3 games, run-in-place):",
          file=sys.stderr)
    # Slot offsets pinned by each game's bank cfg:
    #   menu     $4000-$40FF   ( 256 B, apple1_codetank_menu.cfg)
    #   Galaga   $4100-$60FF   (8 192 B, apple1_galaga_codetank_bank.cfg)
    #   Sokoban  $6100-$75FF   (5 376 B, apple1_sokoban_codetank_bank.cfg)
    #   Snake    $7600-$7FFF   (2 560 B, apple1_snake_codetank_bank.cfg)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, menu,    0x0100, "Menu      ($4000-$40FF)")
    slot(bank, 0x0100, galaga,  0x2000, "Galaga    ($4100-$60FF)")
    slot(bank, 0x2100, sokoban, 0x1500, "Sokoban   ($6100-$75FF)")
    slot(bank, 0x3600, snake,   0x0A00, "Snake     ($7600-$7FFF)")
    return bytes(bank)


def build_game1_upper_bank() -> bytes:
    """Upper 16 kB: TMS_LOGO V2.6 at $4000-$7FFF, runs in place from ROM."""
    print("\n[GAME1] Upper bank (TMS_LOGO V2.6, run-in-place):", file=sys.stderr)
    logo = assemble_multi(
        [LOGO_V2_ASM, LOGO_V2_MATH_ASM, LOGO_V2_VDP_ASM,
         LOGO_V2_EMOTE_ASM, LOGO_V2_TEXT_ASM, LOGO_V2_BUBBLE_ASM,
         LOGO_V2_BUFED_ASM, LOGO_V2_SPRH_ASM],
        LOGO_V2_BANK_CFG, "G1_LogoV2", HALF_SIZE,
        extra_ca65_args=["-D", "CODETANK_BUILD"])
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, logo, HALF_SIZE, "LOGO V2.6 ($4000-$7FFF)")
    return bytes(bank)


# ---------------------------------------------------------------------------
# GAME2 — Rogue (lower) + Chess (upper)
# ---------------------------------------------------------------------------
def build_game2_lower_bank() -> bytes:
    """Lower 16 kB: TMS_Rogue alone, full $4000-$7FFF, run-in-place."""
    print("[GAME2] Lower bank (TMS_Rogue alone, full 16 kB):", file=sys.stderr)
    rogue = assemble_multi(
        [ROGUE_ASM, ROGUE_M1_ASM, ROGUE_BOSS_ASM],
        ROGUE_CODETANK_CFG, "G2_Rogue", HALF_SIZE)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, rogue, HALF_SIZE, "Rogue     ($4000-$7FFF)")
    return bytes(bank)


def build_game2_upper_bank() -> bytes:
    """Upper 16 kB: TMS_Chess alone, full $4000-$7FFF (CODE+ENGINE merged)."""
    print("\n[GAME2] Upper bank (TMS_Chess alone, full 16 kB):", file=sys.stderr)
    chess = assemble_multi(
        [CHESS_ASM, CHESS_ENGINE_ASM, CHESS_TEXT_IO_ASM, CHESS_M1_ASM],
        CHESS_CODETANK_CFG, "G2_Chess", HALF_SIZE)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, chess, HALF_SIZE, "Chess     ($4000-$7FFF)")
    return bytes(bank)


# ---------------------------------------------------------------------------
# GAME3 — Life (lower) + empty (upper)
# ---------------------------------------------------------------------------
def build_game3_lower_bank() -> bytes:
    """Lower 16 kB: TMS_Life alone, run-in-place from $4000-$7FFF."""
    print("[GAME3] Lower bank (TMS_Life alone, full 16 kB):", file=sys.stderr)
    life = assemble(LIFE_ASM, LIFE_FULL_CFG, "G3_Life", HALF_SIZE)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, life, HALF_SIZE, "Life      ($4000-$7FFF)")
    return bytes(bank)


def build_game3_upper_bank() -> bytes:
    """Upper 16 kB: empty (filled with $FF). Reserved for future expansion."""
    print("\n[GAME3] Upper bank: empty ($FF fill, reserved)", file=sys.stderr)
    return b"\xFF" * HALF_SIZE


# ---------------------------------------------------------------------------
def build_game1() -> bytes:
    print("\n========== Codetank_GAME1.rom ==========", file=sys.stderr)
    lower = build_game1_lower_bank()
    upper = build_game1_upper_bank()
    rom = lower + upper
    assert len(rom) == ROM_SIZE
    return rom


def build_game2() -> bytes:
    print("\n========== Codetank_GAME2.rom ==========", file=sys.stderr)
    lower = build_game2_lower_bank()
    upper = build_game2_upper_bank()
    rom = lower + upper
    assert len(rom) == ROM_SIZE
    return rom


def build_game3() -> bytes:
    print("\n========== Codetank_GAME3.rom ==========", file=sys.stderr)
    lower = build_game3_lower_bank()
    upper = build_game3_upper_bank()
    rom = lower + upper
    assert len(rom) == ROM_SIZE
    return rom


def write_rom(rom: bytes, out: pathlib.Path, sidecar: str) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(rom)
    print(f"\n[CodeTank] Wrote {out}  ({len(rom)} bytes)", file=sys.stderr)
    sidecar_path = out.with_suffix(".txt")
    sidecar_path.write_text(sidecar, encoding="utf-8")
    print(f"           Sidecar: {sidecar_path}", file=sys.stderr)


SIDECAR_GAME1 = (
    "Codetank_GAME1.rom — TMS9918 P-LAB CodeTank cartridge\n"
    "  Lower jumper: 4000R → menu → 1=Galaga 2=Sokoban 3=Snake\n"
    "  Upper jumper: 4000R → TMS_LOGO V2.6 turtle interpreter\n"
)

SIDECAR_GAME2 = (
    "Codetank_GAME2.rom — TMS9918 P-LAB CodeTank cartridge\n"
    "  Lower jumper: 4000R → TMS_Rogue (dungeon crawler)\n"
    "  Upper jumper: 4000R → TMS_Chess (Mode-1 chess vs engine)\n"
)

SIDECAR_GAME3 = (
    "Codetank_GAME3.rom — TMS9918 P-LAB CodeTank cartridge\n"
    "  Lower jumper: 4000R → TMS_Life (Conway Game of Life)\n"
    "  Upper jumper: empty (reserved for future expansion)\n"
)


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument(
        "--rom", choices=("1", "2", "3", "all"), default="all",
        help="Which CodeTank ROM to build (default: all 3)",
    )
    args = ap.parse_args()

    need("ca65")
    need("ld65")

    out_dir = ROOT / "roms" / "codetank"

    if args.rom in ("1", "all"):
        rom1 = build_game1()
        write_rom(rom1, out_dir / "Codetank_GAME1.rom", SIDECAR_GAME1)

    if args.rom in ("2", "all"):
        rom2 = build_game2()
        write_rom(rom2, out_dir / "Codetank_GAME2.rom", SIDECAR_GAME2)

    if args.rom in ("3", "all"):
        rom3 = build_game3()
        write_rom(rom3, out_dir / "Codetank_GAME3.rom", SIDECAR_GAME3)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
