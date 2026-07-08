#!/usr/bin/env python3
"""
Build P-LAB CodeTank ROM images for the TMS9918 graphic card.

Four release cartridges + the DevBench flash cartridge are produced (in
roms/codetank/). Each is a 32 kB 28c256 whose lower / upper 16 kB half is
jumper-mapped to $4000-$7FFF, so every program runs in place at $4000
regardless of which half it sits in (juillet 2026 release layout — the
former Codetank_GAME1-7 line-up, reorganised so Claudio's four burns cover
the whole library):

  Codetank_CLASSICS.rom (32 kB) — the timeless two
    Lower 16 kB: Tetris/CodeTank (full bank, run-in-place from $4000)
                 — external drop-in (from GitHub) at
                 software/Apple-1_TMS_CC65/tetris_codetank.bin; when the
                 drop-in is absent it is recovered verbatim from the
                 committed CLASSICS rom (or the legacy GAME1 rom).
    Upper 16 kB: TMS_Chess (full bank, run-in-place from $4000) — Mode-2
                 graphical chess front-end for the shared engine
                 (dev/lib/games/chess), links tms9918m2 + text_bitmap +
                 sprites_chess, built with -D CODETANK_BUILD.
    Jumper Lower → 4000R boots Tetris; jumper Upper → 4000R boots Chess.

  Codetank_BASIC_LOGO.rom (32 kB) — the language cartridge. THE stabilised
    home of both interpreters; the DevBench injection paths for Applesoft
    TMS and LOGO load their banks from here (Pom1BenchHost.cpp).
    Lower 16 kB: TMS_LOGO V2.6 interpreter (full bank, run-in-place from
                 $4000, built with -D CODETANK_BUILD).
    Upper 16 kB: Applesoft TMS9918 interpreter (run-in-place at $4000,
                 sketchs/tms9918/applesoft_tms9918/).
    Jumper Lower → 4000R boots LOGO; jumper Upper → 4000R boots Applesoft.

  Codetank_ARCADE.rom (32 kB) — the action games
    Lower 16 kB: menu → Galaga, Sokoban, Snake (run-in-place)
                 ($4000 menu / $4100 Galaga / $6200 Sokoban / $7600 Snake)
    Upper 16 kB: TMS_Rogue alone (full bank, run-in-place from $4000)
    Jumper Lower → 4000R brings up the arcade picker; jumper Upper →
    4000R boots Rogue.

  Codetank_DEMOS.rom (32 kB) — every demo on one chip
    Lower 16 kB: menu → Life, Mandel, Plasma, Vague, Nyan (run-in-place)
                 ($4000 menu / $4200 Life / $4A00 Mandel / $5200 Plasma /
                  $5A00 Vague / $6000 Nyan)
    Upper 16 kB: demo_sprite_animals (cc65 C + tms9918c runtime + the
                 SCROLL-O-SPRITES Fauna patterns, run-in-place from $4000).
    Jumper Lower → 4000R brings up the demo picker; jumper Upper → 4000R
    animates the four Fauna sprites.

  CODETANKDEV.rom (32 kB) — the DevBench flash cartridge. BOTH banks are
    blank $FF flash slots the in-app DevBench rewrites at runtime (bank
    picked in the DevBench UI). Generated on demand — NOT committed
    (tracked builds of it are flash artefacts; see .gitignore). The
    Applesoft upper bank it used to carry now ships stabilised in
    Codetank_BASIC_LOGO.rom.

(Retired June 2026: the TEST cartridge. Retired juillet 2026 with this
 reorganisation: GAME5 — nino-democ source left the tree — and GAME6 —
 OrbitalPool/SilBench Woz-hex artefacts left the tree; TMS9918_Hello and
 TMS_Split ship as DevBench sketches only, no longer on a cartridge.)

Each game's slot is sized to fit its current assembled binary (see the
.cfg `size = $xxxx` field). `slot()` enforces the boundary and prints a
clear deficit message if a game outgrows its slot.

Usage:
    python3 tools/build_codetank_rom.py                  # build all 4 + DEV
    python3 tools/build_codetank_rom.py --rom=classics   # only CLASSICS
    python3 tools/build_codetank_rom.py --rom=basiclogo  # only BASIC_LOGO
    python3 tools/build_codetank_rom.py --rom=arcade     # only ARCADE
    python3 tools/build_codetank_rom.py --rom=demos      # only DEMOS
    python3 tools/build_codetank_rom.py --rom=dev        # only CODETANKDEV
"""
from __future__ import annotations
import argparse
import hashlib
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
SK                = ROOT / "sketchs" / "tms9918"   # mono-source DevBench sketches
# CodeTank "best-of" cartridge composition layer (launcher menus + per-game
# ROM bank-layout cfgs) — kept OUT of sketchs/, which holds only standalone
# DevBench-runnable programs. The games/demos themselves live under SK; the
# cartridge just packages them at fixed bank offsets.
CT                = DEV / "codetank"
CT_BANK           = CT / "bank_cfgs"
LIB_APPLE1        = ROOT / "dev" / "lib" / "apple1"
LIB_M6502         = ROOT / "dev" / "lib" / "m6502"
LIB_TMS           = ROOT / "dev" / "lib" / "tms9918"
LIB_SOKOBAN       = ROOT / "dev" / "lib" / "games" / "sokoban"
LIB_GEN2          = ROOT / "dev" / "lib" / "gen2"
LIB_CHESS         = ROOT / "dev" / "lib" / "games" / "chess"
LIB_ROGUE         = ROOT / "dev" / "lib" / "games" / "rogue"
LIB_TEXT40        = ROOT / "dev" / "lib" / "text40"

