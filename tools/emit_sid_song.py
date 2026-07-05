#!/usr/bin/env python3
"""emit_sid_song.py -- SID song model -> ca65 table.

Command-line twin of src/sidtrack/SidSongAsmExport.cpp (formatSongAsm): turns a
small JSON model into the linkable `.inc` table that dev/lib/sid/sid_player.asm
plays. Same format the SID tracker's "Export ASM" writes.

Model JSON:
    { "name": "tune",
      "rows": [[57, "TRI", 4], ["OFF", 0, 2], ["TIE", 0, 1], [64, "PULSE", 4]] }
  row = [note, ctrl, frames]
    note   : 0..95 (index into sid_notes.inc), or "OFF"/$FE, or "TIE"/$FF
    ctrl   : waveform -- one of "TRI"/"SAW"/"PULSE"/"NOISE" or a numeric mask
    frames : row duration in ticks (1..255; 0 is the terminator)

Usage: python3 tools/emit_sid_song.py song.json [-o out.inc]
Exit: 0 ok, 2 bad model.
"""
import argparse
import json
import re
import sys

WAVE = {"TRI": 0x10, "SAW": 0x20, "PULSE": 0x40, "NOISE": 0x80}
NOTE_OFF, NOTE_TIE = 0xFE, 0xFF
_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def sanitize(raw: str) -> str:
    out = "".join(c.lower() if c.isalnum() else "_"
                  for c in raw if c.isalnum() or c in "_- ")
    out = re.sub(r"^[_0-9]+", "", out)
    return out or "song"


def note_name(n: int) -> str:
    if n == NOTE_OFF:
        return "---"
    if n == NOTE_TIE:
        return "==="
    if n > 95:
        return "???"
    return f"{_NAMES[n % 12]}{n // 12}"


def parse_note(v) -> int:
    if isinstance(v, str):
        u = v.strip().upper()
        if u in ("OFF", "---"):
            return NOTE_OFF
        if u in ("TIE", "==="):
            return NOTE_TIE
        return int(u, 0)          # allow "$39" / "0x39" / "57"
    return int(v)


def parse_ctrl(v) -> int:
    if isinstance(v, str) and v.strip().upper() in WAVE:
        return WAVE[v.strip().upper()]
    return int(v)


def emit(model: dict) -> str:
    name = sanitize(str(model.get("name", "song")))
    label = "song_" + name
    rows = model.get("rows", [])
    lines = [
        f"; {label} -- SID song for dev/lib/sid/sid_player.asm (note,ctrl,frames rows;",
        f"; frames 0 ends). LDX #<{label} / LDY #>{label} / JSR sid_play_start, "
        f"then JSR sid_play_tick per frame.",
        f"{label}:",
    ]
    for i, r in enumerate(rows):
        if len(r) != 3:
            sys.exit(f"error: row {i} must be [note, ctrl, frames], got {r!r}")
        n, ct, fr = parse_note(r[0]), parse_ctrl(r[1]), int(r[2])
        if not (0 <= n <= 255) or not (0 <= ct <= 255) or not (1 <= fr <= 255):
            sys.exit(f"error: row {i} out of range (frames 1-255): {r!r}")
        lines.append(f"        .byte ${n:02X}, ${ct:02X}, ${fr:02X}   ; {note_name(n)}")
    lines.append("        .byte $00, $00, $00   ; end")
    return "\n".join(lines) + "\n"


def main(argv) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("model", help="SID song model JSON")
    ap.add_argument("-o", "--out", help="output .inc (default: stdout)")
    args = ap.parse_args(argv)

    try:
        with open(args.model) as f:
            model = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"emit_sid_song: cannot read model: {e}", file=sys.stderr)
        return 2

    text = emit(model)
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
        print(f"emit_sid_song: wrote {args.out}", file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
