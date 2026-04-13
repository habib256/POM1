#!/usr/bin/env python3
"""
Génère smiley.inc à partir du jeu de pixels AVIF (art pixel NB).

Source : software/hgr/pixel-set-with-black-white-emojis-pixel-art-cute-emotional-faces-isolated-background_308665-2144.avif

Pipeline :
- Grille 6×4 sur l’image ; les 6 glyphes = ligne 0, colonnes 0..5 (ordre gauche→droite).
- Seuillage adaptatif (fond clair vs sombre par cellule).
- Ponts 1 px (H+V) sur le masque pour fusionner un visage coupé en deux moitiés.
- Recadrage, cadre carré centré, resize 14×14 (NEAREST).
- Doublement 2×2 → 28×28 + passes « bridge » HGR (NTSC).

Run depuis la racine du dépôt :
  python3 software/hgr/_gen_smiley16.py
"""
from pathlib import Path

from PIL import Image
import numpy as np

HERE = Path(__file__).resolve().parent
AVIF_PATH = HERE / "pixel-set-with-black-white-emojis-pixel-art-cute-emotional-faces-isolated-background_308665-2144.avif"

WIDTH = 28
HEIGHT = 28
BYTES_PER_ROW = 4
BYTES_PER_GLYPH = BYTES_PER_ROW * HEIGHT

# Ligne 0 du pack,6 colonnes → grin … surprised (ordre visuel du fichier)
GLYPH_NAMES = ["grin", "laugh", "wink", "neutral", "sad", "surprised"]
GLYPH_CELLS = [(0, c) for c in range(6)]


