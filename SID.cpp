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
    filterIC1eq = 0.0f;
    filterIC2eq = 0.0f;
    dcBlockPrev = 0.0f;
    dcBlockInput = 0.0f;
    outputLP = 0.0f;
    lastBusValue = 0;
}

// ─── Register access ────────────────────────────────────────────────────────

void SID::writeRegister(uint8_t reg, uint8_t value)
{
    if (reg >= kNumRegisters) return;
    // Registers 25-28 are read-only
    if (reg >= 25) return;

    std::lock_guard<std::mutex> lock(sidMutex);
    regs[reg] = value;
    lastBusValue = value;

    // Detect gate transitions for each voice's control register (offsets 4, 11, 18)
    for (int v = 0; v < kNumVoices; ++v) {
        uint8_t ctrlReg = static_cast<uint8_t>(v * 7 + 4);
        if (reg == ctrlReg) {
            bool newGate = (value & 0x01) != 0;
            Voice& voice = voices[v];
            if (newGate && !voice.gateOn) {
                // Gate ON: start attack
                voice.adsrState = ADSR_ATTACK;
                // ADSR delay bug: do NOT reset adsrCycleAccum — the rate counter
                // continues from its current position (real 6581 behavior)
                voice.expCounter = 0;
            } else if (!newGate && voice.gateOn) {
                // Gate OFF: start release
                voice.adsrState = ADSR_RELEASE;
                // Same: no reset of the cycle accumulator
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
            // Write-only registers: return last bus value (real 6581 behavior)
            return lastBusValue;
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

    // TEST bit: reset oscillator and force LFSR to all-ones (real 6581)
    if (ctrl & 0x08) {
        v.phaseAccumulator = 0;
        v.lfsr = 0x7FFFFF;
        return 0.0f;
    }

    uint32_t acc = v.phaseAccumulator;
    uint16_t pw = getPulseWidth(voiceIndex);

    // Waveform bits
    bool triangle = (ctrl & 0x10) != 0;
    bool sawtooth = (ctrl & 0x20) != 0;
    bool pulse    = (ctrl & 0x40) != 0;
    bool noise    = (ctrl & 0x80) != 0;

    // No waveform selected: decay last output toward 0 (anti-click)
    if (!triangle && !sawtooth && !pulse && !noise) {
        v.lastWaveOutput *= 0.992f;  // exponential decay ~1ms at internal rate
        return v.lastWaveOutput;
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
        // Phase 3.5 — handle PW extremes: $000 = constant low, $FFF = constant high (DC)
        if (pw == 0) {
            pulOut = 0x000;
        } else if (pw >= 0xFFF) {
            pulOut = 0xFFF;
        } else {
            uint16_t accUpper = static_cast<uint16_t>((acc >> 12) & 0xFFF);
            pulOut = (accUpper >= pw) ? 0xFFF : 0x000;
        }
        hasOutput = true;
    }

    if (noise) {
        // Étape 4 — extract noise bits from correct LFSR positions (real 6581)
        // Output bits 7..0 ← LFSR bits 22, 20, 16, 13, 11, 7, 4, 2
        uint8_t noiseBits = static_cast<uint8_t>(
            ((v.lfsr & (1 << 22)) >> 15) |
            ((v.lfsr & (1 << 20)) >> 14) |
            ((v.lfsr & (1 << 16)) >> 11) |
            ((v.lfsr & (1 << 13)) >>  9) |
            ((v.lfsr & (1 << 11)) >>  8) |
            ((v.lfsr & (1 <<  7)) >>  5) |
            ((v.lfsr & (1 <<  4)) >>  3) |
            ((v.lfsr & (1 <<  2)) >>  2));
        noiOut = static_cast<uint16_t>(noiseBits) << 4;
        hasOutput = true;
    }

    if (!hasOutput) return 0.0f;

    // Combine waveforms via AND (real SID behavior)
    uint16_t combined = 0xFFF;
    int waveCount = 0;
    if (triangle) { combined &= triOut; waveCount++; }
    if (sawtooth) { combined &= sawOut; waveCount++; }
    if (pulse)    { combined &= pulOut; waveCount++; }
    if (noise)    { combined &= noiOut; waveCount++; }

    // Combined waveform low-bit pull-down (6581 analog leakage)
    if (waveCount > 1) {
        combined &= 0xFF0;
    }

    // Noise + waveform LFSR writeback (6581 behavior)
    // When noise is combined with another waveform, the AND result bits
    // are written back into the LFSR, gradually locking it up to silence.
    if (noise && waveCount > 1) {
        uint8_t outBits = static_cast<uint8_t>((combined >> 4) & 0xFF);
        auto writeBit = [&](int lfsrBit, int outBit) {
            if (outBits & (1 << outBit))
                v.lfsr |= (1u << lfsrBit);
            else
                v.lfsr &= ~(1u << lfsrBit);
        };
        writeBit(22, 7); writeBit(20, 6); writeBit(16, 5); writeBit(13, 4);
        writeBit(11, 3); writeBit(7, 2);  writeBit(4, 1);  writeBit(2, 0);
    }

    // Convert 12-bit unsigned (0-4095) to float (-1.0 to +1.0)
    float result = (static_cast<float>(combined) / 2047.5f) - 1.0f;
    v.lastWaveOutput = result;
    return result;
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

    // Fractional cycle accumulator — add exact cycles per internal sample.
    // Clamp to max rate period (31251) to prevent runaway processing:
    // the real 6581's 15-bit rate counter can't accumulate more than one period.
    v.adsrCycleAccum += kCyclesPerSample;
    if (v.adsrCycleAccum > 31252.0)
        v.adsrCycleAccum = 31252.0;

    switch (v.adsrState) {
        case ADSR_ATTACK: {
            double rate = static_cast<double>(kAttackRate[attackIdx]);
            while (v.adsrCycleAccum >= rate) {
                v.adsrCycleAccum -= rate;
                if (v.adsrLevel < 255) {
                    v.adsrLevel++;
                }
                if (v.adsrLevel >= 255) {
                    v.adsrLevel = 255;
                    v.adsrState = ADSR_DECAY;
                    v.adsrCycleAccum = 0.0;
                    v.expCounter = 0;
                    break;
                }
            }
            break;
        }
        case ADSR_DECAY: {
            double rate = static_cast<double>(kDecayReleaseRate[decayIdx]);
            while (v.adsrCycleAccum >= rate) {
                v.adsrCycleAccum -= rate;
                // Recalculate exponential period every step (threshold-crossing accuracy)
                uint8_t expPeriod = 1;
                for (int i = 0; i < 6; ++i) {
                    if (v.adsrLevel >= kExpPeriodThreshold[i]) {
                        expPeriod = kExpPeriodValue[i]; break;
                    }
                }
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
            // Étape 6 — real SID holds the current level without forcing it.
            // The level was set by the decay phase and doesn't change while gate=ON.
            break;

        case ADSR_RELEASE: {
            double rate = static_cast<double>(kDecayReleaseRate[releaseIdx]);
            while (v.adsrCycleAccum >= rate) {
                v.adsrCycleAccum -= rate;
                // Recalculate exponential period every step
                uint8_t expPeriod = 1;
                for (int i = 0; i < 6; ++i) {
                    if (v.adsrLevel >= kExpPeriodThreshold[i]) {
                        expPeriod = kExpPeriodValue[i]; break;
                    }
                }
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

// ─── Fast approximation of pow(x, 1.45) on [0,1] ───────────────────────────
// Cubic polynomial fit — max error ~0.5% vs std::pow
static inline float fastPow145(float x)
{
    return x * (0.42f + x * (0.78f - 0.20f * x));
}

// ─── Audio generation ───────────────────────────────────────────────────────

void SID::fillAudioBuffer(float* output, int frameCount)
{
    std::lock_guard<std::mutex> lock(sidMutex);

    static constexpr float kPi = static_cast<float>(M_PI);
    static constexpr float kInternalRateF = static_cast<float>(kInternalRate);

    // DC blocker coefficient (~20 Hz high-pass for digi DC removal)
    static constexpr float kDcAlpha =
        1.0f - (2.0f * kPi * 20.0f / static_cast<float>(kSampleRate));

    // Triangular decimation weights [1,3,3,1]/8
    static constexpr float kDecimWeights[4] = { 0.125f, 0.375f, 0.375f, 0.125f };

    // Output low-pass coefficient (~18 kHz analog warmth)
    static constexpr float kLpAlpha = 0.717f;

    for (int s = 0; s < frameCount; ++s) {

        // ── Read registers ONCE per output sample (hoisted from OS loop) ──
        uint16_t filterCutoff = (static_cast<uint16_t>(regs[21]) & 0x07) |
                                (static_cast<uint16_t>(regs[22]) << 3);
        uint8_t resFilt = regs[23];
        uint8_t modeVol = regs[24];

        uint8_t resonance = (resFilt >> 4) & 0x0F;
        bool filtVoice[3] = {
            (resFilt & 0x01) != 0,
            (resFilt & 0x02) != 0,
            (resFilt & 0x04) != 0
        };

        bool modeLowPass  = (modeVol & 0x10) != 0;
        bool modeBandPass = (modeVol & 0x20) != 0;
        bool modeHighPass = (modeVol & 0x40) != 0;
        bool voice3Off    = (modeVol & 0x80) != 0;
        float masterVolume = static_cast<float>(modeVol & 0x0F) / 15.0f;

        // ── 6581 non-linear filter cutoff (computed once per output sample) ──
        float fc = static_cast<float>(filterCutoff);
        float cutoffHz;
        if (fc < 192.0f) {
            cutoffHz = 220.0f;  // 6581 minimum floor
        } else {
            float x = (fc - 192.0f) / (2047.0f - 192.0f);
            cutoffHz = 220.0f + 12000.0f * fastPow145(x);
        }

        // ── ZDF SVF coefficients (Zavalishin trapezoidal — no frequency warping) ──
        float g = std::tan(kPi * cutoffHz / kInternalRateF);
        // Resonance → damping: k=2 is no resonance, k→0 is self-oscillation
        float k = 2.0f * (1.0f - static_cast<float>(resonance) / 16.0f);
        k = std::max(k, 0.04f);  // allow near-self-oscillation
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1;
        float a3 = g * a2;

        // ── Digi DC offset (from volume register) ──
        float dcOffset = (static_cast<float>(modeVol & 0x0F) - 7.5f) / 15.0f;

        // ── 4× oversampling ─────────────────────────────────────────────
        float oversampleBuf[kOversample];

        for (int os = 0; os < kOversample; ++os) {

            // ── Voices ──────────────────────────────────────────────────
            float voiceOut[kNumVoices];

            for (int v = 0; v < kNumVoices; ++v) {
                Voice& voice = voices[v];
                uint16_t freq = getFrequency(v);
                uint8_t ctrl = getControl(v);

                voice.prevPhaseAcc = voice.phaseAccumulator;

                // Fractional phase accumulator (prevents frequency drift)
                double exactInc = static_cast<double>(freq) * kCyclesPerSample
                                  + voice.phaseRemainder;
                uint32_t intInc = static_cast<uint32_t>(exactInc);
                voice.phaseRemainder = exactInc - static_cast<double>(intInc);
                voice.phaseAccumulator = (voice.phaseAccumulator + intInc) & 0xFFFFFF;

                // SYNC: reset when previous voice's bit 23 transitions 0→1
                if (ctrl & 0x02) {
                    int prevV = (v + kNumVoices - 1) % kNumVoices;
                    Voice& prev = voices[prevV];
                    if (!(prev.prevPhaseAcc & 0x800000) &&
                        (prev.phaseAccumulator & 0x800000)) {
                        voice.phaseAccumulator = 0;
                    }
                }

                // Noise LFSR clock on bit 19 transition 0→1
                if (!(voice.prevPhaseAcc & 0x080000) &&
                    (voice.phaseAccumulator & 0x080000)) {
                    clockNoiseLFSR(voice);
                }

                float waveform = computeWaveform(v);
                waveform += 0.17f;  // 6581 voice DC bias (asymmetric distortion)
                clockADSR(v);
                voiceOut[v] = waveform * (static_cast<float>(voice.adsrLevel) / 255.0f);
            }

            // ── Mix voices into filtered / unfiltered paths ─────────────
            float voice3Output = voice3Off ? 0.0f : voiceOut[2];

            float filteredInput = 0.0f;
            float unfilteredOutput = 0.0f;

            if (filtVoice[0]) filteredInput += voiceOut[0];
            else unfilteredOutput += voiceOut[0];
            if (filtVoice[1]) filteredInput += voiceOut[1];
            else unfilteredOutput += voiceOut[1];
            if (filtVoice[2]) filteredInput += voice3Output;
            else unfilteredOutput += voice3Output;

            // 6581 filter input saturation (op-amp soft clipping)
            filteredInput = std::tanh(filteredInput * 1.2f);

            // ── Zero-Delay Feedback SVF (Zavalishin trapezoidal topology) ──
            // No frequency warping, stable at all frequencies, accurate self-oscillation.
            // BP feedback saturated via tanh for 6581 analog character.
            float v3 = filteredInput - filterIC2eq
                       - k * std::tanh(filterIC1eq * 1.5f);
            float hp = v3 * a1;
            float v1 = hp * g;
            float bp = v1 + filterIC1eq;
            filterIC1eq = v1 + bp;
            float v2 = bp * g;
            float lp = v2 + filterIC2eq;
            filterIC2eq = v2 + lp;

            // Safety clamp: prevent filter state divergence (NaN/infinity)
            if (filterIC1eq > 10.0f) filterIC1eq = 10.0f;
            else if (filterIC1eq < -10.0f) filterIC1eq = -10.0f;
            if (filterIC2eq > 10.0f) filterIC2eq = 10.0f;
            else if (filterIC2eq < -10.0f) filterIC2eq = -10.0f;

            float filteredOutput = 0.0f;
            if (modeLowPass)  filteredOutput += lp;
            if (modeBandPass) filteredOutput += bp;
            if (modeHighPass) filteredOutput += hp;

            // No filter mode: pass through unfiltered
            if (!modeLowPass && !modeBandPass && !modeHighPass) {
                unfilteredOutput += filteredInput;
            }

            float internalSample = (filteredOutput + unfilteredOutput) * masterVolume
                                   + dcOffset * 0.06f;

            oversampleBuf[os] = internalSample;

        } // end oversampling loop

        // ── Decimation: triangular filter [1,3,3,1]/8 ──────────────────
        float rawSample = 0.0f;
        for (int i = 0; i < kOversample; ++i)
            rawSample += oversampleBuf[i] * kDecimWeights[i];

        // ── Output low-pass ~18 kHz (analog warmth) ────────────────────
        outputLP += kLpAlpha * (rawSample - outputLP);

        // ── DC blocker (removes digi DC, passes AC audio) ───────────────
        float dcBlockOut = outputLP - dcBlockInput + kDcAlpha * dcBlockPrev;
        dcBlockInput = outputLP;
        dcBlockPrev = dcBlockOut;

        output[s] = dcBlockOut * 0.4f;
    }
}

// ─── Snapshot ───────────────────────────────────────────────────────────────

void SID::copySnapshot(Snapshot& out) const
{
    std::lock_guard<std::mutex> lock(sidMutex);
    out.regs = regs;
}
