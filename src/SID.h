// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// SID — P-LAB A1-SID Sound Card. MOS 6581 / CSG 8580 emulation, wrapping
// libresidfp (cycle-accurate, GPL-2.0+, vendored under third_party/libresidfp).
// I/O at $C800-$CFFF (29 registers, address & 0x1F).
//
// Public API kept compatible with the previous in-house engine so Memory,
// PeripheralBus, AudioDevice and the snapshot/UI integrations don't change.
// What's new: setChipModel(MOS6581/CSG8580) selectable from the Hardware
// menu (the physical A1-SID card accepts either chip in its socket).

#ifndef SID_H
#define SID_H

#include "AudioDevice.h"
#include "LockOrder.h"
#include "RealtimeDiagnostics.h"
#include "CpuClock.h"
#include "Peripheral.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>

namespace reSIDfp { class SID; }

namespace pom1 {

class SID : public AudioSource, public Peripheral
{
public:
    std::string_view name() const override { return "A1-SID"; }
    std::string_view mutexLabel() const override { return "SID::chipMutex"; }

    static constexpr int kNumRegisters = 29;
    static constexpr int kSampleRate   = 44100;

    enum class ChipModel : uint8_t {
        MOS6581 = 0,   ///< Vintage 6581 with non-linear filter
        MOS8580 = 1,   ///< Cleaner CSG 8580 revision (mapped to libresidfp CSG8580)
    };

    /// `outputSampleRate` is the actual rate the audio device runs at
    /// (`AudioDevice::getActualSampleRate()`). libresidfp is configured
    /// with this rate so that one second of emulated CPU cycles produces
    /// exactly one second's worth of samples — keeping the music tempo
    /// in lockstep with the audio device's wallclock consumption.
    /// Defaults to kSampleRate (44.1 kHz) when unspecified.
    explicit SID(int outputSampleRate = kSampleRate);
    ~SID() override;

    /// Full reset: chip state + shadowRegs + sample ring. The ring reset
    /// writes `ringTail`, which is normally owned by the audio-callback
    /// consumer, so this call is ONLY safe when the SID is not currently
    /// registered in `AudioDevice::sources` (e.g. called right after
    /// `removeSource` in setSIDEnabled(false)). Calling it while the
    /// audio thread can still execute `fillAudioBuffer` creates a data
    /// race on `ringTail` with visible audio glitches. For hardReset
    /// paths where the SID stays in `sources`, use `resetChip()` instead
    /// and let the ring drain naturally through the audio callback.
    void reset() override;

    /// Chip-only reset: `chip->reset()` + clear shadowRegs. Does NOT
    /// touch the sample ring — safe to call while the SID is registered
    /// as an audio source. Any residual samples in the ring drain
    /// through `fillAudioBuffer` in at most ~370 ms (ring capacity /
    /// sample rate), and once `chip->reset()` returns, newly produced
    /// samples are silence, so the transition is a short tail of the
    /// previous program fading to silence — no race, no pop.
    void resetChip();

    /// I/O — called from PeripheralBus dispatch. `reg` is in [0, 28].
    void    writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);

    /// Cycle tick — called from Memory::advanceCycles after each CPU slice.
    /// Drives libresidfp's clock at the *emulated* CPU rate (so the music
    /// tempo follows the emulation speed, not the audio device sample rate).
    /// Produced samples are pushed into an internal ring buffer that the
    /// audio callback drains.
    void advanceCycles(int cycles);

    /// AudioSource — called from the audio callback thread. Mono float32
    /// samples in [-1, +1] at 44.1 kHz. Drains the ring buffer; outputs
    /// silence on underrun (audio thread faster than emulation).
    void fillAudioBuffer(float* output, int frameCount) override;
    void copyRealtimeDiagnostics(RealtimeDiagnostics& out) const;

    /// Hot-swap chip model. Internally rebuilds libresidfp's filter chain
    /// (the 6581 and 8580 use entirely different filter models).
    void      setChipModel(ChipModel m);
    ChipModel getChipModel() const { return currentModel.load(std::memory_order_relaxed); }

    struct Snapshot {
        std::array<uint8_t, kNumRegisters> regs{};
        ChipModel chipModel = ChipModel::MOS6581;
    };
    void copySnapshot(Snapshot& out) const;

