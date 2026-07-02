// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HGR sprite blit — see HgrSpriteBlit.h. Pure byte placement, pinned by
// hgr_sprite_blit_smoke against hgrpaint::hgrByteOffset.

#include "HgrSpriteBlit.h"

#include "HgrPaintModel.h"   // hgrpaint::hgrByteOffset (page-relative interleave)

namespace hgrsprite {

// Page-relative offset of byte column `bc` (0..39) at row `r` (0..191), or -1 if
// out of range. hgrByteOffset takes a pixel x; the byte column is x/7, so bc*7
// picks the leftmost pixel of the column.
static int byteAddr(int bc, int r)
{
    if (bc < 0 || bc >= kByteCols || r < 0 || r >= kRows) return -1;
    return hgrpaint::hgrByteOffset(bc * 7, r);
}

void extract(const uint8_t* page, int srcByteCol, int srcRow,
             int wBytes, int hRows, uint8_t* out)
{
    for (int r = 0; r < hRows; ++r)
        for (int b = 0; b < wBytes; ++b) {
            const int off = byteAddr(srcByteCol + b, srcRow + r);
            out[r * wBytes + b] = (off >= 0) ? page[off] : 0;
        }
}

void stamp(const uint8_t* sprite, int wBytes, int hRows,
           int dstByteCol, int dstRow,
           const std::function<void(int, uint8_t)>& poke)
{
    for (int r = 0; r < hRows; ++r)
        for (int b = 0; b < wBytes; ++b) {
            const int off = byteAddr(dstByteCol + b, dstRow + r);
            if (off >= 0) poke(off, sprite[r * wBytes + b]);
        }
}

} // namespace hgrsprite
