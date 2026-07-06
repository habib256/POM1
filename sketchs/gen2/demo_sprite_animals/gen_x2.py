#!/usr/bin/env python3
"""Bake the four Fauna mono masters into their x2 single-colour HGR forms.

Byte-for-byte port of gen2_hgr_inflate_x2 (dev/lib/gen2c/gen2_hgr_x2.c) so the
demo needs NO runtime inflate: the x2 bytes are embedded as C constants. Keeping
the demo off gen2_hgr_x2.o also lets it link under the in-app DevBench (whose
GEN2-C link set doesn't pull that module). Masters come verbatim from
dev/lib/gen2/sprites/sprites_fauna_hgr.asm (16 rows x 3 bytes, 7px/byte).
"""

# gen2.h GEN2_X2_* colour ids
BLACK, WHITE, VIOLET, GREEN, BLUE, ORANGE = range(6)

MASTERS = {
    "dog": [
        0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0x18,0x0C,0x00, 0x18,0x0C,0x00, 0x70,0x03,0x00, 0x6A,0x06,0x00,
        0x7C,0x0F,0x01, 0x00,0x4E,0x00, 0x60,0x5F,0x00, 0x70,0x5F,0x00,
        0x68,0x5F,0x00, 0x60,0x5F,0x00, 0x60,0x5F,0x00, 0x50,0x37,0x00,
    ],
    "octopus": [
        0x00,0x00,0x00, 0x48,0x27,0x00, 0x68,0x2F,0x00, 0x64,0x4F,0x00,
        0x64,0x4F,0x00, 0x48,0x27,0x00, 0x16,0x53,0x01, 0x7A,0x3F,0x01,
        0x30,0x1B,0x00, 0x5C,0x77,0x00, 0x12,0x13,0x01, 0x4A,0x24,0x01,
        0x48,0x24,0x00, 0x08,0x20,0x00, 0x10,0x10,0x00, 0x10,0x10,0x00,
    ],
    "bat": [
        0x00,0x00,0x00, 0x00,0x02,0x00, 0x70,0x03,0x00, 0x1C,0x00,0x00,
        0x7E,0x7F,0x00, 0x7E,0x7F,0x01, 0x78,0x7F,0x01, 0x00,0x7C,0x01,
        0x00,0x7F,0x01, 0x60,0x7F,0x00, 0x50,0x17,0x00, 0x70,0x1F,0x00,
        0x50,0x14,0x00, 0x10,0x10,0x00, 0x20,0x08,0x00, 0x40,0x07,0x00,
    ],
    "lion": [
        0x00,0x00,0x00, 0x00,0x0B,0x00, 0x60,0x1F,0x00, 0x70,0x3E,0x00,
        0x78,0x3F,0x00, 0x78,0x33,0x00, 0x58,0x03,0x00, 0x18,0x17,0x00,
        0x38,0x12,0x02, 0x78,0x0C,0x01, 0x78,0x01,0x01, 0x78,0x7F,0x01,
        0x70,0x07,0x00, 0x68,0x7B,0x00, 0x5C,0x2B,0x01, 0x12,0x28,0x01,
    ],
    "rabbit": [
        0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x08,0x20,0x00,
        0x24,0x48,0x00, 0x66,0x4C,0x01, 0x66,0x4C,0x01, 0x6E,0x6C,0x01,
        0x7E,0x7F,0x01, 0x3E,0x7B,0x01, 0x6E,0x6F,0x01, 0x4A,0x24,0x01,
        0x02,0x00,0x01, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
    ],
    "spider": [
        0x00,0x00,0x00, 0x78,0x04,0x00, 0x78,0x13,0x00, 0x70,0x0F,0x00,
        0x48,0x5F,0x00, 0x20,0x3E,0x00, 0x00,0x7F,0x00, 0x74,0x3F,0x00,
        0x78,0x5F,0x00, 0x78,0x03,0x00, 0x78,0x09,0x00, 0x74,0x27,0x00,
        0x60,0x1F,0x00, 0x10,0x2B,0x00, 0x40,0x3E,0x00, 0x00,0x00,0x00,
    ],
    "cat": [
        0x00,0x00,0x00, 0x70,0x00,0x00, 0x78,0x01,0x00, 0x68,0x01,0x00,
        0x08,0x04,0x00, 0x10,0x03,0x00, 0x00,0x08,0x00, 0x40,0x07,0x00,
        0x10,0x10,0x00, 0x60,0x0F,0x00, 0x60,0x0F,0x00, 0x3C,0x7B,0x00,
        0x5E,0x77,0x01, 0x1A,0x30,0x01, 0x12,0x10,0x01, 0x14,0x50,0x00,
    ],
    "snake": [
        0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0x40,0x3F,0x00, 0x60,0x7F,0x00, 0x60,0x7F,0x00, 0x70,0x44,0x01,
        0x36,0x3B,0x01, 0x36,0x2B,0x01, 0x54,0x2C,0x01, 0x44,0x4E,0x01,
        0x38,0x7C,0x01, 0x78,0x7D,0x00, 0x44,0x03,0x00, 0x2A,0x7E,0x00,
    ],
}