# --- ARCADE sources (menu + Galaga + Sokoban + Snake lower; Rogue upper) ---
MENU_ASM          = CT / "game1_menu" / "codetank_menu.asm"
MENU_CFG          = CT / "game1_menu" / "apple1_codetank_menu.cfg"
GALAGA_ASM        = SK / "game_galaga"  / "TMS_Galaga.asm"
GALAGA_BANK_CFG   = CT_BANK / "apple1_galaga_codetank_bank.cfg"
SOKOBAN_ASM       = SK / "game_sokoban" / "TMS_Sokoban.asm"
SOKOBAN_BANK_CFG  = CT_BANK / "apple1_sokoban_codetank_bank.cfg"
SNAKE_ASM         = SK / "game_snake"   / "TMS_Snake.asm"
SNAKE_BANK_CFG    = CT_BANK / "apple1_snake_codetank_bank.cfg"

# --- LOGO V2 (BASIC_LOGO lower) ---------------------------------------------
LOGO_V2_ASM        = SK / "tool_logo" / "TMS_Logo_16k.asm"
LOGO_V2_BANK_CFG   = CT_BANK / "apple1_logo_v2_codetank_bank.cfg"
LOGO_V2_MATH_ASM   = LIB_M6502   / "math.asm"
LOGO_V2_VDP_ASM    = LIB_TMS     / "tms9918m2.asm"
LOGO_V2_EMOTE_ASM  = LIB_TMS     / "sprites_emotes.asm"
LOGO_V2_TEXT_ASM   = LIB_TMS     / "text_bitmap.asm"
LOGO_V2_BUBBLE_ASM = LIB_TMS     / "bubble.asm"
LOGO_V2_BUFED_ASM  = LIB_TMS     / "buffer_editor.asm"
LOGO_V2_SPRH_ASM   = LIB_TMS     / "sprite_helpers.asm"

# --- Rogue (ARCADE upper) ---------------------------------------------------
ROGUE_ASM          = SK / "game_rogue" / "TMS_Rogue.asm"
ROGUE_BOSS_ASM     = SK / "game_rogue" / "sprites_boss.asm"
ROGUE_CODETANK_CFG = SK / "game_rogue" / "apple1_rogue.cfg"
ROGUE_M1_ASM       = LIB_TMS / "tms9918m1.asm"

# Nyan/CodeTank (DEMOS lower, $6000 slot) — assembled from the mono-source
# DevBench sketch with a bank cfg pinning the slot start.
NYAN_ASM          = SK / "demo_nyan_cat" / "TMS_Nyan_CodeTank.asm"
NYAN_RLE_ASM      = SK / "demo_nyan_cat" / "nyan_rle.asm"
NYAN_DEMOS_BANK_CFG = CT_BANK / "apple1_nyan_codetank_demos_bank.cfg"

