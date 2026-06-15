#!/usr/bin/env python3
"""Construit GEN2Countdown (C / GEN2 HGR) avec cl65 et émet son sidecar Woz-hex.

Comme GEN2Snake, c'est un build C / cc65 : on appelle directement `cl65` avec la
MEME invocation que le POM1 Bench pour la cible "Uncle Bernie GEN2 HGR (C)" — le
linker config GEN2 C + le runtime gen2c + la base texte apple1c, le tout à $6000.

Sorties sous "software/Graphic HGR/":
    GEN2Countdown.bin   — binaire brut, origine $6000
    GEN2Countdown.txt   — Wozmon hex (AAAA: BB BB ...) + une ligne 6000R finale

Run:  python3 emit_GEN2Countdown_txt.py   (le Makefile l'appelle après le build)
"""
import pathlib
import subprocess
import sys

START_ADDR = 0x6000
BYTES_PER_LINE = 8

PROJ = pathlib.Path(__file__).resolve().parent
ROOT = PROJ.parents[2]                       # dev/projects/<name> -> repo root
CC65 = ROOT / "dev" / "cc65"
LIB = ROOT / "dev" / "lib"
GEN2C = LIB / "gen2c"
APPLE1C = LIB / "apple1c"
GEN2CFG = CC65 / "apple1_gen2_c.cfg"
OUT_DIR = ROOT / "software" / "Graphic HGR"


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out_bin = OUT_DIR / "GEN2Countdown.bin"
    out_txt = OUT_DIR / "GEN2Countdown.txt"

    cmd = [
        "cl65", "-t", "none", "-Oirs",
        "-C", str(GEN2CFG),
        "-I", str(GEN2C), "-I", str(APPLE1C),
        str(PROJ / "GEN2Countdown.c"),
        str(GEN2C / "gen2.c"),
        str(GEN2C / "gen2_blit.s"),
        str(APPLE1C / "apple1io.c"),
        str(APPLE1C / "apple1io_asm.s"),
        "-o", str(out_bin),
    ]
    print("$ " + " ".join(cmd), file=sys.stderr)
    subprocess.run(cmd, check=True, cwd=str(ROOT))

    # Émet le .txt Woz-hex à partir du .bin lié.
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
