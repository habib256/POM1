#!/usr/bin/env python3
"""Build the two A-1-CrazyCycle variants into software/Graphic HGR/.

Both bundle the UBERNIE HGR picture as a second hex zone at $2000, so each
single .txt carries code ($E000) + image — load it and `E000R`.

  A-1-CrazyCycle.txt          FANTASY  — GEN2 HGR on a 1:1 / SRAM (Briel) host
                              or POM1 with DRAM refresh OFF. 65 CPU cycles per
                              scanline (1 CPU cycle == 1 video cycle).

  A-1-CrazyCycle-SILICON.txt  SILICON  — GEN2 HGR on a real DRAM Apple-1 or
                              POM1 with DRAM refresh ON. 61 CPU cycles + 4
                              hardware refresh stalls = 65 beam cycles per
                              scanline, so the beam-raced window stays put.

A-1-CrazyCycle.asm is the single source of truth (FANTASY by default, SILICON
under `-D SILICON`). For Uncle Bernie this script also emits a standalone,
conditional-free A-1-CrazyCycle-SILICON.asm (the SILICON branches resolved) so
the real-DRAM card has a readable, directly-assemblable source — and it asserts
that source builds byte-identically to the `-D SILICON` build.
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]                      # repo root (sketchs/gen2/<demo>/..)
sys.path.insert(0, str(ROOT / "dev" / "cc65"))
from emit_woz import emit                    # noqa: E402

ASSET = ROOT / "sdcard" / "NONO" / "HGR" / "UBERNIE#062000"
OUTDIR = ROOT / "software" / "Graphic HGR"
SRC = "A-1-CrazyCycle.asm"
SIL_SRC = "A-1-CrazyCycle-SILICON.asm"


def flatten_silicon(text):
    """Resolve `.ifdef/.ifndef SILICON` blocks with SILICON DEFINED, leaving
    every other directive (table .if, .assert, …) untouched. The demo's SILICON
    blocks never nest other conditionals, so the next `.endif` always matches."""
    out, lines, i = [], text.splitlines(keepends=True), 0
    while i < len(lines):
        s = lines[i].strip()
        is_ifdef = s.startswith(".ifdef SILICON")
        is_ifndef = s.startswith(".ifndef SILICON")
        if is_ifdef or is_ifndef:
            active = is_ifdef            # SILICON is defined in this output
            i += 1
            true_branch, false_branch, in_else, depth = [], [], False, 0
            while i < len(lines):
                t = lines[i].strip()
                if t.startswith(".if"):
                    depth += 1
                elif t == ".else" and depth == 0:
                    in_else = True
                    i += 1
                    continue
                elif t.startswith(".endif"):
                    if depth == 0:
                        i += 1
                        break
                    depth -= 1
                (false_branch if in_else else true_branch).append(lines[i])
                i += 1
            out.extend(true_branch if active else false_branch)
        else:
            out.append(lines[i])
            i += 1
    return "".join(out)


def build(asm, out_txt, asflags, label):
    return emit(
        asm_files=[asm],
        lib_dirs=["apple1", "gen2"],
        cfg="apple1_gen2.cfg",
        out_txt=OUTDIR / out_txt,
        start_addr=0xE000,
        extra_zones=[(0x2000, ASSET)],
        project_dir=str(HERE),
        asflags=asflags,
        header_lines=[f"// A-1-CrazyCycle ({label}) - GEN2 HGR beam-race demo"],
    )


# 1. FANTASY (65-cycle slots) straight from the conditional source.
build(SRC, "A-1-CrazyCycle.txt", (), "FANTASY: 65 CPU cycles/scanline, 1:1 host")

# 2. Emit the standalone SILICON source for Uncle Bernie (conditionals resolved).
banner = (
    ";  A-1-CrazyCycle-SILICON.asm  -  GENERATED from A-1-CrazyCycle.asm.\n"
    ";  SILICON build: 61 CPU cycles + 4 DRAM refresh stalls = 65 beam cycles\n"
    ";  per scanline, for a real DRAM Apple-1 / POM1 with refresh ON.\n"
    ";  DO NOT EDIT here - edit A-1-CrazyCycle.asm and re-run build.py.\n"
    ";  Assemble: ca65 -I <apple1> -I <gen2> A-1-CrazyCycle-SILICON.asm\n\n"
)
(HERE / SIL_SRC).write_text(banner + flatten_silicon((HERE / SRC).read_text()))

# 3. Build SILICON from that standalone source -> the shipped .txt.
build(SIL_SRC, "A-1-CrazyCycle-SILICON.txt", (),
      "SILICON: 61 CPU + 4 refresh = 65 beam, real DRAM / refresh-on")

# 4. Assert the standalone source == the `-D SILICON` build of the conditional one.
build(SRC, "_silcheck.txt", ["-D", "SILICON"], "silicon-check")
a = (ROOT / "build" / "A-1-CrazyCycle-SILICON.bin").read_bytes()
b = (ROOT / "build" / "A-1-CrazyCycle.bin").read_bytes()
(OUTDIR / "_silcheck.txt").unlink(missing_ok=True)
assert a == b, "standalone SILICON asm diverges from -D SILICON build!"
print("OK: standalone SILICON source is byte-identical to the -D SILICON build")