# --- Tetris/CodeTank (CLASSICS lower) ---------------------------------------
# External drop-in 16 KB image (no in-repo source; fetched from GitHub). Drop
# it at software/Apple-1_TMS_CC65/tetris_codetank.bin; we splat it verbatim
# into GAME1's lower bank (full run-in-place bank, jumper Lower → 4000R).
TETRIS_CT_BIN     = CODETANK_CC65_BIN / "tetris_codetank.bin"

# --- DEMOS sources (menu+Life+Mandel+Plasma+Vague+Nyan lower; Animals upper)
# DEMOS lower menu + 5 demo programs sharing the lower bank via the menu
# pattern (bank cfgs pin each slot's start address).
DEMOS_MENU_ASM    = CT / "demos_menu" / "codetank_demos_menu.asm"
DEMOS_MENU_CFG    = CT / "demos_menu" / "apple1_codetank_demos_menu.cfg"

LIFE_ASM          = SK / "demo_life"   / "TMS_Life.asm"
LIFE_BANK_CFG     = CT_BANK / "apple1_life_codetank_demos_bank.cfg"

MANDEL_ASM        = SK / "demo_mandel" / "TMS_Mandel.asm"
MANDEL_BANK_CFG   = CT_BANK / "apple1_mandel_codetank_demos_bank.cfg"
MANDEL_VDP_ASM    = LIB_TMS / "tms9918m2.asm"

PLASMA_ASM        = SK / "demo_plasma" / "TMS_Plasma.asm"
PLASMA_BANK_CFG   = CT_BANK / "apple1_plasma_codetank_demos_bank.cfg"
PLASMA_VDP_ASM    = LIB_TMS / "tms9918m1.asm"

VAGUE_ASM         = SK / "demo_vague" / "TMS_Vague.asm"
VAGUE_BANK_CFG    = CT_BANK / "apple1_vague_codetank_demos_bank.cfg"
VAGUE_M1_ASM      = LIB_TMS / "tms9918m1.asm"

ANIMALS_C         = SK / "demo_sprite_animals" / "demo_sprite_animals.c"
FAUNA_ASM         = LIB_TMS / "sprites_fauna.asm"

# --- tms9918c C runtime (mirrors the DevBench cl65 command in
#     Pom1BenchHost.cpp::tms9918cBenchRuntimeCl65Args — link the FULL
#     runtime; the 16 kB fill=yes bank absorbs it, exactly like a
#     DevBench "Run on CodeTank" build) -------------------------------------
LIB_TMS9918C      = ROOT / "dev" / "lib" / "tms9918c"
LIB_GFX           = ROOT / "dev" / "lib" / "gfx"
LIB_TELEMETRY     = ROOT / "dev" / "lib" / "telemetry"
CODETANK_C_CFG    = LIB_TMS9918C / "cc65" / "codetank_c.cfg"
TMS9918C_RUNTIME  = [
    "apple1.c", "apple1_asm.s", "tms9918.c", "tms_fast.s",
    "screen1.c", "c64font.c", "screen1_input.c",
    "screen2_init.c", "screen2_text.c", "screen2_pixel.c",
    "screen2_geom.c", "screen1_ext.c", "screen2_ext.c",
    "sprites.c", "sprite_shadow.c", "vsync.c", "printlib.c", "random.c",
]
GFX_RUNTIME       = [
    "gfx_line.c", "gfx_rect.c", "gfx_circle.c", "gfx_ellipse.c",
    "gfx_num_dec.c", "gfx_num_hex.c", "gfx_text.c",
    "gfx_backend_tms.c", "gfx_backend_tms_rect.c", "gfx_text_backend_tms.c",
]

# (The TEST cartridge was retired June 2026; GAME5/GAME6 and their packer
#  machinery left with the juillet 2026 four-cartridge reorganisation —
#  nino-democ and the OrbitalPool/SilBench Woz-hex artefacts are no longer
#  in the tree. TMS_Split / TMS9918_Hello remain standalone DevBench
#  sketches, no longer packaged on a cartridge.)


