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
#include "Logger.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#if POM1_IS_WASM
#include <emscripten.h>
// WebAudio handles device I/O via JS; we still want miniaudio's decoder
// API (ma_decoder / dr_mp3 / stb_vorbis / dr_flac) compiled in so
// CassetteDevice can accept .mp3/.ogg/.flac tapes in the browser build
// too — MA_NO_DEVICE_IO strips the backend layer while keeping the
// format decoders.
#define MA_NO_DEVICE_IO
#endif
// Ogg Vorbis via stb_vorbis. miniaudio's Vorbis backend activates when
// STB_VORBIS_INCLUDE_STB_VORBIS_H is defined — including the .c file
// before miniaudio.h defines that guard and drops the decoder into this
// TU. stb_vorbis raises benign signed/unsigned comparison warnings
// under -Wall; silence them locally so the rest of the project keeps
// its warning profile intact.
// MSVC needs the same treatment and did not have it: stb_vorbis alone accounts
// for 102 of the 134 distinct warning sites the Windows job reports, all of them
// charged to THIS translation unit because the .c is included here — the same
// reason GCC needs the pragmas above. `warning(push, 0)` drops to /W0 for the
// include and restores the file's level after it, which is what makes
// -DPOM1_WERROR=ON survivable on Windows without turning individual codes off
// project-wide.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wextra"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#include "third_party/stb_vorbis.c"
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if defined(_WIN32)
// miniaudio includes <windows.h> on Windows, which defines min/max macros unless
// NOMINMAX is set. Those macros break std::min/std::max calls in this TU and in
// any test target that compiles it.
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#endif

// GCC's -Wstringop-overflow mis-analyses miniaudio's 64-bit atomic loads
// (ma_atomic_load_64 → __atomic_load_n) as writing 8 bytes "into a region of
// size 0 … at address zero" — a false positive in the vendored implementation.
// Silence it locally so the project keeps its warning profile intact.
#define MINIAUDIO_IMPLEMENTATION
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wextra"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
#include "third_party/miniaudio.h"
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if defined(_WIN32)
#  ifdef min
#    undef min
#  endif
#  ifdef max
#    undef max
#  endif
#endif

// ─── Mixing ─────────────────────────────────────────────────────────────────

void AudioDevice::mixSources(float* output, int frameCount)
{
#if POM1_REALTIME_DIAGNOSTICS
    const auto callbackStart = std::chrono::steady_clock::now();
    callbackCount.fetch_add(1, std::memory_order_relaxed);
#endif
    std::memset(output, 0, static_cast<size_t>(frameCount) * sizeof(float));

    const int chunkMax = static_cast<int>(tmpBuf.size());
    if (chunkMax <= 0) return;   // constructor always sizes it; defensive only

    // Pin a stable immutable source list without taking a lock. Recheck the
    // published index after incrementing: if a producer swapped between the
    // first load and the pin, release and retry so it can safely recycle that
    // buffer. removeSource() waits for the retired buffer's count to reach zero
    // before returning, which is the lifetime fence for source destruction.
    unsigned sourceIndex;
    for (;;) {
        sourceIndex = activeSourceSnapshot.load(std::memory_order_acquire);
        sourceReaders[sourceIndex].fetch_add(1, std::memory_order_acquire);
        if (activeSourceSnapshot.load(std::memory_order_acquire) == sourceIndex)
            break;
        sourceReaders[sourceIndex].fetch_sub(1, std::memory_order_release);
    }
    const SourceSnapshot& sources = sourceSnapshots[sourceIndex];

    // Walk the request in scratch-sized chunks instead of resizing tmpBuf to
    // fit: this runs on the audio thread and must not allocate. Sources are
    // stateful streams, so asking one for N frames then M more is identical to
    // asking for N+M — chunking is invisible to them.
    for (int offset = 0; offset < frameCount; offset += chunkMax) {
        const int n = std::min(chunkMax, frameCount - offset);
        for (std::size_t source = 0; source < sources.count; ++source) {
            AudioSource* src = sources.entries[source];
            src->fillAudioBuffer(tmpBuf.data(), n);
            for (int i = 0; i < n; ++i)
                output[offset + i] += tmpBuf[i];
        }
    }

    for (int i = 0; i < frameCount; ++i)
        output[i] = std::max(-1.0f, std::min(1.0f, output[i]));

    sourceReaders[sourceIndex].fetch_sub(1, std::memory_order_release);
#if POM1_REALTIME_DIAGNOSTICS
    pom1::updateAtomicMaximum(maxCallbackNs, static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - callbackStart).count()));
