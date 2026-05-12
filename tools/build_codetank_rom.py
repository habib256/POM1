#!/usr/bin/env python3
"""
Build P-LAB CodeTank ROM images for the TMS9918 graphic card.

Four ROMs are produced (in roms/codetank/):

  Codetank_GAME1.rom (32 kB)
    Lower 16 kB: menu → Galaga, Sokoban, Snake (run-in-place)
                 ($4000 menu / $4100 Galaga / $6200 Sokoban / $7600 Snake)
    Upper 16 kB: TMS_LOGO V2.6 interpreter (run-in-place from $4000)
    Jumper Lower → 4000R brings up the picker; jumper Upper → 4000R boots
    LOGO directly.

  Codetank_GAME2.rom (32 kB)
    Lower 16 kB: TMS_Rogue alone (full bank, run-in-place from $4000)
    Upper 16 kB: TMS_Nyan_CodeTank (full bank, run-in-place from $4000)
                 — drop-in image from
                 software/Apple-1_TMS_CC65/TMS_Nyan_CodeTank.bin.
    Jumper Lower → 4000R boots Rogue; jumper Upper → 4000R animates Nyan.

  Codetank_GAME3.rom (32 kB)
    Lower 16 kB: TMS_Tetris/CodeTank (full bank, run-in-place from $4000)
                 — drop-in image from
                 software/Apple-1_TMS_CC65/tetris_codetank.bin.
    Upper 16 kB: menu → Life, Mandel, Plasma (run-in-place)
                 ($4000 menu / $4100 Life / $4900 Mandel / $5100 Plasma)
    Jumper Lower → 4000R boots Tetris; jumper Upper → 4000R brings up the
    picker.

  Codetank_TEST.rom (32 kB)
    Lower 16 kB: TMS_SilBench (full bank, run-in-place from $4000)
                 — silicon-validation 29-test benchmark suite.
    Upper 16 kB: menu → Clone, Split (run-in-place)
                 ($4000 menu / $4100 Clone / $4500 Split)
    Jumper Lower → 4000R boots SilBench; jumper Upper → 4000R brings up
    the test picker.

  Codetank_GAME4.rom (32 kB)
    Lower 16 kB: TMS_LightCorridor (full bank, run-in-place from $4000)
                 — wireframe perspective tunnel + paddle + ball, 3
                 difficulty levels. Original implementation inspired by
                 the Light Corridor (Infogrames 1990) concept.
    Upper 16 kB: reserved ($FF fill — future expansion).
    Jumper Lower → 4000R boots Light Corridor.

Each game's slot is sized to fit its current assembled binary (see the
.cfg `size = $xxxx` field). `slot()` enforces the boundary and prints a
clear deficit message if a game outgrows its slot.

Usage:
    python3 tools/build_codetank_rom.py            # build all 5
    python3 tools/build_codetank_rom.py --rom=1    # only GAME1
    python3 tools/build_codetank_rom.py --rom=2    # only GAME2
    python3 tools/build_codetank_rom.py --rom=3    # only GAME3
    python3 tools/build_codetank_rom.py --rom=4    # only GAME4
    python3 tools/build_codetank_rom.py --rom=test # only TEST
"""
from __future__ import annotations
import argparse
import pathlib
import shutil
import subprocess
import sys

ROOT  = pathlib.Path(__file__).resolve().parents[1]
# cc65 / apple1-videocard-lib drop-ins (16 KiB @ $4000) land here.
CODETANK_CC65_BIN = ROOT / "software" / "Apple-1_TMS_CC65"
BUILD = ROOT / "build" / "codetank"

ROM_SIZE  = 0x8000   # 32 kB (28c256)
HALF_SIZE = 0x4000   # 16 kB

DEV               = ROOT / "dev" / "projects"
LIB_APPLE1        = ROOT / "dev" / "lib" / "apple1"
LIB_M6502         = ROOT / "dev" / "lib" / "m6502"
LIB_TMS           = ROOT / "dev" / "lib" / "tms9918"
LIB_SOKOBAN       = ROOT / "dev" / "lib" / "games" / "sokoban"
LIB_HGR           = ROOT / "dev" / "lib" / "hgr"
LIB_CHESS         = ROOT / "dev" / "lib" / "games" / "chess"

