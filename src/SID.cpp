// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "SID.h"
#include "Logger.h"
#include "SnapshotIO.h"

// Pull in the vendored libresidfp upstream class. The libresidfp include
// directory (PUBLIC on the libresidfp_static target) puts its own SID.h
// on the include path under the same name as ours; we go through the
// residfp aggregate header to disambiguate.
#include "residfp/residfp_defs.h"
// The libresidfp upstream SID.h lives under the libresidfp_static target's
// PUBLIC include dir (third_party/libresidfp/src). Including it here by
// quoted name would re-include this file (same basename); use the relative
// path to disambiguate.
#include "../third_party/libresidfp/src/SID.h"

#include <algorithm>
#include <cstring>

namespace pom1 {

namespace {
constexpr double kCpuClockHz = static_cast<double>(POM1_CPU_CLOCK_HZ);

reSIDfp::ChipModel toUpstream(SID::ChipModel m)
{
    return (m == SID::ChipModel::MOS6581) ? reSIDfp::MOS6581 : reSIDfp::CSG8580;
}
} // namespace

SID::SID(int outputSampleRate)
    : outputRate(outputSampleRate)
{
    rebuildChip(currentModel);
    pom1::log().info("SID",
        std::string("libresidfp engine initialised (MOS6581 @ ") +
        std::to_string(outputRate) + " Hz)");
}

SID::~SID() = default;

void SID::rebuildChip(ChipModel m)
{
    // Caller must hold chipMutex (or guarantee no concurrent access — true
    // during construction). libresidfp's setChipModel can throw SIDError
    // if a filter model fails to initialise; we let it propagate (it would
    // indicate a corrupt vendored data table, i.e. a build problem).
    chip = std::make_unique<reSIDfp::SID>();
    chip->setChipModel(toUpstream(m));
    chip->setSamplingParameters(kCpuClockHz, reSIDfp::DECIMATE,
                                static_cast<double>(outputRate));
    chip->reset();
    currentModel = m;
}

void SID::resetChip()
{
    std::lock_guard<std::mutex> lock(chipMutex);
    chip->reset();
    shadowRegs.fill(0);
}

void SID::reset()
{
    resetChip();
    // Drain the ring so a leftover tail of samples from the previous
    // program doesn't bleed into the next one. `ringTail` is owned by the
    // audio-callback consumer in SPSC, so this store is ONLY race-free
    // when the SID is not currently registered in AudioDevice::sources —
    // i.e. after `removeSource()` in setSIDEnabled(false), or before
    // `addSource()` on first enable. For hardReset paths where the SID
    // stays wired to the mixer, callers must use `resetChip()` instead
    // (the ring will drain naturally through the audio callback).
    const size_t h = ringHead.load(std::memory_order_relaxed);
    ringTail.store(h, std::memory_order_release);
}

void SID::writeRegister(uint8_t reg, uint8_t value)
{
    if (reg >= kNumRegisters) return;
    std::lock_guard<std::mutex> lock(chipMutex);
    shadowRegs[reg] = value;
    chip->write(reg, value);
}

uint8_t SID::readRegister(uint8_t reg)
{
    if (reg >= kNumRegisters) return 0;
    std::lock_guard<std::mutex> lock(chipMutex);
    return chip->read(reg);
}

void SID::advanceCycles(int cycles)
{
    if (cycles <= 0) return;

    std::lock_guard<std::mutex> lock(chipMutex);

    short staging[kStagingShorts];
    int remaining = cycles;
    while (remaining > 0) {
        const int batch = std::min(remaining, kMaxCyclesPerBatch);
        const int produced = chip->clock(static_cast<unsigned int>(batch), staging);
        remaining -= batch;

        for (int i = 0; i < produced; ++i) {
            const float sample = static_cast<float>(staging[i]) * (1.0f / 32768.0f);

            const size_t head = ringHead.load(std::memory_order_relaxed);
            const size_t next = (head + 1) % kRingCapacity;

            if (next == ringTail.load(std::memory_order_acquire)) {
                size_t tail = ringTail.load(std::memory_order_relaxed);
                ringTail.store((tail + 1) % kRingCapacity, std::memory_order_release);
            }

            ringBuf[head] = sample;
            ringHead.store(next, std::memory_order_release);
        }
    }
}

void SID::fillAudioBuffer(float* output, int frameCount)
{
    if (!output || frameCount <= 0) return;

    for (int i = 0; i < frameCount; ++i) {
        const size_t tail = ringTail.load(std::memory_order_relaxed);
        if (tail == ringHead.load(std::memory_order_acquire)) {
            output[i] = 0.0f;
        } else {
            output[i] = ringBuf[tail];
            ringTail.store((tail + 1) % kRingCapacity, std::memory_order_release);
        }
    }
}

void SID::setChipModel(ChipModel m)
{
    if (m == currentModel) return;
    std::lock_guard<std::mutex> lock(chipMutex);
    rebuildChip(m);
    // Restore last-written register state so a music program in flight
    // doesn't go silent on chip swap.
    for (uint8_t r = 0; r < kNumRegisters; ++r) {
        chip->write(r, shadowRegs[r]);
    }
    pom1::log().info("SID", std::string("chip model -> ") +
                     (m == ChipModel::MOS6581 ? "MOS6581" : "CSG8580"));
}

void SID::copySnapshot(Snapshot& out) const
{
    std::lock_guard<std::mutex> lock(chipMutex);
    out.regs = shadowRegs;
    out.chipModel = currentModel;
}

void SID::serialize(SnapshotWriter& w) const
{
    std::lock_guard<std::mutex> lock(chipMutex);
    w.writeU8(static_cast<uint8_t>(currentModel));
    w.writeBytes(shadowRegs.data(), shadowRegs.size());
}

void SID::deserialize(SnapshotReader& r)
{
    const uint8_t model = r.readU8();
    std::array<uint8_t, kNumRegisters> regs{};
    r.readBytes(regs.data(), regs.size());
    // Apply outside the chipMutex by going through the public setters —
    // setChipModel rebuilds the libresidfp filter chain (taking chipMutex
    // internally) and writeRegister re-pokes each register so the engine
    // restarts in a consistent functional state.
    setChipModel(static_cast<ChipModel>(model));
    for (uint8_t i = 0; i < kNumRegisters; ++i) {
        writeRegister(i, regs[i]);
    }
}

} // namespace pom1
