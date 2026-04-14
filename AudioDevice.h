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
#include <memory>
#include <mutex>
#include <vector>

#if !POM1_IS_WASM
struct ma_device;
#endif

/// Interface for audio sources that can be mixed by AudioDevice.
class AudioSource
{
public:
    virtual ~AudioSource() = default;
    /// Fill output buffer with frameCount mono float32 samples.
    /// Called from the audio callback thread — must be fast and thread-safe.
    virtual void fillAudioBuffer(float* output, int frameCount) = 0;
};

/// Central audio device that owns the hardware output (miniaudio on desktop,
/// Web Audio on WASM) and mixes registered AudioSource instances.
class AudioDevice
{
public:
    static constexpr uint32_t kSampleRate = 44100;

    AudioDevice();
    ~AudioDevice();

    void addSource(AudioSource* source);
    void removeSource(AudioSource* source);

    bool isAvailable() const { return audioAvailable; }

    /// Returns the actual sample rate negotiated with the OS audio device
    /// (miniaudio may pick a different native rate than `kSampleRate` if
    /// the hardware doesn't support 44.1 kHz natively, e.g. macOS Apple
    /// Silicon often runs the built-in output at 48 kHz). Sources that
    /// generate cycle-synchronous audio (SID) must use this rate so their
    /// production matches what the OS consumes — otherwise music tempo
    /// drifts by the rate ratio. WASM always returns kSampleRate.
    uint32_t getActualSampleRate() const { return actualSampleRate; }

    /// Called from the audio callback — mixes all sources into output.
    void mixSources(float* output, int frameCount);

private:
    bool initAudio();
    void shutdownAudio();

    std::vector<AudioSource*> sources;
    mutable std::mutex sourcesMutex;
    std::vector<float> tmpBuf;
    bool audioAvailable = false;
    uint32_t actualSampleRate = kSampleRate;

#if !POM1_IS_WASM
    struct MaDeviceDeleter { void operator()(ma_device* d) const noexcept; };
    std::unique_ptr<ma_device, MaDeviceDeleter> device;
    static void audioDataCallback(ma_device* pDevice, void* pOutput,
                                  const void* pInput, uint32_t frameCount);
#endif
};

#endif // AUDIODEVICE_H
