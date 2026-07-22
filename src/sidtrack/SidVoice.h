// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SID voice-1 register math — the pure, emulator-agnostic core of the SID
// tracker's LIVE PREVIEW. Given a note + instrument it produces the exact
// sequence of (chip-relative register, value) writes that play (or release) a
// note on voice 1, which the host applies to the live SID chip via
// EmulationController::pokeSidRegisters. No ImGui / GL / emulator dependency —
// ports verbatim to POM2. Freq cross-checked against the shipped
// dev/lib/sid/sid_notes.inc table by sid_song_asm_export_smoke.

#ifndef SIDTRACK_SID_VOICE_H
#define SIDTRACK_SID_VOICE_H

#include <cstdint>
#include <utility>
#include <vector>

namespace sidtrack {

// Chip-relative SID register indices (reg & 0x1F), matching SID::writeRegister's
// `reg` parameter and dev/lib/sid/sid.inc (voice 1 = base + 0..6).
enum SidReg : uint8_t {
    REG_V1_FREQLO = 0, REG_V1_FREQHI = 1,
    REG_V1_PWLO   = 2, REG_V1_PWHI   = 3,
    REG_V1_CR     = 4, REG_V1_AD     = 5, REG_V1_SR = 6,
    REG_FCLO      = 21, REG_FCHI = 22, REG_RES_FILT = 23,
    REG_VOLUME    = 24,
};
constexpr uint8_t SID_GATE = 0x01;

// Per-note instrument the preview/tracker programs onto voice 1.
struct Instrument {
    uint8_t  ad     = 0x28;      // attack (hi nibble) / decay (lo nibble)
    uint8_t  sr     = 0xA8;      // sustain (hi) / release (lo)
    uint16_t pw     = 0x0800;    // pulse width (12-bit) — used with WAVE_PULSE
    uint8_t  volume = 0x0F;      // master volume (lo nibble of $C818)
};

// 16-bit SID frequency value for note index 0..95 (C0..B7), equal temperament
// A4(57)=440 Hz at the Apple-1 clock. Byte-identical to sid_notes.inc:
//   value = round(freq_hz * 2^24 / 1022727),  freq_hz = 440 * 2^((n-57)/12).
uint16_t noteFreq(uint8_t note);

// Register writes to PLAY `note` on voice 1 with `ctrl` waveform + `inst`: sets
// volume, ADSR, pulse width, the 16-bit frequency, then CR = ctrl | GATE (the
// gate edge that starts the ADSR attack). `note` must be 0..95.
std::vector<std::pair<uint8_t, uint8_t>>
noteOnRegisters(uint8_t note, uint8_t ctrl, const Instrument& inst);

// Register writes to RELEASE the current note: CR = ctrl with the gate bit
// cleared (ADSR enters release). `ctrl` is the waveform that was playing.
std::vector<std::pair<uint8_t, uint8_t>> noteOffRegisters(uint8_t ctrl);

// Register writes to hard-silence voice 1 (CR = 0): a panic/stop.
std::vector<std::pair<uint8_t, uint8_t>> silenceRegisters();

}  // namespace sidtrack

#endif  // SIDTRACK_SID_VOICE_H
