// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "sidtrack/SidVoice.h"

#include <cmath>

namespace sidtrack {

// Apple-1 CPU clock (POM1_CPU_CLOCK_HZ) — the SID phase-accumulator reference.
// Kept local (this module has no emulator dependency); the value is the single
// source of truth in CpuClock.h and in tools/build_sid_notes.py.
static constexpr double kCpuClockHz = 1022727.0;

uint16_t noteFreq(uint8_t note) {
    if (note > 95) note = 95;
    const double hz = 440.0 * std::pow(2.0, (static_cast<int>(note) - 57) / 12.0);
    double v = std::round(hz * 16777216.0 / kCpuClockHz);   // * 2^24 / clock
    if (v < 0.0) v = 0.0;
    if (v > 65535.0) v = 65535.0;
    return static_cast<uint16_t>(v);
}

std::vector<std::pair<uint8_t, uint8_t>>
noteOnRegisters(uint8_t note, uint8_t ctrl, const Instrument& inst) {
    const uint16_t f = noteFreq(note);
    return {
        {REG_VOLUME,   static_cast<uint8_t>(inst.volume & 0x0F)},
        {REG_V1_AD,    inst.ad},
        {REG_V1_SR,    inst.sr},
        {REG_V1_PWLO,  static_cast<uint8_t>(inst.pw & 0xFF)},
        {REG_V1_PWHI,  static_cast<uint8_t>((inst.pw >> 8) & 0x0F)},
        {REG_V1_FREQLO, static_cast<uint8_t>(f & 0xFF)},
        {REG_V1_FREQHI, static_cast<uint8_t>((f >> 8) & 0xFF)},
        // CR last: the gate edge must land AFTER freq/ADSR are set.
        {REG_V1_CR,    static_cast<uint8_t>(ctrl | SID_GATE)},
    };
}

std::vector<std::pair<uint8_t, uint8_t>> noteOffRegisters(uint8_t ctrl) {
    return {{REG_V1_CR, static_cast<uint8_t>(ctrl & ~SID_GATE)}};
}

std::vector<std::pair<uint8_t, uint8_t>> silenceRegisters() {
    return {{REG_V1_CR, 0x00}};
}

}  // namespace sidtrack
