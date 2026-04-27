#!/usr/bin/env python3
"""
Build a 32 kB P-LAB CodeTank ROM image for the TMS9918 graphic card.

Default layout (one ROM file, both halves used):

  Lower 16 kB ($4000-$7FFF when CodeTank board jumper = Lower):
    $4000-$40FF  Menu (codetank_menu.asm) - prints
                   1 = GALAGA
                   2 = SOKOBAN
                   3 = SNAKE
                   4 = LIFE
                   5 = TETRIS (jumper UP)
                 and JMPs to the chosen entry.
    $4100-$5DFF  TMS_A1GALAGA  (linked at $4100, 7 424 B slot)
    $5E00-$70FF  TMS_SOKOBAN   (linked at $5E00, 4 864 B slot)
    $7100-$79FF  TMS_SNAKE     (linked at $7100, 2 304 B slot)
    $7A00-$7FFF  TMS_LIFE      (linked at $7A00, 1 536 B slot)

  Upper 16 kB ($4000-$7FFF when CodeTank board jumper = Upper):
    $4000-$407F  Tetris launcher (tetris_loader.asm) - copies the payload
                 from $4080 down to $0280 in RAM, then JMPs $0280.
    $4080-$5DAB  Tetris payload (raw 7 308 B from software/tms9918/tetris.bin)
    $5DAC-$7FFF  $FF padding

The lower-bank games (Galaga, Sokoban, Snake, Life) all run **in place
from ROM**. Galaga/Sokoban/Snake fit in the bare 4 kB Apple-1 footprint;
Life uses two 884-B grids at $1000/$1400 so it needs at least 8 kB RAM.
Tetris (no source available - Nippur72/KickC) is a binary blob copied
into RAM at $0280-$1FAC, so it also needs >=8 kB DRAM. POM1 preset 8
ships 16 kB which covers everything.

Optional --layout=split: the original two-bank layout (Galaga in lower,
Sokoban in upper, no Snake / Tetris). Kept around for board-jumper
demonstrations - the menu-bank layout above is what preset 8 loads.

Usage:
    python3 tools/build_codetank_rom.py
    python3 tools/build_codetank_rom.py --layout=split -o roms/codetank/galaga_sokoban_split.rom
"""
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

# Menu-bank layout offsets within the lower 16 kB bank.
MENU_OFFSET    = 0x0000   # bank+$0000 = CPU $4000
GALAGA_OFFSET  = 0x0100   # bank+$0100 = CPU $4100
SOKOBAN_OFFSET = 0x1E00   # bank+$1E00 = CPU $5E00
SNAKE_OFFSET   = 0x3100   # bank+$3100 = CPU $7100
LIFE_OFFSET    = 0x3A00   # bank+$3A00 = CPU $7A00
MENU_SLOT_SIZE    = GALAGA_OFFSET  - MENU_OFFSET     # 0x0100 =   256 B
GALAGA_SLOT_SIZE  = SOKOBAN_OFFSET - GALAGA_OFFSET   # 0x1D00 = 7 424 B
SOKOBAN_SLOT_SIZE = SNAKE_OFFSET   - SOKOBAN_OFFSET  # 0x1300 = 4 864 B (TIGHT)
SNAKE_SLOT_SIZE   = LIFE_OFFSET    - SNAKE_OFFSET    # 0x0900 = 2 304 B
LIFE_SLOT_SIZE    = HALF_SIZE      - LIFE_OFFSET     # 0x0600 = 1 536 B

# Upper-bank Tetris layout: 128-byte loader + raw payload right after.
TETRIS_LOADER_OFFSET   = 0x0000     # bank+$0000 = CPU $4000
TETRIS_PAYLOAD_OFFSET  = 0x0080     # bank+$0080 = CPU $4080
TETRIS_LOADER_SLOT     = TETRIS_PAYLOAD_OFFSET - TETRIS_LOADER_OFFSET  # 128 B

# Sources moved from software/tms9918/ to dev/projects/<name>/ during the
# 2026-04 dev/ reorg. Each project now owns its own .asm + .cfg files;
# the runtime artifacts (tetris.bin, *.bin/.txt outputs) still live under
# software/tms9918/.
DEV = ROOT / "dev" / "projects"

