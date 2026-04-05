// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "SID.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─── ADSR rate tables ───────────────────────────────────────────────────────
// CPU cycles per envelope step.  Derived from the MOS 6581 datasheet timing.
// Attack: time from 0→255 = rate * 255.
// Decay/Release: time from 255→0 = rate * 255 (before exponential scaling).

const int SID::kAttackRate[16] = {
    9, 32, 63, 95, 149, 220, 267, 313,
    392, 977, 1954, 3126, 3907, 11720, 19532, 31251
};

const int SID::kDecayReleaseRate[16] = {
    9, 32, 63, 95, 149, 220, 267, 313,
    392, 977, 1954, 3126, 3907, 11720, 19532, 31251
};

// Exponential decay thresholds: the real SID slows down the decay/release
// rate at lower envelope levels, producing a more natural sound.
const uint8_t SID::kExpPeriodThreshold[6] = { 93, 54, 26, 14, 6, 0 };
const uint8_t SID::kExpPeriodValue[6]     = {  1,  2,  4,  8, 16, 30 };

// ─── Constructor / Reset ────────────────────────────────────────────────────

SID::SID()
{
    reset();
}

void SID::reset()
{
    std::lock_guard<std::mutex> lock(sidMutex);
    regs.fill(0);
    for (int i = 0; i < kNumVoices; ++i) {
        voices[i] = Voice{};
    }
    filterLP = 0.0f;
    filterBP = 0.0f;
}

// ─── Register access ────────────────────────────────────────────────────────

void SID::writeRegister(uint8_t reg, uint8_t value)
{
    if (reg >= kNumRegisters) return;
    // Registers 25-28 are read-only
    if (reg >= 25) return;

    std::lock_guard<std::mutex> lock(sidMutex);
    regs[reg] = value;

    // Detect gate transitions for each voice's control register (offsets 4, 11, 18)
    for (int v = 0; v < kNumVoices; ++v) {
        uint8_t ctrlReg = static_cast<uint8_t>(v * 7 + 4);
        if (reg == ctrlReg) {
            bool newGate = (value & 0x01) != 0;
            Voice& voice = voices[v];
            if (newGate && !voice.gateOn) {
                // Gate ON: start attack
                voice.adsrState = ADSR_ATTACK;
                voice.adsrCounter = 0;
                voice.expCounter = 0;
            } else if (!newGate && voice.gateOn) {
                // Gate OFF: start release
                voice.adsrState = ADSR_RELEASE;
                voice.adsrCounter = 0;
                voice.expCounter = 0;
            }
            voice.gateOn = newGate;
        }
    }
}

uint8_t SID::readRegister(uint8_t reg)
{
    if (reg >= kNumRegisters) return 0;

    std::lock_guard<std::mutex> lock(sidMutex);

    switch (reg) {
        case 25: return 0;   // POTX — no paddles
        case 26: return 0;   // POTY — no paddles
        case 27: {
            // OSC3 — upper 8 bits of voice 3 oscillator
            uint32_t acc = voices[2].phaseAccumulator;
            uint8_t ctrl = getControl(2);
            if (ctrl & 0x80) {
                // Noise: return upper 8 bits of LFSR
                return static_cast<uint8_t>((voices[2].lfsr >> 15) & 0xFF);
            }
            return static_cast<uint8_t>((acc >> 16) & 0xFF);
        }
        case 28:
            // ENV3 — voice 3 envelope level
            return voices[2].adsrLevel;
        default:
            // Write-only registers: return 0
            return 0;
    }
}

// ─── Helpers: register decoding ─────────────────────────────────────────────

uint16_t SID::getFrequency(int voice) const
{
    int base = voice * 7;
    return static_cast<uint16_t>(regs[base]) |
           (static_cast<uint16_t>(regs[base + 1]) << 8);
}

uint16_t SID::getPulseWidth(int voice) const
{
    int base = voice * 7;
    return (static_cast<uint16_t>(regs[base + 2]) |
           (static_cast<uint16_t>(regs[base + 3] & 0x0F) << 8));
}

uint8_t SID::getControl(int voice) const
{
    return regs[voice * 7 + 4];
}