# --- GAME1 sources (menu + Galaga + Sokoban + Snake lower; LOGO upper) -----
MENU_ASM          = DEV / "tms9918_codetank_menu" / "codetank_menu.asm"
MENU_CFG          = DEV / "tms9918_codetank_menu" / "apple1_codetank_menu.cfg"
GALAGA_ASM        = DEV / "tms9918_galaga"        / "TMS_Galaga.asm"
GALAGA_BANK_CFG   = DEV / "tms9918_galaga"        / "apple1_galaga_codetank_bank.cfg"
SOKOBAN_ASM       = DEV / "tms9918_sokoban"       / "TMS_Sokoban.asm"
SOKOBAN_BANK_CFG  = DEV / "tms9918_sokoban"       / "apple1_sokoban_codetank_bank.cfg"
SNAKE_ASM         = DEV / "tms9918_snake"         / "TMS_Snake.asm"
SNAKE_BANK_CFG    = DEV / "tms9918_snake"         / "apple1_snake_codetank_bank.cfg"

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

# --- GAME2 sources (Rogue lower; Nyan/CodeTank prebuilt upper) -------------
ROGUE_ASM          = DEV / "tms9918_rogue" / "TMS_Rogue.asm"
ROGUE_BOSS_ASM     = DEV / "tms9918_rogue" / "sprites_boss.asm"
ROGUE_CODETANK_CFG = DEV / "tms9918_rogue" / "apple1_rogue.cfg"
ROGUE_M1_ASM       = LIB_TMS / "tms9918m1.asm"

# Nyan/CodeTank (GAME2 upper) — drop-in 16 KB image. The .bin in
# software/Apple-1_TMS_CC65/ is assembled by the project's own
# Makefile (cf. dev/projects/tms9918_nyan_codetank/) at $4000 and pads
# itself to fill the bank; we splat it verbatim into the upper half.
NYAN_CT_BIN       = CODETANK_CC65_BIN / "TMS_Nyan_CodeTank.bin"

# --- GAME3 sources (Tetris/CodeTank lower; menu+Life+Mandel+Plasma upper) --
# Tetris/CodeTank — drop-in 16 KB image (full upper-bank-style binary,
# no source / no assembly: we splat the bytes into the lower bank).
TETRIS_CT_BIN     = CODETANK_CC65_BIN / "tetris_codetank.bin"

# GAME3 upper menu + 3 demo programs (Life, Mandel, Plasma) sharing the
# upper bank via the menu pattern.
GAME3_MENU_ASM    = DEV / "tms9918_codetank_game3_menu" / "codetank_game3_menu.asm"
GAME3_MENU_CFG    = DEV / "tms9918_codetank_game3_menu" / "apple1_codetank_game3_menu.cfg"

LIFE_ASM          = DEV / "tms9918_life"   / "TMS_Life.asm"
LIFE_BANK_CFG     = DEV / "tms9918_life"   / "apple1_life_codetank_game3_bank.cfg"

MANDEL_ASM        = DEV / "tms9918_mandel" / "TMS_Mandel.asm"
MANDEL_BANK_CFG   = DEV / "tms9918_mandel" / "apple1_mandel_codetank_bank.cfg"
MANDEL_VDP_ASM    = LIB_TMS / "tms9918m2.asm"

PLASMA_ASM        = DEV / "tms9918_plasma" / "TMS_Plasma.asm"
PLASMA_BANK_CFG   = DEV / "tms9918_plasma" / "apple1_plasma_codetank_bank.cfg"
PLASMA_VDP_ASM    = LIB_TMS / "tms9918m1.asm"

# --- TEST sources (SilBench lower; menu+Clone+Split upper) -----------------
# Lower bank = TMS_SilBench (29-test silicon benchmark suite, canonical
# regression fixture for the May 2026 silicon model).
SILBENCH_ASM        = DEV / "tms9918_silbench" / "TMS_SilBench.asm"
SILBENCH_BANK_CFG   = DEV / "tms9918_silbench" / "apple1_silbench_codetank.cfg"
SILBENCH_M1_ASM     = LIB_TMS / "tms9918m1.asm"
SILBENCH_5S_ASM     = LIB_TMS / "tms9918_5strigger.asm"

# Upper bank = menu + Clone + Split (two TMS9918 silicon-bug mini-tests).
TEST_MENU_ASM       = DEV / "tms9918_codetank_test_menu" / "codetank_test_menu.asm"
TEST_MENU_CFG       = DEV / "tms9918_codetank_test_menu" / "apple1_codetank_test_menu.cfg"

