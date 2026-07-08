#!/usr/bin/env python3
"""
Extract every 16x16 sprite category from Quale's SCROLL-O-SPRITES sheet
(May 2013, CC-BY-3.0) at pic/undefined - Imgur.png.

Generalises tools/extract_scroll_expressions.py: same TMS9918 quarter-block
byte layout, same preview style, but driven by a manifest covering all 14
sprite sections (Characters, Fauna, Trollkind, The Unliving, Creatures,
Building, Traps & Devices, Overworld, Exploration, Food & Drink, Outfit,
Magick, Music, Symbols).

Each section emits one ca65-style .asm under dev/lib/tms9918/ (one .byte
block per sprite, 32 bytes each, in TL/BL/TR/BR order) plus a preview PNG
under screenshots/. Outputs are gated by --write; default is dry-run with
.asm to stdout.

The font section (8x8 glyphs) is handled by extract_scroll_font.py.

Usage:
    python3 tools/extract_scroll_sprites.py --write             # all sections
    python3 tools/extract_scroll_sprites.py --only fauna --write
    python3 tools/extract_scroll_sprites.py --only fauna        # dry-run
    python3 tools/extract_scroll_sprites.py --verify-emotes     # diff Expression
"""
from __future__ import annotations

import argparse
import pathlib
import sys
from typing import List, Tuple

import numpy as np
from PIL import Image, ImageDraw, ImageFont


REPO = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_SRC = REPO / "pic" / "undefined - Imgur.png"
LIB_DIR = REPO / "dev" / "lib" / "tms9918"
PREVIEW_DIR = REPO / "screenshots"

CELL_X0 = 32
CELL_PITCH = 16