# ---------------------------------------------------------------------------
def need(tool: str) -> None:
    if shutil.which(tool) is None:
        raise SystemExit(
            f"{tool} not found in PATH. Install cc65 (apt: sudo apt install cc65)."
        )


def _common_includes(asm: pathlib.Path) -> list[str]:
    return [
        "-I", str(LIB_APPLE1), "-I", str(LIB_M6502), "-I", str(LIB_TMS),
        "-I", str(LIB_SOKOBAN), "-I", str(LIB_GEN2), "-I", str(LIB_CHESS),
        "-I", str(LIB_ROGUE), "-I", str(LIB_TEXT40), "-I", str(asm.parent),
    ]


def _ca65(asm: pathlib.Path, obj: pathlib.Path,
          extra_args: list[str] | None = None) -> None:
    # extra_args FIRST: callers pass generated -I dirs (BUILD) that must
    # shadow any stray same-named .inc committed under the lib include
    # paths — ca65 searches -I dirs in order.
    cmd = ["ca65", *(extra_args or []), *_common_includes(asm),
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
        if ("tms9918_pad18" in t
                or "tms9918_pad12" in t
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
        if ("tms9918_pad18" in t
                or "tms9918_pad12" in t
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


def build_c_codetank(name: str, c_src: pathlib.Path,
                     extra_asm: list[pathlib.Path] | None = None) -> bytes:
    """Compile a cc65 C sketch into a 16 kB run-in-place CodeTank bank,
    mirroring the DevBench's desktop cl65 command (Pom1BenchHost.cpp):
    `cl65 -t none -Oirs -DPOM1_GFX_TMS -C codetank_c.cfg` + the FULL
    tms9918c + gfx runtime. extra_asm modules (.asm, e.g. the
    SCROLL-O-SPRITES fauna patterns) are pre-assembled with ca65 like
    the Bench does for a sketch's extraAsm. codetank_c.cfg has
    `fill = yes, fillval = $FF`, so the output is exactly HALF_SIZE.

    The sketch .c is compiled from a COPY under build/codetank/ so cl65's
    intermediate .o never lands next to the sketch (mirrors the DevBench's
    temp-source discipline; the sketches only include runtime headers
    resolved via -I, so the copy compiles identically)."""
    BUILD.mkdir(parents=True, exist_ok=True)
    binp = BUILD / f"{name}.bin"
    src_copy = BUILD / f"{name}_{c_src.name}"
    src_copy.write_bytes(c_src.read_bytes())
    extra_objs: list[str] = []
    for xa in (extra_asm or []):
        obj = BUILD / f"{name}_{xa.stem}.o"
        _ca65(xa, obj)
        extra_objs.append(str(obj))
    cmd = ["cl65", "-t", "none", "-Oirs", "-DPOM1_GFX_TMS",
           "-C", str(CODETANK_C_CFG),
           "-I", str(LIB_TMS9918C), "-I", str(LIB_GFX),
           "-I", str(LIB_TELEMETRY),
           str(src_copy),
           *[str(LIB_TMS9918C / f) for f in TMS9918C_RUNTIME],
           *[str(LIB_GFX / f) for f in GFX_RUNTIME],
           *extra_objs,
           "-o", str(binp)]
    subprocess.run(cmd, check=True, cwd=str(ROOT))
    data = binp.read_bytes()
    if len(data) != HALF_SIZE:
        raise SystemExit(
            f"{name}: cl65 produced {len(data)} B, expected a filled "
            f"{HALF_SIZE} B bank (codetank_c.cfg fill=yes)")
    return data


# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# CLASSICS — Tetris (lower) + TMS_Chess (upper)
# ---------------------------------------------------------------------------
def build_classics_lower_bank() -> bytes:
    """Lower 16 kB: Tetris/CodeTank prebuilt, full $4000-$7FFF run-in-place.
    Drop-in image from software/Apple-1_TMS_CC65/ (16 384 B exact).

    SELF-HEALING FALLBACK: the external drop-in is not in the tree, so when
    it is absent the Tetris image is recovered verbatim from the committed
    Codetank_CLASSICS.rom's lower half (bytes 0..16383 — `rom = lower +
    upper`); on the very first build after the juillet 2026 reorganisation
    it falls back to the legacy Codetank_GAME1.rom's lower half. Either way
    the Chess upper bank always rebuilds from current sources."""
    print("[CLASSICS] Lower bank (Tetris/CodeTank, full 16 kB):",
          file=sys.stderr)
    if not TETRIS_CT_BIN.exists():
        for prev_name in ("Codetank_CLASSICS.rom", "Codetank_GAME1.rom"):
            prev_rom = ROOT / "roms" / "codetank" / prev_name
            if prev_rom.exists() and prev_rom.stat().st_size == 2 * HALF_SIZE:
                print(f"  {TETRIS_CT_BIN} missing — recovering Tetris "
                      f"verbatim from {prev_rom} lower half", file=sys.stderr)
                return prev_rom.read_bytes()[:HALF_SIZE]
        raise SystemExit(
            f"ERROR: {TETRIS_CT_BIN} not found and no previous "
            f"Codetank_CLASSICS.rom / Codetank_GAME1.rom to recover the "
            f"lower bank from")
    return splat_full_bank(TETRIS_CT_BIN, "Tetris/CT ($4000-$7FFF)")


# TMS_Chess (CLASSICS upper) — multi-module like Rogue/LOGO: the TMS renderer
# + game loop links the shared chess engine, the Mode-2 driver, the on-bitmap
# font, and the chess piece silhouettes. Built with -D CODETANK_BUILD for the
# status/help text. Also runnable in-app via the DevBench
# (sketchs/tms9918/game_chess/.sketch.json).
CHESS_TMS_ASM      = SK / "game_chess" / "TMS_Chess.asm"
CHESS_CODETANK_CFG = SK / "game_chess" / "apple1_chess_codetank.cfg"
CHESS_ENGINE_ASM   = LIB_CHESS / "chess_engine.asm"
CHESS_M2_ASM       = LIB_TMS / "tms9918m2.asm"
CHESS_TEXT_ASM     = LIB_TMS / "text_bitmap.asm"
CHESS_SPRITES_ASM  = LIB_TMS / "sprites_chess.asm"


def build_classics_upper_bank() -> bytes:
    """Upper 16 kB: TMS_Chess alone, full $4000-$7FFF, run-in-place (any
    bank runs at $4000 — the jumper picks the half)."""
    print("\n[CLASSICS] Upper bank (TMS_Chess, full 16 kB):", file=sys.stderr)
    chess = assemble_multi(
        [CHESS_TMS_ASM, CHESS_ENGINE_ASM, CHESS_M2_ASM,
         CHESS_TEXT_ASM, CHESS_SPRITES_ASM],
        CHESS_CODETANK_CFG, "CL_Chess", HALF_SIZE,
        extra_ca65_args=["-D", "CODETANK_BUILD", "-D", "CHESS_SMART_EVAL"])
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, chess, HALF_SIZE, "Chess     ($4000-$7FFF)")
    return bytes(bank)


# ---------------------------------------------------------------------------
# BASIC_LOGO — TMS_LOGO V2.6 (lower) + Applesoft TMS9918 (upper)
# ---------------------------------------------------------------------------
def build_basiclogo_lower_bank() -> bytes:
    """Lower 16 kB: TMS_LOGO V2.6 at $4000-$7FFF, runs in place from ROM.
    Built with -D CODETANK_BUILD for the full feature set (on-bitmap text,
    speech bubbles, buffer editor). The DevBench LOGO injection path loads
    this bank (Pom1BenchHost.cpp) — jumper Lower, 4000R."""
    print("[BASIC_LOGO] Lower bank (TMS_LOGO V2.6, run-in-place):",
          file=sys.stderr)
    logo = assemble_multi(
        [LOGO_V2_ASM, LOGO_V2_MATH_ASM, LOGO_V2_VDP_ASM,
         LOGO_V2_EMOTE_ASM, LOGO_V2_TEXT_ASM, LOGO_V2_BUBBLE_ASM,
         LOGO_V2_BUFED_ASM, LOGO_V2_SPRH_ASM],
        LOGO_V2_BANK_CFG, "BL_LogoV2", HALF_SIZE,
        extra_ca65_args=["-D", "CODETANK_BUILD"])
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, logo, HALF_SIZE, "LOGO V2.6 ($4000-$7FFF)")
    return bytes(bank)


def build_basiclogo_upper_bank() -> bytes:
    """Upper 16 kB: the Applesoft TMS9918 interpreter (run-in-place at
    $4000). The DevBench Applesoft-TMS injection path loads this bank
    (Pom1BenchHost.cpp) — jumper Upper, 4000R."""
    print("\n[BASIC_LOGO] Upper bank (Applesoft TMS9918):", file=sys.stderr)
    ad = SK / "applesoft_tms9918"
    asoft = assemble_multi(
        [ad / "applesoft-tms9918.s", ad / "io.s"],
        ad / "applesoft_tms9918.cfg", "BL_applesoft", HALF_SIZE)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, asoft, HALF_SIZE, "Applesoft ($4000-$7FFF)")
    return bytes(bank)


