#!/usr/bin/env python3
"""
build_preshift_sprites.py -- bake 1bpp sprites into Buzzard-Bait-style 7-phase
PRE-SHIFTED banks for the GEN2 HGR runtime (dev/lib/gen2c, gen2_hgr_sprite).

WHY. HIRES packs 7 pixels per byte, so positioning a sprite at an arbitrary x
needs an (x mod 7) bit shift -- expensive per byte at runtime. Sirius' 1983
"Buzzard Bait" engine (see sketchs/gen2/a2port_buzzard_bait/DISASSEMBLY.md S3)
sidesteps it: store the sprite PRE-SHIFTED in all 7 sub-byte phases, then at
runtime just pick the phase for x%7 and do a byte-aligned blit. This tool builds
those 7 phases offline; gen2_hgr_sprite consumes them.

DATA ABI (must match gen2.h gen2_sprite_t):
    stride = ceil((w + 6) / 7)   bytes per row, uniform across the 7 phases.
    bank   = 7 phase blocks, each h*stride bytes, row-major, 7px/byte
             (bit 0 = leftmost pixel, bit 7 always 0). Phase p at offset
             p*(h*stride). Source pixel (row, col) of phase p lands at packed
             bit (col + p): byte (col+p)//7, bit (col+p)%7.

INPUT -- an ASCII-art sprite sheet (.txt), dependency-free:

    ; comments start with ; or #
    sprite ball 9x9          <- 'sprite' NAME WxH
    ....#....
    ..#####..
    .#######.
    .#######.
    #########
    .#######.
    .#######.
    ..#####..
    ....#....

    sprite paddle 21x4
    #####################
    #####################
    #####################
    #####################

In a pixel row, '#', 'X', '*', '1', 'O' (and 'o') are ON; everything else
(".", " ", "-") is OFF. Exactly H rows must follow each header.

OUTPUT:
    --lang c   (default) -> a C header: a `static const unsigned char NAME_ps[]`
                            plus `static const gen2_sprite_t NAME = {...}`.
    --lang asm           -> a ca65 .s (label `_NAME_ps`/`NAME_ps:` + .byte data)
                            and a .inc with NAME_STRIDE / NAME_H constants.
    --masked             -> ALSO bake a 7-phase pre-shifted MASK bank per sprite
                            and emit `gen2_mspr_t NAME = {NAME_ps, NAME_mk, ...}`
                            instead of the gen2_sprite_t (C only). The mask ABI
                            (gen2.h): a 1-bit KEEPS the background; mask bytes =
                            ~coverage with bit 7 FORCED 1 so the background's
                            HIRES palette-group bit always survives; data bit 7
                            stays 0. coverage = the sprite's ON pixels, dilated
                            by --halo N px (8-neighbour, N passes, clipped to
                            the WxH box) for a black outline around the sprite.
                            Consumed by the gen2_sprmask.s kernels / the
                            gen2_spr_* engine (dst = (dst & mask) | data).

USAGE:
    python3 tools/build_preshift_sprites.py SHEET.txt -o OUT.h
    python3 tools/build_preshift_sprites.py SHEET.txt --lang asm -o OUT.s
    python3 tools/build_preshift_sprites.py SHEET.txt --masked -o OUT.h
    python3 tools/build_preshift_sprites.py --selftest      # no input needed
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys
from typing import List, Tuple

NPHASE = 7
ON_CHARS = set("#Xx*1OoZ@")
HEADER_RE = re.compile(r"^\s*sprite\s+([A-Za-z_][A-Za-z0-9_]*)\s+(\d+)x(\d+)\s*$")

Sprite = Tuple[str, int, int, List[int]]   # (name, w, h, rows-as-bitmasks LSB=col0)


def stride_for(w: int) -> int:
    """Bytes per row per phase = ceil((w + 6) / 7) — room for the phase-6 spill."""
    return (w + 6 + 6) // 7          # == ceil((w+6)/7)


def parse_sheet(text: str) -> List[Sprite]:
    sprites: List[Sprite] = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        raw = lines[i]
        i += 1
        s = raw.strip()
        if not s or s[0] in ";#":
            # allow '#' only as a whole-line comment when it's not pixel data;
            # pixel rows are gathered by count below, so a stray '#' here is a comment.
            continue
        m = HEADER_RE.match(raw)
        if not m:
            raise SystemExit(f"line {i}: expected 'sprite NAME WxH', got: {raw!r}")
        name, w, h = m.group(1), int(m.group(2)), int(m.group(3))
        if w < 1 or h < 1 or w > 273:
            raise SystemExit(f"sprite {name}: bad size {w}x{h}")
        rows: List[int] = []
        for r in range(h):
            if i >= len(lines):
                raise SystemExit(f"sprite {name}: expected {h} rows, got {r}")
            row = lines[i]
            i += 1
            bits = 0
            for c in range(w):
                ch = row[c] if c < len(row) else "."
                if ch in ON_CHARS:
                    bits |= (1 << c)          # bit c = source column c (LSB = col 0)
            rows.append(bits)
        sprites.append((name, w, h, rows))
    if not sprites:
        raise SystemExit("no sprites found in input")
    return sprites


def bake(sprite: Sprite) -> Tuple[int, int, List[int]]:
    """Return (stride, h, bank-bytes) — 7 pre-shifted phase blocks."""
    name, w, h, rows = sprite
    stride = stride_for(w)
    bank: List[int] = []
    for phase in range(NPHASE):
        for r in range(h):
            out = [0] * stride
            src = rows[r]
            for col in range(w):
                if src & (1 << col):
                    bitpos = col + phase
                    out[bitpos // 7] |= (1 << (bitpos % 7))   # bit 7 never set
            bank.extend(out)
    assert len(bank) == NPHASE * h * stride
    return stride, h, bank


def dilate(rows: List[int], w: int, h: int, n: int) -> List[int]:
    """8-neighbour dilation, n passes, clipped to the w x h box (the halo cannot
    grow the sprite's byte rectangle -- stride/h are fixed by the source)."""
    box = (1 << w) - 1
    cov = list(rows)
    for _ in range(n):
        new = []
        for r in range(h):
            bits = cov[r] | (cov[r] << 1) | (cov[r] >> 1)
            if r > 0:
                bits |= cov[r - 1] | (cov[r - 1] << 1) | (cov[r - 1] >> 1)
            if r + 1 < h:
                bits |= cov[r + 1] | (cov[r + 1] << 1) | (cov[r + 1] >> 1)
            new.append(bits & box)
        cov = new
    return cov


def bake_mask(sprite: Sprite, halo: int) -> Tuple[int, int, List[int]]:
    """Return (stride, h, mask-bank-bytes): 7 pre-shifted phase blocks of
    ~coverage with bit 7 forced 1 (a 1-bit KEEPS the background -- gen2.h
    gen2_mspr_t ABI). Derived by baking the (dilated) coverage exactly like the
    data bank, then complementing the low 7 bits of every byte."""
    name, w, h, rows = sprite
    cov = dilate(rows, w, h, halo)
    stride, _h, cov_bank = bake((name, w, h, cov))
    mask_bank = [0x80 | (~b & 0x7F) for b in cov_bank]
    return stride, h, mask_bank


def verify_masked(sprite: Sprite, halo: int,
                  data_bank: List[int], mask_bank: List[int],
                  stride: int, h: int) -> None:
    """Masked-bank invariants (gen2.h ABI): every mask byte has bit 7 set, every
    data byte has bit 7 clear, and no data bit falls where the mask KEEPS the
    background (data & mask & 0x7F == 0 -- the draw could not deposit it)."""
    name = sprite[0]
    if len(mask_bank) != len(data_bank):
        raise AssertionError(f"{name}: mask/data bank size mismatch")
    for i, (d, m) in enumerate(zip(data_bank, mask_bank)):
        if not (m & 0x80):
            raise AssertionError(f"{name}: mask byte {i} bit7 clear ({m:#x})")
        if d & 0x80:
            raise AssertionError(f"{name}: data byte {i} bit7 set ({d:#x})")
        if d & m & 0x7F:
            raise AssertionError(
                f"{name}: data bit outside coverage at byte {i} "
                f"(data={d:#x} mask={m:#x})")
    # the coverage bank obeys the same 7-phase shift law as the data bank
    cov_bank = [(~m) & 0x7F for m in mask_bank]
    covered = decode_phase(stride, h, cov_bank, 0)
    for phase in range(1, NPHASE):
        pp = decode_phase(stride, h, cov_bank, phase)
        for r in range(h):
            if pp[r] != (covered[r] << phase):
                raise AssertionError(
                    f"{name}: mask phase {phase} row {r} is not phase0<<{phase}")
    if halo == 0:
        # without a halo, coverage == the sprite pixels exactly
        p0d = decode_phase(stride, h, data_bank, 0)
        for r in range(h):
            if covered[r] != p0d[r]:
                raise AssertionError(f"{name}: halo-0 coverage != data, row {r}")


def decode_phase(stride: int, h: int, bank: List[int], phase: int) -> List[int]:
    """Re-read one phase block back into per-row source bitmasks (LSB=screen col)."""
    base = phase * h * stride
    rows = []
    for r in range(h):
        row = bank[base + r * stride: base + (r + 1) * stride]
        bits = 0
        for j, b in enumerate(row):
            for k in range(7):
                if b & (1 << k):
                    bits |= (1 << (j * 7 + k))
        rows.append(bits)
    return rows


def verify(sprite: Sprite, stride: int, h: int, bank: List[int]) -> None:
    """Phase p, drawn at column c, must equal phase 0 drawn at pixel c*7 + p.
    Equivalently: phase p's on-screen pixels == phase 0's pixels shifted by p."""
    name, w, h2, rows = sprite
    p0 = decode_phase(stride, h, bank, 0)
    # phase 0 must reproduce the source exactly (no shift)
    for r in range(h):
        if (p0[r] & ((1 << w) - 1)) != rows[r]:
            raise AssertionError(f"{name}: phase 0 row {r} != source")
        if p0[r] >> w:
            raise AssertionError(f"{name}: phase 0 row {r} has stray high bits")
    # every later phase == phase 0 shifted left by p (LSB == leftmost screen col)
    for phase in range(1, NPHASE):
        pp = decode_phase(stride, h, bank, phase)
        for r in range(h):
            if pp[r] != (p0[r] << phase):
                raise AssertionError(
                    f"{name}: phase {phase} row {r} is not phase0<<{phase} "
                    f"({pp[r]:#x} vs {p0[r] << phase:#x})")
        # the spill must still fit inside `stride` bytes
        if (max(pp) if pp else 0) >> (stride * 7):
            raise AssertionError(f"{name}: phase {phase} overflows stride")


# ---------------------------------------------------------------------------- #
#  Emitters                                                                     #
# ---------------------------------------------------------------------------- #
def _byte_rows(data: List[int], per_line: int = 12) -> List[str]:
    out = []
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        out.append(", ".join(f"0x{b:02X}" for b in chunk))
    return out


def emit_c(sprites, baked, title: str, masks=None) -> str:
    guard = "PRESHIFT_" + re.sub(r"[^A-Za-z0-9]", "_", title).upper() + "_H"
    if masks is None:
        use = "Use with gen2_hgr_sprite() (dev/lib/gen2c, link GEN2C_PRESHIFT_SRCS)."
    else:
        use = ("Use with the gen2_spr_* engine (dev/lib/gen2c, link "
               "GEN2C_SPRENGINE_SRCS + GEN2C_SPRMASK_SRCS).")
    L = [f"/* {title} -- 7-phase pre-shifted GEN2 HGR sprite bank"
         f"{' (masked)' if masks is not None else ''}.",
         " * Auto-generated by tools/build_preshift_sprites.py -- DO NOT EDIT.",
         f" * {use} */",
         f"#ifndef {guard}", f"#define {guard}", "",
         '#include "gen2.h"', ""]
    for idx, ((name, w, h, _), (stride, _h, bank)) in enumerate(zip(sprites, baked)):
        L.append(f"/* {name}: {w}x{h} px, stride={stride} B/row, "
                 f"7 phases, {len(bank)} B total */")
        L.append(f"static const unsigned char {name}_ps[{len(bank)}] = {{")
        for line in _byte_rows(bank):
            L.append(f"    {line},")
        L.append("};")
        if masks is None:
            L.append(f"static const gen2_sprite_t {name} = "
                     f"{{ {name}_ps, {stride}, {h} }};")
        else:
            mbank = masks[idx][2]
            L.append(f"static const unsigned char {name}_mk[{len(mbank)}] = {{")
            for line in _byte_rows(mbank):
                L.append(f"    {line},")
            L.append("};")
            L.append(f"static const gen2_mspr_t {name} = "
                     f"{{ {name}_ps, {name}_mk, {stride}, {h} }};")
        L.append("")
    L.append(f"#endif /* {guard} */")
    return "\n".join(L) + "\n"


def emit_asm(sprites, baked, title: str) -> Tuple[str, str]:
    s = [f"; {title} -- 7-phase pre-shifted GEN2 HGR sprite bank.",
         "; Auto-generated by tools/build_preshift_sprites.py -- DO NOT EDIT.",
         "; Layout per sprite: 7 phase blocks, each H rows x STRIDE bytes, 7px/byte.",
         ""]
    inc = [f"; {title} -- constants for the pre-shifted sprite bank (immediate-mode).",
           "; .include this, .import the *_ps data labels from the sister .s.", ""]
    for (name, w, h, _), (stride, _h, bank) in zip(sprites, baked):
        s.append(f"        .export _{name}_ps, {name}_ps")
        s.append(f"_{name}_ps:")
        s.append(f"{name}_ps:")
        for line in _byte_rows([b for b in bank]):
            s.append("        .byte " + line)
        s.append("")
        up = name.upper()
        inc.append(f"{up}_W      = {w}")
        inc.append(f"{up}_H      = {h}")
        inc.append(f"{up}_STRIDE = {stride}")
        inc.append(f"{up}_PHASES = {NPHASE}")
        inc.append("")
    return "\n".join(s) + "\n", "\n".join(inc) + "\n"


# ---------------------------------------------------------------------------- #
#  Self-test (no input file needed)                                             #
# ---------------------------------------------------------------------------- #
_SELFTEST_SHEET = """\
sprite t_dot 1x1
#
sprite t_row 9x1
#.#.#.#.#
sprite t_box 8x3
########
#......#
########
sprite t_tri 13x4
......#......
.....###.....
....#####....
...#######...
"""


def selftest() -> int:
    sprites = parse_sheet(_SELFTEST_SHEET)
    for sp in sprites:
        stride, h, bank = bake(sp)
        verify(sp, stride, h, bank)
        # spot-check a known case: t_dot phase p -> single bit at packed pos p
        if sp[0] == "t_dot":
            for phase in range(NPHASE):
                blk = bank[phase * h * stride:(phase + 1) * h * stride]
                assert blk[0] == (1 << phase), (phase, blk)
        # masked twin: bank + mask must satisfy the gen2_mspr_t invariants
        for halo in (0, 1):
            ms, mh, mbank = bake_mask(sp, halo)
            assert (ms, mh) == (stride, h)
            verify_masked(sp, halo, bank, mbank, stride, h)
        # spot-check t_dot masked, halo 0: mask byte 0 of phase p keeps
        # everything BUT bit p, and bit 7 is forced 1
        if sp[0] == "t_dot":
            _s, _h, mbank = bake_mask(sp, 0)
            for phase in range(NPHASE):
                assert mbank[phase] == (0xFF ^ (1 << phase)), (phase, mbank)
    # round-trip the emitters (syntactic only)
    baked = [bake(sp) for sp in sprites]
    masks = [bake_mask(sp, 0) for sp in sprites]
    _ = emit_c(sprites, baked, "selftest")
    _ = emit_c(sprites, baked, "selftest_masked", masks)
    _a, _i = emit_asm(sprites, baked, "selftest")
    print(f"selftest OK: {len(sprites)} sprites, "
          f"{sum(len(b[2]) for b in baked)} bank bytes, "
          f"7-phase shift + masked invariants verified")
    return 0


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description="Bake 1bpp sprites into 7-phase "
                                             "pre-shifted GEN2 HGR banks.")
    ap.add_argument("sheet", nargs="?", help="ASCII-art sprite sheet (.txt)")
    ap.add_argument("-o", "--out", help="output file (default: stdout)")
    ap.add_argument("--lang", choices=("c", "asm"), default="c")
    ap.add_argument("--name", help="bank title (default: input stem)")
    ap.add_argument("--masked", action="store_true",
                    help="also bake pre-shifted masks; emit gen2_mspr_t (C only)")
    ap.add_argument("--halo", type=int, default=0, metavar="N",
                    help="dilate the mask coverage by N px (black outline; "
                         "clipped to the sprite box; default 0)")
    ap.add_argument("--selftest", action="store_true",
                    help="run built-in correctness checks and exit")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.sheet:
        ap.error("need a SHEET (or --selftest)")
    if args.masked and args.lang != "c":
        ap.error("--masked emits gen2_mspr_t C banks only (use --lang c)")
    if args.halo < 0:
        ap.error("--halo must be >= 0")

    path = pathlib.Path(args.sheet)
    sprites = parse_sheet(path.read_text())
    baked = [bake(sp) for sp in sprites]
    for sp, bk in zip(sprites, baked):
        verify(sp, *bk)                  # always self-verify before emitting
    masks = None
    if args.masked:
        masks = [bake_mask(sp, args.halo) for sp in sprites]
        for sp, bk, mk in zip(sprites, baked, masks):
            verify_masked(sp, args.halo, bk[2], mk[2], bk[0], bk[1])
    title = args.name or path.stem

    if args.lang == "c":
        out = emit_c(sprites, baked, title, masks)
        if args.out:
            pathlib.Path(args.out).write_text(out)
            print(f"wrote {args.out}: {len(sprites)} sprites")
        else:
            sys.stdout.write(out)
    else:
        asm, inc = emit_asm(sprites, baked, title)
        if args.out:
            outp = pathlib.Path(args.out)
            outp.write_text(asm)
            incp = outp.with_suffix(".inc")
            incp.write_text(inc)
            print(f"wrote {outp} + {incp}: {len(sprites)} sprites")
        else:
            sys.stdout.write(asm)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