# Manifest: (section_key, label_prefix, list of (cell_y, n_cells) per row,
#           optional list of semantic names — one per cell, in row-major order;
#           if None, falls back to numeric naming).
#
# Cell-y coordinates verified by pixel scan (see plan file). Each entry's
# total cell count must equal len(names) when names provided.
SECTIONS: List[dict] = [
    {
        "key": "characters",
        "prefix": "char",
        "title": "Characters",
        "rows": [(96, 13), (112, 13), (128, 7)],
        "names": [
            # Row 1 (slots 1..13)
            "normal_m", "archer_m", "knight_m", "priest_m",
            "thief_m", "wizard_m", "monk_m", "ruler_m",
            "child_m", "dog", "lunatic_m", "wrestler_m", "phantom_m",
            # Row 2 (slots 14..26)
            "normal_f", "archer_f", "knight_f", "priest_f",
            "thief_f", "wizard_f", "monk_f", "ruler_f",
            "child_f", "cat", "lunatic_f", "wrestler_f", "phantom_f",
            # Row 3 (slots 27..33)
            "elder_m", "pirate_m", "mage", "cultist",
            "necromancer_m", "assassin", "mermaid_f",
        ],
    },
    {
        "key": "expression",   # for --verify-emotes only; emit skipped by default
        "prefix": "emote",
        "title": "Expression",
        "rows": [(176, 12)],
        "names": [
            "serious", "happy", "excited", "sad", "hurt", "angry",
            "upset", "smug", "sick", "sleeping", "yarr", "nerd",
        ],
    },
    {
        "key": "fauna",
        "prefix": "fauna",
        "title": "Fauna",
        "rows": [(224, 13)],
        "names": [
            "rat", "bat", "centipede", "spider",
            "scorpion", "snail", "snake", "wolf",
            "lion", "bear", "drake", "dragon", "horse",
        ],
    },
    {
        "key": "trollkind",
        "prefix": "troll",
        "title": "Trollkind",
        "rows": [(272, 4)],
        "names": ["goblin", "hobgoblin", "cyclops", "orc"],
    },
    {
        "key": "unliving",
        "prefix": "undead",
        "title": "The Unliving",
        "rows": [(320, 8)],
        "names": [
            "skull", "hand", "crossbones", "diehard",
            "mummy", "ghost", "skull", "stalker",
        ],
    },
    {
        "key": "creatures",
        "prefix": "creat",
        "title": "Creatures",
        "rows": [(368, 8)],
        "names": [
            "jelly", "kraken", "gazer", "imp",
            "ifrit", "mimic", "automaton", "golem",
        ],
    },
    {
        "key": "building",
        "prefix": "bldg",
        "title": "Building",
        "rows": [(416, 16), (432, 7)],
        "names": [
            # Row 1 (slots 1..16)
            "brick_a", "brick_b", "brick_c",
            "stone_a", "stone_b", "stone_c",
            "panel_ready", "panel_used",
            "block", "slot",
            "house", "stairs", "battlement", "door_open",
            "pyre", "wheel",
            # Row 2 (slots 17..23)
            "tombstone", "sign", "stool",
            "cupboard", "bookcase", "table", "bed",
        ],
    },
    {
        "key": "traps_devices",
        "prefix": "trap",
        "title": "Traps & Devices",
        "rows": [(496, 12)],
        "names": [
            "grate", "web", "beartrap_ready", "beartrap_used",
            "trap_ready", "trap_used", "trapdoor_ready", "trapdoor_used",
            "button_ready", "button_used", "switch_left", "switch_right",
        ],
    },
    {
        "key": "overworld",
        "prefix": "world",
        "title": "Overworld",
        "rows": [(544, 13)],
        "names": [
            "grass", "pebbles", "flower",
            "tree_full", "tree_pine", "tree_bare",
            "waves", "boulders",
            "cave", "house", "castlle", "cart", "ship",
        ],
    },
    {
        "key": "exploration",
        "prefix": "expl",
        "title": "Exploration",
        "rows": [(592, 16), (608, 5)],
        "names": [
            # Row 1
            "torch", "lantern", "shovel", "pickaxe",
            "rope", "bomb", "chest_ready", "chest_used",
            "urn", "broken", "key_a", "key_b",
            "bag", "coins", "nugget", "crystal",
            # Row 2
            "gem", "corpse", "bone", "shell", "fossil",
        ],
    },
    {
        "key_b": "food_drink",
        "prefix": "food",
        "title": "Food & Drink",
        "rows": [(656, 16), (672, 6)],
        "names": [
            # Row 1
            "drumstick", "steak", "kebab", "fish",
            "eyeball", "bread", "egg", "cheese",
            "fruit", "vegetable", "root", "leaf",
            "herb", "mushroom", "candy", "cupcake",
            # Row 2
            "beer", "pot", "trophy", "vial", "bottle", "jug",
        ],
    },
    {
        "key": "outfit",
        "prefix": "outfit",
        "title": "Outfit",
        "rows": [(720, 16), (736, 11)],
        "names": [
            # Row 1
            "dagger", "sword", "axe", "spear",
            "pole", "club", "hammer", "shield_a",
            "shield_b", "bow", "arrows", "helm",
            "hood", "hat", "gloves", "bracers",
            # Row 2
            "boots", "pants", "belt",
            "tunic", "chainmail", "plate", "cloak",
            "ring", "necklace", "crown", "glasses",
        ],
    },
    {
        "key": "magick",
        "prefix": "magic",
        "title": "Magick",
        "rows": [(784, 15)],
        "names": [
            "staff", "wand", "rod", "orb",
            "ankh", "skull", "letter", "scroll",
            "book", "altar", "cauldron", "emanation",
            "totem", "divine", "arcane",
        ],
    },
    {
        "key": "music",
        "prefix": "music",
        "title": "Music",
        "rows": [(832, 6)],
        "names": ["lute", "harp", "pipes", "bell", "xylophone", "drum"],
    },
    {
        "key": "symbols",
        "prefix": "sym",
        "title": "Symbols",
        "rows": [(880, 16), (896, 7)],
        "names": [
            # Row 1
            "at", "up", "up_right", "plus", "ex", "heart",
            "star", "sun", "moon", "target", "warning", "music",
            "fire", "ice", "water", "lightning",
            # Row 2
            "confusion", "poison", "sleep", "dead", "attack",
            "defend", "wait",
        ],
    },
]


def load_bw(src: pathlib.Path) -> np.ndarray:
    img = Image.open(src).convert("RGB")
    arr = np.array(img)
    cream = ((arr[:, :, 0] > 200) &
             (arr[:, :, 1] > 200) &
             (arr[:, :, 2] > 150)).astype(np.uint8)
    return cream


def extract_cell(bw: np.ndarray, x: int, y: int) -> np.ndarray:
    return bw[y:y + 16, x:x + 16]


def to_tms9918_bytes(g: np.ndarray) -> List[int]:
    """16x16 -> 32 bytes in TL, BL, TR, BR quarter-block order
    (native TMS9918 16x16 sprite layout)."""
    assert g.shape == (16, 16), g.shape
    out = []
    for y0, x0 in ((0, 0), (8, 0), (0, 8), (8, 8)):
        for r in range(8):
            byte = 0
            for x in range(8):
                if g[y0 + r, x0 + x]:
                    byte |= 1 << (7 - x)
            out.append(byte)
    return out


