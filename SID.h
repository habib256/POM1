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

#ifndef SID_H
#define SID_H

#include <array>
#include <cstdint>
#include <mutex>

/// P-LAB A1-SID Sound Card — MOS 6581/8580 SID emulation.
/// I/O mapped at $C800-$CFFF (29 registers, address & 0x1F).
/// 3 voices with triangle/sawtooth/pulse/noise oscillators,
/// ADSR envelopes, programmable multimode filter, 4-bit master volume.
class SID
{
public:
    static constexpr int kNumVoices    = 3;
    static constexpr int kNumRegisters = 29;

    // Audio constants
    static constexpr int    kSampleRate    = 44100;
    static constexpr double kCpuClockHz    = 1000000.0;
    static constexpr double kCyclesPerSample = kCpuClockHz / kSampleRate; // ~22.676

    SID();

    void reset();

    // I/O interface — called from Memory::memRead / memWrite
    void    writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);

    // Audio generation — called from audio callback thread (CassetteDevice::fillAudioBuffer)
    void generateSamples(float* output, int frameCount);

    // Snapshot for UI (register display in memory map)
    struct Snapshot {
        std::array<uint8_t, kNumRegisters> regs{};
    };
    void copySnapshot(Snapshot& out) const;

private:
    // ADSR envelope states
    enum AdsrState : uint8_t {
        ADSR_ATTACK  = 0,
        ADSR_DECAY   = 1,
        ADSR_SUSTAIN = 2,
        ADSR_RELEASE = 3
    };

    struct Voice {
        uint32_t phaseAccumulator  = 0;   // 24-bit oscillator phase
        uint32_t prevPhaseAcc      = 0;   // previous value (for noise LFSR clocking)
        uint32_t lfsr              = 0x7FFFF8; // 23-bit noise LFSR

        uint8_t  adsrLevel         = 0;   // 0-255 envelope output
        AdsrState adsrState        = ADSR_RELEASE;
        int      adsrCounter       = 0;   // rate counter (CPU cycles)
        uint8_t  expCounter        = 0;   // exponential decay period counter
        bool     gateOn            = false;
    };

    // Register file
    std::array<uint8_t, kNumRegisters> regs{};

    // Voice state
    Voice voices[kNumVoices];

    // State-variable filter state
    float filterLP = 0.0f;
    float filterBP = 0.0f;

    // Thread safety (register writes from emulation thread, audio from callback thread)
    mutable std::mutex sidMutex;

    // Helpers
    uint16_t getFrequency(int voice) const;
    uint16_t getPulseWidth(int voice) const;
    uint8_t  getControl(int voice) const;
    uint8_t  getAttackDecay(int voice) const;
    uint8_t  getSustainRelease(int voice) const;

    float    computeWaveform(int voiceIndex);
    void     clockADSR(int voiceIndex);
    void     clockNoiseLFSR(Voice& v);

    // ADSR rate tables (CPU cycles per envelope step)
    static const int kAttackRate[16];
    static const int kDecayReleaseRate[16];

    // Exponential decay period thresholds
    static const uint8_t kExpPeriodThreshold[6];
    static const uint8_t kExpPeriodValue[6];
};

#endif // SID_H