COLOURS = {"dog": WHITE, "octopus": BLUE, "bat": GREEN, "lion": ORANGE,
           "rabbit": VIOLET, "spider": ORANGE, "cat": BLUE, "snake": GREEN}
COLNAME = {WHITE: "WHITE", BLUE: "BLUE", GREEN: "GREEN", ORANGE: "ORANGE",
           VIOLET: "VIOLET"}

WBYTES, H = 3, 16


def inflate_x2(mono, wbytes, h, color):
    dW, dH, wpx = wbytes * 2, h * 2, wbytes * 7
    litLeft  = color in (VIOLET, BLUE, WHITE)
    litRight = color in (GREEN, ORANGE, WHITE)
    p1       = color in (BLUE, ORANGE)
    out = [0] * (dW * dH)
    for sy in range(h):
        rowbase = (sy + sy) * dW
        for sx in range(wpx):
            if (mono[sy * wbytes + sx // 7] & (1 << (sx % 7))) == 0:
                continue
            for k in range(2):
                if (k == 0 and not litLeft) or (k == 1 and not litRight):
                    continue
                dc = (sx << 1) + k
                byte, bit = dc // 7, dc % 7
                out[rowbase + byte] |= (1 << bit)
                out[rowbase + dW + byte] |= (1 << bit)
                if p1:
                    out[rowbase + byte] |= 0x80
                    out[rowbase + dW + byte] |= 0x80
    return out


# --- Pre-shifted x2 banks for 2 px-smooth motion -----------------------------
# A byte-aligned blit (gen2_hgr_blit7) can only place an x2 sprite every 14 px
# (one colour-clock-doubled byte pair) or the hue flips. To move in 2 px steps we
# BAKE the sprite at each of the 7 even sub-positions within a 14 px period
# (shifts 0,2,4,6,8,10,12 px). An even shift preserves the NTSC dot parity, so the
# hue survives; the palette group bit (bit 7) is re-derived per shifted byte. Each
# phase widens the 6-byte (42 px) sprite to 8 bytes (56 px). At runtime the demo
# blits phase (x % 14) / 2 at byte column 2 * (x / 14).
PS_PHASES = (0, 2, 4, 6, 8, 10, 12)   # even sub-pixel offsets, 2 px apart
# A 16px master inflates to x2 cols 0..31; the widest phase (+12) reaches col 43
# -> byte 6, so 7 dest bytes/row suffice (byte 7 is always empty). Asserted below.
PS_DSTB   = 7                         # dest bytes/row after the widest shift
SRC_COLS  = WBYTES * 2 * 7            # 42 lit columns in the 6-byte x2 form


def preshift_bank(name):
    """7 phases x 32 rows x 8 bytes = 1792 bytes, phases contiguous."""
    """Return the 7 phases as [phase][row][byte] (7 rows-of-PS_DSTB), full size."""
    x2 = inflate_x2(MASTERS[name], WBYTES, H, COLOURS[name])
    dW = WBYTES * 2                                   # 6 source bytes/row
    groupbit = 0x80 if COLOURS[name] in (BLUE, ORANGE) else 0
    phases = []
    for p in PS_PHASES:
        rows = []
        for row in range(H * 2):                      # 32 doubled rows
            lit = []
            for b in range(dW):
                byte = x2[row * dW + b]
                for bit in range(7):
                    lit.append((byte >> bit) & 1)
            dst = [0] * PS_DSTB
            for c in range(SRC_COLS):
                if not lit[c]:
                    continue
                dc = c + p
                dst[dc // 7] |= (1 << (dc % 7))
            if groupbit:                              # palette bit on lit bytes
                for b in range(PS_DSTB):
                    if dst[b]:
                        dst[b] |= 0x80
            rows.append(dst)
        phases.append(rows)
    return phases


def bake(name):
    """Trim each sprite to its bounding box: the used rows (phase-independent)
    and, per phase, the used byte window. Returns metadata + trimmed data so the
    runtime blits ~5x(24..30) instead of 7x32 (blit7 pays for zero bytes too)."""
    phases = preshift_bank(name)
    NR = H * 2                                        # 32 rows
    lit7 = lambda v: v & 0x7F                         # ignore palette bit for bbox
    # rows used: any phase-0 row with content (rows don't move with h-shift)
    used_row = [any(lit7(phases[0][r][b]) for b in range(PS_DSTB)) for r in range(NR)]
    yoff = next(r for r in range(NR) if used_row[r])
    ylast = next(r for r in range(NR - 1, -1, -1) if used_row[r])
    nrows = ylast - yoff + 1
    # per-phase byte window (xoff, width) — each phase baked at its OWN width so a
    # phase whose content fits 4 bytes doesn't pay for a 5th (saves ~a few hundred
    # bytes across 8 sprites, which is what fits the DevBench full-link).
    xoff, wp = [], []
    for ph in phases:
        cols = [b for b in range(PS_DSTB)
                if any(lit7(ph[r][b]) for r in range(yoff, yoff + nrows))]
        x0, x1 = min(cols), max(cols)
        xoff.append(x0)
        wp.append(x1 - x0 + 1)
    # trimmed data: phase pi = nrows rows x wp[pi] bytes, from xoff[pi]
    data = []
    for pi, ph in enumerate(phases):
        x0 = xoff[pi]
        for r in range(yoff, yoff + nrows):
            for b in range(wp[pi]):
                data.append(ph[r][x0 + b])
    return dict(yoff=yoff, nrows=nrows, xoff=xoff, wp=wp, data=data)


def emit_bank(name):
    m = bake(name)
    NM = name.upper()
    NR = m['nrows']
    lines = [f"/* {name} -> x2 {COLNAME[COLOURS[name]]}, 7 phases (2px), TRIMMED to "
             f"{NR} rows x per-phase width {m['wp']}: {len(m['data'])} B */",
             f"#define {NM}_YOFF {m['yoff']}u",
             f"#define {NM}_ROWS {NR}u",
             f"static const unsigned char {name}_xoff[7] = "
             f"{{ {','.join(str(x) + 'u' for x in m['xoff'])} }};",
             f"static const unsigned char {name}_wp[7]   = "
             f"{{ {','.join(str(w) + 'u' for w in m['wp'])} }};",
             f"static const unsigned char x2ps_{name}[{len(m['data'])}] = {{"]
    off = 0
    for i in range(7):
        lines.append(f"    /* phase {i}: shift {PS_PHASES[i]}px, xoff {m['xoff'][i]}, "
                     f"w {m['wp'][i]} */")
        w = m['wp'][i]
        for r in range(NR):
            lines.append("    " + ",".join(f"0x{b:02X}" for b in m['data'][off:off + w]) + ",")
            off += w
    lines.append("};")
    return "\n".join(lines)


if __name__ == "__main__":
    print("/* AUTO-GENERATED by gen_x2.py — do not edit.  Regenerate:")
    print(" *   python3 gen_x2.py   (paste the output over the block in GEN2Animals.c) */")
    for n in ("dog", "octopus", "bat", "lion",
              "rabbit", "spider", "cat", "snake"):
        print(emit_bank(n))
        print()
