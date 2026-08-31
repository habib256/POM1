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

#ifndef AUDIODEVICE_H
#define AUDIODEVICE_H

#include "POM1Build.h"

#include <cstdint>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include "AudioService.h"        // AudioSource + the pom1::IAudioService seam
#include "RealtimeDiagnostics.h"

#if !POM1_IS_WASM
struct ma_device;
#endif

/// Central audio device that owns the hardware output (miniaudio on desktop,
/// Web Audio on WASM) and mixes registered AudioSource instances. The one
/// implementation of pom1::IAudioService that talks to real hardware — whoever
/// builds the machine constructs it and hands it over; the core never does.
class AudioDevice final : public pom1::IAudioService
{
public:
    static constexpr uint32_t kSampleRate = 44100;

    explicit AudioDevice(bool initializeHardware = true, int preferredLatencyMs = 0);
    ~AudioDevice() override;

    void addSource(AudioSource* source) override;
    void removeSource(AudioSource* source) override;

    bool isAvailable() const override { return audioAvailable; }

    /// Returns the actual sample rate negotiated with the OS audio device
    /// (miniaudio may pick a different native rate than `kSampleRate` if
    /// the hardware doesn't support 44.1 kHz natively, e.g. macOS Apple
    /// Silicon often runs the built-in output at 48 kHz). Sources that
    /// generate cycle-synchronous audio (SID) must use this rate so their
    /// production matches what the OS consumes — otherwise music tempo
    /// drifts by the rate ratio. WASM always returns kSampleRate.
    uint32_t getActualSampleRate() const override { return actualSampleRate; }

    /// Called from the audio callback — mixes all sources into output.
    void mixSources(float* output, int frameCount) override;
    void copyRealtimeDiagnostics(pom1::RealtimeDiagnostics& out) const override;

    /// `preferredLatencyMs` sizes the output buffer for that many milliseconds
    /// of cushion instead of the ~17 ms default (256 frames × 3 periods at
    /// 44.1 kHz).
    ///
    /// The default is deliberately tight — on a desktop it keeps key clicks and
    /// SID notes in step with the picture. It is *too* tight on a machine that
    /// cannot always refill the ring in time: on a Raspberry Pi kiosk the
    /// symptom is continuous crackle, and no amount of emulator tuning fixes it
    /// because the miss happens in the OS scheduler. 100-150 ms of cushion
    /// trades imperceptible extra delay for silence between the drop-outs.
    /// (Ported from NeoST's `--audio-latency`, same lesson on the same box.)
    ///
    /// It is a constructor argument because the device is now built by whoever
    /// owns it (main_imgui.cpp, right after the CLI is parsed) rather than deep
    /// inside Memory. It used to be a static set before construction — a global
    /// whose only purpose was to reach across that gap. Clamped to [20, 250] ms;
    /// 0 keeps the built-in default. No effect on WASM, where the browser owns
    /// the buffer size.

private:
    bool initAudio();
    void shutdownAudio();

    /// Per-source scratch, sized ONCE in the constructor. `mixSources` runs on
    /// the OS audio thread, where a heap allocation is a real-time hazard: it
    /// can take a lock in the allocator and blow the buffer deadline. A backend
    /// asking for more frames than this is mixed in chunks rather than growing
    /// the buffer, so the callback never allocates whatever the period size is.
    static constexpr int kMixScratchFrames = 8192;

    static constexpr std::size_t kMaxSources = 8;
    struct SourceSnapshot {
        std::array<AudioSource*, kMaxSources> entries{};
        std::size_t count = 0;
    };

    /// Producer-side serialization only. The real-time callback never takes
    /// this mutex: it pins one immutable snapshot with reader counters.
    mutable std::mutex sourcePublishMutex;
    std::array<SourceSnapshot, 2> sourceSnapshots{};
    std::atomic<unsigned> activeSourceSnapshot{0};
    std::array<std::atomic<unsigned>, 2> sourceReaders{{0, 0}};
    std::vector<float> tmpBuf;
    bool audioAvailable = false;
    uint32_t actualSampleRate = kSampleRate;
    int preferredLatencyMs_ = 0;   // 0 = miniaudio's default cushion
#if POM1_REALTIME_DIAGNOSTICS
    std::atomic<uint64_t> callbackCount{0};
    std::atomic<uint64_t> maxCallbackNs{0};
#endif

#if !POM1_IS_WASM
    struct MaDeviceDeleter { void operator()(ma_device* d) const noexcept; };
    std::unique_ptr<ma_device, MaDeviceDeleter> device;
    static void audioDataCallback(ma_device* pDevice, void* pOutput,
                                  const void* pInput, uint32_t frameCount);
#endif
};

#endif // AUDIODEVICE_H
