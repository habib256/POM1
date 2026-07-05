// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "sfxbeep/SfxPulse.h"

namespace sfxbeep {

uint32_t halfCycleCycles(uint8_t period) {
    // beep_sfx.asm sfx_burst, one half-cycle:
    //   lda per(3) + beq(2) + lda $C030(4) + ldy per(3)
    //   + inner dey/bne loop (Y iterations of ~5c, last branch -1)
    //   + dex(2) + bne(3)
    // period 0 is the REST path where ldy #0 runs 256 iterations.
    const uint32_t iters = (period == 0) ? 256u : static_cast<uint32_t>(period);
    return 5u * iters + 16u;
}

std::vector<std::pair<uint32_t, bool>> sfxToPulses(const std::vector<Step>& steps) {
    std::vector<std::pair<uint32_t, bool>> out;
    bool level = false;                       // speaker starts low
    for (const Step& s : steps) {
        if (s.period == 0) {
            // Rest: hold the current level (no toggle) for the whole step.
            const uint32_t cyc = halfCycleCycles(0) * static_cast<uint32_t>(s.length);
            if (cyc) out.push_back({cyc, level});
        } else {
            const uint32_t hc = halfCycleCycles(s.period);
            for (uint8_t i = 0; i < s.length; ++i) {
                level = !level;               // the square-wave transition
                out.push_back({hc, level});
            }
        }
    }
    return out;
}

}  // namespace sfxbeep