# ---------------------------------------------------------------------------
# ARCADE — menu + Galaga + Sokoban + Snake (lower) + Rogue (upper)
# ---------------------------------------------------------------------------
def build_arcade_lower_bank() -> bytes:
    """Lower 16 kB — menu at $4000 dispatches to 3 games by entry address.
    Each game's bank cfg pins its start address; we copy each binary into
    its slot and verify it fits.

    Slot offsets pinned by each game's bank cfg:
      menu     $4000-$40FF   ( 256 B, apple1_codetank_menu.cfg)
      Galaga   $4100-$61FF   (8 448 B, apple1_galaga_codetank_bank.cfg)
      Sokoban  $6200-$75FF   (5 120 B, apple1_sokoban_codetank_bank.cfg)
      Snake    $7600-$7FFF   (2 560 B, apple1_snake_codetank_bank.cfg)
    May 2026 v2 reshuffle: cross-JSR strict-mode pads pushed Galaga past
    the previous 8 192 B slot, so shifted Sokoban entry +256 B (matching
    codetank_menu.asm's SOKOBAN_ENTRY = $6200)."""
    print("[ARCADE] Lower bank (menu + 3 games, run-in-place):",
          file=sys.stderr)
    menu    = assemble(MENU_ASM,    MENU_CFG,         "AR_menu",    0x0100)
    galaga  = assemble(GALAGA_ASM,  GALAGA_BANK_CFG,  "AR_Galaga",  0x4000)
    sokoban = assemble(SOKOBAN_ASM, SOKOBAN_BANK_CFG, "AR_Sokoban", 0x4000)
    snake   = assemble(SNAKE_ASM,   SNAKE_BANK_CFG,   "AR_Snake",   0x4000)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, menu,    0x0100, "Menu      ($4000-$40FF)")
    slot(bank, 0x0100, galaga,  0x2100, "Galaga    ($4100-$61FF)")
    slot(bank, 0x2200, sokoban, 0x1400, "Sokoban   ($6200-$75FF)")
    slot(bank, 0x3600, snake,   0x0A00, "Snake     ($7600-$7FFF)")
    return bytes(bank)


