// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// 1-bit beeper SFX → speaker-pulse segments — the pure, emulator-agnostic core
// of the beeper editor's LIVE PREVIEW. Converts an SFX step list into the
// (cpu-cycles, speaker-level) segments a host queues straight into the cassette
// audio path (CassetteDevice::previewBeep), so the editor can audition an SFX
// with NO $C030 toggling and NO CPU time — the audio callback synthesises it.
// The per-half-cycle cycle count mirrors dev/lib/beep/beep_sfx.asm's sfx_burst
// inner loop so the previewed PITCH matches what a program will actually hear.
// No ImGui / GL / emulator dependency — ports verbatim to POM2. Pinned by
// sfx_asm_export_smoke.

#ifndef SFXBEEP_SFX_PULSE_H
#define SFXBEEP_SFX_PULSE_H

#include <cstdint>
#include <utility>
#include <vector>

#include "sfxbeep/SfxModel.h"

namespace sfxbeep {

// CPU cycles a single speaker half-cycle takes in beep_sfx.asm's sfx_burst for
// pitch `period` (period 0 = the rest's fixed 256-iteration delay). Matches the
// asm inner loop: lda/beq/lda $C030/ldy + (period × dey/bne) + dex/bne ≈ 5·P+16.
uint32_t halfCycleCycles(uint8_t period);

// SFX steps → square-wave segments as (cycles, level) pairs. A tone step
// (period > 0) emits `length` segments of halfCycleCycles(period), the speaker
// level TOGGLING each one (the square wave). A rest step (period 0) emits one
// segment holding the current level for the whole step (silence). The level is
// continuous across steps (starts low). Feed straight to CassetteDevice::
// previewBeep — same table beep_sfx.asm plays, minus the CPU.
std::vector<std::pair<uint32_t, bool>> sfxToPulses(const std::vector<Step>& steps);

}  // namespace sfxbeep

#endif  // SFXBEEP_SFX_PULSE_H