CLONE_ASM           = DEV / "tms9918_clone" / "TMS_Clone.asm"
CLONE_BANK_CFG      = DEV / "tms9918_clone" / "apple1_clone_codetank_bank.cfg"
CLONE_VDP_ASM       = LIB_TMS / "tms9918m2.asm"

SPLIT_ASM           = DEV / "tms9918_split" / "TMS_Split.asm"
SPLIT_BANK_CFG      = DEV / "tms9918_split" / "apple1_split_codetank_bank.cfg"
SPLIT_M1_ASM        = LIB_TMS / "tms9918m1.asm"
SPLIT_5S_ASM        = LIB_TMS / "tms9918_5strigger.asm"

# --- GAME4 sources (Light Corridor — full lower 16 kB; upper reserved) -----
LIGHT_CORRIDOR_ASM     = DEV / "tms9918_light_corridor" / "TMS_LightCorridor.asm"
LIGHT_CORRIDOR_BANK_CFG = (
    DEV / "tms9918_light_corridor" / "apple1_light_corridor_codetank_bank.cfg"
)
LIGHT_CORRIDOR_VDP_ASM = LIB_TMS / "tms9918m2.asm"


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
        if ("tms9918_pad12" in t
                or "tms9918_pad24" in t
                or "tms9918_pad40" in t):
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
        if ("tms9918_pad12" in t
                or "tms9918_pad24" in t
                or "tms9918_pad40" in t):
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


def splat_full_bank(bin_path: pathlib.Path, label: str) -> bytes:
    """Read a drop-in binary (assembled at $4000 by its own project) and
    return a 16 KB bank with the binary at offset 0 and $FF fill for the
    rest. Used for prebuilt full-bank programs (Nyan/CodeTank,
    Tetris/CodeTank) where there's no Makefile in build_codetank_rom.py.
    Accepts inputs ≤ HALF_SIZE; they get padded to fill the bank."""
    if not bin_path.is_file():
        raise SystemExit(f"  ERROR: {bin_path} not found — cannot build {label}")
    data = bin_path.read_bytes()
    if len(data) > HALF_SIZE:
        raise SystemExit(
            f"  ERROR: {bin_path} is {len(data)} B, exceeds {HALF_SIZE} B "
            f"half-bank")
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, data, HALF_SIZE, label)
    return bytes(bank)


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

    Slot offsets pinned by each game's bank cfg:
      menu     $4000-$40FF   ( 256 B, apple1_codetank_menu.cfg)
      Galaga   $4100-$61FF   (8 448 B, apple1_galaga_codetank_bank.cfg)
      Sokoban  $6200-$75FF   (5 120 B, apple1_sokoban_codetank_bank.cfg)
      Snake    $7600-$7FFF   (2 560 B, apple1_snake_codetank_bank.cfg)
    May 2026 v2 reshuffle: cross-JSR strict-mode pads pushed Galaga past
    the previous 8 192 B slot, so shifted Sokoban entry +256 B (matching
    codetank_menu.asm's SOKOBAN_ENTRY = $6200)."""
    menu, galaga, sokoban, snake = assemble_game1_lower_binaries()
    print("[GAME1] Lower bank layout (menu + 3 games, run-in-place):",
          file=sys.stderr)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, menu,    0x0100, "Menu      ($4000-$40FF)")
    slot(bank, 0x0100, galaga,  0x2100, "Galaga    ($4100-$61FF)")
    slot(bank, 0x2200, sokoban, 0x1400, "Sokoban   ($6200-$75FF)")
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
# GAME2 — Rogue (lower) + Nyan/CodeTank (upper)
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
    """Upper 16 kB: TMS_Nyan_CodeTank prebuilt binary, full $4000-$7FFF.
    Drop-in image from software/Apple-1_TMS_CC65/ — assembled by
    dev/projects/tms9918_nyan_codetank's own Makefile and padded to fill
    the bank."""
    print("\n[GAME2] Upper bank (TMS_Nyan_CodeTank, full 16 kB):",
          file=sys.stderr)
    return splat_full_bank(NYAN_CT_BIN, "Nyan/CT   ($4000-$7FFF)")


