#!/usr/bin/env python3
"""Build GEN2TextHello (C / GEN2 TEXT) with cl65 and emit its Woz-hex sidecar.

Like GEN2Lores and GEN2Countdown, this is a C / cc65 build: we call `cl65`
directly with the SAME invocation the POM1 Bench fires for the "Uncle Bernie
GEN2 HGR (C)" target — the GEN2 C linker config + the gen2c runtime + the
apple1c text base, all at $6000. TEXT mode rides the same gen2c runtime
(the $C250-$C257 soft switches are macros in gen2.h), so the "GEN2 HGR"
target works as-is.

Outputs under "software/Graphic HGR/":
    GEN2TextHello.bin   - raw binary, origin $6000
    GEN2TextHello.txt   - Wozmon hex (AAAA: BB BB ...) + a final 6000R line

Run:  python3 emit_GEN2TextHello_txt.py   (the Makefile calls it after build)
"""
import pathlib
import subprocess
import sys

START_ADDR = 0x6000
BYTES_PER_LINE = 8

PROJ = pathlib.Path(__file__).resolve().parent
ROOT = PROJ.parents[3]                       # dev/projects/<group>/<name> -> repo root
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
    out_bin = OUT_DIR / "GEN2TextHello.bin"
    out_txt = OUT_DIR / "GEN2TextHello.txt"

    cmd = [
        "cl65", "-t", "none", "-Oirs",
        "-C", str(GEN2CFG),
        "-I", str(GEN2C), "-I", str(APPLE1C), "-I", str(GFX),
        str(PROJ / "GEN2TextHello.c"),
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

    # Emit the .txt Woz-hex sidecar from the linked .bin.
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