uint8_t SID::getAttackDecay(int voice) const
{
    return regs[voice * 7 + 5];
}

uint8_t SID::getSustainRelease(int voice) const
{
    return regs[voice * 7 + 6];
}

// ─── Noise LFSR ─────────────────────────────────────────────────────────────

void SID::clockNoiseLFSR(Voice& v)
{
    // 23-bit LFSR: feedback = bit 22 XOR bit 17
    uint32_t bit22 = (v.lfsr >> 22) & 1;
    uint32_t bit17 = (v.lfsr >> 17) & 1;
    uint32_t feedback = bit22 ^ bit17;
    v.lfsr = ((v.lfsr << 1) | feedback) & 0x7FFFFF;
}

// ─── Waveform computation ───────────────────────────────────────────────────

float SID::computeWaveform(int voiceIndex)
{
    Voice& v = voices[voiceIndex];
    uint8_t ctrl = getControl(voiceIndex);

    // TEST bit: reset oscillator
    if (ctrl & 0x08) {
        v.phaseAccumulator = 0;
        return 0.0f;
    }

    uint32_t acc = v.phaseAccumulator;
    uint16_t pw = getPulseWidth(voiceIndex);

    // Waveform bits
    bool triangle = (ctrl & 0x10) != 0;
    bool sawtooth = (ctrl & 0x20) != 0;
    bool pulse    = (ctrl & 0x40) != 0;
    bool noise    = (ctrl & 0x80) != 0;

    // No waveform selected: silent
    if (!triangle && !sawtooth && !pulse && !noise) {
        return 0.0f;
    }

    // Compute individual waveform outputs as 12-bit unsigned values (0-4095)
    uint16_t triOut = 0, sawOut = 0, pulOut = 0, noiOut = 0;
    bool hasOutput = false;

    if (sawtooth) {
        sawOut = static_cast<uint16_t>((acc >> 12) & 0xFFF);
        hasOutput = true;
    }

    if (triangle) {
        uint32_t triAcc = acc;
        // Ring modulation: XOR with previous voice's bit 23
        if (ctrl & 0x04) {
            int prevVoice = (voiceIndex + kNumVoices - 1) % kNumVoices;
            if (voices[prevVoice].phaseAccumulator & 0x800000)
                triAcc ^= 0x800000;
        }
        // Fold: if bit 23 is set, invert upper 12 bits
        uint16_t raw = static_cast<uint16_t>((triAcc >> 11) & 0xFFF);
        if (triAcc & 0x800000)
            raw ^= 0xFFF;
        triOut = raw;
        hasOutput = true;
    }

    if (pulse) {
        // Compare upper 12 bits of accumulator with pulse width
        uint16_t accUpper = static_cast<uint16_t>((acc >> 12) & 0xFFF);
        pulOut = (accUpper >= pw) ? 0xFFF : 0x000;
        hasOutput = true;
    }

    if (noise) {
        // Upper 8 bits of LFSR, scaled to 12 bits
        noiOut = static_cast<uint16_t>(((v.lfsr >> 15) & 0xFF) << 4);
        hasOutput = true;
    }

    if (!hasOutput) return 0.0f;

    // Combine waveforms: when multiple are selected, AND them (real SID behavior)
    uint16_t combined = 0xFFF;
    int waveCount = 0;
    if (triangle) { combined &= triOut; waveCount++; }
    if (sawtooth) { combined &= sawOut; waveCount++; }
    if (pulse)    { combined &= pulOut; waveCount++; }
    if (noise)    { combined &= noiOut; waveCount++; }

    // If only one waveform, use it directly (avoid AND reducing to 0)
    if (waveCount == 1) {
        if (triangle) combined = triOut;
        else if (sawtooth) combined = sawOut;
        else if (pulse) combined = pulOut;
        else combined = noiOut;
    }

    // Convert 12-bit unsigned (0-4095) to float (-1.0 to +1.0)
    return (static_cast<float>(combined) / 2047.5f) - 1.0f;
}

// ─── ADSR envelope ──────────────────────────────────────────────────────────

