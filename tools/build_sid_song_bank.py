#!/usr/bin/env python3
"""build_sid_song_bank.py -- generate the 50-tune SID song bank.

Single source for two mirror outputs (like the beeper bank):
  dev/lib/sid/sid_song_bank50.inc   game-loadable ca65 tables (sid_player.asm)
  src/sidtrack/SidSongBank.h        embedded C++ text (parsed by parseSongAsm)

Melodies are written in a tiny note DSL: space-separated tokens "NOTE/frames"
(frames optional, default = QUARTER). NOTE is like C4 / F#5 / A#3 ; "-" is a
rest (gate off) ; "=" is a tie (hold). Recognisable public-domain / famous
phrases; approximate but singable. Rerun after edits:
    python3 tools/build_sid_song_bank.py
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT_INC = ROOT / "dev" / "lib" / "sid" / "sid_song_bank50.inc"
OUT_HDR = ROOT / "src" / "sidtrack" / "SidSongBank.h"

WAVE = {"TRI": 0x10, "SAW": 0x20, "PULSE": 0x40, "NOISE": 0x80}
SEMI = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6,
        "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
Q, E, H, S, DQ = 12, 6, 24, 3, 18   # quarter/eighth/half/sixteenth/dotted-qtr

OFF, TIE = 0xFE, 0xFF


def note(tok):
    if tok in ("-", "OFF"):
        return OFF
    if tok in ("=", "TIE"):
        return TIE
    m = re.fullmatch(r"([A-G]#?)(\d)", tok)
    if not m:
        sys.exit(f"bad note {tok!r}")
    n = SEMI[m.group(1)] + int(m.group(2)) * 12
    if not (0 <= n <= 95):
        sys.exit(f"note {tok} out of range ({n})")
    return n


def parse(s, default=Q):
    rows = []
    for t in s.split():
        if "/" in t:
            nm, fr = t.split("/")
            fr = int(fr)
        else:
            nm, fr = t, default
        rows.append((note(nm), max(1, min(255, fr))))
    return rows


# (label, waveform, "notes")  -- 50 varied, marquee tunes first.
SONGS = [
 ("nokia", "PULSE", "E5/6 D5/6 F#4 G#4 C#5/6 B4/6 D4 E4 B4/6 A4/6 C#4 E4 A4/24"),
 ("fur_elise", "TRI", "E5/6 D#5/6 E5/6 D#5/6 E5/6 B4/6 D5/6 C5/6 A4 -/3 C4/6 E4/6 A4/6 B4 -/3 E4/6 G#4/6 B4/6 C5"),
 ("iphone_marimba", "TRI", "E4/6 A4/6 C#5/6 A4/6 E4/6 A4/6 C#5/6 A4/6 E4/6 A4/6 C#5/6 E5/6 D5/6 A4/6 F#4/6 A4/6"),
 ("crazy_frog", "PULSE", "F4 -/3 A#4/6 F4/6 -/3 F4/6 G#4/6 F4/6 D#4/6 F4 -/3 C5/6 F4/6 -/3 F4/6 C#5/6 C5/6 G#4/6 F4/6 C5/6 F5 D#4/6 C4/6 G4 F4/24"),
 ("ode_to_joy", "TRI", "E4 E4 F4 G4 G4 F4 E4 D4 C4 C4 D4 E4 E4/18 D4/6 D4/24"),
 ("mario", "PULSE", "E5/6 E5/6 -/6 E5/6 -/6 C5/6 E5/6 -/6 G5/12 -/12 G4/12"),
 ("tetris", "PULSE", "E5 B4/6 C5/6 D5 C5/6 B4/6 A4 A4/6 C5/6 E5 D5/6 C5/6 B4/18 C5/6 D5 E5 C5 A4 A4/24"),
 ("beethoven5", "SAW", "G4/6 G4/6 G4/6 D#4/24 -/6 F4/6 F4/6 F4/6 D4/24"),
 ("twinkle", "TRI", "C4 C4 G4 G4 A4 A4 G4/24 F4 F4 E4 E4 D4 D4 C4/24"),
 ("jingle_bells", "PULSE", "E4 E4 E4/24 E4 E4 E4/24 E4 G4 C4 D4 E4/36"),
 ("happy_birthday", "TRI", "C4/6 C4/6 D4 C4 F4 E4/24 C4/6 C4/6 D4 C4 G4 F4/24"),
 ("mary_lamb", "TRI", "E4 D4 C4 D4 E4 E4 E4/24 D4 D4 D4/24 E4 G4 G4/24"),
 ("row_boat", "TRI", "C4 C4 C4/18 D4/6 E4 E4/18 D4/6 E4/18 F4/6 G4/36"),
 ("frere_jacques", "TRI", "C4 D4 E4 C4 C4 D4 E4 C4 E4 F4 G4/24 E4 F4 G4/24"),
 ("london_bridge", "TRI", "G4/18 A4/6 G4 F4 E4 F4 G4/24 D4 E4 F4/24 E4 F4 G4/24"),
 ("old_macdonald", "TRI", "G4 G4 G4 D4 E4 E4 D4/24 B4 B4 A4 A4 G4/36"),
 ("silent_night", "TRI", "G4/18 A4/6 G4 E4/36 G4/18 A4/6 G4 E4/36 D5/24 D5 B4/36"),
 ("we_wish_xmas", "PULSE", "D4 G4 G4 A4 G4 F#4 E4 E4 E4 A4 A4 B4 A4 G4/24"),
 ("auld_lang_syne", "TRI", "C4 F4/18 F4/6 F4 A4 G4/18 F4/6 G4 A4/6 G4/6 F4 F4 A4 C5/36"),
 ("amazing_grace", "TRI", "G4/6 C5/18 E5/6 C5 E5/18 D5/6 C5/18 A4/6 G4/36 G4/6 C5/18 E5/6 C5 E5/18 G5/36"),
 ("when_saints", "PULSE", "C4 E4 F4 G4/36 C4 E4 F4 G4/36 C4 E4 F4 G4 E4 C4 E4 D4/36"),
 ("yankee_doodle", "PULSE", "C4 C4 D4 E4 C4 E4 D4 G3 C4 C4 D4 E4 C4/18 B3/6 C4/36"),
 ("oh_susanna", "TRI", "C4/6 D4/6 E4 G4 G4/18 A4/6 G4 E4 C4 D4 E4 E4 D4 C4 D4/36"),
 ("clementine", "TRI", "C4/6 C4 C4/18 F4/6 A4/36 A4/6 G4/36 F4/6 G4 A4/6 F4 F4 G4/18 E4/6 C4/36"),
 ("camptown", "PULSE", "G4 G4 E4 G4 A4 G4 E4/24 E4 D4/36 G4 G4 E4 G4 A4 G4 E4/36"),
 ("la_cucaracha", "PULSE", "C4 C4 C4 F4 A4/24 C4 C4 C4 F4 A4/24 F4 F4 E4 E4 D4 D4 C4/36"),
 ("pop_weasel", "TRI", "C4 C4 D4 D4 E4 G4 E4 C4 C4 C4 D4 D4 E4 C4/18 -/6 C4/36"),
 ("this_old_man", "TRI", "G4 E4 G4 G4 E4 G4 A4 G4 F4 E4 D4 E4 F4 G4/24"),
 ("greensleeves", "TRI", "A4 C5 D5 E5/18 F5/6 E5 D5 B4/18 G4/6 A4 B4 C5/18 A4/6 A4 G#4 A4/36"),
 ("scarborough", "TRI", "A4/24 A4 E5/24 E5 B4 C5 B4 A4/24 F#4/24 G4/12 A4/24 A4/12 E4/48"),
 ("turkey_straw", "PULSE", "C5 B4 A4 G4 F4 E4 D4/18 E4/6 F4 F4 E4 D4 C4 D4/24"),
 ("skip_to_lou", "TRI", "G4 E4 E4/24 F4 D4 D4/24 C4 D4 E4 F4 G4 G4 G4/24"),
 ("bingo", "TRI", "C4 C4 G4 G4 A4 A4 A4 G4 F4 F4 E4 E4 D4 D4 C4/24"),
 ("itsy_spider", "TRI", "G4 C5 C5 C5 D5 E5 E5 E5 D5 C5 D5 E5 C5/36"),
 ("wheels_bus", "PULSE", "C4 F4 F4 F4 A4 C5 A4 F4 G4 E4 C4/36 C4 F4 F4 F4/24"),
 ("hot_cross_buns", "TRI", "E4 D4 C4/24 E4 D4 C4/24 C4 C4 C4 C4 D4 D4 D4 D4 E4 D4 C4/24"),
 ("baa_black_sheep", "TRI", "C4 C4 G4 G4 A4 B4 C5 A4 G4/24 F4 F4 E4 E4 D4 D4 C4/24"),
 ("hickory_dock", "PULSE", "A4 B4 C5 -/6 A4 B4 C5 -/6 C5 D5 E5 F5 E5 D5 C5/24"),
 ("rockabye", "TRI", "F4 A4/18 F4/6 A4 F4 A4/18 F4/6 C5 A4/18 F4/6 A4 G4/18 F4/6 E4 F4/24"),
 ("brahms_lullaby", "TRI", "E4/6 E4/6 G4 E4/6 E4/6 G4 E4 G4 C5 B4 A4 A4 G4/24"),
 ("canon_d", "TRI", "F#5 E5 D5 C#5 B4 A4 B4 C#5 D5 C#5 B4 A4 G4 F#4 G4 E4"),
 ("blue_danube", "TRI", "D4 G4 B4/24 G4/6 B4/6 D5 A4/24 -/6 A4/6 C5/48"),
 ("william_tell", "PULSE", "E4/6 E4/6 E4 E4/6 E4/6 E4 E4 G4 C4 D4 E4/24 F4/6 F4/6 F4 F4 E4 E4 E4 D4 D4 E4/24"),
 ("mountain_king", "SAW", "B3 C#4 D4 E4 F#4 D4 F#4/24 F4 D4 F4/24 E4 C#4 E4/24"),
 ("habanera", "TRI", "D5 C#5 C5 B4 A#4 A#4 B4 C5 C5 B4 A#4 A4 G#4 G#4 A4 B4"),
 ("zarathustra", "SAW", "C4/24 G4/24 C5/24 E5/18 D#5/48"),
 ("valkyries", "SAW", "B3/6 E4/18 B3/6 E4/12 G4/18 E4/6 G4/12 B4/18 G4/6 B4/12 D5/36"),
 ("greensleeves2", "TRI", "E5/18 F5/6 G5 F5 E5 D5 B4 D5 E5 D5 C5 B4 A4/36"),
 ("taps", "TRI", "G3/12 G3/6 C4/36 G3/12 C4/6 E4/48 G3/12 C4/6 E4/36 G3 C4 E4/48"),
 ("maribou_marimba", "TRI", "A4/6 E5/6 A5/6 E5/6 A4/6 E5/6 A5/6 E5/6 B4/6 F#5/6 B5/6 F#5/6 A4/6 E5/6 A5/6 E5/6"),
]


def hex2(v):
    return f"${v & 0xFF:02X}"


def name_of(n):
    if n == OFF:
        return "---"
    if n == TIE:
        return "==="
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    return f"{names[n % 12]}{n // 12}"


def build():
    assert len(SONGS) == 50, f"expected 50 songs, got {len(SONGS)}"
    seen = set()
    inc = [
        "; ============================================================================",
        "; sid_song_bank50.inc -- 50 famous/varied melodies for sid_player.asm.",
        "; ----------------------------------------------------------------------------",
        "; GENERATED by tools/build_sid_song_bank.py -- DO NOT EDIT (rerun the tool).",
        "; Rows: .byte note, ctrl(waveform), frames ; note $FE=off $FF=tie, frames 0",
        "; ends. Play: LDX #<song_nokia / LDY #>song_nokia / JSR sid_play_start, then",
        "; JSR sid_play_tick per frame. The SID tracker loads the same 50 in its bank",
        "; browser (mirror: src/sidtrack/SidSongBank.h).",
        "; ============================================================================",
        "",
    ]
    hdr_chunks = []
    for label, wave, notes in SONGS:
        assert label not in seen, f"dup {label}"
        seen.add(label)
        w = WAVE[wave]
        rows = parse(notes)
        lbl = f"song_{label}"
        inc.append(f"{lbl}:")
        chunk = f'"{lbl}:\\n"\n'
        for n, fr in rows:
            ctrl = 0 if n in (OFF, TIE) else w
            inc.append(f"        .byte {hex2(n)}, {hex2(ctrl)}, {hex2(fr)}   ; {name_of(n)}")
            chunk += f'    " .byte {hex2(n)},{hex2(ctrl)},{hex2(fr)}\\n"\n'
        inc.append("        .byte $00, $00, $00   ; end")
        inc.append("")
        chunk += '    " .byte $00,$00,$00\\n"'
        hdr_chunks.append(chunk)

    OUT_INC.write_text("\n".join(inc) + "\n")

    hdr = [
        "// Pom1 Apple 1 Emulator",
        "// Copyright (C) 2000-2026 Verhille Arnaud",
        "//",
        "// Built-in 50-tune SID song bank for the tracker's bank browser. C++ mirror of",
        "// dev/lib/sid/sid_song_bank50.inc. GENERATED by tools/build_sid_song_bank.py --",
        "// DO NOT EDIT (rerun the tool). Parsed via sidtrack::parseSongAsm.",
        "",
        "#ifndef SIDTRACK_SID_SONG_BANK_H",
        "#define SIDTRACK_SID_SONG_BANK_H",
        "",
        "namespace sidtrack {",
        "",
        "inline const char* kSidSongBank50 =",
    ]
    hdr.append("\n".join(hdr_chunks) + ";")
    hdr += ["", "}  // namespace sidtrack", "", "#endif  // SIDTRACK_SID_SONG_BANK_H"]
    OUT_HDR.write_text("\n".join(hdr) + "\n")

    print(f"wrote {OUT_INC.relative_to(ROOT)} + {OUT_HDR.relative_to(ROOT)} ({len(SONGS)} songs)")


if __name__ == "__main__":
    build()
