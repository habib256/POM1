// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "sfxbeep/SfxModel.h"

namespace sfxbeep {

void SfxModel::insertStep(std::size_t i, Step s) {
    if (i > steps_.size()) i = steps_.size();
    steps_.insert(steps_.begin() + static_cast<std::ptrdiff_t>(i), clampStep(s));
}

void SfxModel::setStep(std::size_t i, Step s) {
    if (i < steps_.size()) steps_[i] = clampStep(s);
}

void SfxModel::removeStep(std::size_t i) {
    if (i < steps_.size())
        steps_.erase(steps_.begin() + static_cast<std::ptrdiff_t>(i));
}

unsigned SfxModel::totalHalfCycles() const {
    unsigned t = 0;
    for (const Step& s : steps_) t += s.length;
    return t;
}

}  // namespace sfxbeep
