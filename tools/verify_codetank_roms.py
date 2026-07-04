#!/usr/bin/env python3
"""verify_codetank_roms.py — the "Claudio gate" for CodeTank EPROM burns.

Claudio Parmigiani burns these ROMs into a real 28c256 and runs them on a
Replica-1 1:1 with a real NMOS TMS9918A. Every difference between POM1's
tolerant defaults and his silicon is a way to disappoint him. This harness
runs every CodeTank bank under the HARSHEST conditions POM1 can model:

  --silicon-strict   openMSX slot-table VRAM access windows (too-fast $CC00
                     traffic drops, newest-wins) — the #1 real-silicon killer
                     (Galaga SAT corruption class, verified on hardware
                     2026-05-12).
  --vram-noise       mt19937 VRAM at power-on instead of POM1's deterministic
                     bistable $FF/$00 — warm P-LAB DRAM shows noise, so any
                     table/SAT/name region a program displays without
                     initialising renders garbage ONLY on real hardware.
                     (TMS9918-SPRITE_INIT.md §4.2 failure class.)
  --dram-refresh     the Replica-1's DRAM steals 4/65 CPU cycles — pacing
                     loops run slower than on POM1's default Briel/SRAM
                     model, shifting every VDP access phase.

For each ROM x jumper a scripted run boots the bank (4000R), walks the menu
with deterministic key injection (--paste-at-cycle), and checks:
  1. ZERO `[TMS9918 DROP` lines on stderr (a drop on POM1-strict means lost
     bytes on Claudio's chip);
  2. the framebuffer at every checkpoint is NOT blank/uniform (program alive
     and drawing — catches crashes, un-initialised modes, black screens);
  3. checkpoint PNGs land next to the report for eyeball review.

Exit 0 = every bank passed = the .rom files are burn-ready.
Exit 1 = at least one failure (report says which bank, which condition).
Exit 77 = POM1 binary not built (ctest SKIP convention).

Usage:
    python3 tools/verify_codetank_roms.py [--pom1 build/POM1] [--out DIR]
    python3 tools/verify_codetank_roms.py --quick   # boot checks only
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROMS = ROOT / "roms" / "codetank"

# Each scenario: (name, rom, jumper, [(cycle, keys)...], [checkpoint cycles])
# Key scripts are best-effort menu walks — they must at least reach a state
# that draws; wrong keys are harmlessly ignored by the programs.
M = 1_000_000
SCENARIOS = [
    # GAME1 lower — Tetris (Nino Porcino). RETURN starts, then a few moves.
    ("GAME1-lower-Tetris", "Codetank_GAME1.rom", "lower",
     [(6 * M, "\r"), (10 * M, "j"), (14 * M, "l"), (18 * M, "k")],
     [8 * M, 22 * M]),
    # GAME1 upper — menu -> Galaga (1), keyboard pick, fire through title.
    ("GAME1-upper-Galaga", "Codetank_GAME1.rom", "upper",
     [(6 * M, "1"), (12 * M, "1"), (18 * M, " "), (24 * M, "j")],
     [10 * M, 28 * M]),
    # GAME1 upper — menu -> Sokoban (2).
    ("GAME1-upper-Sokoban", "Codetank_GAME1.rom", "upper",
     [(6 * M, "2"), (12 * M, " "), (16 * M, "d"), (20 * M, "w")],
     [10 * M, 24 * M]),
    # GAME1 upper — menu -> Snake (3), keyboard pick, walls pick, play.
    ("GAME1-upper-Snake", "Codetank_GAME1.rom", "upper",
     [(6 * M, "3"), (12 * M, "1"), (16 * M, "1"), (20 * M, "w"), (24 * M, "d")],
     [10 * M, 28 * M]),
    # GAME2 lower — Rogue alone. Keyboard pick then a few moves.
    ("GAME2-lower-Rogue", "Codetank_GAME2.rom", "lower",
     [(8 * M, "1"), (14 * M, "j"), (18 * M, "l"), (22 * M, "k")],
     [12 * M, 26 * M]),
    # GAME2 upper — Nyan (free-running animation, no keys needed).
    ("GAME2-upper-Nyan", "Codetank_GAME2.rom", "upper",
     [],
     [6 * M, 14 * M]),
    # GAME3 lower — TMS LOGO V2.6 (boots to its REPL banner).
    ("GAME3-lower-Logo", "Codetank_GAME3.rom", "lower",
     [],
     [8 * M]),
    # GAME3 upper — menu -> Life (1) / then next pattern key.
    ("GAME3-upper-Life", "Codetank_GAME3.rom", "upper",
     [(6 * M, "1"), (16 * M, "k")],
     [12 * M, 22 * M]),
    # GAME3 upper — menu -> Mandel (2): slow progressive render.
    ("GAME3-upper-Mandel", "Codetank_GAME3.rom", "upper",
     [(6 * M, "2")],
     [20 * M]),
    # GAME3 upper — menu -> Plasma (3).
    ("GAME3-upper-Plasma", "Codetank_GAME3.rom", "upper",
     [(6 * M, "3")],
     [14 * M]),
    # GAME4 lower — menu -> Split (1). Auto-exits to Wozmon after ~600
    # frames (~10 M cycles): keep both checkpoints inside the run window.
    ("GAME4-lower-Split", "Codetank_GAME4.rom", "lower",
     [(6 * M, "1")],
     [9 * M, 14 * M]),
    # GAME4 lower — menu -> Vague (2): free-running boat-on-a-wave.
    ("GAME4-lower-Vague", "Codetank_GAME4.rom", "lower",
     [(6 * M, "2")],
     [10 * M, 20 * M]),
    # GAME4 lower — menu -> Hello (3): static HELLO POM1 screen.
    ("GAME4-lower-Hello", "Codetank_GAME4.rom", "lower",
     [(6 * M, "3")],
     [9 * M, 14 * M]),
    # GAME4 upper — demo_sprite_animals (free-running, no keys needed).
    ("GAME4-upper-Animals", "Codetank_GAME4.rom", "upper",
     [],
     [6 * M, 14 * M]),
    # NOTE: GAME5 (nino-democ / screen1) and GAME6 (Maze3D / OrbitalPool /
    # Stars / SilBench) were retired in the July-2026 cleanup (their .rom
    # banks + source demos removed from the tree). The canonical burn-ready
    # CodeTank library is GAME1-4; add scenarios back here if those banks
    # are ever rebuilt.
]

DROP_RE = re.compile(r"^\[TMS9918 DROP")


def png_is_boring(path: Path) -> bool:
    """True when the dumped frame is (nearly) uniform — program not drawing.
    Reads the raw PNG bytes: a uniform frame compresses to almost nothing;
    the border-only white boot screen lands ~200-400 B. Anything alive is
    well past 1 kB. Cheap, dependency-free heuristic backed by the hash in
    POM1's [GFX] stderr line for exact comparisons."""
    try:
        return path.stat().st_size < 900
    except OSError:
        return True