#endif
}

void AudioDevice::copyRealtimeDiagnostics(pom1::RealtimeDiagnostics& out) const
{
#if POM1_REALTIME_DIAGNOSTICS
    out.audioCallbacks = callbackCount.load(std::memory_order_relaxed);
    out.maxAudioCallbackNs = maxCallbackNs.load(std::memory_order_relaxed);
#else
    (void)out;
#endif
}

// ─── Source management ──────────────────────────────────────────────────────

void AudioDevice::addSource(AudioSource* source)
{
    if (!source) return;
    std::lock_guard<std::mutex> lock(sourcePublishMutex);
    const unsigned oldIndex = activeSourceSnapshot.load(std::memory_order_acquire);
    const unsigned nextIndex = 1u - oldIndex;
    while (sourceReaders[nextIndex].load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    sourceSnapshots[nextIndex] = sourceSnapshots[oldIndex];
    SourceSnapshot& next = sourceSnapshots[nextIndex];
    if (std::find(next.entries.begin(), next.entries.begin() + next.count, source) !=
        next.entries.begin() + next.count)
        return;
    if (next.count >= kMaxSources) {
        pom1::log().error("Audio", "source registry capacity exceeded");
        return;
    }
    next.entries[next.count++] = source;
    activeSourceSnapshot.store(nextIndex, std::memory_order_release);
}

void AudioDevice::removeSource(AudioSource* source)
{
    std::lock_guard<std::mutex> lock(sourcePublishMutex);
    const unsigned oldIndex = activeSourceSnapshot.load(std::memory_order_acquire);
    const unsigned nextIndex = 1u - oldIndex;
    while (sourceReaders[nextIndex].load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    sourceSnapshots[nextIndex] = sourceSnapshots[oldIndex];
    SourceSnapshot& next = sourceSnapshots[nextIndex];
    const auto end = next.entries.begin() + next.count;
    const auto found = std::find(next.entries.begin(), end, source);
    if (found == end) return;
    std::move(found + 1, end, found);
    next.entries[--next.count] = nullptr;
    activeSourceSnapshot.store(nextIndex, std::memory_order_release);
    // A caller may destroy `source` as soon as removeSource returns. Wait until
    // every callback pinned to the retired list has stopped dereferencing it.
    while (sourceReaders[oldIndex].load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
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

AudioDevice::AudioDevice(bool initializeHardware, int preferredLatencyMs)
{
    if (preferredLatencyMs > 0) {
        if (preferredLatencyMs < 20)  preferredLatencyMs = 20;
        if (preferredLatencyMs > 250) preferredLatencyMs = 250;
        preferredLatencyMs_ = preferredLatencyMs;
    }
    // Size the mixing scratch before the device exists, so the very first
    // callback finds it ready. See kMixScratchFrames in the header.
    tmpBuf.resize(static_cast<size_t>(kMixScratchFrames));
    if (initializeHardware) initAudio();
}

AudioDevice::~AudioDevice()
{
    shutdownAudio();
}

bool AudioDevice::initAudio()
{
#if POM1_IS_WASM
    g_wasmAudioDevice = this;

    // Build the AudioContext + ScriptProcessorNode. The JS sets
    // window._pom1Audio to the handle on success, or to null on failure
    // (no AudioContext, allocator failure, etc.). We read the outcome back
    // via emscripten_run_script_int below — that returns ctx.sampleRate
    // (may differ from 44100: Firefox/Safari often force the device rate,
    // and the SID must be configured against the *actual* consumer rate or
    // the music tempo drifts).
    emscripten_run_script(
        "try {"
        "  var AC = window.AudioContext || window.webkitAudioContext;"
        "  if (!AC) { window._pom1Audio = null; }"
        "  else {"
        "    var ctx = new AC({sampleRate: 44100});"
        "    var bufSize = 2048;"
        "    var proc = ctx.createScriptProcessor(bufSize, 0, 1);"
        "    var heapBuf = Module._malloc(bufSize * 4);"
        "    if (!heapBuf) { ctx.close(); window._pom1Audio = null; }"
        "    else {"
        "      proc.onaudioprocess = function(e) {"
        "        Module._pom1_fillAudioBuffer(heapBuf, bufSize);"
        "        var out = e.outputBuffer.getChannelData(0);"
        "        out.set(Module.HEAPF32.subarray(heapBuf >> 2, (heapBuf >> 2) + bufSize));"
        "      };"
        "      proc.connect(ctx.destination);"
        "      window._pom1Audio = {ctx: ctx, proc: proc, buf: heapBuf};"
        "      var resume = function() { if (ctx.state === 'suspended') ctx.resume(); };"
        "      document.addEventListener('click', resume, {once: true});"
        "      document.addEventListener('keydown', resume, {once: true});"
        "    }"
        "  }"
        "} catch (ex) { window._pom1Audio = null; }"
    );

    const int actualRate = emscripten_run_script_int(
        "(window._pom1Audio && window._pom1Audio.ctx) ? (window._pom1Audio.ctx.sampleRate | 0) : -1"
    );

    if (actualRate <= 0) {
        g_wasmAudioDevice = nullptr;
        audioAvailable = false;
        return false;
    }
    actualSampleRate = static_cast<uint32_t>(actualRate);
    pom1::log().info("Audio",
        std::string("WebAudio context: requested ") + std::to_string(kSampleRate) +
        " Hz, got " + std::to_string(actualSampleRate) + " Hz" +
        (actualSampleRate == kSampleRate ? "" : " (browser-negotiated rate — sources will use the actual rate)"));
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
    if (preferredLatencyMs_ > 0) {
        // Keep 3 periods and stretch each one: miniaudio wakes the callback
        // once per period, so fewer/larger wake-ups is exactly what a loaded
        // Pi needs. The total cushion is periodSizeInFrames × periods.
        uint32_t frames = (kSampleRate * static_cast<uint32_t>(preferredLatencyMs_))
                          / (1000u * config.periods);
        if (frames < 64) frames = 64;
        config.periodSizeInFrames = frames;
        pom1::log().info("Audio",
            "output cushion " + std::to_string(preferredLatencyMs_) + " ms (" +
            std::to_string(frames) + " frames x " + std::to_string(config.periods) +
            " periods)");
    }
    config.dataCallback = &AudioDevice::audioDataCallback;
    config.pUserData = this;

    // ma_device_init / _start / _uninit are paired APIs, so we manage the raw
    // pointer manually during init and only hand it to unique_ptr once the
    // device is fully live. That keeps the deleter's invariant intact
    // (MaDeviceDeleter always calls ma_device_uninit + delete).
    ma_device* raw = new ma_device();
    if (ma_device_init(nullptr, &config, raw) != MA_SUCCESS) {
        delete raw;
        audioAvailable = false;
        return false;
    }
    if (ma_device_start(raw) != MA_SUCCESS) {
        ma_device_uninit(raw);
        delete raw;
        audioAvailable = false;
        return false;
    }

    // Capture the sample rate that miniaudio actually negotiated with the
    // OS device. When it differs from kSampleRate (e.g. macOS Apple
    // Silicon often ends up at 48 kHz), cycle-accurate sources like SID
    // must use this value to avoid tempo drift.
    actualSampleRate = raw->sampleRate;
    pom1::log().info("Audio",
        std::string("miniaudio device: requested ") + std::to_string(kSampleRate) +
        " Hz, got " + std::to_string(actualSampleRate) + " Hz" +
        (actualSampleRate == kSampleRate ? "" : " (rate mismatch — sources will use the actual rate)"));

    device.reset(raw);
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
    // device.reset() -> MaDeviceDeleter -> ma_device_uninit synchronously
    // drains the callback, so by the time we clear `sources` no audio
    // thread is racing against us.
    device.reset();
#endif
    audioAvailable = false;
    // Drop all registered AudioSource pointers. The caller (or a later
    // initAudio()) is responsible for re-adding them; leaving stale raw
    // pointers here would cause UAF if sources have been destroyed in the
    // meantime and initAudio() is later called again.
    std::lock_guard<std::mutex> lock(sourcePublishMutex);
    sourceSnapshots[0] = {};
    sourceSnapshots[1] = {};
    activeSourceSnapshot.store(0, std::memory_order_release);
}

#if !POM1_IS_WASM
void AudioDevice::MaDeviceDeleter::operator()(ma_device* d) const noexcept
{
    if (!d) return;
    ma_device_uninit(d);
    delete d;
}
#endif