def cells_for_section(bw: np.ndarray, section: dict) -> List[np.ndarray]:
    cells = []
    for cell_y, n in section["rows"]:
        for c in range(n):
            cells.append(extract_cell(bw, CELL_X0 + c * CELL_PITCH, cell_y))
    return cells


def labels_for_section(section: dict) -> List[str]:
    n_total = sum(n for _, n in section["rows"])
    names = section.get("names")
    if names is not None:
        if len(names) != n_total:
            raise ValueError(
                f"section {section['key']}: {len(names)} names vs {n_total} cells")
        return [f"{section['prefix']}_{nm}_pat" for nm in names]
    return [f"{section['prefix']}{i + 1:02d}_pat" for i in range(n_total)]


def emit_asm(section: dict, cells: List[np.ndarray]) -> str:
    labels = labels_for_section(section)
    title = section["title"]
    key = section["key"]
    n = len(cells)
    out = []
    out.append("; " + "=" * 76)
    out.append(f"; sprites_{key}.asm  --  {n} sprites (16x16, TMS9918 sprite mode)")
    out.append("; " + "-" * 76)
    out.append(f"; SCROLL-O-SPRITES \"{title}\" by Quale, May 2013, CC-BY-3.0.")
    out.append("; Lifted from pic/undefined - Imgur.png by tools/extract_scroll_sprites.py.")
    out.append(";")
    out.append("; Layout: each 16x16 sprite occupies 32 bytes -- the first 16 bytes are")
    out.append("; the left half (column 0..7), the next 16 the right half (column 8..15).")
    out.append("; Native TMS9918 16x16 sprite layout: stream the 32 bytes into a")
    out.append("; sprite-pattern slot starting at base $3800 + slot*32.")
    out.append("; " + "=" * 76)
    # Wrap export list at 5 names per line for readability
    chunk = 5
    for i in range(0, len(labels), chunk):
        out.append(".export " + ", ".join(labels[i:i + chunk]))
    out.append("")
    out.append('.segment "CODE"')
    out.append("")
    for i, (label, cell) in enumerate(zip(labels, cells)):
        bs = to_tms9918_bytes(cell)
        slot_comment = f"; slot {i + 1:02d}/{n} of \"{title}\" row -- "
        # Strip the prefix and suffix from the label for the comment hint
        base = label[len(section["prefix"]) + 1:-4]
        out.append(slot_comment + base)
        out.append(f"{label}:")
        for off in (0, 8, 16, 24):
            out.append("        .byte " +
                       ", ".join(f"${b:02X}" for b in bs[off:off + 8]))
    out.append("")
    return "\n".join(out)