# ---------------------------------------------------------------------------
# GAME3 — Tetris/CodeTank (lower) + menu + Life + Mandel + Plasma (upper)
# ---------------------------------------------------------------------------
def build_game3_lower_bank() -> bytes:
    """Lower 16 kB: tetris_codetank.bin prebuilt, full $4000-$7FFF.
    Drop-in image from software/Apple-1_TMS_CC65/ (16 384 B exact)."""
    print("[GAME3] Lower bank (Tetris/CodeTank, full 16 kB):", file=sys.stderr)
    return splat_full_bank(TETRIS_CT_BIN, "Tetris/CT ($4000-$7FFF)")


def build_game3_upper_bank() -> bytes:
    """Upper 16 kB layout — menu at $4000 dispatches to 3 small demos.
    Slot offsets pinned by each program's bank cfg:
      menu     $4000-$40FF   ( 256 B, apple1_codetank_game3_menu.cfg)
      Life     $4100-$48FF   (2 048 B, apple1_life_codetank_game3_bank.cfg)
      Mandel   $4900-$50FF   (2 048 B, apple1_mandel_codetank_bank.cfg)
      Plasma   $5100-$58FF   (2 048 B, apple1_plasma_codetank_bank.cfg)
      reserved $5900-$7FFF   (free, $FF-fill)"""
    print("\n[GAME3] Upper bank (menu + Life + Mandel + Plasma, run-in-place):",
          file=sys.stderr)
    menu   = assemble(GAME3_MENU_ASM, GAME3_MENU_CFG, "G3U_menu", 0x0100)
    life   = assemble(LIFE_ASM,       LIFE_BANK_CFG,  "G3U_Life", 0x0800)
    mandel = assemble_multi(
        [MANDEL_ASM, MANDEL_VDP_ASM], MANDEL_BANK_CFG, "G3U_Mandel", 0x0800)
    plasma = assemble_multi(
        [PLASMA_ASM, PLASMA_VDP_ASM], PLASMA_BANK_CFG, "G3U_Plasma", 0x0800)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, menu,   0x0100, "Menu      ($4000-$40FF)")
    slot(bank, 0x0100, life,   0x0800, "Life      ($4100-$48FF)")
    slot(bank, 0x0900, mandel, 0x0800, "Mandel    ($4900-$50FF)")
    slot(bank, 0x1100, plasma, 0x0800, "Plasma    ($5100-$58FF)")
    return bytes(bank)


# ---------------------------------------------------------------------------
# TEST — TMS_SilBench (lower) + menu + Clone + Split (upper)
# ---------------------------------------------------------------------------
def build_test_lower_bank() -> bytes:
    """Lower 16 kB: TMS_SilBench run-in-place from $4000-$7FFF.
    29-test silicon benchmark — each test renders a visual on the
    TMS9918 then prints a transcribable line on the Apple-1 PIA display
    so an operator can diff Replica-1+P-LAB silicon vs POM1 strict."""
    print("[TEST] Lower bank (TMS_SilBench, full 16 kB):", file=sys.stderr)
    silbench = assemble_multi(
        [SILBENCH_ASM, SILBENCH_M1_ASM, SILBENCH_5S_ASM],
        SILBENCH_BANK_CFG, "TEST_SilBench", HALF_SIZE)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, silbench, HALF_SIZE, "SilBench  ($4000-$7FFF)")
    return bytes(bank)


def build_test_upper_bank() -> bytes:
    """Upper 16 kB layout — menu at $4000 dispatches to 2 silicon-bug
    mini-tests.
    Slot offsets pinned by each program's bank cfg:
      menu     $4000-$40FF   ( 256 B, apple1_codetank_test_menu.cfg)
      Clone    $4100-$44FF   (1 024 B, apple1_clone_codetank_bank.cfg)
      Split    $4500-$48FF   (1 024 B, apple1_split_codetank_bank.cfg)
      reserved $4900-$7FFF   (free, $FF-fill)"""
    print("\n[TEST] Upper bank (menu + Clone + Split, run-in-place):",
          file=sys.stderr)
    menu  = assemble(TEST_MENU_ASM, TEST_MENU_CFG, "TESTU_menu", 0x0100)
    clone = assemble_multi(
        [CLONE_ASM, CLONE_VDP_ASM], CLONE_BANK_CFG, "TESTU_Clone", 0x0400)
    split = assemble_multi(
        [SPLIT_ASM, SPLIT_M1_ASM, SPLIT_5S_ASM],
        SPLIT_BANK_CFG, "TESTU_Split", 0x0400)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, menu,  0x0100, "Menu      ($4000-$40FF)")
    slot(bank, 0x0100, clone, 0x0400, "Clone     ($4100-$44FF)")
    slot(bank, 0x0500, split, 0x0400, "Split     ($4500-$48FF)")
    return bytes(bank)