def build_arcade_upper_bank() -> bytes:
    """Upper 16 kB: TMS_Rogue alone, full $4000-$7FFF, run-in-place."""
    print("\n[ARCADE] Upper bank (TMS_Rogue alone, full 16 kB):",
          file=sys.stderr)
    rogue = assemble_multi(
        [ROGUE_ASM, ROGUE_M1_ASM, ROGUE_BOSS_ASM],
        ROGUE_CODETANK_CFG, "AR_Rogue", HALF_SIZE)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, rogue, HALF_SIZE, "Rogue     ($4000-$7FFF)")
    return bytes(bank)


# ---------------------------------------------------------------------------
# DEMOS — menu + Life + Mandel + Plasma + Vague + Nyan (lower);
#         demo_sprite_animals C (upper)
# ---------------------------------------------------------------------------
def build_demos_lower_bank() -> bytes:
    """Lower 16 kB layout — menu at $4000 dispatches to 5 demos.
    Slot offsets pinned by each program's bank cfg:
      menu     $4000-$41FF   (  512 B, apple1_codetank_demos_menu.cfg)
      Life     $4200-$49FF   (2 048 B, apple1_life_codetank_demos_bank.cfg)
      Mandel   $4A00-$51FF   (2 048 B, apple1_mandel_codetank_demos_bank.cfg)
      Plasma   $5200-$59FF   (2 048 B, apple1_plasma_codetank_demos_bank.cfg)
      Vague    $5A00-$5FFF   (1 536 B, apple1_vague_codetank_demos_bank.cfg)
      Nyan     $6000-$7FFF   (8 192 B, apple1_nyan_codetank_demos_bank.cfg)"""
    print("[DEMOS] Lower bank (menu + 5 demos, run-in-place):",
          file=sys.stderr)
    menu   = assemble(DEMOS_MENU_ASM, DEMOS_MENU_CFG, "DM_menu", 0x0200)
    life   = assemble(LIFE_ASM,       LIFE_BANK_CFG,  "DM_Life", 0x0800)
    mandel = assemble_multi(
        [MANDEL_ASM, MANDEL_VDP_ASM], MANDEL_BANK_CFG, "DM_Mandel", 0x0800)
    plasma = assemble_multi(
        [PLASMA_ASM, PLASMA_VDP_ASM], PLASMA_BANK_CFG, "DM_Plasma", 0x0800)
    vague  = assemble_multi(
        [VAGUE_ASM, VAGUE_M1_ASM], VAGUE_BANK_CFG, "DM_Vague", 0x0600)
    nyan   = assemble_multi(
        [NYAN_ASM, NYAN_RLE_ASM], NYAN_DEMOS_BANK_CFG, "DM_Nyan", 0x2000)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, menu,   0x0200, "Menu      ($4000-$41FF)")
    slot(bank, 0x0200, life,   0x0800, "Life      ($4200-$49FF)")
    slot(bank, 0x0A00, mandel, 0x0800, "Mandel    ($4A00-$51FF)")
    slot(bank, 0x1200, plasma, 0x0800, "Plasma    ($5200-$59FF)")
    slot(bank, 0x1A00, vague,  0x0600, "Vague     ($5A00-$5FFF)")
    slot(bank, 0x2000, nyan,   0x2000, "Nyan      ($6000-$7FFF)")
    return bytes(bank)


