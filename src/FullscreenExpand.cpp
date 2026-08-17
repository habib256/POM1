// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// FullscreenExpand.cpp — see FullscreenExpand.h.

#include "FullscreenExpand.h"

namespace pom1 {

void FullscreenExpandSettler::arm(float displayW, float displayH)
{
    frames_ = kSettleFrames;
    lastW_  = displayW;
    lastH_  = displayH;
}

void FullscreenExpandSettler::cancel()
{
    frames_ = 0;
}

bool FullscreenExpandSettler::step(float displayW, float displayH)
{
    if (frames_ <= 0) return false;

    if (displayW != lastW_ || displayH != lastH_) {
        // Still moving — the frame is mid-animation. Re-arm so the expand
        // lands on the size the display actually settles at, not on whatever
        // this intermediate frame happens to be.
        lastW_  = displayW;
        lastH_  = displayH;
        frames_ = kSettleFrames;
        return false;
    }

    return --frames_ == 0;
}

} // namespace pom1