void SID::clockADSR(int voiceIndex)
{
    Voice& v = voices[voiceIndex];
    uint8_t ad = getAttackDecay(voiceIndex);
    uint8_t sr = getSustainRelease(voiceIndex);

    uint8_t attackIdx  = (ad >> 4) & 0x0F;
    uint8_t decayIdx   = ad & 0x0F;
    uint8_t sustainLvl = ((sr >> 4) & 0x0F) * 17; // 0-15 → 0-255
    uint8_t releaseIdx = sr & 0x0F;

    // Advance counter by the number of CPU cycles per audio sample (~22.68)
    int cyclesToAdvance = static_cast<int>(kCyclesPerSample);

    switch (v.adsrState) {
        case ADSR_ATTACK: {
            int rate = kAttackRate[attackIdx];
            v.adsrCounter += cyclesToAdvance;
            while (v.adsrCounter >= rate) {
                v.adsrCounter -= rate;
                if (v.adsrLevel < 255) {
                    v.adsrLevel++;
                }
                if (v.adsrLevel >= 255) {
                    v.adsrLevel = 255;
                    v.adsrState = ADSR_DECAY;
                    v.adsrCounter = 0;
                    v.expCounter = 0;
                    break;
                }
            }
            break;
        }
        case ADSR_DECAY: {
            int rate = kDecayReleaseRate[decayIdx];
            // Apply exponential period scaling
            uint8_t expPeriod = 1;
            for (int i = 0; i < 6; ++i) {
                if (v.adsrLevel >= kExpPeriodThreshold[i]) {
                    expPeriod = kExpPeriodValue[i];
                    break;
                }
            }
            v.adsrCounter += cyclesToAdvance;
            while (v.adsrCounter >= rate) {
                v.adsrCounter -= rate;
                v.expCounter++;
                if (v.expCounter >= expPeriod) {
                    v.expCounter = 0;
                    if (v.adsrLevel > sustainLvl) {
                        v.adsrLevel--;
                    }
                }
                if (v.adsrLevel <= sustainLvl) {
                    v.adsrLevel = sustainLvl;
                    v.adsrState = ADSR_SUSTAIN;
                    break;
                }
            }
            break;
        }
        case ADSR_SUSTAIN:
            // Hold at sustain level; do nothing
            v.adsrLevel = sustainLvl;
            break;

        case ADSR_RELEASE: {
            int rate = kDecayReleaseRate[releaseIdx];
            uint8_t expPeriod = 1;
            for (int i = 0; i < 6; ++i) {
                if (v.adsrLevel >= kExpPeriodThreshold[i]) {
                    expPeriod = kExpPeriodValue[i];
                    break;
                }
            }
            v.adsrCounter += cyclesToAdvance;
            while (v.adsrCounter >= rate) {
                v.adsrCounter -= rate;
                v.expCounter++;
                if (v.expCounter >= expPeriod) {
                    v.expCounter = 0;
                    if (v.adsrLevel > 0) {
                        v.adsrLevel--;
                    }
                }
                if (v.adsrLevel == 0) break;
            }
            break;
        }
    }
}

// ─── Audio generation ───────────────────────────────────────────────────────