def render_preview(cells: List[np.ndarray], labels: List[str],
                   title: str, out_path: pathlib.Path,
                   scale: int = 8, cols: int = 8) -> None:
    n = len(cells)
    rows = (n + cols - 1) // cols
    cell_px = 16 * scale
    pad = 12
    label_h = 16
    title_h = 22
    bg = (28, 28, 36)
    fg = (255, 255, 255)
    grid = (60, 60, 76)
    text = (200, 210, 230)
    title_color = (255, 220, 120)
    slot_color = (140, 160, 200)

    sheet_w = pad + cols * (cell_px + pad)
    sheet_h = pad + title_h + rows * (cell_px + label_h + pad)

    img = Image.new("RGB", (sheet_w, sheet_h), bg)
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 11)
        slot_font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 9)
        title_font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 14)
    except OSError:
        font = ImageFont.load_default()
        slot_font = font
        title_font = font

    draw.text(
        (pad, pad),
        f"SCROLL-O-SPRITES \"{title}\" ({n} sprites, 16x16, CC-BY-3.0 Quale)",
        fill=title_color, font=title_font,
    )
    y0 = pad + title_h
    for i, (cell, label) in enumerate(zip(cells, labels)):
        r = i // cols
        c = i % cols
        x = pad + c * (cell_px + pad)
        cy = y0 + r * (cell_px + label_h + pad)
        for ry in range(16):
            for rx in range(16):
                if cell[ry, rx]:
                    draw.rectangle(
                        [x + rx * scale, cy + ry * scale,
                         x + (rx + 1) * scale - 1,
                         cy + (ry + 1) * scale - 1],
                        fill=fg,
                    )
        mid = x + 8 * scale
        draw.line([(mid, cy), (mid, cy + cell_px)], fill=grid)
        draw.line([(x, cy + 8 * scale),
                   (x + cell_px, cy + 8 * scale)], fill=grid)
        draw.rectangle([x, cy, x + cell_px - 1, cy + cell_px - 1],
                       outline=grid)
        # Slot index bottom-left of cell
        draw.text((x + 2, cy + cell_px - 11), f"{i + 1:02d}",
                  fill=slot_color, font=slot_font)
        tw = draw.textlength(label, font=font)
        draw.text((x + (cell_px - tw) // 2, cy + cell_px + 2),
                  label, fill=text, font=font)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(out_path)


def find_section(key: str) -> dict:
    for s in SECTIONS:
        if s["key"] == key:
            return s
    raise KeyError(f"unknown section: {key}")


def verify_emotes(bw: np.ndarray) -> int:
    """Re-extract Expression and diff pattern bytes against the existing
    sprites_emotes.asm. Returns 0 on byte-for-byte match, 1 otherwise."""
    target = LIB_DIR / "sprites_emotes.asm"
    if not target.exists():
        print(f"[verify] missing {target}", file=sys.stderr)
        return 2
    section = find_section("expression")
    cells = cells_for_section(bw, section)
    # Existing sprites_emotes.asm uses bare names (normal_pat, happy_pat, ...)
    # without the "emote_" prefix the new manifest produces, so match on the
    # raw semantic name from the manifest.
    raw_names = section["names"]
    legacy_labels = [f"{nm}_pat" for nm in raw_names]
    expected = {legacy: to_tms9918_bytes(cell)
                for legacy, cell in zip(legacy_labels, cells)}
    actual: dict = {legacy: [] for legacy in legacy_labels}
    cur = None
    for line in target.read_text().splitlines():
        s = line.strip()
        if s.endswith(":") and s[:-1] in actual:
            cur = s[:-1]
            continue
        if cur and s.startswith(".byte"):
            for tok in s.removeprefix(".byte").split(","):
                tok = tok.strip()
                if tok.startswith("$"):
                    actual[cur].append(int(tok[1:], 16))
            if len(actual[cur]) >= 32:
                cur = None
    fails = 0
    for label in legacy_labels:
        if expected[label] != actual[label]:
            fails += 1
            print(f"[verify] MISMATCH {label}", file=sys.stderr)
            print(f"  expected: {[f'{b:02X}' for b in expected[label]]}",
                  file=sys.stderr)
            print(f"  actual:   {[f'{b:02X}' for b in actual[label]]}",
                  file=sys.stderr)
    if fails == 0:
        print("[verify] OK -- 12 Expression sprites match sprites_emotes.asm "
              "byte-for-byte", file=sys.stderr)
        return 0
    print(f"[verify] {fails}/12 mismatched", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--src", type=pathlib.Path, default=DEFAULT_SRC)
    p.add_argument("--lib-dir", type=pathlib.Path, default=LIB_DIR)
    p.add_argument("--preview-dir", type=pathlib.Path, default=PREVIEW_DIR)
    p.add_argument("--only", type=str, default=None,
                   help="extract a single section by key")
    p.add_argument("--write", action="store_true",
                   help="write .asm files (default: stdout dry-run)")
    p.add_argument("--no-preview", action="store_true",
                   help="skip preview PNG generation")
    p.add_argument("--verify-emotes", action="store_true",
                   help="diff regenerated Expression vs sprites_emotes.asm")
    args = p.parse_args(argv)

    bw = load_bw(args.src)
    if args.verify_emotes:
        return verify_emotes(bw)

    sections = SECTIONS
    if args.only:
        sections = [find_section(args.only)]
    else:
        # Skip Expression in the default sweep -- it's already in
        # sprites_emotes.asm. Caller can opt in via --only expression.
        sections = [s for s in sections if s["key"] != "expression"]

    for section in sections:
        cells = cells_for_section(bw, section)
        labels = labels_for_section(section)
        asm = emit_asm(section, cells)

        out_asm = args.lib_dir / f"sprites_{section['key']}.asm"
        if args.write:
            args.lib_dir.mkdir(parents=True, exist_ok=True)
            out_asm.write_text(asm)
            print(f"[write] {out_asm}  ({len(cells)} sprites, "
                  f"{len(cells) * 32} B data)", file=sys.stderr)
        else:
            print(asm)

        if not args.no_preview:
            out_png = args.preview_dir / f"scroll_{section['key']}.png"
            render_preview(cells, labels, section["title"], out_png)
            print(f"[preview] {out_png}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