MENU_ASM          = DEV / "tms9918_codetank_menu" / "codetank_menu.asm"
MENU_CFG          = DEV / "tms9918_codetank_menu" / "apple1_codetank_menu.cfg"
GALAGA_ASM        = DEV / "tms9918_galaga"        / "TMS_Galaga.asm"
GALAGA_BANK_CFG   = DEV / "tms9918_galaga"        / "apple1_galaga_codetank_bank.cfg"
GALAGA_SPLIT_CFG  = DEV / "tms9918_galaga"        / "apple1_galaga_codetank.cfg"
SOKOBAN_ASM       = DEV / "tms9918_sokoban"       / "TMS_Sokoban.asm"
SOKOBAN_BANK_CFG  = DEV / "tms9918_sokoban"       / "apple1_sokoban_codetank_bank.cfg"
SOKOBAN_SPLIT_CFG = DEV / "tms9918_sokoban"       / "apple1_sokoban_codetank.cfg"
SNAKE_ASM         = DEV / "tms9918_snake"         / "TMS_Snake.asm"
SNAKE_BANK_CFG    = DEV / "tms9918_snake"         / "apple1_snake_codetank_bank.cfg"
LIFE_ASM          = DEV / "tms9918_life"          / "TMS_Life.asm"
LIFE_BANK_CFG     = DEV / "tms9918_life"          / "apple1_life_codetank_bank.cfg"
TETRIS_LOADER_ASM = DEV / "tms9918_tetris_loader" / "tetris_loader.asm"
TETRIS_LOADER_CFG = DEV / "tms9918_tetris_loader" / "apple1_tetris_loader.cfg"
TETRIS_BIN        = TMS / "tetris.bin"
LIB_APPLE1        = ROOT / "dev" / "lib" / "apple1"
LIB_M6502         = ROOT / "dev" / "lib" / "m6502"
LIB_TMS           = ROOT / "dev" / "lib" / "tms9918"
LIB_SOKOBAN       = ROOT / "dev" / "lib" / "sokoban"
LIB_HGR           = ROOT / "dev" / "lib" / "hgr"


def need(tool: str) -> None:
    if shutil.which(tool) is None:
        raise SystemExit(
            f"{tool} not found in PATH. Install cc65 (Debian/Ubuntu: "
            f"`sudo apt install cc65`)."
        )


def assemble(asm: pathlib.Path, cfg: pathlib.Path, name: str,
             max_size: int) -> bytes:
    """Run ca65 + ld65 against `cfg`, return the assembled bytes.

    Adds dev/lib/{apple1,m6502,tms9918} to the include path so projects
    that .include shared lib helpers (print.asm, prng16.asm, etc.) link
    cleanly. The asm file's own directory is also exposed so a project's
    private .inc files (sprite tables, level data) keep working.
    """
    BUILD.mkdir(parents=True, exist_ok=True)
    obj = BUILD / f"{name}.o"
    binp = BUILD / f"{name}.bin"
    subprocess.run(
        ["ca65",
         "-I", str(LIB_APPLE1),
         "-I", str(LIB_M6502),
         "-I", str(LIB_TMS),
         "-I", str(LIB_SOKOBAN),
         "-I", str(LIB_HGR),
         "-I", str(asm.parent),
         "-o", str(obj), str(asm)],
        check=True, cwd=str(ROOT),
    )
    subprocess.run(
        ["ld65", "-C", str(cfg), "-o", str(binp), str(obj)],
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
         name: str, *, ascii_pad: bool = False) -> None:
    """Copy `data` into the bank at `offset`, log slot usage."""
    pad = slot_size - len(data)
    print(
        f"  {name:<28} {len(data):5d} B / {slot_size:5d} B slot "
        f"({len(data)*100/slot_size:5.1f}%, {pad:5d} B padding)",
        file=sys.stderr,
    )
    bank[offset:offset + len(data)] = data


def build_lower_bank() -> bytes:
    """Lower 16 kB: menu + 4 TMS games (Galaga, Sokoban, Snake, Life)
    all running in place from ROM. Bank is packed tight - Sokoban's
    slot has only 100 B headroom over its current code."""
    print("[CodeTank] Lower bank (menu + 4 TMS games, run-in-place):",
          file=sys.stderr)
    menu    = assemble(MENU_ASM,     MENU_CFG,         "codetank_menu",
                       MENU_SLOT_SIZE)
    galaga  = assemble(GALAGA_ASM,   GALAGA_BANK_CFG,  "TMS_Galaga_bank",
                       GALAGA_SLOT_SIZE)
    sokoban = assemble(SOKOBAN_ASM,  SOKOBAN_BANK_CFG, "TMS_Sokoban_bank",
                       SOKOBAN_SLOT_SIZE)
    snake   = assemble(SNAKE_ASM,    SNAKE_BANK_CFG,   "TMS_Snake_bank",
                       SNAKE_SLOT_SIZE)
    life    = assemble(LIFE_ASM,     LIFE_BANK_CFG,    "TMS_Life_bank",
                       LIFE_SLOT_SIZE)
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, MENU_OFFSET,    menu,    MENU_SLOT_SIZE,    "menu       ($4000-$40FF)")
    slot(bank, GALAGA_OFFSET,  galaga,  GALAGA_SLOT_SIZE,  "Galaga     ($4100-$5DFF)")
    slot(bank, SOKOBAN_OFFSET, sokoban, SOKOBAN_SLOT_SIZE, "Sokoban    ($5E00-$70FF)")
    slot(bank, SNAKE_OFFSET,   snake,   SNAKE_SLOT_SIZE,   "Snake      ($7100-$79FF)")
    slot(bank, LIFE_OFFSET,    life,    LIFE_SLOT_SIZE,    "Life       ($7A00-$7FFF)")
    return bytes(bank)