def grid_cells(im_w, im_h, ncols, nrows):
    cw = [im_w // ncols + (1 if i < im_w % ncols else 0) for i in range(ncols)]
    ch = [im_h // nrows + (1 if i < im_h % nrows else 0) for i in range(nrows)]
    cx = np.cumsum([0] + cw[:-1])
    cy = np.cumsum([0] + ch[:-1])
    return cw, ch, cx, cy


def cell_to_mask(a: np.ndarray) -> np.ndarray:
    """a: uint8 grayscale crop → masque 0/1 (1 = pixel HGR « allumé », visage clair)."""
    m = float(np.mean(a))
    if m > 120.0:
        thr = max(120.0, min(200.0, float(np.percentile(a, 35)) + 85.0))
        g = (a.astype(np.float32) >= thr).astype(np.uint8)
    else:
        thr = min(140.0, float(np.percentile(a, 65)) - 15.0)
        g = (a.astype(np.float32) >= thr).astype(np.uint8)
    f = float(np.mean(g))
    if f > 0.72:
        g = (a.astype(np.float32) >= thr + 25.0).astype(np.uint8)
    elif f < 0.18:
        g = (a.astype(np.float32) >= max(30.0, thr - 35.0)).astype(np.uint8)
    return g


def bridge_mask_h(g: np.ndarray) -> np.ndarray:
    h, w = g.shape
    o = g.copy()
    for y in range(h):
        for x in range(1, w - 1):
            if g[y, x] == 0 and g[y, x - 1] == 1 and g[y, x + 1] == 1:
                o[y, x] = 1
    return o


def bridge_mask_v(g: np.ndarray) -> np.ndarray:
    h, w = g.shape
    o = g.copy()
    for y in range(1, h - 1):
        for x in range(w):
            if g[y, x] == 0 and g[y - 1, x] == 1 and g[y + 1, x] == 1:
                o[y, x] = 1
    return o


def bridge_mask_hv(g: np.ndarray, rounds: int = 4) -> np.ndarray:
    for _ in range(rounds):
        g = bridge_mask_h(g)
        g = bridge_mask_v(g)
    return g


def pad_square_center(g: np.ndarray, fill: int = 0) -> np.ndarray:
    h, w = g.shape
    s = max(h, w)
    out = np.full((s, s), fill, dtype=np.uint8)
    y0 = (s - h) // 2
    x0 = (s - w) // 2
    out[y0 : y0 + h, x0 : x0 + w] = g
    return out


def crop_content(g: np.ndarray, pad: int = 3) -> np.ndarray:
    ys, xs = np.where(g > 0)
    if len(ys) == 0:
        return g
    y0, y1 = ys.min(), ys.max()
    x0, x1 = xs.min(), xs.max()
    y0 = max(0, y0 - pad)
    x0 = max(0, x0 - pad)
    y1 = min(g.shape[0] - 1, y1 + pad)
    x1 = min(g.shape[1] - 1, x1 + pad)
    return g[y0 : y1 + 1, x0 : x1 + 1]


def to_14x14(g: np.ndarray) -> np.ndarray:
    im = Image.fromarray((g * 255).astype(np.uint8), mode="L")
    im = im.resize((14, 14), Image.NEAREST)
    return (np.array(im) >= 128).astype(np.uint8)


def upscale_2x(tile14: np.ndarray) -> np.ndarray:
    out = np.zeros((HEIGHT, WIDTH), dtype=np.uint8)
    for y in range(14):
        for x in range(14):
            v = tile14[y, x]
            out[y * 2 : y * 2 + 2, x * 2 : x * 2 + 2] = v
    return out


def bridge_single_gaps(grid: np.ndarray) -> np.ndarray:
    g = grid.copy()
    for y in range(HEIGHT):
        for x in range(1, WIDTH - 1):
            if g[y, x] == 0 and g[y, x - 1] == 1 and g[y, x + 1] == 1:
                g[y, x] = 1
    return g


def pack_row(row):
    out = []
    for grp in range(BYTES_PER_ROW):
        b = 0
        for i in range(7):
            b |= int(row[grp * 7 + i]) << i
        out.append(b & 0x7F)
    return out


def pack_glyph(grid: np.ndarray):
    return [b for y in range(HEIGHT) for b in pack_row(grid[y])]


def extract_glyph(im: Image.Image, row: int, col: int, cw, ch, cx, cy) -> np.ndarray:
    x0, y0 = int(cx[col]), int(cy[row])
    x1, y1 = x0 + cw[col], y0 + ch[row]
    a = np.array(im.crop((x0, y0, x1, y1)))
    g = cell_to_mask(a)
    g = bridge_mask_hv(g, rounds=4)
    g = crop_content(g)
    g = pad_square_center(g, fill=0)
    g14 = to_14x14(g)
    g28 = upscale_2x(g14)
    g28 = bridge_single_gaps(g28)
    g28 = bridge_single_gaps(g28)
    return g28


def main():
    if not AVIF_PATH.is_file():
        raise SystemExit(f"Missing AVIF: {AVIF_PATH}")

    im = Image.open(AVIF_PATH).convert("L")
    W, H = im.size
    ncols, nrows = 6, 4
    cw, ch, cx, cy = grid_cells(W, H, ncols, nrows)

    glyphs = []
    for name, (r, c) in zip(GLYPH_NAMES, GLYPH_CELLS):
        glyphs.append((name, extract_glyph(im, r, c, cw, ch, cx, cy)))

    lines = [
        "; =============================================================================",
        "; smiley.inc — 28x28 HGR (POM1)",
        "; =============================================================================",
        "; Source: pixel-set-with-black-white-emojis-...2144.avif",
        "; Grid 6x4, row0 cols0-5; 14x14 NN resize, x2 + HGR gap-bridge (NTSC).",
        "; 28 lines x 4 bytes = 112 bytes/glyph.",
        "; Order: 0=grin 1=laugh 2=wink 3=neutral 4=sad 5=surprised (L→R on sheet)",
        "; =============================================================================",
        "",
        "HGR_SMILEY_BYTES_PER_GLYPH = 112",
        "HGR_SMILEY_GLYPH_COUNT     = 6",
        "",
        "hgr_smiley28_font:",
    ]

    packed_all = []
    for idx, (name, g) in enumerate(glyphs):
        packed = pack_glyph(g)
        packed_all.extend(packed)
        lines.append(f"        ; --- {idx} {name} (from AVIF r0c{idx}) ---")
        for y in range(HEIGHT):
            o = y * BYTES_PER_ROW
            bb = packed[o : o + BYTES_PER_ROW]
            lines.append(
                "        .byte "
                + ", ".join(f"${x:02X}" for x in bb)
                + f"  ; y{y}"
            )

    lines.append("")
    lines.append(
        ".assert (* - hgr_smiley28_font) = (HGR_SMILEY_GLYPH_COUNT * HGR_SMILEY_BYTES_PER_GLYPH), error"
    )

    out = HERE / "smiley.inc"
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("Wrote", out, len(packed_all), "bytes")


if __name__ == "__main__":
    main()
