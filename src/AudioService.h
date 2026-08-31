// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// AudioService.h — the audio seam the emulator core is written against.
//
// `Memory` used to construct an `AudioDevice` itself, which meant every core —
// including the one a unit test builds — reached for the host's sound hardware
// on its way up. That is a host service, and a core that creates its own host
// services cannot be built hermetically. This header is the interface it is
// handed instead: six methods, exactly what the core and the controller use.
//
// The seam is the same shape as `ResourceLocator` (where do files live?) and
// `DisplayDevice` (where do characters go?): the decision belongs to whoever
// builds the machine, not to the machine.

#ifndef AUDIOSERVICE_H
#define AUDIOSERVICE_H

#include "RealtimeDiagnostics.h"

#include <array>
#include <cstddef>
#include <cstdint>

/// Interface for audio sources that can be mixed by an audio service.
/// Defined here rather than in AudioDevice.h so a source (CassetteDevice, SID)
/// needs the seam, not the miniaudio-backed implementation of it.
class AudioSource
{
public:
    virtual ~AudioSource() = default;
    /// Fill output buffer with frameCount mono float32 samples.
    /// Called from the audio callback thread — must be fast and thread-safe.
    virtual void fillAudioBuffer(float* output, int frameCount) = 0;
};

namespace pom1 {

/// What the emulator core needs from audio: register/unregister sources, know
/// the rate they must produce at, and let a frontend pull the mix.
class IAudioService
{
public:
    virtual ~IAudioService() = default;

    virtual void addSource(AudioSource* source) = 0;
    /// MUST be a lifetime barrier: when this returns, no callback may still be
    /// dereferencing `source`, because the caller is about to destroy it.
    virtual void removeSource(AudioSource* source) = 0;

    /// False when no output exists (headless double, or a device that failed to
    /// open). Sources use it to skip work nobody can hear — never to change
    /// what the emulated machine computes.
    virtual bool isAvailable() const = 0;

    /// The rate the consumer will actually pull at. Cycle-driven sources (SID,
    /// cassette) must produce at this rate or their tempo drifts by the ratio;
    /// see AudioDevice::getActualSampleRate for why it is not always 44.1 kHz.
    virtual uint32_t getActualSampleRate() const = 0;

    virtual void mixSources(float* output, int frameCount) = 0;
    virtual void copyRealtimeDiagnostics(RealtimeDiagnostics& out) const = 0;
};

/// In-memory double: registers sources and hands back silence. No device, no
/// mixing, no threads — for a core that is built to be inspected rather than
/// listened to. `isAvailable()` is false, so cassette/SID skip the work whose
/// only purpose is to be heard, and the emulated machine is unaffected.
///
/// Deliberately NOT the mixer with the hardware switched off: that is
/// `AudioDevice(false)`, which still mixes and is what the audio tests use.
/// This one exists to prove a core can be built with no audio machinery at all.
class NullAudioService final : public IAudioService
{
public:
    static constexpr std::size_t kMaxSources = 8;

    void addSource(AudioSource* source) override
    {
        if (!source || count_ >= kMaxSources) return;
        for (std::size_t i = 0; i < count_; ++i)
            if (sources_[i] == source) return;
        sources_[count_++] = source;
    }

    void removeSource(AudioSource* source) override
    {
        for (std::size_t i = 0; i < count_; ++i) {
            if (sources_[i] != source) continue;
            for (std::size_t j = i + 1; j < count_; ++j) sources_[j - 1] = sources_[j];
            sources_[--count_] = nullptr;
            return;
        }
    }

    bool isAvailable() const override { return false; }
    uint32_t getActualSampleRate() const override { return 44100; }

    void mixSources(float* output, int frameCount) override
    {
        if (!output) return;
        for (int i = 0; i < frameCount; ++i) output[i] = 0.0f;
    }

    void copyRealtimeDiagnostics(RealtimeDiagnostics& out) const override { out = {}; }

    /// Test observation: how many sources the core registered, and which.
    std::size_t sourceCount() const { return count_; }
    bool hasSource(const AudioSource* source) const
    {
        for (std::size_t i = 0; i < count_; ++i)
            if (sources_[i] == source) return true;
        return false;
    }

private:
    std::array<AudioSource*, kMaxSources> sources_{};
    std::size_t count_ = 0;
};

} // namespace pom1

#endif // AUDIOSERVICE_H
