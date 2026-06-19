#!/usr/bin/env python3
"""
Extract 8x8 quadrants from Quale's SCROLL-O-SPRITES (16x16 sprites in
dev/lib/tms9918/sprites_*.asm) and emit a ca65-includable tileset for
the TMS9918 Mode 1 pattern table.

Each Quale sprite is 32 bytes laid out as 4 rows of 8 .byte values:
  row 0: bytes 0..7   = LEFT half top    (cols 0..7,  rows 0..7 )
  row 1: bytes 8..15  = LEFT half bottom (cols 0..7,  rows 8..15)
  row 2: bytes 16..23 = RIGHT half top   (cols 8..15, rows 0..7 )
  row 3: bytes 24..31 = RIGHT half bottom (cols 8..15, rows 8..15)

This script slices a chosen 8x8 quadrant per tile. For a roguelike
the top-left (TL) is usually the busiest part of the original sprite
and reads cleanest at 8x8; cobble / brick patterns work in any
quadrant. The picker per tile is configurable in the PALETTE dict.

TMS9918 Mode 1 colour table constraint: 32 bytes at VRAM $2000, ONE
fg/bg colour byte per group of 8 chars. So tile char IDs must be
PLANNED in groups of 8 by intended colour:

  chars  0..7  group 0   stone walls/floors        (gray   on black)
  chars  8..15 group 1   doors/stairs/containers   (brown  on black)
  chars 16..23 group 2   items / equipment         (yellow on black)
  chars 24..31 group 3   magic items               (cyan   on black)
  chars 32..39 group 4   monster fallback chars    (red    on black)
  chars 40..47 group 5   reserved (HUD digits)     (white  on black)

The output .inc file is a single 256-char pattern table (2048 bytes)
suitable for streaming verbatim into VRAM at $0000 via vdp_set_write
+ vdp_upload loop. Unused chars are padded with $00 bytes.

Output:
  dev/projects/tms9918/game_rogue/tileset_rogue.inc

Usage:
  python3 tools/extract_quale_8x8_tiles.py
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIB_TMS = ROOT / "dev" / "lib" / "tms9918"
OUT_INC = ROOT / "dev" / "projects" / "tms9918_rogue" / "tileset_rogue.inc"
FONT_ASM = LIB_TMS / "font_quale.asm"


# Quadrant indices into the 32-byte sprite array.
QUADRANTS = {
    "TL": (0, 8),     # top-left  8x8: bytes 0..7
    "BL": (8, 16),    # bot-left  8x8: bytes 8..15
    "TR": (16, 24),   # top-right 8x8: bytes 16..23
    "BR": (24, 32),   # bot-right 8x8: bytes 24..31
}


# Tile palette: maps tile BASE char ID -> (sprite_label, name).
# Each tile is a full 16x16 Quale sprite expanded into FOUR consecutive
# char slots:
#   base + 0  = TL quadrant (top-left  8x8, bytes  0..7  of the sprite)
#   base + 1  = TR quadrant (top-right 8x8, bytes 16..23)
#   base + 2  = BL quadrant (bot-left  8x8, bytes  8..15)
#   base + 3  = BR quadrant (bot-right 8x8, bytes 24..31)
# Renderer writes 4 chars per logical tile in a 2x2 name-table block.
#
# Char-ID grouping respects the Mode 1 colour-table constraint (one
# fg/bg per group of 8 chars), so adjacent tiles in PALETTE share a
# colour group: 2 tiles per group.
PALETTE: dict[int, tuple[str | None, str]] = {
    # --- Group 0 (chars 0..7): stone (gray on black) ---
    0:  (None,                     "empty"),       # all-zero quadrants (background)
    4:  ("bldg_brick_wall_pat",    "wall"),        # full 16x16 brick

    # --- Group 1 (chars 8..15): portals (yellow on black) ---
    8:  ("bldg_stairs_down_pat",   "stairs_down"),  # was mis-labelled "house"
    12: ("bldg_door_wood_pat",     "door"),         # plank+iron-band wooden door

    # --- Group 2 (chars 16..23): stairs-up + trap (lt-yellow on black) ---
    # TILE_STAIRS_UP renders a cobblestone block (bldg_cobble3_pat) so the
    # cell reads "the way up just collapsed" — Berlin-Interpretation
    # one-way descent. The TILE_STAIRS_UP equate keeps its semantic name
    # (the collision rule still treats the cell as a hard wall in
    # check_collision); only the visual changes.
    16: ("bldg_cobble3_pat",       "stairs_up"),
    # TILE_TRAP_PIT renders Quale's pit graphic. Lt-yellow outline reads
    # as "danger zone" against the gray walls. Stepping on it costs HP
    # (see trigger_pit + main_loop hook in TMS_Rogue.asm); the pit stays
    # visible afterwards so a second visit costs HP again — players
    # learn to remember pit positions across the FOV-wipe.
    20: ("trap_pit_pat",           "trap_pit"),

    # --- Group 3 (chars 24..31): items (cyan on black) ---
    24: ("expl_coin_pat",          "gold"),
    28: ("expl_flask_pat",         "potion"),

    # --- Groups 4..15 (chars 32..127): ASCII-aligned font, white on black.
    # Driven by FONT_PALETTE below — glyph at char N renders ASCII char
    # with code N, so `.byte "ROGUE"` in ca65 streams the right bytes.
}


# ASCII-aligned font glyphs sliced from font_quale.asm. Each entry maps
# an ASCII code (which becomes the TMS9918 char ID) to a font_quale
# label. ca65 lets us write `.byte "ROGUE"` and the char codes match
# 1:1 with the pattern table — no per-string translation needed.
FONT_PALETTE: dict[int, str] = {
    ord(" "): None,                              # space -> all-zero pattern
    ord("0"): "font_quale_digits_0",
    ord("1"): "font_quale_digits_1",
    ord("2"): "font_quale_digits_2",
    ord("3"): "font_quale_digits_3",
    ord("4"): "font_quale_digits_4",
    ord("5"): "font_quale_digits_5",
    ord("6"): "font_quale_digits_6",
    ord("7"): "font_quale_digits_7",
    ord("8"): "font_quale_digits_8",
    ord("9"): "font_quale_digits_9",
    # NB: font_quale.asm punctuation labels are systematically shifted by
    # one relative to the visual Quale source sheet (the extraction script
    # mis-aligned the punctuation row). Pick by visual content, not by
    # the human-readable label name. Verified by decoding the bytes.
    ord("("): "font_quale_punct_lbrack",         # actually a left-paren-like glyph
    ord(")"): "font_quale_punct_rbrack",         # actually a right-paren-like glyph
    ord("-"): "font_quale_punct_lt",             # actually a horizontal line
    ord("+"): "font_quale_punct_minus",          # actually a plus/cross
    ord("/"): "font_quale_punct_plus",           # actually a forward slash
    ord("\\"):"font_quale_punct_bslash",         # this one is correctly named
}
# A..Z at char IDs 65..90.
for _i in range(26):
    FONT_PALETTE[65 + _i] = f"font_quale_upper_{chr(65 + _i)}"


# Hand-authored glyphs for chars that font_quale.asm doesn't ship at all
# (or ships visibly broken). Each entry = 8 rows of 8 px (MSB = leftmost
# col), matching Quale's "row 0 mostly blank" convention. Drawn deliberately
# narrow so they don't visually crowd the digits and uppercase letters
# they share rows with in the HUD.
MANUAL_GLYPHS: dict[int, list[int]] = {
    ord("."): [0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00],
    ord(":"): [0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00],
    ord("?"): [0x00, 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18],
    ord("["): [0x00, 0x1C, 0x10, 0x10, 0x10, 0x10, 0x1C, 0x00],
    ord("]"): [0x00, 0x38, 0x08, 0x08, 0x08, 0x08, 0x38, 0x00],
}


# Mode 1 colour byte format: high nibble = fg, low nibble = bg.
# TMS9918 palette indices: 0=transp, 1=blk, 2=med-grn, 3=lt-grn,
# 4=dk-blu, 5=lt-blu, 6=dk-red, 7=cyan, 8=med-red, 9=lt-red,
# 10=dk-yel, 11=lt-yel, 12=dk-grn, 13=magenta, 14=gray, 15=white.
COLOR_TABLE = [
    0xE1,  # group 0 (chars  0.. 7) gray   on black -- stone
    0xA1,  # group 1 (chars  8..15) dk-yel on black -- doors/stairs
    0xB1,  # group 2 (chars 16..23) lt-yel on black -- items
    0x71,  # group 3 (chars 24..31) cyan   on black -- magic
    0xF1,  # group 4 (chars 32..39) white  on black -- font (' ' + punct)
    0xF1,  # group 5 (chars 40..47) white  on black -- font (punct)
    0xF1,  # group 6
    0xF1,  # group 7
    0xF1,  # group 8
    0xF1,  # group 9
    0xF1,  # group 10
    0xF1,  # group 11
    0xF1,  # group 12
    0xF1,  # group 13
    0xF1,  # group 14
    0xF1,  # group 15
    0xF1,  # group 16
    0xF1,  # group 17
    0xF1,  # group 18
    0xF1,  # group 19
    0xF1,  # group 20
    0xF1,  # group 21
    0xF1,  # group 22
    0xF1,  # group 23
    0xF1,  # group 24
    0xF1,  # group 25
    0xF1,  # group 26
    0xF1,  # group 27
    0xF1,  # group 28
    0xF1,  # group 29
    0xF1,  # group 30
    0xF1,  # group 31
]
assert len(COLOR_TABLE) == 32


# --- sprite / font source parser --------------------------------------------

# Match `<label>:` (sprites use `_pat` suffix, font_quale uses mixed-case
# names like font_quale_upper_A). Skip directive lines (starting with `.`)
# and comments (starting with `;`).
LABEL_RE = re.compile(r"^([a-zA-Z][a-zA-Z0-9_]*):\s*$")
BYTE_RE  = re.compile(r"^\s*\.byte\s+([^;]+?)(?:\s*;.*)?$")


def parse_byte_blocks(asm_path: pathlib.Path,
                      block_size: int) -> dict[str, list[int]]:
    """Return {label: [block_size bytes]} for every label in `asm_path`
    that is followed by exactly `block_size` bytes of `.byte` data
    before the next label. Sprites use 32-byte blocks (16x16 split into
    quadrants); font_quale glyphs use 8-byte blocks (one 8x8 tile).
    """
    out: dict[str, list[int]] = {}
    label: str | None = None
    bytes_collected: list[int] = []
    for raw in asm_path.read_text().splitlines():
        m = LABEL_RE.match(raw)
        if m:
            if label is not None and len(bytes_collected) == block_size:
                out[label] = bytes_collected
            label = m.group(1)
            bytes_collected = []
            continue
        if label is None:
            continue
        if len(bytes_collected) >= block_size:
            continue
        m = BYTE_RE.match(raw)
        if not m:
            continue
        for tok in m.group(1).split(","):
            tok = tok.strip()
            if not tok:
                continue
            if tok.startswith("$"):
                bytes_collected.append(int(tok[1:], 16))
            elif tok.lower().startswith("0x"):
                bytes_collected.append(int(tok[2:], 16))
            else:
                try:
                    bytes_collected.append(int(tok, 10))
                except ValueError:
                    # Symbolic operand — skip whole label (rare in lib).
                    bytes_collected = []
                    label = None
                    break
    if label is not None and len(bytes_collected) == block_size:
        out[label] = bytes_collected
    return out


def load_all_sprites() -> dict[str, list[int]]:
    """Scan every sprites_*.asm in lib/tms9918, return merged label dict."""
    merged: dict[str, list[int]] = {}
    for asm in sorted(LIB_TMS.glob("sprites_*.asm")):
        sprites = parse_byte_blocks(asm, block_size=32)
        for k in sprites:
            if k in merged:
                raise SystemExit(f"label {k!r} duplicated in {asm.name}")
        merged.update(sprites)
    return merged


def load_font() -> dict[str, list[int]]:
    """Parse font_quale.asm (8-byte glyphs)."""
    return parse_byte_blocks(FONT_ASM, block_size=8)


# --- emitter ----------------------------------------------------------------

def quadrant(sprite_bytes: list[int], q: str) -> list[int]:
    lo, hi = QUADRANTS[q]
    return sprite_bytes[lo:hi]


def emit_inc(sprites: dict[str, list[int]],
             font: dict[str, list[int]]) -> str:
    lines: list[str] = []
    lines.append("; ============================================================================")
    lines.append("; tileset_rogue.inc -- generated by tools/extract_quale_8x8_tiles.py")
    lines.append("; ----------------------------------------------------------------------------")
    lines.append("; 8x8 pattern table for TMS9918 Mode 1.")
    lines.append(";   chars  0..31  -- tile graphics sliced from Quale's SCROLL-O-SPRITES")
    lines.append(";                    (16x16 sprites_*.asm, one chosen 8x8 quadrant each)")
    lines.append(";   ASCII range    -- font glyphs from Quale's font_quale.asm at the")
    lines.append(";                    matching ASCII char IDs ('A'=65, '0'=48, etc.) so")
    lines.append(";                    `.byte \"ROGUE\"` in ca65 streams the right VRAM bytes.")
    lines.append(";")
    lines.append("; DO NOT EDIT BY HAND. Re-run the script if the palette changes.")
    lines.append("; ============================================================================")
    lines.append("")
    lines.append(".export tileset_rogue, tileset_color_table")
    lines.append("")

    # --- Tile base char IDs (referenced from TMS_Rogue.asm) ---
    # Each CHAR_* equate is the BASE char ID; the renderer writes
    # base+0..3 as the four quadrants of the 16x16 logical tile. The
    # bit-packed `map_buffer` stores dense 4-bit `TILE_*` ids (defined
    # in TMS_Rogue.asm); render_map turns a dense id into a CHAR_*
    # base via the `tile_char_base[id]` LUT.
    lines.append("; --- Tile base char IDs (use via `tile_char_base` LUT in render_map) ---")
    lines.append("; Each tile occupies 4 consecutive chars: base+0=TL, +1=TR, +2=BL, +3=BR.")
    lines.append("; The dense `TILE_*` symbols (4-bit ids used inside the bit-packed")
    lines.append("; map_buffer) live in TMS_Rogue.asm; render_map turns a dense id into")
    lines.append("; a CHAR_* base via `tile_char_base[id]`.")
    for cid in sorted(PALETTE):
        _, name = PALETTE[cid]
        lines.append(f"CHAR_{name.upper():<14} = {cid}")
    lines.append("")

    # Build a unified char-id -> (rows, comment) map.
    # PALETTE entries expand each 16x16 Quale sprite into 4 consecutive
    # 8x8 char patterns at base+{0,1,2,3} = TL,TR,BL,BR. Order matches
    # the 2x2 name-table write sequence (row-major) so render_map can
    # stream base..base+3 in a single 4-byte loop.
    chars: dict[int, tuple[list[int], str]] = {}
    for tile_base, (label, name) in PALETTE.items():
        for q_idx, q_name in enumerate(("TL", "TR", "BL", "BR")):
            cid = tile_base + q_idx
            if cid in chars:
                raise SystemExit(
                    f"char {cid} occupied twice in PALETTE (tile {name})")
            if label is None:
                chars[cid] = ([0] * 8, f"{name}.{q_name} (blank)")
            else:
                if label not in sprites:
                    raise SystemExit(
                        f"sprite {label!r} not found in sprites_*.asm "
                        f"(typo in PALETTE? tile_base {tile_base})")
                chars[cid] = (quadrant(sprites[label], q_name),
                              f"{name}.{q_name} ({label})")

    for cid, label in FONT_PALETTE.items():
        if cid in chars:
            raise SystemExit(
                f"char {cid} ({chr(cid)!r}) collides between PALETTE and FONT_PALETTE")
        if label is None:
            chars[cid] = ([0] * 8, f"'{chr(cid)}' (blank)")
        else:
            if label not in font:
                raise SystemExit(
                    f"font glyph {label!r} not found in font_quale.asm "
                    f"(typo in FONT_PALETTE? char {cid} = {chr(cid)!r})")
            chars[cid] = (font[label], f"'{chr(cid)}' ({label})")

    for cid, rows in MANUAL_GLYPHS.items():
        if cid in chars:
            raise SystemExit(
                f"char {cid} ({chr(cid)!r}) collides between MANUAL_GLYPHS "
                f"and PALETTE/FONT_PALETTE")
        if len(rows) != 8:
            raise SystemExit(
                f"MANUAL_GLYPHS[{cid}] must be 8 rows, got {len(rows)}")
        chars[cid] = (list(rows), f"'{chr(cid)}' (manual)")

    # --- Pattern table: 256 chars * 8 bytes = 2048 bytes ---
    lines.append("; --- Pattern table (256 * 8 = 2048 B), upload to VRAM $0000 -------------")
    lines.append("tileset_rogue:")
    max_cid = 255
    for cid in range(max_cid + 1):
        if cid in chars:
            rows, src_note = chars[cid]
            comment = f"; char {cid:3d}  {src_note}"
        else:
            rows = [0] * 8
            comment = f"; char {cid:3d}  (unused)"
        body = ", ".join(f"${b:02X}" for b in rows)
        lines.append(f"        .byte {body}    {comment}")
    lines.append("")

    # --- Colour table: 32 bytes at VRAM $2000 ---
    lines.append("; --- Colour table (32 B), upload to VRAM $2000 --------------------------")
    lines.append("; One fg/bg byte per group of 8 chars. fg = high nibble, bg = low nibble.")
    lines.append("tileset_color_table:")
    body = ", ".join(f"${b:02X}" for b in COLOR_TABLE)
    lines.append(f"        .byte {body}")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    sprites = load_all_sprites()
    font = load_font()
    out = emit_inc(sprites, font)
    OUT_INC.parent.mkdir(parents=True, exist_ok=True)
    OUT_INC.write_text(out)
    print(f"Wrote {OUT_INC} ({len(out)} chars, "
          f"{len(PALETTE)} tiles + {len(FONT_PALETTE)} font glyphs defined, "
          f"{len(sprites)} sprites + {len(font)} font glyphs available)",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