def build_demos_upper_bank() -> bytes:
    """Upper 16 kB: demo_sprite_animals (cc65 C + tms9918c runtime + the
    SCROLL-O-SPRITES fauna patterns), full $4000-$7FFF run-in-place."""
    print("\n[DEMOS] Upper bank (demo_sprite_animals, cc65 C, full 16 kB):",
          file=sys.stderr)
    animals = build_c_codetank("DM_Animals", ANIMALS_C, [FAUNA_ASM])
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, animals, HALF_SIZE, "Animals/C ($4000-$7FFF)")
    return bytes(bank)


def build_two_bank(rom_name: str, lower_fn, upper_fn) -> bytes:
    """Concatenate a lower + upper 16 kB bank into a 32 kB CodeTank ROM."""
    print(f"\n========== {rom_name} ==========", file=sys.stderr)
    rom = lower_fn() + upper_fn()
    assert len(rom) == ROM_SIZE
    return rom


def build_codetankdev() -> bytes:
    """CODETANKDEV.rom — the DevBench flash cartridge.

    BOTH 16 kB banks are blank ($FF) flash slots: the in-app DevBench writes
    the current asm / C TMS9918 build into the bank picked in its UI and
    boots the matching jumper -> 4000R. No toolchain input — POM1 itself
    (flashCodeTankDevRom in Pom1BenchHost.cpp) creates the file identically
    on demand, desktop and WASM alike, which is why this rom is generated,
    never committed. The Applesoft TMS9918 bank it historically carried now
    ships stabilised in Codetank_BASIC_LOGO.rom (upper)."""
    print("\n========== CODETANKDEV.rom ==========", file=sys.stderr)
    print("[CODETANKDEV] Lower + upper banks: blank $FF (DevBench flash "
          "slots)", file=sys.stderr)
    rom = b"\xFF" * ROM_SIZE
    assert len(rom) == ROM_SIZE
    return rom


def write_rom(rom: bytes, out: pathlib.Path, sidecar: str) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(rom)
    print(f"\n[CodeTank] Wrote {out}  ({len(rom)} bytes)", file=sys.stderr)
    sidecar_path = out.with_suffix(".txt")
    sidecar_path.write_text(sidecar, encoding="utf-8")
    print(f"           Sidecar: {sidecar_path}", file=sys.stderr)