def build_upper_bank_tetris() -> bytes:
    """Upper 16 kB: tiny loader at $4000 + raw Tetris payload at $4080."""
    print("\n[CodeTank] Upper bank (Tetris launcher + payload):",
          file=sys.stderr)
    loader = assemble(TETRIS_LOADER_ASM, TETRIS_LOADER_CFG,
                      "tetris_loader", TETRIS_LOADER_SLOT)
    payload = TETRIS_BIN.read_bytes()
    if not payload:
        raise SystemExit(f"{TETRIS_BIN}: empty file")
    payload_room = HALF_SIZE - TETRIS_PAYLOAD_OFFSET
    if len(payload) > payload_room:
        raise SystemExit(
            f"tetris.bin is {len(payload)} B, exceeds upper-bank payload "
            f"slot of {payload_room} B"
        )
    bank = bytearray(b"\xFF" * HALF_SIZE)
    slot(bank, TETRIS_LOADER_OFFSET, loader,  TETRIS_LOADER_SLOT,
         "tetris loader  ($4000-$407F)")
    slot(bank, TETRIS_PAYLOAD_OFFSET, payload, payload_room,
         "tetris payload ($4080+)   ")
    return bytes(bank)


def build_split_halves() -> tuple[bytes, bytes]:
    """Two 16 kB halves: Galaga at $4000 in lower, Sokoban at $4000
    in upper. Pick a game by moving the board jumper."""
    print("[CodeTank] split-bank layout (lower=Galaga, upper=Sokoban):",
          file=sys.stderr)
    galaga  = assemble(GALAGA_ASM,  GALAGA_SPLIT_CFG,  "TMS_Galaga_split",
                       HALF_SIZE)
    sokoban = assemble(SOKOBAN_ASM, SOKOBAN_SPLIT_CFG, "TMS_Sokoban_split",
                       HALF_SIZE)
    lower = bytearray(b"\xFF" * HALF_SIZE); slot(lower, 0, galaga,  HALF_SIZE,
                                                 "Galaga  (lower 16 kB)    ")
    upper = bytearray(b"\xFF" * HALF_SIZE); slot(upper, 0, sokoban, HALF_SIZE,
                                                 "Sokoban (upper 16 kB)    ")
    return bytes(lower), bytes(upper)


def write_sidecar(path: pathlib.Path, body: str) -> None:
    sidecar = path.with_suffix(".txt")
    sidecar.write_text(body, encoding="ascii")
    print(f"  Sidecar:  {sidecar}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--layout",
        choices=("menu", "split"),
        default="menu",
        help="ROM layout (default: menu)",
    )
    ap.add_argument(
        "-o", "--output",
        type=pathlib.Path,
        default=None,
        help="output ROM path (default depends on --layout)",
    )
    ap.add_argument(
        "--no-sidecar",
        action="store_true",
        help="don't write the .txt sidecar description next to the ROM",
    )
    args = ap.parse_args()

    need("ca65")
    need("ld65")

    out = args.output or (
        ROOT / "roms" / "codetank" /
        ("galaga_sokoban_menu.rom" if args.layout == "menu"
         else "galaga_sokoban_split.rom")
    )
    out.parent.mkdir(parents=True, exist_ok=True)

    if args.layout == "menu":
        lower = build_lower_bank()
        upper = build_upper_bank_tetris()
        rom = lower + upper
        sidecar_body = (
            "TMS9918: Galaga / Sokoban / Snake / Life (lower jumper, menu) "
            "+ Tetris (upper jumper, auto-launch). Type 4000R.\n"
        )
    else:
        lower, upper = build_split_halves()
        rom = lower + upper
        sidecar_body = (
            "TMS9918 split bank: Lower = Galaga, Upper = Sokoban. "
            "Pick with the board jumper, then 4000R.\n"
        )

    assert len(rom) == ROM_SIZE, len(rom)
    out.write_bytes(rom)
    print(
        f"\n[CodeTank] Wrote {out}  ({len(rom)} bytes)\n"
        f"  Wozmon entry: 4000R   (after plugging the CodeTank card)",
        file=sys.stderr,
    )
    if not args.no_sidecar:
        write_sidecar(out, sidecar_body)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