    // Round-trip the SID through a .snap file. Captures: 29 shadow registers
    // (last value written by the CPU) + chip model. NOT captured: libresidfp's
    // internal filter integrators / oscillator phase / envelope counters
    // (the engine doesn't expose them — see SID.h header comment about
    // write-only registers and bus fade). On load, each shadow register is
    // re-poked through writeRegister() so the chip restarts from a
    // consistent functional state — there is a brief filter-settle
    // transient on resume but no audible discontinuity for typical music
    // payloads.
    void serialize(pom1::SnapshotWriter& writer) const override;
    void deserialize(pom1::SnapshotReader& reader) override;

private:
    /// libresidfp::SID::clock(cycles, buf) writes one short per produced
    /// sample. At 1.022 MHz / 44.1 kHz the ratio is ~23 cycles per sample,
    /// so 4096 cycles -> ~178 samples max; 8192 staging shorts is plenty.
    static constexpr int kStagingShorts = 8192;

    /// Cap cycles per chip->clock() call so the staging buffer never
    /// overflows. 4096 cycles ≈ 178 samples max @ 44.1 kHz.
    static constexpr int kMaxCyclesPerBatch = 4096;

    /// Catch-up granularity: how many emulated cycles may pile up before the
    /// chip is clocked. `Memory::advanceCycles` is called ONCE PER 6502
    /// INSTRUCTION (M6502.cpp), i.e. every 2-7 cycles, so clocking the chip
    /// straight from there meant ~300 000 chip->clock() calls and ~300 000
    /// chipMutex lock/unlock pairs per emulated second — with batches so small
    /// that kMaxCyclesPerBatch above never once applied. Measured cost of that
    /// arrangement (august 2026, idle Wozmon loop, nothing writing to the SID):
    /// **+15.6 points of one core**, more than the rest of the emulator put
    /// together, and enough to matter on the Raspberry Pi kiosk.
    ///
    /// So cycles accumulate and the chip is clocked in one go. Fidelity is
    /// preserved where it is OBSERVABLE: every register read or write flushes
    /// the accumulator first, so a note-on is still clocked into the chip at
    /// the exact cycle the CPU wrote it. Same pattern as the TMS9918's beam
    /// catch-up. 1024 cycles ≈ 1 ms of emulated time ≈ 44 samples at 44.1 kHz,
    /// against a 16 384-sample ring (~370 ms), so the audio thread never sees
    /// the difference.
    static constexpr int kCatchUpCycles = 1024;

    /// Ring buffer between the emulation thread (producer, advanceCycles)
    /// and the audio thread (consumer, fillAudioBuffer). 16384 samples ≈
    /// 370 ms at 44.1 kHz — enough to absorb audio jitter without adding
    /// noticeable latency. SPSC semantics: producer mutates head only,
    /// consumer mutates tail only.
    static constexpr size_t kRingCapacity = 16384;

    std::unique_ptr<reSIDfp::SID> chip;
    // Atomic: read lock-free by getChipModel()/SnapshotPublisher and the
    // setChipModel() early-out, written under chipMutex in rebuildChip().
    std::atomic<ChipModel> currentModel { ChipModel::MOS6581 };
    int outputRate = kSampleRate;

    /// Shadow of the last value written to each register. libresidfp does
    /// not expose its internal register file (intentional — see header
    /// comment in residfp/SID.h about write-only registers and bus fade),
    /// so we track writes ourselves to populate Snapshot::regs for the UI.
    std::array<uint8_t, kNumRegisters> shadowRegs{};

    /// Sample ring (float). Atomic head/tail for lock-free SPSC access.
    std::array<float, kRingCapacity> ringBuf{};
    std::atomic<size_t> ringHead{0};
    std::atomic<size_t> ringTail{0};
#if POM1_REALTIME_DIAGNOSTICS
    std::atomic<uint64_t> ringUnderruns{0};
    std::atomic<uint64_t> ringOverflows{0};
#endif

    /// Serialises register writes / chip->clock() / setChipModel(). The
    /// audio thread (fillAudioBuffer) does NOT take this mutex — it only
    /// reads from the ring via atomic head/tail, so chip->clock() in
    /// advanceCycles can run unblocked.
    mutable RankedMutex<LockRank::Peripheral> chipMutex;

    /// Emulated cycles run but not yet clocked into the chip. Producer-side
    /// only: written by advanceCycles() on the emulation thread and by the
    /// flush sites, which are either that same thread or a UI-thread path that
    /// already holds EmulationController::stateMutex (snapshot restore, chip
    /// swap) with the emulation thread parked. Deliberately NOT atomic —
    /// making it so would advertise a concurrency that does not exist.
    int pendingCycles = 0;

    /// Clock everything accumulated so far, then reset. Takes chipMutex, so
    /// callers must NOT already hold it.
    void flushPendingCycles();

    /// Clock `cycles` into the chip and push the produced samples to the ring.
    /// chipMutex must already be held.
    void clockChipLocked(int cycles);

    void rebuildChip(ChipModel m);
};

} // namespace pom1

#endif // SID_H