# ---------------------------------------------------------------------------
# GAME4 — Light Corridor (lower) + reserved (upper)
# ---------------------------------------------------------------------------
def build_game4_lower_bank() -> bytes:
    """Lower 16 kB: TMS_LightCorridor alone, full $4000-$7FFF, run-in-place.
    Original implementation inspired by Infogrames' Light Corridor (1990):
    wireframe perspective tunnel + paddle + ball with z-scaled sprite
    swap, 3 difficulty levels."""
    print("[GAME4] Lower bank (TMS_LightCorridor, full 16 kB):",
          file=sys.stderr)
    lc = assemble_multi(
        [LIGHT_CORRIDOR_ASM, LIGHT_CORRIDOR_VDP_ASM],
        LIGHT_CORRIDOR_BANK_CFG, "G4_LightCorridor", HALF_SIZE)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, lc, HALF_SIZE, "LightCorr ($4000-$7FFF)")
    return bytes(bank)


def build_game4_upper_bank() -> bytes:
    """Upper 16 kB: reserved for future expansion (currently $FF fill)."""
    print("\n[GAME4] Upper bank (reserved, $FF fill):", file=sys.stderr)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    print(f"  Reserved upper bank          0 B / {HALF_SIZE:5d} B slot "
          f"(  0.0%, {HALF_SIZE:5d} B free)", file=sys.stderr)
    return bytes(bank)


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


def build_test() -> bytes:
    print("\n========== Codetank_TEST.rom ==========", file=sys.stderr)
    lower = build_test_lower_bank()
    upper = build_test_upper_bank()
    rom = lower + upper
    assert len(rom) == ROM_SIZE
    return rom


def build_game4() -> bytes:
    print("\n========== Codetank_GAME4.rom ==========", file=sys.stderr)
    lower = build_game4_lower_bank()
    upper = build_game4_upper_bank()
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
    "  Upper jumper: 4000R → TMS_Nyan_CodeTank (12-frame Mode III animation)\n"
)

SIDECAR_GAME3 = (
    "Codetank_GAME3.rom — TMS9918 P-LAB CodeTank cartridge\n"
    "  Lower jumper: 4000R → Tetris/CodeTank (full 16 kB drop-in)\n"
    "  Upper jumper: 4000R → menu → 1=Life 2=Mandel 3=Plasma\n"
)

SIDECAR_TEST = (
    "Codetank_TEST.rom — TMS9918 silicon-validation utilities\n"
    "  Lower jumper: 4000R → TMS_SilBench (29-test silicon benchmark\n"
    "                suite — interactive menu (A=run all, 1..9=single\n"
    "                test, ESC=exit). Each test renders a visual on the\n"
    "                TMS9918 then prints a transcribable line on the\n"
    "                Apple-1 native PIA display. Canonical regression\n"
    "                fixture for the May 2026 silicon model — see\n"
    "                dev/projects/tms9918_silbench/README.md)\n"
    "  Upper jumper: 4000R → menu → 1=Clone (sprite-clone bug N.8)\n"
    "                              2=Split (5th-sprite palette split)\n"
)

SIDECAR_GAME4 = (
    "Codetank_GAME4.rom — TMS9918 P-LAB CodeTank cartridge\n"
    "  Lower jumper: 4000R → TMS_LightCorridor (wireframe perspective\n"
    "                tunnel + paddle + ball, 3 difficulty levels).\n"
    "                Controls: A/Q D = left/right, W/Z S = up/down,\n"
    "                SPACE = launch, ESC = exit to Wozmon.\n"
    "                Original implementation inspired by Infogrames'\n"
    "                Light Corridor (1990) concept — original code,\n"
    "                original assets.\n"
    "  Upper jumper: reserved ($FF fill — future expansion).\n"
)


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument(
        "--rom", choices=("1", "2", "3", "4", "test", "all"), default="all",
        help="Which CodeTank ROM to build (default: all 5)",
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

    if args.rom in ("test", "all"):
        romT = build_test()
        write_rom(romT, out_dir / "Codetank_TEST.rom", SIDECAR_TEST)

    if args.rom in ("4", "all"):
        rom4 = build_game4()
        write_rom(rom4, out_dir / "Codetank_GAME4.rom", SIDECAR_GAME4)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
