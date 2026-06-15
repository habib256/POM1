#!/usr/bin/env python3
"""Build GEN2Snake (C / GEN2 HGR) with cl65 and emit its Woz-hex sidecar.

Unlike the asm projects (which share dev/cc65/emit_woz.py — a ca65/ld65 path),
this is a C / cc65 build, so it drives `cl65` directly with the SAME invocation
the POM1 Bench uses for the "Uncle Bernie GEN2 HGR (C)" target: the GEN2 C
linker config + the gen2c runtime + the shared apple1c text base, all at $6000.

Outputs land under software/Telemetry/:
    GEN2Snake.bin   — raw binary, origin $6000
    GEN2Snake.txt   — Wozmon hex (AAAA: BB BB ...) + a trailing 6000R run line

Run:  python3 emit_GEN2Snake_txt.py   (the Makefile calls this after the build)
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
TELEM = LIB / "telemetry"
GEN2CFG = CC65 / "apple1_gen2_c.cfg"
OUT_DIR = ROOT / "software" / "Telemetry"


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out_bin = OUT_DIR / "GEN2Snake.bin"
    out_txt = OUT_DIR / "GEN2Snake.txt"

    cmd = [
        "cl65", "-t", "none", "-Oirs",
        "-C", str(GEN2CFG),
        "-I", str(GEN2C), "-I", str(APPLE1C), "-I", str(TELEM),
        str(PROJ / "GEN2Snake.c"),
        str(GEN2C / "gen2.c"),
        str(GEN2C / "gen2_blit.s"),
        str(APPLE1C / "apple1io.c"),
        str(APPLE1C / "apple1io_asm.s"),
        "-o", str(out_bin),
    ]
    print("$ " + " ".join(cmd), file=sys.stderr)
    subprocess.run(cmd, check=True, cwd=str(ROOT))

    # Emit the Woz-hex .txt from the linked .bin.
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
