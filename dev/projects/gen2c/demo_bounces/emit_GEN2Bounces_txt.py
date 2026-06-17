#!/usr/bin/env python3
"""Construit GEN2Bounces (C / GEN2 HGR) avec cl65 et émet son sidecar Woz-hex.

Vitrine des primitives vectorielles + nombres HUD + double buffering de gen2c.
Même invocation cl65 que le POM1 Bench pour la cible "Uncle Bernie GEN2 HGR (C)".

Sorties sous "software/Graphic HGR/":
    GEN2Bounces.bin   — binaire brut, origine $6000
    GEN2Bounces.txt   — Wozmon hex (AAAA: BB BB ...) + une ligne 6000R finale

Run:  python3 emit_GEN2Bounces_txt.py   (le Makefile l'appelle après le build)
"""
import pathlib
import subprocess
import sys

START_ADDR = 0x6000
BYTES_PER_LINE = 8

PROJ = pathlib.Path(__file__).resolve().parent
ROOT = PROJ.parents[3]                       # dev/projects/<name> -> repo root
CC65 = ROOT / "dev" / "cc65"
LIB = ROOT / "dev" / "lib"
GEN2C = LIB / "gen2c"
APPLE1C = LIB / "apple1c"
GFX = LIB / "gfx"
GFXLIB = GFX / "gfx-gen2.lib"
GEN2CFG = CC65 / "apple1_gen2_c.cfg"
OUT_DIR = ROOT / "software" / "Graphic HGR"


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out_bin = OUT_DIR / "GEN2Bounces.bin"
    out_txt = OUT_DIR / "GEN2Bounces.txt"

    cmd = [
        "cl65", "-t", "none", "-Oirs",
        "-C", str(GEN2CFG),
        "-I", str(GEN2C), "-I", str(APPLE1C), "-I", str(GFX),
        str(PROJ / "GEN2Bounces.c"),
        str(GEN2C / "gen2.c"),
        str(GEN2C / "gen2_blit.s"),
        str(APPLE1C / "apple1io.c"),
        str(APPLE1C / "apple1io_asm.s"),
        str(GFXLIB),
        "-o", str(out_bin),
    ]
    print("$ " + " ".join(cmd), file=sys.stderr)
    if not GFXLIB.is_file():
        subprocess.run(["make", "-C", str(GFX), "gen2"], check=True, cwd=str(ROOT))
    subprocess.run(cmd, check=True, cwd=str(ROOT))

    data = out_bin.read_bytes()
    lines: list[str] = []
    addr = START_ADDR
    for i in range(0, len(data), BYTES_PER_LINE):
        chunk = data[i : i + BYTES_PER_LINE]
        lines.append(f"{addr:04X}: " + " ".join(f"{b:02X}" for b in chunk))
        addr += len(chunk)
    lines.append(f"{START_ADDR:04X}R")
    out_txt.write_text("\n".join(lines) + "\n", encoding="ascii")

    print(f"Wrote {out_bin} ({len(data)} bytes)", file=sys.stderr)
    print(f"Wrote {out_txt}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
