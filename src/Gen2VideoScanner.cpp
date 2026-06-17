#include "Gen2VideoScanner.h"

// ─── MAME `apple2video.cpp:124-201 scanner_address` ─────────────────────────
//
// Verbatim port of POM2 `Memory::floatingBus()` (which is itself a verbatim
// port of MAME), with the Apple IIe-only inputs removed:
//
//   * 80STORE — the GEN2 has no aux bank, so PAGE2 always selects the
//     displayed page (POM2 gated Page2 on `!(iieMemMode & MF_80STORE)`).
//   * iieMode — the GEN2 models the original Apple II video counter, so the
//     text-mode H-blank "phantom row" ($1000) is always present (POM2 gated it
//     on `!iieMode`).
//
// Input: h_clock [0..64] (active video from 25), v_clock [0..261] (active video
// from 0). Output: the 16-bit DRAM address the video scanner is fetching.
//
// Page-2 HGR ($4000 base): CONFIRMED by Bernie's PDF (Q5, 2026-06-12) —
// primary and secondary pages exist for TEXT and HIRES exactly like the
// Apple II; HIRES page 2 = $4000-$5FFF, selected by $C255.
uint16_t Gen2VideoScanner::scannerAddress(uint64_t frameCycle, const DisplayState& ds,
                                          uint64_t lines)
{
    const uint64_t cyc     = frameCycle % (kCyclesPerLine * lines);
    const int      v_clock = static_cast<int>(cyc / kCyclesPerLine);  // 0..261 (311 @ 50 Hz)
    const int      h_clock = static_cast<int>(cyc % kCyclesPerLine);  // 0..64

    int       Hires = (ds.hiRes && !ds.textMode) ? 1 : 0;
    const int Mixed = ds.mixedMode ? 1 : 0;
    const int Page2 = ds.page2 ? 1 : 0;   // GEN2 has no 80STORE override

    // MAME `apple2video.cpp:140`: two 0-states ([0, 0..63]).
    const int h_state = h_clock - (h_clock > 0);
    const int h_0 = (h_state >> 0) & 1;
    const int h_1 = (h_state >> 1) & 1;
    const int h_2 = (h_state >> 2) & 1;
    const int h_3 = (h_state >> 3) & 1;
    const int h_4 = (h_state >> 4) & 1;
    const int h_5 = (h_state >> 5) & 1;

    // MAME `apple2video.cpp:149`: V[543210CBA] = 100000000 = 256+v. The frame
    // is 262 (or 312) lines, so subtract the line count when v wraps past 256.
    int v_state = 256 + v_clock;
    if (v_clock >= 256) v_state -= static_cast<int>(lines);
    const int v_A = (v_state >> 0) & 1;
    const int v_B = (v_state >> 1) & 1;
    const int v_C = (v_state >> 2) & 1;
    const int v_0 = (v_state >> 3) & 1;
    const int v_1 = (v_state >> 4) & 1;
    const int v_2 = (v_state >> 5) & 1;
    const int v_3 = (v_state >> 6) & 1;
    const int v_4 = (v_state >> 7) & 1;

    // Mixed-mode bottom 4 text rows: HGR off when Mixed && v_4 && v_2.
    if (Hires && Mixed && v_4 && v_2) Hires = 0;

    const int addend0 = 0x0D;
    const int addend1 = (h_5 << 2) | (h_4 << 1) | (h_3 << 0);
    const int addend2 = (v_4 << 3) | (v_3 << 2) | (v_4 << 1) | (v_3 << 0);
    const int sum     = (addend0 + addend1 + addend2) & 0x0F;

    uint16_t address = 0;
    address |= static_cast<uint16_t>(h_0 << 0);
    address |= static_cast<uint16_t>(h_1 << 1);
    address |= static_cast<uint16_t>(h_2 << 2);
    address |= static_cast<uint16_t>(sum << 3);
    address |= static_cast<uint16_t>(v_0 << 7);
    address |= static_cast<uint16_t>(v_1 << 8);
    address |= static_cast<uint16_t>(v_2 << 9);
    if (Hires) {
        address |= static_cast<uint16_t>(v_A << 10);
        address |= static_cast<uint16_t>(v_B << 11);
        address |= static_cast<uint16_t>(v_C << 12);
        // HGR page base: $2000 (page 1) or $4000 (page 2).
        address |= static_cast<uint16_t>(Page2 ? 0x4000 : 0x2000);
    } else {
        // Text base $0400 (page 1) / $0800 (page 2).
        address |= static_cast<uint16_t>(Page2 ? 0x0800 : 0x0400);
        // Apple II / II+ H-blank "phantom row": during HBL the text scanner
        // adds $1000. The IIe fixed this; the GEN2 models the original II.
        if (h_clock < 25) {
            address |= 0x1000;
        }
    }
    return address;
}

// ─── HST0 — Bernie's H/V-blank flag ─────────────────────────────────────────
//
// Verbatim port of the behavioural model in Bernie's PDF
// (`doc/reference/ColorGraphicsCard_doc_for_Arnaud.pdf`, Appendix 1 / Listing 2,
// transcribed in doc/GEN2_RELEASE_questions.md Q3):
//
//   int hst0_state(int line, int hcnt)
//   {
//     if((hcnt > 12) && (hcnt < 16)) return 0; // in the BURST period
//     if(line > 191) return 1;                 // in VBLANK
//     if(hcnt > 24) return 0;                  // in live scan
//     return 1;                                // in HBLANK
//   }
//
// Note the ordering is load-bearing: the 3-cycle color-burst notch (hcnt
// 13-15) reads 0 even inside V-blank — robust software ORs two samples to
// mask it (Bernie's Listing 1). Do not "fix" by checking VBL first.
int Gen2VideoScanner::hst0State(int line, int hcnt)
{
    if ((hcnt > 12) && (hcnt < 16)) return 0;  // in the BURST period
    if (line > 191) return 1;                  // in VBLANK
    if (hcnt > 24) return 0;                   // in live scan
    return 1;                                  // in HBLANK
}
