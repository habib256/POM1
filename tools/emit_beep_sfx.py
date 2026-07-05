#!/usr/bin/env python3
"""emit_beep_sfx.py -- 1-bit beeper SFX model -> ca65 table.

The command-line twin of src/sfxbeep/SfxAsmExport.cpp (formatSfxAsm): turns a
small JSON model into the linkable `.inc` table that dev/lib/beep/beep_sfx.asm
plays. Same format the beeper editor's "Export ASM" writes, so a table authored
either way is interchangeable and round-trips through parseSfxAsm.

Model JSON:
    { "name": "coin",
      "steps": [[96, 24], [64, 40]] }     # [period, length] pairs
  period = pitch (inner-delay count; 0 = REST/silence),
  length = duration in speaker half-cycles (1..255; 0 is the table terminator).

Usage:
    python3 tools/emit_beep_sfx.py song.json [-o out.inc]     # file or stdout
    python3 tools/emit_beep_sfx.py song.json --run "LDX #<sfx_coin ..."
Exit: 0 ok, 2 bad model.
"""
import argparse
import json
import re
import sys


def sanitize(raw: str) -> str:
    out = "".join(c.lower() if c.isalnum() else "_"
                  for c in raw if c.isalnum() or c in "_- ")
    out = re.sub(r"^[_0-9]+", "", out)
    return out or "sfx"


def emit(model: dict) -> str:
    name = sanitize(str(model.get("name", "sfx")))
    label = "sfx_" + name
    steps = model.get("steps", [])
    lines = [
        f"; {label} -- 1-bit beeper SFX (period,length steps; length 0 ends).",
        f"; Generated for dev/lib/beep/beep_sfx.asm -- `.include` then "
        f"LDX #<{label} / LDY #>{label} / JSR sfx_play.",
        f"{label}:",
    ]
    for i, st in enumerate(steps):
        if len(st) != 2:
            sys.exit(f"error: step {i} must be [period, length], got {st!r}")
        p, l = int(st[0]), int(st[1])
        if not (0 <= p <= 255) or not (1 <= l <= 255):
            sys.exit(f"error: step {i} out of range (period 0-255, length 1-255): {st!r}")
        lines.append(f"        .byte ${p:02X}, ${l:02X}          ; step {i}")
    lines.append("        .byte $00, $00          ; end")
    return "\n".join(lines) + "\n"


def main(argv) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("model", help="SFX model JSON")
    ap.add_argument("-o", "--out", help="output .inc (default: stdout)")
    args = ap.parse_args(argv)

    try:
        with open(args.model) as f:
            model = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"emit_beep_sfx: cannot read model: {e}", file=sys.stderr)
        return 2

    text = emit(model)
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
        print(f"emit_beep_sfx: wrote {args.out}", file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