SIDECAR_CLASSICS = (
    "Codetank_CLASSICS.rom — TMS9918 P-LAB CodeTank cartridge (the classics)\n"
    "  Lower jumper: 4000R → Tetris/CodeTank (full 16 kB drop-in)\n"
    "  Upper jumper: 4000R → TMS_Chess (graphical Mode-2 chess vs AI / 2P)\n"
)

SIDECAR_BASIC_LOGO = (
    "Codetank_BASIC_LOGO.rom — TMS9918 P-LAB CodeTank cartridge (languages)\n"
    "  Lower jumper: 4000R → TMS_LOGO V2.6 turtle interpreter\n"
    "  Upper jumper: 4000R → Applesoft TMS9918 BASIC interpreter\n"
)

SIDECAR_ARCADE = (
    "Codetank_ARCADE.rom — TMS9918 P-LAB CodeTank cartridge (action games)\n"
    "  Lower jumper: 4000R → menu → 1=Galaga 2=Sokoban 3=Snake\n"
    "  Upper jumper: 4000R → TMS_Rogue (dungeon crawler)\n"
)

SIDECAR_DEMOS = (
    "Codetank_DEMOS.rom — TMS9918 P-LAB CodeTank cartridge (demos)\n"
    "  Lower jumper: 4000R → menu → 1=Life 2=Mandel 3=Plasma 4=Vague 5=Nyan\n"
    "  Upper jumper: 4000R → demo_sprite_animals (4 Fauna sprites, cc65 C)\n"
)

SIDECAR_CODETANKDEV = (
    "CODETANKDEV.rom — TMS9918 P-LAB DevBench flash cartridge (generated,\n"
    "  never committed). Both banks are blank $FF flash slots the in-app\n"
    "  DevBench rewrites; pick the bank in the DevBench UI, boot 4000R.\n"
)


# The cartridge catalogue: one row per selectable ROM. The four release carts
# are lower+upper bank concatenations (the per-bank builders carry the real
# per-game variation); CODETANKDEV is built whole (lower/upper builders =
# None). `needs_cl65` marks carts with a cc65 C bank. Adding a cart is one
# row here — no main() edits.
CARTS = [
    # key,         filename,                   sidecar,             lower_builder,              upper_builder,              needs_cl65
    ("classics",  "Codetank_CLASSICS.rom",    SIDECAR_CLASSICS,    build_classics_lower_bank,  build_classics_upper_bank,  False),
    ("basiclogo", "Codetank_BASIC_LOGO.rom",  SIDECAR_BASIC_LOGO,  build_basiclogo_lower_bank, build_basiclogo_upper_bank, False),
    ("arcade",    "Codetank_ARCADE.rom",      SIDECAR_ARCADE,      build_arcade_lower_bank,    build_arcade_upper_bank,    False),
    ("demos",     "Codetank_DEMOS.rom",       SIDECAR_DEMOS,       build_demos_lower_bank,     build_demos_upper_bank,     True),
    ("dev",       "CODETANKDEV.rom",          SIDECAR_CODETANKDEV, None,                       None,                       False),
]


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument(
        "--rom", choices=[c[0] for c in CARTS] + ["all"],
        default="all",
        help="Which CodeTank ROM to build (default: all — the 4 release carts + CODETANKDEV)",
    )
    args = ap.parse_args()

    selected = [c for c in CARTS if args.rom in (c[0], "all")]

    if any(lower_fn is not None for _k, _f, _s, lower_fn, _u, _c in selected):
        need("ca65")
        need("ld65")
    if any(needs_cl65 for *_, needs_cl65 in selected):
        need("cl65")               # DEMOS upper (Animals) is a cc65 C build

    out_dir = ROOT / "roms" / "codetank"
    for _key, fname, sidecar, lower_fn, upper_fn, _needs_cl65 in selected:
        if lower_fn is None:                       # CODETANKDEV — built whole
            rom = build_codetankdev()
        else:
            rom = build_two_bank(fname, lower_fn, upper_fn)
        write_rom(rom, out_dir / fname, sidecar)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
