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

#include "AudioDevice.h"

#include <algorithm>
#include <cstring>

#if POM1_IS_WASM
#include <emscripten.h>
#else
#define MINIAUDIO_IMPLEMENTATION
#include "third_party/miniaudio.h"
#endif

// ─── Mixing ─────────────────────────────────────────────────────────────────

void AudioDevice::mixSources(float* output, int frameCount)
{
    std::memset(output, 0, static_cast<size_t>(frameCount) * sizeof(float));

    float tmpBuf[512];

    std::lock_guard<std::mutex> lock(sourcesMutex);
    for (AudioSource* src : sources) {
        int remaining = frameCount;
        int offset = 0;
        while (remaining > 0) {
            int chunk = std::min(remaining, 512);
            src->fillAudioBuffer(tmpBuf, chunk);
            for (int i = 0; i < chunk; ++i)
                output[offset + i] += tmpBuf[i];
            offset += chunk;
            remaining -= chunk;
        }
    }

    // Clamp to [-1, 1]
    for (int i = 0; i < frameCount; ++i)
        output[i] = std::max(-1.0f, std::min(1.0f, output[i]));
}

// ─── Source management ──────────────────────────────────────────────────────

void AudioDevice::addSource(AudioSource* source)
{
    if (!source) return;
    std::lock_guard<std::mutex> lock(sourcesMutex);
    sources.push_back(source);
}

void AudioDevice::removeSource(AudioSource* source)
{
    std::lock_guard<std::mutex> lock(sourcesMutex);
    sources.erase(std::remove(sources.begin(), sources.end(), source), sources.end());
}

// ─── Platform callbacks ─────────────────────────────────────────────────────

#if POM1_IS_WASM

static AudioDevice* g_wasmAudioDevice = nullptr;

extern "C" {
EMSCRIPTEN_KEEPALIVE
void pom1_fillAudioBuffer(float* buf, int frames)
{
    if (g_wasmAudioDevice)
        g_wasmAudioDevice->mixSources(buf, frames);
    else
        std::fill(buf, buf + frames, 0.0f);
}
}

#else

void AudioDevice::audioDataCallback(ma_device* pDevice, void* pOutput,
                                     const void* /*pInput*/, uint32_t frameCount)
{
    AudioDevice* self = static_cast<AudioDevice*>(pDevice->pUserData);
    float* output = static_cast<float*>(pOutput);
    if (self == nullptr) {
        std::fill(output, output + frameCount, 0.0f);
        return;
    }
    self->mixSources(output, static_cast<int>(frameCount));
}

#endif

// ─── Init / Shutdown ────────────────────────────────────────────────────────

AudioDevice::AudioDevice()
{
    initAudio();
}

AudioDevice::~AudioDevice()
{
    shutdownAudio();
}

bool AudioDevice::initAudio()
{
#if POM1_IS_WASM
    g_wasmAudioDevice = this;

    emscripten_run_script(
        "var ctx = new (window.AudioContext || window.webkitAudioContext)({sampleRate: 44100});"
        "var bufSize = 512;"
        "var proc = ctx.createScriptProcessor(bufSize, 0, 1);"
        "var heapBuf = Module._malloc(bufSize * 4);"
        "proc.onaudioprocess = function(e) {"
        "  Module._pom1_fillAudioBuffer(heapBuf, bufSize);"
        "  var out = e.outputBuffer.getChannelData(0);"
        "  out.set(Module.HEAPF32.subarray(heapBuf >> 2, (heapBuf >> 2) + bufSize));"
        "};"
        "proc.connect(ctx.destination);"
        "window._pom1Audio = {ctx: ctx, proc: proc, buf: heapBuf};"
        "var resume = function() { if (ctx.state === 'suspended') ctx.resume(); };"
        "document.addEventListener('click', resume, {once: true});"
        "document.addEventListener('keydown', resume, {once: true});"
    );

    audioAvailable = true;
    return true;
#else
    shutdownAudio();

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = kSampleRate;
    config.periodSizeInFrames = 256;
    config.periods = 3;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.dataCallback = &AudioDevice::audioDataCallback;
    config.pUserData = this;

    device = new ma_device();
    if (ma_device_init(nullptr, &config, device) != MA_SUCCESS) {
        delete device;
        device = nullptr;
        audioAvailable = false;
        return false;
    }

    if (ma_device_start(device) != MA_SUCCESS) {
        ma_device_uninit(device);
        delete device;
        device = nullptr;
        audioAvailable = false;
        return false;
    }

    audioAvailable = true;
    return true;
#endif
}

void AudioDevice::shutdownAudio()
{
#if POM1_IS_WASM
    emscripten_run_script(
        "if (window._pom1Audio) {"
        "  window._pom1Audio.proc.disconnect();"
        "  window._pom1Audio.ctx.close();"
        "  Module._free(window._pom1Audio.buf);"
        "  window._pom1Audio = null;"
        "}"
    );
    g_wasmAudioDevice = nullptr;
#else
    if (device != nullptr) {
        ma_device_uninit(device);
        delete device;
        device = nullptr;
    }
#endif
    audioAvailable = false;
}
