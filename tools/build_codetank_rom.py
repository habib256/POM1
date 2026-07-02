#!/usr/bin/env python3
"""
Build P-LAB CodeTank ROM images for the TMS9918 graphic card.

Six game ROMs + the DevBench cartridge are produced (in roms/codetank/).
Each is a 32 kB 28c256 whose lower / upper 16 kB half is jumper-mapped to
$4000-$7FFF, so every program runs in place at $4000 regardless of which
half it sits in:

  Codetank_GAME1.rom (32 kB) — the games cartridge
    Lower 16 kB: Tetris/CodeTank (full bank, run-in-place from $4000)
                 — external drop-in (from GitHub) at
                 software/Apple-1_TMS_CC65/tetris_codetank.bin.
    Upper 16 kB: menu → Galaga, Sokoban, Snake (run-in-place)
                 ($4000 menu / $4100 Galaga / $6200 Sokoban / $7600 Snake)
    Jumper Lower → 4000R boots Tetris; jumper Upper → 4000R brings up the
    arcade picker.

  Codetank_GAME2.rom (32 kB)
    Lower 16 kB: TMS_Rogue alone (full bank, run-in-place from $4000)
    Upper 16 kB: TMS_Nyan_CodeTank (full bank, run-in-place from $4000)
                 — assembled from sketchs/tms9918/demo_nyan_cat/.
    Jumper Lower → 4000R boots Rogue; jumper Upper → 4000R animates Nyan.

  Codetank_GAME3.rom (32 kB) — LOGO + graphics demos
    Lower 16 kB: TMS_LOGO V2.6 interpreter (full bank, run-in-place from
                 $4000, built with -D CODETANK_BUILD).
    Upper 16 kB: menu → Life, Mandel, Plasma (run-in-place)
                 ($4000 menu / $4100 Life / $4900 Mandel / $5100 Plasma)
    Jumper Lower → 4000R boots LOGO; jumper Upper → 4000R brings up the
    demo picker.

  Codetank_GAME4.rom (32 kB) — "TMS DEMO" cartridge
    Lower 16 kB: menu → TMS_Split, TMS_Vague, TMS9918_Hello (run-in-place)
                 ($4000 menu / $4100 Split / $5100 Vague / $6100 Hello)
    Upper 16 kB: demo_sprite_animals (cc65 C + tms9918c runtime + the
                 SCROLL-O-SPRITES Fauna patterns, run-in-place from $4000).
    Jumper Lower → 4000R brings up the demo picker; jumper Upper → 4000R
    animates the four Fauna sprites.

  Codetank_GAME5.rom (32 kB) — "NINO C DEMOS" cartridge
    Lower 16 kB: nino-democ (cc65 C port of Nino Porcino's upstream
                 apple1-videocard-lib demo menu, full bank at $4000).
    Upper 16 kB: demo_screen1 (cc65 C, screen1 text/reverse/charset/
                 sprites/input demo, full bank at $4000).
    Jumper Lower → 4000R boots the demo menu; jumper Upper → demo_screen1.

  Codetank_GAME6.rom (32 kB) — "CLASSICS" cartridge (source-less programs)
    Lower 16 kB: menu+packer → Maze3D, OrbitalPool, Stars
                 ($4000 menu+loader / $4200 Maze3D run-in-place /
                  $5E00+ packed Woz-hex images copied to RAM at launch)
    Upper 16 kB: TMS_SilBench (29-test silicon benchmark) run-in-place —
                 its historical image IS linked at $4000 (it was born a
                 CodeTank ROM: code in ROM, BSS at $0200), so it boots
                 straight off the jumper with no menu; it has its own.
    OrbitalPool / Stars / SilBench have no in-tree source (removed in
    686fe03): their shipped Woz-hex .txt under software/Graphic TMS9918/
    are parsed and packed verbatim — except OrbitalPool, whose runtime
    state page ($4000-$4016, dropped/ROM-shadowed on every strict
    preset AND on the CodeTank window) is relocated to $0F00 by the
    byte-exact ORBITAL_STATE_PATCH below. Maze3D's image needs RAM up to
    $1BFF (impossible on Parmigiani dual-bank), so its source was
    resurrected under dev/projects/codetank/game6_maze3d/ and relinked
    run-in-place at $4200 with state squeezed into $0E00-$0EFF.

(Retired June 2026: the TEST cartridge — Clone deleted, Split kept only as the
 DevBench sketch demo_split — and the ORIGINAL GAME4/LightCorridor, whose
 source no longer exists; the GAME4 name was recycled July 2026 for the TMS
 demo cartridge above.)

Each game's slot is sized to fit its current assembled binary (see the
.cfg `size = $xxxx` field). `slot()` enforces the boundary and prints a
clear deficit message if a game outgrows its slot.

Usage:
    python3 tools/build_codetank_rom.py            # build all 6 + DEV
    python3 tools/build_codetank_rom.py --rom=1    # only GAME1
    python3 tools/build_codetank_rom.py --rom=4    # only GAME4
    python3 tools/build_codetank_rom.py --rom=6    # only GAME6
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

# --- GAME1 sources (Tetris lower; menu + Galaga + Sokoban + Snake upper) ---
MENU_ASM          = CT / "game1_menu" / "codetank_menu.asm"
MENU_CFG          = CT / "game1_menu" / "apple1_codetank_menu.cfg"
GALAGA_ASM        = SK / "game_galaga"  / "TMS_Galaga.asm"
GALAGA_BANK_CFG   = CT_BANK / "apple1_galaga_codetank_bank.cfg"
SOKOBAN_ASM       = SK / "game_sokoban" / "TMS_Sokoban.asm"
SOKOBAN_BANK_CFG  = CT_BANK / "apple1_sokoban_codetank_bank.cfg"
SNAKE_ASM         = SK / "game_snake"   / "TMS_Snake.asm"
SNAKE_BANK_CFG    = CT_BANK / "apple1_snake_codetank_bank.cfg"

# --- LOGO V2 (GAME3 lower) -------------------------------------------------
LOGO_V2_ASM        = SK / "tool_logo" / "TMS_Logo_16k.asm"
LOGO_V2_BANK_CFG   = CT_BANK / "apple1_logo_v2_codetank_bank.cfg"
LOGO_V2_MATH_ASM   = LIB_M6502   / "math.asm"
LOGO_V2_VDP_ASM    = LIB_TMS     / "tms9918m2.asm"
LOGO_V2_EMOTE_ASM  = LIB_TMS     / "sprites_emotes.asm"
LOGO_V2_TEXT_ASM   = LIB_TMS     / "text_bitmap.asm"
LOGO_V2_BUBBLE_ASM = LIB_TMS     / "bubble.asm"
LOGO_V2_BUFED_ASM  = LIB_TMS     / "buffer_editor.asm"
LOGO_V2_SPRH_ASM   = LIB_TMS     / "sprite_helpers.asm"

# --- GAME2 sources (Rogue lower; Nyan/CodeTank prebuilt upper) -------------
ROGUE_ASM          = SK / "game_rogue" / "TMS_Rogue.asm"
ROGUE_BOSS_ASM     = SK / "game_rogue" / "sprites_boss.asm"
ROGUE_CODETANK_CFG = SK / "game_rogue" / "apple1_rogue.cfg"
ROGUE_M1_ASM       = LIB_TMS / "tms9918m1.asm"

# Nyan/CodeTank (GAME2 upper) — assembled from source at $4000 (mono-source
# DevBench sketch), then splatted into the upper half (pads to 16 KB).
NYAN_ASM          = SK / "demo_nyan_cat" / "TMS_Nyan_CodeTank.asm"
NYAN_RLE_ASM      = SK / "demo_nyan_cat" / "nyan_rle.asm"
NYAN_CFG          = SK / "demo_nyan_cat" / "apple1_nyan_codetank.cfg"

# --- Tetris/CodeTank (GAME1 lower) -----------------------------------------
# External drop-in 16 KB image (no in-repo source; fetched from GitHub). Drop
# it at software/Apple-1_TMS_CC65/tetris_codetank.bin; we splat it verbatim
# into GAME1's lower bank (full run-in-place bank, jumper Lower → 4000R).
TETRIS_CT_BIN     = CODETANK_CC65_BIN / "tetris_codetank.bin"

# --- GAME3 sources (LOGO V2.6 lower; menu+Life+Mandel+Plasma upper) ---------
# GAME3 upper menu + 3 demo programs (Life, Mandel, Plasma) sharing the
# upper bank via the menu pattern.
GAME3_MENU_ASM    = CT / "game3_menu" / "codetank_game3_menu.asm"
GAME3_MENU_CFG    = CT / "game3_menu" / "apple1_codetank_game3_menu.cfg"

LIFE_ASM          = SK / "demo_life"   / "TMS_Life.asm"
LIFE_BANK_CFG     = CT_BANK / "apple1_life_codetank_game3_bank.cfg"

MANDEL_ASM        = SK / "demo_mandel" / "TMS_Mandel.asm"
MANDEL_BANK_CFG   = CT_BANK / "apple1_mandel_codetank_bank.cfg"
MANDEL_VDP_ASM    = LIB_TMS / "tms9918m2.asm"

PLASMA_ASM        = SK / "demo_plasma" / "TMS_Plasma.asm"
PLASMA_BANK_CFG   = CT_BANK / "apple1_plasma_codetank_bank.cfg"
PLASMA_VDP_ASM    = LIB_TMS / "tms9918m1.asm"

# --- GAME4 sources (menu+Split+Vague+Hello lower; sprite_animals C upper) ---
GAME4_MENU_ASM    = CT / "game4_menu" / "codetank_game4_menu.asm"
GAME4_MENU_CFG    = CT / "game4_menu" / "apple1_codetank_game4_menu.cfg"

SPLIT_ASM         = SK / "demo_split" / "TMS_Split.asm"
SPLIT_BANK_CFG    = CT_BANK / "apple1_split_codetank_game4_bank.cfg"
SPLIT_M1_ASM      = LIB_TMS / "tms9918m1.asm"
SPLIT_5S_ASM      = LIB_TMS / "tms9918_5strigger.asm"

VAGUE_ASM         = SK / "demo_vague" / "TMS_Vague.asm"
VAGUE_BANK_CFG    = CT_BANK / "apple1_vague_codetank_game4_bank.cfg"

HELLO_ASM         = SK / "tms9918_hello" / "tms9918_hello.s"
HELLO_BANK_CFG    = CT_BANK / "apple1_hello_codetank_game4_bank.cfg"

ANIMALS_C         = SK / "demo_sprite_animals" / "demo_sprite_animals.c"
FAUNA_ASM         = LIB_TMS / "sprites_fauna.asm"

# --- GAME5 sources (nino-democ lower; demo_screen1 upper — both cc65 C) -----
NINODEMOC_C       = SK / "nino-democ" / "nino-democ.c"
SCREEN1_C         = SK / "demo_screen1" / "demo_screen1.c"

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

# --- GAME6 sources (menu+packer + Maze3D lower; SilBench upper) -------------
SOFT_TMS          = ROOT / "software" / "Graphic TMS9918"
GAME6_MENU_ASM    = CT / "game6_menu" / "codetank_game6_menu.asm"
GAME6_MENU_CFG    = CT / "game6_menu" / "apple1_codetank_game6_menu.cfg"
MAZE3D_ASM        = CT / "game6_maze3d" / "TMS_Maze3D.asm"
MAZE3D_BANK_CFG   = CT_BANK / "apple1_maze3d_codetank_game6_bank.cfg"
ORBITAL_TXT       = SOFT_TMS / "TMS_OrbitalPool.txt"
STARS_TXT         = SOFT_TMS / "TMS_Stars.txt"
SILBENCH_TXT      = SOFT_TMS / "TMS_SilBench.txt"
# (TMS_Maze3D.txt is NOT packed — image spans $0280-$1A78 + BSS $1B00,
#  beyond the Parmigiani dual-bank low-RAM ceiling $0FFF; see MAZE3D_ASM.
#  TMS_Nyan_Fantasy.txt is EXCLUDED by design: it targets the Multiplexing
#  Fantasy preset, not real silicon.)

# Parmigiani dual-bank low RAM window the packed images may be copied to.
# $0000-$01FF (ZP + stack) is off-limits for load images; $1000-$7FFF is
# out-of-range on the real 4K+4K layout (strict OOR drops it).
PACK_RAM_LO = 0x0200
PACK_RAM_HI = 0x1000

# OrbitalPool state-page relocation ($40xx -> $0Fxx). The historical
# binary keeps its 23 runtime scalars at $4000-$4016 ("non-critical,
# NOT in output file") — dead on arrival both under strict OOR (writes
# in [$1000,$8000) dropped, reads $FF → the $4013 exit_flag test bails
# instantly) and on a CodeTank cart (the window IS this ROM). The fix
# relocates the page to free low RAM at $0F00-$0F16 (below Stars' BSS
# at $0F80, above every packed image top). The 82 offsets below are the
# high bytes of the absolute operands referencing that page, computed
# by double-assembling the git-recovered source (686fe03^:dev/projects/
# tms9918_orbital_pool/TMS_OrbitalPool.asm) at state=$4000 vs $0F00 and
# diffing — every diff is a lone $40→$0F high byte. The SHA-256 guard
# pins the exact shipped image so a regenerated .txt can never be
# silently mis-patched.
ORBITAL_IMAGE_SHA256 = (
    "9e3a74c9dc666968583d41297f4a1bb56f0b7b06a9f3ec315202a6f09e6005fc")
ORBITAL_STATE_PATCH = [
    4, 7, 10, 15, 30, 36, 65, 70, 73, 78, 95, 98, 103, 129, 137, 145,
    153, 161, 169, 176, 184, 191, 216, 221, 226, 247, 252, 258, 289,
    300, 308, 315, 318, 327, 350, 357, 362, 386, 391, 412, 426, 434,
    450, 455, 463, 479, 581, 600, 620, 632, 655, 677, 689, 711, 714,
    723, 782, 981, 998, 1017, 1022, 1029, 1040, 1045, 1048, 1057, 1063,
    1073, 1081, 1102, 1278, 1286, 1307, 1315, 1469, 1486, 1495, 1508,
    1519, 1525, 1531, 1537,
]

# (The TEST cartridge was retired June 2026 — Clone deleted, Split kept only
#  as the standalone DevBench sketch sketchs/tms9918/demo_split/.)


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
        "-I", str(LIB_ROGUE), "-I", str(asm.parent),
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
# Woz-hex parsing + ROM->RAM packer plumbing (GAME6)
# ---------------------------------------------------------------------------
def parse_woz_txt(path: pathlib.Path) -> tuple[list[tuple[int, bytes]], int | None]:
    """Parse a Wozmon-hex .txt (the Memory::loadHexDump dialect) into
    ([(dest_addr, payload_bytes)...], run_addr | None).

    Handled: `AAAA: HH HH ..` lines, continuation lines (bare hex data
    keeps filling from the current address), multi-zone (a new `AAAA:`
    that isn't contiguous starts a new segment), `//` `#` `;` comments
    (leading or inline), and the trailing `AAAA R` run-address line.
    NOT handled (never used by the shipped TMS artefacts): the ACI `T`
    turbo prefix and `X` block markers — parsing errors out on them so
    a mis-parse can't silently truncate an EPROM payload."""
    segments: list[tuple[int, bytearray]] = []
    cur_addr: int | None = None
    run_addr: int | None = None

    def put(addr: int, b: int) -> None:
        if segments and segments[-1][0] + len(segments[-1][1]) == addr:
            segments[-1][1].append(b)
        else:
            segments.append((addr, bytearray([b])))

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw
        for mark in ("//", ";", "#"):
            p = line.find(mark)
            if p >= 0:
                line = line[:p]
        line = line.strip()
        if not line:
            continue
        for tok in line.replace(":", ": ").split():
            if tok.endswith(":"):                       # address prefix
                cur_addr = int(tok[:-1], 16)
                continue
            if tok.upper().endswith("R"):               # run address
                run_addr = int(tok[:-1], 16)
                continue
            if len(tok) == 2 and cur_addr is not None:  # data byte
                put(cur_addr, int(tok, 16))
                cur_addr += 1
                continue
            raise SystemExit(f"{path.name}: unsupported token {tok!r} "
                             f"(T/X turbo markers are not packable)")
    if not segments:
        raise SystemExit(f"{path.name}: no data parsed")
    return [(a, bytes(d)) for (a, d) in segments], run_addr


def patch_orbital_state(image: bytes, label: str) -> bytes:
    """Apply ORBITAL_STATE_PATCH ($40xx state page -> $0Fxx). Guarded by
    the image hash + a $40 check at every offset — refuses anything but
    the exact shipped binary."""
    got = hashlib.sha256(image).hexdigest()
    if got != ORBITAL_IMAGE_SHA256:
        raise SystemExit(
            f"  ERROR: {label} image sha256 {got[:16]}… does not match the "
            f"pinned shipped artefact — the state-page patch offsets are "
            f"only valid for that exact binary (recompute them by "
            f"double-assembling the source at state=$4000 vs $0F00)")
    out = bytearray(image)
    for off in ORBITAL_STATE_PATCH:
        if out[off] != 0x40:
            raise SystemExit(
                f"  ERROR: {label} patch offset {off} holds "
                f"${out[off]:02X}, expected $40")
        out[off] = 0x0F
    print(f"  {label}: relocated state page $40xx->$0Fxx "
          f"({len(ORBITAL_STATE_PATCH)} operands patched)", file=sys.stderr)
    return bytes(out)


def check_pack_ram(name: str, segments: list[tuple[int, bytes]]) -> None:
    """Every packed segment must land inside the Parmigiani dual-bank low
    RAM window [$0200, $1000) — anything else is unreachable on the real
    4K+4K layout (and the CodeTank window itself is ROM)."""
    for (dest, data) in segments:
        end = dest + len(data)
        if dest < PACK_RAM_LO or end > PACK_RAM_HI:
            raise SystemExit(
                f"  ERROR: {name} segment ${dest:04X}-${end - 1:04X} leaves "
                f"the dual-bank low RAM window "
                f"[${PACK_RAM_LO:04X}, ${PACK_RAM_HI:04X}) — not loadable "
                f"on real Parmigiani silicon")


def emit_pack_table_inc(inc_path: pathlib.Path,
                        progs: list[dict], menu_lines: list[str]) -> None:
    """Write the generated .inc consumed by codetank_game6_menu.asm.
    Each prog dict: {"name": str, "run": int,
                     "segs": [(rom_src, dest, length)...]}  (segs may be
    empty → run-in-place, the loader JMPs straight to `run`)."""
    L = [
        "; AUTO-GENERATED by tools/build_codetank_rom.py — DO NOT EDIT.",
        "; Per-program ROM->RAM segment tables + menu text for the GAME6",
        "; generic packer menu (codetank_game6_menu.asm).",
        "",
        f"NPROGS = {len(progs)}",
        "",
        "prog_seg_lo: .byte " + ", ".join(f"<prog{i}_segs"
                                          for i in range(len(progs))),
        "prog_seg_hi: .byte " + ", ".join(f">prog{i}_segs"
                                          for i in range(len(progs))),
        "prog_run_lo: .byte " + ", ".join(f"<${p['run']:04X}"
                                          for p in progs),
        "prog_run_hi: .byte " + ", ".join(f">${p['run']:04X}"
                                          for p in progs),
        "",
    ]
    for i, p in enumerate(progs):
        L.append(f"prog{i}_segs:                     ; {p['name']}"
                 + ("" if p["segs"] else " (run-in-place, no copy)"))
        for (src, dest, ln) in p["segs"]:
            L.append(f"        .word ${src:04X}, ${dest:04X}, ${ln:04X}")
        L.append("        .word $0000, $0000, $0000  ; end of list")
    L.append("")
    L.append("menu_text:")
    for ml in menu_lines[:-1]:                 # CR after every line…
        L.append(f'        .byte "{ml}", $0D' if ml else "        .byte $0D")
    L.append(f'        .byte "{menu_lines[-1]}"')   # …except the prompt
    L.append("        .byte 0")
    L.append("")
    inc_path.write_text("\n".join(L), encoding="utf-8")


# ---------------------------------------------------------------------------
# GAME1 — Tetris (lower) + menu + Galaga + Sokoban + Snake (upper)
# ---------------------------------------------------------------------------
def build_game1_lower_bank() -> bytes:
    """Lower 16 kB: Tetris/CodeTank prebuilt, full $4000-$7FFF run-in-place.
    Drop-in image from software/Apple-1_TMS_CC65/ (16 384 B exact).

    SELF-HEALING FALLBACK (juillet 2026): the external drop-in was removed
    from the tree, which made `--rom 1` fail entirely — so the UPPER bank
    (menu + Galaga + Sokoban + Snake) could no longer pick up library fixes
    destined for real-hardware EPROM burns. When the drop-in is absent, the
    Tetris image is recovered verbatim from the committed
    Codetank_GAME1.rom's lower half (bytes 0..16383 — `rom = lower + upper`)
    so the upper bank always rebuilds from current sources."""
    print("[GAME1] Lower bank (Tetris/CodeTank, full 16 kB):", file=sys.stderr)
    if not TETRIS_CT_BIN.exists():
        prev_rom = ROOT / "roms" / "codetank" / "Codetank_GAME1.rom"
        if prev_rom.exists() and prev_rom.stat().st_size == 2 * HALF_SIZE:
            print(f"  {TETRIS_CT_BIN} missing — recovering Tetris verbatim "
                  f"from {prev_rom} lower half", file=sys.stderr)
            return prev_rom.read_bytes()[:HALF_SIZE]
        raise SystemExit(
            f"ERROR: {TETRIS_CT_BIN} not found and no previous "
            f"Codetank_GAME1.rom to recover the lower bank from")
    return splat_full_bank(TETRIS_CT_BIN, "Tetris/CT ($4000-$7FFF)")


def build_game1_upper_bank() -> bytes:
    """Upper 16 kB — menu at $4000 dispatches to 3 games by entry address.
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
    print("\n[GAME1] Upper bank (menu + 3 games, run-in-place):",
          file=sys.stderr)
    menu    = assemble(MENU_ASM,    MENU_CFG,         "G1_menu",    0x0100)
    galaga  = assemble(GALAGA_ASM,  GALAGA_BANK_CFG,  "G1_Galaga",  0x4000)
    sokoban = assemble(SOKOBAN_ASM, SOKOBAN_BANK_CFG, "G1_Sokoban", 0x4000)
    snake   = assemble(SNAKE_ASM,   SNAKE_BANK_CFG,   "G1_Snake",   0x4000)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, menu,    0x0100, "Menu      ($4000-$40FF)")
    slot(bank, 0x0100, galaga,  0x2100, "Galaga    ($4100-$61FF)")
    slot(bank, 0x2200, sokoban, 0x1400, "Sokoban   ($6200-$75FF)")
    slot(bank, 0x3600, snake,   0x0A00, "Snake     ($7600-$7FFF)")
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
    """Upper 16 kB: TMS_Nyan_CodeTank, full $4000-$7FFF, run-in-place.
    Assembled from the mono-source sketch (TMS_Nyan_CodeTank.asm + nyan_rle
    data; tms9918_pad auto-linked) and padded to fill the bank."""
    print("\n[GAME2] Upper bank (TMS_Nyan_CodeTank, full 16 kB):",
          file=sys.stderr)
    nyan = assemble_multi([NYAN_ASM, NYAN_RLE_ASM], NYAN_CFG,
                          "G2_Nyan", HALF_SIZE)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, nyan, HALF_SIZE, "Nyan/CT   ($4000-$7FFF)")
    return bytes(bank)


# ---------------------------------------------------------------------------
# GAME3 — LOGO V2.6 (lower) + menu + Life + Mandel + Plasma (upper)
# ---------------------------------------------------------------------------
def build_game3_lower_bank() -> bytes:
    """Lower 16 kB: TMS_LOGO V2.6 at $4000-$7FFF, runs in place from ROM.
    Built with -D CODETANK_BUILD for the full feature set (on-bitmap text,
    speech bubbles, buffer editor)."""
    print("[GAME3] Lower bank (TMS_LOGO V2.6, run-in-place):", file=sys.stderr)
    logo = assemble_multi(
        [LOGO_V2_ASM, LOGO_V2_MATH_ASM, LOGO_V2_VDP_ASM,
         LOGO_V2_EMOTE_ASM, LOGO_V2_TEXT_ASM, LOGO_V2_BUBBLE_ASM,
         LOGO_V2_BUFED_ASM, LOGO_V2_SPRH_ASM],
        LOGO_V2_BANK_CFG, "G3_LogoV2", HALF_SIZE,
        extra_ca65_args=["-D", "CODETANK_BUILD"])
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, logo, HALF_SIZE, "LOGO V2.6 ($4000-$7FFF)")
    return bytes(bank)


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
# GAME4 — menu + Split + Vague + Hello (lower); demo_sprite_animals C (upper)
# ---------------------------------------------------------------------------
def build_game4_lower_bank() -> bytes:
    """Lower 16 kB — menu at $4000 dispatches to 3 TMS demos.
    Slot offsets pinned by each demo's bank cfg:
      menu     $4000-$40FF   (  256 B, apple1_codetank_game4_menu.cfg)
      Split    $4100-$50FF   (4 096 B, apple1_split_codetank_game4_bank.cfg)
      Vague    $5100-$60FF   (4 096 B, apple1_vague_codetank_game4_bank.cfg)
      Hello    $6100-$68FF   (2 048 B, apple1_hello_codetank_game4_bank.cfg)
      reserved $6900-$7FFF   (free, $FF-fill)
    Split auto-exits to Wozmon after ~10 s, Vague exits on ESC, Hello
    parks in an infinite loop — re-enter the menu with 4000R."""
    print("[GAME4] Lower bank (menu + Split + Vague + Hello, run-in-place):",
          file=sys.stderr)
    menu  = assemble(GAME4_MENU_ASM, GAME4_MENU_CFG, "G4_menu", 0x0100)
    split = assemble_multi(
        [SPLIT_ASM, SPLIT_M1_ASM, SPLIT_5S_ASM],
        SPLIT_BANK_CFG, "G4_Split", 0x1000)
    vague = assemble_multi(
        [VAGUE_ASM, SPLIT_M1_ASM], VAGUE_BANK_CFG, "G4_Vague", 0x1000)
    hello = assemble(HELLO_ASM, HELLO_BANK_CFG, "G4_Hello", 0x0800)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, menu,  0x0100, "Menu      ($4000-$40FF)")
    slot(bank, 0x0100, split, 0x1000, "Split     ($4100-$50FF)")
    slot(bank, 0x1100, vague, 0x1000, "Vague     ($5100-$60FF)")
    slot(bank, 0x2100, hello, 0x0800, "Hello     ($6100-$68FF)")
    return bytes(bank)


def build_game4_upper_bank() -> bytes:
    """Upper 16 kB: demo_sprite_animals (cc65 C + tms9918c runtime + the
    SCROLL-O-SPRITES fauna patterns), full $4000-$7FFF run-in-place."""
    print("\n[GAME4] Upper bank (demo_sprite_animals, cc65 C, full 16 kB):",
          file=sys.stderr)
    animals = build_c_codetank("G4_Animals", ANIMALS_C, [FAUNA_ASM])
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, animals, HALF_SIZE, "Animals/C ($4000-$7FFF)")
    return bytes(bank)


# ---------------------------------------------------------------------------
# GAME5 — nino-democ C (lower) + demo_screen1 C (upper)
# ---------------------------------------------------------------------------
def build_game5_lower_bank() -> bytes:
    """Lower 16 kB: nino-democ (cc65 C port of Nino Porcino's upstream
    demo menu), full $4000-$7FFF run-in-place."""
    print("[GAME5] Lower bank (nino-democ, cc65 C, full 16 kB):",
          file=sys.stderr)
    demo = build_c_codetank("G5_NinoDemoC", NINODEMOC_C)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, demo, HALF_SIZE, "NinoDemo  ($4000-$7FFF)")
    return bytes(bank)


def build_game5_upper_bank() -> bytes:
    """Upper 16 kB: demo_screen1 (cc65 C screen1 text/reverse/charset/
    sprites/input demo), full $4000-$7FFF run-in-place."""
    print("\n[GAME5] Upper bank (demo_screen1, cc65 C, full 16 kB):",
          file=sys.stderr)
    demo = build_c_codetank("G5_Screen1", SCREEN1_C)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, demo, HALF_SIZE, "Screen1   ($4000-$7FFF)")
    return bytes(bank)


# ---------------------------------------------------------------------------
# GAME6 — menu+packer + Maze3D (lower); SilBench (upper)
# ---------------------------------------------------------------------------
def build_game6_lower_bank() -> bytes:
    """Lower 16 kB — generic packer menu at $4000. Layout:
      menu+table $4000-$41FF  (  512 B, apple1_codetank_game6_menu.cfg)
      Maze3D     $4200-$5DFF  (7 168 B, run-in-place, resurrected source)
      images     $5E00-...    (packed Woz-hex payloads, 16-B aligned)
    OrbitalPool + Stars are parsed from their shipped .txt, RAM-window
    checked, and copied to $0280+ by the menu's loader at launch (their
    dest ranges overlap — fine, only one runs at a time)."""
    print("[GAME6] Lower bank (menu+packer + Maze3D + OrbitalPool + Stars):",
          file=sys.stderr)
    maze = assemble(MAZE3D_ASM, MAZE3D_BANK_CFG, "G6_Maze3D", 0x1C00)

    packs = []                       # (label, run_addr, [(dest, data)...])
    for (label, txt, patch) in (
            ("OrbitalPool", ORBITAL_TXT, True),
            ("Stars",       STARS_TXT,   False)):
        segs, run = parse_woz_txt(txt)
        if patch:
            if len(segs) != 1:
                raise SystemExit(f"  ERROR: {label}: expected one segment "
                                 f"to patch, got {len(segs)}")
            segs = [(segs[0][0], patch_orbital_state(segs[0][1], label))]
        check_pack_ram(label, segs)
        packs.append((label, run if run is not None else 0x0280, segs))

    # Assign ROM addresses: payloads land back-to-back (16-B aligned)
    # right after the Maze3D slot at $5E00.
    rom_cursor = 0x5E00
    progs = [{"name": "Maze3D", "run": 0x4200, "segs": []}]
    payload_at: list[tuple[int, bytes]] = []       # (bank_offset, data)
    for (label, run, segs) in packs:
        entry = {"name": label, "run": run, "segs": []}
        for (dest, data) in segs:
            entry["segs"].append((rom_cursor, dest, len(data)))
            payload_at.append((rom_cursor - 0x4000, data))
            rom_cursor = (rom_cursor + len(data) + 15) & ~15
        progs.append(entry)
    if rom_cursor > 0x8000:
        raise SystemExit(f"  ERROR: packed images overflow the bank by "
                         f"{rom_cursor - 0x8000} bytes")

    inc = BUILD / "codetank_game6_table.inc"
    BUILD.mkdir(parents=True, exist_ok=True)
    emit_pack_table_inc(inc, progs, [
        "",
        "P-LAB CODETANK GAME6",
        "",
        "1 = MAZE 3D (DUNGEON CRAWLER)",
        "2 = ORBITAL POOL (GRAVITY BILLIARDS)",
        "3 = STARS (PARALLAX STARFIELD)",
        "(SILBENCH ON UPPER JUMPER)",
        "",
        "PICK 1, 2 OR 3 ? ",
    ])
    menu = assemble_multi([GAME6_MENU_ASM], GAME6_MENU_CFG, "G6_menu",
                          0x0200, extra_ca65_args=["-I", str(BUILD)])

    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, menu, 0x0200, "Menu+pack ($4000-$41FF)")
    slot(bank, 0x0200, maze, 0x1C00, "Maze3D    ($4200-$5DFF)")
    for ((off, data), (label, _run, _segs)) in zip(payload_at, packs):
        slot(bank, off, data, len(data),
             f"{label:<9} (${0x4000 + off:04X}+, pack)")
    print(f"  pack window ends ${rom_cursor - 1:04X} "
          f"({0x8000 - rom_cursor} B free)", file=sys.stderr)
    return bytes(bank)


def build_game6_upper_bank() -> bytes:
    """Upper 16 kB: TMS_SilBench run-in-place. The shipped Woz-hex image
    is a NATIVE CodeTank build (686fe03^ apple1_silbench_codetank.cfg:
    CODE ro at $4000, BSS at $0200, ZP $00-$3F) — parse it, assert it is
    one contiguous $4000-based segment, splat at bank offset 0."""
    print("\n[GAME6] Upper bank (TMS_SilBench, run-in-place):",
          file=sys.stderr)
    segs, run = parse_woz_txt(SILBENCH_TXT)
    if len(segs) != 1 or segs[0][0] != 0x4000:
        raise SystemExit("  ERROR: TMS_SilBench.txt is not one contiguous "
                         "$4000 image — cannot run in place")
    if run not in (None, 0x4000):
        raise SystemExit(f"  ERROR: TMS_SilBench.txt run address "
                         f"${run:04X} != $4000")
    data = segs[0][1]
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, 0x0000, data, HALF_SIZE, "SilBench  ($4000-$7FFF)")
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


def build_game4() -> bytes:
    print("\n========== Codetank_GAME4.rom ==========", file=sys.stderr)
    lower = build_game4_lower_bank()
    upper = build_game4_upper_bank()
    rom = lower + upper
    assert len(rom) == ROM_SIZE
    return rom


def build_game5() -> bytes:
    print("\n========== Codetank_GAME5.rom ==========", file=sys.stderr)
    lower = build_game5_lower_bank()
    upper = build_game5_upper_bank()
    rom = lower + upper
    assert len(rom) == ROM_SIZE
    return rom


def build_game6() -> bytes:
    print("\n========== Codetank_GAME6.rom ==========", file=sys.stderr)
    lower = build_game6_lower_bank()
    upper = build_game6_upper_bank()
    rom = lower + upper
    assert len(rom) == ROM_SIZE
    return rom


def build_codetankdev() -> bytes:
    """CODETANKDEV.rom — the unified TMS9918 DevBench cartridge.

      Lower 16 kB: blank ($FF) flash slot. The in-app DevBench writes the
                   current asm / C TMS9918 build here at runtime and boots
                   jumper Lower -> 4000R (it preserves this upper bank).
      Upper 16 kB: the Applesoft TMS9918 interpreter (run-in-place at $4000),
                   loaded for BASIC .apf injection via jumper Upper -> 4000R.
    """
    print("\n========== CODETANKDEV.rom ==========", file=sys.stderr)
    print("[CODETANKDEV] Lower bank: blank $FF (runtime asm/C flash slot)",
          file=sys.stderr)
    lower = b"\xFF" * HALF_SIZE
    print("[CODETANKDEV] Upper bank (Applesoft TMS9918):", file=sys.stderr)
    ad = SK / "applesoft_tms9918"
    asoft = assemble_multi(
        [ad / "applesoft-tms9918.s", ad / "io.s"],
        ad / "applesoft_tms9918.cfg", "applesoft_tms9918", HALF_SIZE)
    upper = bytearray(b"\xFF" * HALF_SIZE)
    slot(upper, 0, asoft, HALF_SIZE, "applesoft-tms9918 (upper)")
    rom = lower + bytes(upper)
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
    "  Lower jumper: 4000R → Tetris/CodeTank (full 16 kB drop-in)\n"
    "  Upper jumper: 4000R → menu → 1=Galaga 2=Sokoban 3=Snake\n"
)

SIDECAR_GAME2 = (
    "Codetank_GAME2.rom — TMS9918 P-LAB CodeTank cartridge\n"
    "  Lower jumper: 4000R → TMS_Rogue (dungeon crawler)\n"
    "  Upper jumper: 4000R → TMS_Nyan_CodeTank (12-frame Mode III animation)\n"
)

SIDECAR_GAME3 = (
    "Codetank_GAME3.rom — TMS9918 P-LAB CodeTank cartridge\n"
    "  Lower jumper: 4000R → TMS_LOGO V2.6 turtle interpreter\n"
    "  Upper jumper: 4000R → menu → 1=Life 2=Mandel 3=Plasma\n"
)

SIDECAR_GAME4 = (
    "Codetank_GAME4.rom — TMS9918 P-LAB CodeTank cartridge (TMS demos)\n"
    "  Lower jumper: 4000R → menu → 1=Split 2=Vague 3=Hello\n"
    "  Upper jumper: 4000R → demo_sprite_animals (4 Fauna sprites, cc65 C)\n"
)

SIDECAR_GAME5 = (
    "Codetank_GAME5.rom — TMS9918 P-LAB CodeTank cartridge (Nino C demos)\n"
    "  Lower jumper: 4000R → nino-democ (1=Screen1 2=Screen2 0=Wozmon)\n"
    "  Upper jumper: 4000R → demo_screen1 (text/reverse/charset/sprites/input)\n"
)

SIDECAR_GAME6 = (
    "Codetank_GAME6.rom — TMS9918 P-LAB CodeTank cartridge (classics)\n"
    "  Lower jumper: 4000R → menu → 1=Maze3D 2=OrbitalPool 3=Stars\n"
    "                (OrbitalPool/Stars are copied ROM→RAM $0280 by the menu)\n"
    "  Upper jumper: 4000R → TMS_SilBench (29-test silicon benchmark menu)\n"
)

SIDECAR_CODETANKDEV = (
    "CODETANKDEV.rom — TMS9918 P-LAB unified DevBench cartridge\n"
    "  Lower jumper: 4000R → the DevBench's flashed asm/C build (blank $FF here)\n"
    "  Upper jumper: 4000R → Applesoft TMS9918 upper bank\n"
)


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument(
        "--rom", choices=("1", "2", "3", "4", "5", "6", "dev", "all"),
        default="all",
        help="Which CodeTank ROM to build (default: all — GAME1-6 + CODETANKDEV)",
    )
    args = ap.parse_args()

    need("ca65")
    need("ld65")
    if args.rom in ("4", "5", "all"):
        need("cl65")               # GAME4 upper / GAME5 are cc65 C builds

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

    if args.rom in ("4", "all"):
        rom4 = build_game4()
        write_rom(rom4, out_dir / "Codetank_GAME4.rom", SIDECAR_GAME4)

    if args.rom in ("5", "all"):
        rom5 = build_game5()
        write_rom(rom5, out_dir / "Codetank_GAME5.rom", SIDECAR_GAME5)

    if args.rom in ("6", "all"):
        rom6 = build_game6()
        write_rom(rom6, out_dir / "Codetank_GAME6.rom", SIDECAR_GAME6)

    if args.rom in ("dev", "all"):
        romd = build_codetankdev()
        write_rom(romd, out_dir / "CODETANKDEV.rom", SIDECAR_CODETANKDEV)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