void SID::generateSamples(float* output, int frameCount)
{
    std::lock_guard<std::mutex> lock(sidMutex);

    // Read filter/volume registers once per buffer
    uint16_t filterCutoff = (static_cast<uint16_t>(regs[21]) & 0x07) |
                            (static_cast<uint16_t>(regs[22]) << 3);
    uint8_t resFilt  = regs[23];
    uint8_t modeVol  = regs[24];

    uint8_t resonance   = (resFilt >> 4) & 0x0F;
    bool    filtVoice[3] = {
        (resFilt & 0x01) != 0,
        (resFilt & 0x02) != 0,
        (resFilt & 0x04) != 0
    };
    bool    filtExt      = (resFilt & 0x08) != 0;
    (void)filtExt; // no external input

    bool    modeLowPass  = (modeVol & 0x10) != 0;
    bool    modeBandPass = (modeVol & 0x20) != 0;
    bool    modeHighPass = (modeVol & 0x40) != 0;
    bool    voice3Off    = (modeVol & 0x80) != 0;
    float   masterVolume = static_cast<float>(modeVol & 0x0F) / 15.0f;

    // Filter coefficient from 11-bit cutoff (approximate mapping)
    // SID cutoff range: ~30 Hz to ~12 kHz with 6581 filter caps
    float cutoffHz = 30.0f + (static_cast<float>(filterCutoff) / 2047.0f) * 11970.0f;
    float f = 2.0f * std::sin(static_cast<float>(M_PI) * cutoffHz / static_cast<float>(kSampleRate));
    f = std::min(f, 0.45f); // clamp for stability

    // Resonance: 4-bit → Q damping (higher resonance = less damping)
    float q = 1.0f - (static_cast<float>(resonance) / 17.0f);
    q = std::max(q, 0.05f); // prevent instability

    for (int s = 0; s < frameCount; ++s) {
        float voiceOut[kNumVoices];

        for (int v = 0; v < kNumVoices; ++v) {
            Voice& voice = voices[v];
            uint16_t freq = getFrequency(v);
            uint8_t ctrl = getControl(v);

            // Save previous phase for noise LFSR clocking
            voice.prevPhaseAcc = voice.phaseAccumulator;

            // Advance phase accumulator
            // Each sample spans ~22.68 CPU cycles; accumulator advances by freq per cycle
            uint32_t phaseInc = static_cast<uint32_t>(
                static_cast<double>(freq) * kCyclesPerSample);
            voice.phaseAccumulator = (voice.phaseAccumulator + phaseInc) & 0xFFFFFF;

            // SYNC: reset this voice's accumulator when the previous voice's bit 23 transitions 0→1
            if (ctrl & 0x02) {
                int prevV = (v + kNumVoices - 1) % kNumVoices;
                Voice& prev = voices[prevV];
                if (!(prev.prevPhaseAcc & 0x800000) && (prev.phaseAccumulator & 0x800000)) {
                    voice.phaseAccumulator = 0;
                }
            }

            // Clock noise LFSR when bit 19 transitions 0→1
            if (!(voice.prevPhaseAcc & 0x080000) && (voice.phaseAccumulator & 0x080000)) {
                clockNoiseLFSR(voice);
            }

            // Compute waveform
            float waveform = computeWaveform(v);

            // Clock ADSR envelope
            clockADSR(v);

            // Apply envelope
            voiceOut[v] = waveform * (static_cast<float>(voice.adsrLevel) / 255.0f);
        }

        // Mute voice 3 from output if bit 7 of MODE/VOL is set
        // (voice 3 still runs for modulation/OSC3/ENV3 reads)
        float voice3Output = voice3Off ? 0.0f : voiceOut[2];

        // Separate filtered and unfiltered signals
        float filteredInput = 0.0f;
        float unfilteredOutput = 0.0f;

        if (filtVoice[0]) filteredInput += voiceOut[0];
        else unfilteredOutput += voiceOut[0];

        if (filtVoice[1]) filteredInput += voiceOut[1];
        else unfilteredOutput += voiceOut[1];

        if (filtVoice[2]) filteredInput += voice3Output;
        else unfilteredOutput += voice3Output;

        // State-variable filter
        filterLP += f * filterBP;
        float hp = filteredInput - filterLP - q * filterBP;
        filterBP += f * hp;

        // Combine filter outputs based on mode
        float filteredOutput = 0.0f;
        if (modeLowPass)  filteredOutput += filterLP;
        if (modeBandPass) filteredOutput += filterBP;
        if (modeHighPass) filteredOutput += hp;

        // If no filter mode selected but voices are routed to filter, pass through unfiltered
        if (!modeLowPass && !modeBandPass && !modeHighPass) {
            unfilteredOutput += filteredInput;
            filteredOutput = 0.0f;
        }

        // Final mix with master volume
        float sample = (filteredOutput + unfilteredOutput) * masterVolume;

        // Scale to reasonable output level (~0.4 max, complementing cassette ±0.22)
        sample *= 0.4f;

        output[s] = sample;
    }
}

// ─── Snapshot ───────────────────────────────────────────────────────────────

void SID::copySnapshot(Snapshot& out) const
{
    std::lock_guard<std::mutex> lock(sidMutex);
    out.regs = regs;
}