def run_scenario(pom1: Path, name, rom, jumper, keys, checkpoints,
                 out_dir: Path, quick: bool):
    """Returns (ok: bool, details: str). One POM1 run per checkpoint so each
    capture is deterministic (cycle-driven --paste-at-cycle mode)."""
    rom_path = ROMS / rom
    if not rom_path.exists():
        return False, f"missing ROM {rom_path}"
    cps = checkpoints[:1] if quick else checkpoints
    problems = []
    for cp in cps:
        png = out_dir / f"{name}_{cp // M}M.png"
        cmd = [str(pom1), "--headless", "--preset", "9",
               "--silicon-strict", "--vram-noise", "--dram-refresh",
               "--enable", "codetank",
               "--codetank-rom", str(rom_path),
               "--codetank-jumper", jumper,
               "--run", "0x4000"]
        for (cyc, k) in keys:
            if cyc < cp:
                cmd += ["--paste-at-cycle", str(cyc), k]
        cmd += ["--dump-after-cycles", str(cp), "--dump-tms-frame", str(png)]
        try:
            proc = subprocess.run(cmd, cwd=ROOT, capture_output=True,
                                  text=True, timeout=420)
        except subprocess.TimeoutExpired:
            problems.append(f"@{cp // M}M: TIMEOUT")
            continue
        drops = [ln for ln in proc.stderr.splitlines() if DROP_RE.match(ln)]
        if drops:
            sites = Counter(re.search(r"PC=\$(\w+)", d).group(1)
                            for d in drops if "PC=$" in d)
            problems.append(f"@{cp // M}M: {len(drops)} VDP DROPS "
                            f"(top PCs: {sites.most_common(3)})")
        if proc.returncode != 0:
            problems.append(f"@{cp // M}M: POM1 rc={proc.returncode}")
        elif png_is_boring(png):
            problems.append(f"@{cp // M}M: frame blank/uniform ({png.name})")
    if problems:
        return False, "; ".join(problems)
    return True, f"{len(cps)} checkpoint(s) clean"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pom1", default=None)
    ap.add_argument("--out", default=None,
                    help="checkpoint PNG dir (default: temp dir, printed)")
    ap.add_argument("--quick", action="store_true",
                    help="first checkpoint of each scenario only")
    ap.add_argument("--only", default=None,
                    help="substring filter on scenario names")
    args = ap.parse_args()

    pom1 = Path(args.pom1) if args.pom1 else ROOT / "build" / "POM1"
    if not pom1.exists():
        print("SKIP: POM1 not built (build/POM1 missing)")
        return 77

    out_dir = Path(args.out) if args.out else Path(
        tempfile.mkdtemp(prefix="codetank_verify_"))
    out_dir.mkdir(parents=True, exist_ok=True)

    scenarios = [s for s in SCENARIOS
                 if not args.only or args.only in s[0]]
    print(f"Claudio gate: {len(scenarios)} scenario(s), conditions = "
          f"silicon-strict + vram-noise + dram-refresh")
    print(f"Checkpoint PNGs -> {out_dir}\n")

    failures = 0
    for (name, rom, jumper, keys, cps) in scenarios:
        ok, detail = run_scenario(pom1, name, rom, jumper, keys, cps,
                                  out_dir, args.quick)
        tag = "PASS" if ok else "FAIL"
        print(f"  [{tag}] {name:24s} {detail}")
        if not ok:
            failures += 1

    print()
    if failures:
        print(f"FAIL: {failures}/{len(scenarios)} scenario(s) would "
              f"disappoint Claudio — fix before burning EPROMs.")
        return 1
    print(f"PASS: all {len(scenarios)} scenarios clean under "
          f"real-silicon conditions. ROMs are burn-ready.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
