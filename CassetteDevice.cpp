#include "CassetteDevice.h"
#include "POM1Build.h"

// miniaudio is compiled via AudioDevice.cpp (MINIAUDIO_IMPLEMENTATION lives
// there). We only need the function prototypes for the decoder API; no
// implementation define here. On WASM the AudioDevice TU defines
// MA_NO_DEVICE_IO before the implementation include so decoders are still
// compiled in without the backend layer.
#include "third_party/miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

namespace {

constexpr char kAciMagic[] = "POM1ACI1";
constexpr uint32_t kMaxRealtimeGapCycles = 50000;
constexpr uint32_t kAudioRampInSamples = 64;

uint16_t readLe16(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8));
}

uint32_t readLe32(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void writeLe16(std::ofstream& file, uint16_t value)
{
    const uint8_t bytes[2] = {
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF)
    };
    file.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void writeLe32(std::ofstream& file, uint32_t value)
{
    const uint8_t bytes[4] = {
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 24) & 0xFF)
    };
    file.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

std::string lowerExtension(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

} // namespace

CassetteDevice::CassetteDevice()
{
    reset();
}

void CassetteDevice::fillAudioBuffer(float* output, int frameCount)
{
    // Direct audio streaming path — used when the tape was loaded with the
    // ACI card unplugged. miniaudio decodes + resamples the file on demand
    // at the device's output rate; we just pull frames and apply a brief
    // ramp-in so Play doesn't click.
    if (audioStreamMode) {
        std::lock_guard<std::mutex> lock(audioStreamMutex);
        if (!audioStreamDecoderOpen || !playbackActive ||
            playbackPaused.load(std::memory_order_acquire)) {
            std::fill_n(output, frameCount, 0.0f);
            return;
        }
        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(&audioStreamDecoder,
                                   output,
                                   static_cast<ma_uint64>(frameCount),
                                   &framesRead);
        audioStreamCursor += framesRead;

        // Headroom so the file doesn't clip when mixed with SID / live
        // cassette output. -3 dB is enough for typical speech/music.
        constexpr float kStreamGain = 0.71f;
        const float vol = volume.load(std::memory_order_relaxed);
        const int consumed = static_cast<int>(framesRead);
        for (int i = 0; i < consumed; ++i) {
            output[i] *= kStreamGain * vol;
            if (audioRampInSamplesRemaining > 0) {
                const float ramp = 1.0f - (static_cast<float>(audioRampInSamplesRemaining) /
                                           static_cast<float>(kAudioRampInSamples));
                output[i] *= ramp;
                audioRampInSamplesRemaining--;
            }
        }
        // EOF → halt playback and zero-fill the remainder.
        if (consumed < frameCount) {
            std::fill_n(output + consumed, frameCount - consumed, 0.0f);
            if (framesRead == 0) {
                playbackActive = false;
            }
        }
        // Mix the mode-transition clunk on top of the stream output too —
        // the click has to be heard when a NoTape → AudioStream transition
        // fires (the decoder hasn't started streaming samples yet, so the
        // raw output is all zeros without this).
        {
            std::lock_guard<std::mutex> lock(audioMutex);
            if (clickCursor < clickBuffer.size()) {
                const int mix = std::min<int>(frameCount,
                    static_cast<int>(clickBuffer.size() - clickCursor));
                for (int i = 0; i < mix; ++i) {
                    output[i] += clickBuffer[clickCursor++] * vol;
                }
            }
        }
        return;
    }

    static constexpr float kFilterAlpha = 0.33f;
    std::lock_guard<std::mutex> lock(audioMutex);
    if (playbackPaused.load(std::memory_order_acquire)) {
        std::fill_n(output, frameCount, 0.0f);
        audioPlaybackSample = 0.0f;
        return;
    }
    const float vol = volume.load(std::memory_order_relaxed);
    const double cpuCyclesPerSample =
        static_cast<double>(POM1_CPU_CLOCK_HZ) /
        static_cast<double>(std::max<uint32_t>(1, audioOutputSampleRate));
    for (int i = 0; i < frameCount; ++i) {
        // Playback (PROGRAM TAPE, speaker-driven): square wave walked from
        // loadedDurations at wallclock pace. Takes priority over the
        // recording-feedback queue — you can't both play a loaded tape and
        // record onto it simultaneously on a real deck either.
        float targetSample = 0.0f;
        if (speakerPlaybackActive) {
            targetSample = speakerLevel ? 0.22f : -0.22f;
            speakerCyclesRemaining -= cpuCyclesPerSample;
            while (speakerCyclesRemaining <= 0.0 && speakerPlaybackActive) {
                speakerLevel = !speakerLevel;
                ++speakerIndex;
                if (speakerIndex >= loadedDurations.size()) {
                    speakerPlaybackActive = false;
                    break;
                }
                speakerCyclesRemaining += static_cast<double>(loadedDurations[speakerIndex]);
            }
        } else if (!audioQueue.empty()) {
            // Recording-feedback path: CPU-side toggleOutput() pushes the
            // bytes the Apple-1 is writing to $C000. Gives the user live
            // modulation audio while dumping to tape.
            targetSample = audioQueue.front().sampleValue;
            if (audioQueue.front().remainingSamples > 0) {
                audioQueue.front().remainingSamples--;
            }
            if (audioQueue.front().remainingSamples == 0) {
                audioQueue.pop_front();
            }
        }
        if (audioRampInSamplesRemaining > 0) {
            const float ramp = 1.0f - (static_cast<float>(audioRampInSamplesRemaining) /
                                       static_cast<float>(kAudioRampInSamples));
            targetSample *= ramp;
            audioRampInSamplesRemaining--;
        }
        audioPlaybackSample += (targetSample - audioPlaybackSample) * kFilterAlpha;
        float s = audioPlaybackSample * vol;
        // Mechanical clunk — mixed on top of whatever else is playing so
        // the mode-transition feedback is audible with or without a tape.
        if (clickCursor < clickBuffer.size()) {
            s += clickBuffer[clickCursor++] * vol;
        }
        output[i] = s;
    }
}


double CassetteDevice::getQueuedAudioSeconds() const
{
    std::lock_guard<std::mutex> lock(audioMutex);
    uint64_t queuedSamples = 0;
    for (const auto& segment : audioQueue) {
        queuedSamples += segment.remainingSamples;
    }
    return static_cast<double>(queuedSamples) / static_cast<double>(audioOutputSampleRate);
}

void CassetteDevice::setHardwareAccurateLiveAudio(bool enabled)
{
    if (hardwareAccurateLiveAudio != enabled) {
        clearLiveAudioState();
    }
    hardwareAccurateLiveAudio = enabled;
}

void CassetteDevice::setLiveAudioTimebaseHz(uint32_t hz)
{
    liveAudioTimebaseHz = std::max<uint32_t>(1, hz);
}

void CassetteDevice::setVolume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 2.0f) v = 2.0f;
    volume.store(v, std::memory_order_relaxed);
}

void CassetteDevice::resetPlaybackState()
{
    playbackArmed = loadedTapeReady && !loadedDurations.empty();
    playbackActive = false;
    playbackIndex = 0;
    cyclesUntilInputToggle = 0;
    inputLevel = loadedInitialLevel;
    // Any in-flight REW is implicitly cancelled: the whole point of
    // resetPlaybackState is that the tape is back at index 0. Leaving
    // rewinding=true from a previous REW would cause advanceRewind to
    // clamp to "already at start" on its next call anyway, but clearing
    // here keeps the invariant observable from the outside.
    rewinding = false;
    rewCarryCycles = 0;
    // Reset the "last $C081 read" stamp to current cycle so the leader-
    // rewind check in readTapeInput doesn't fire on the very first poll
    // after a fresh load/rewind.
    lastTapeInputCycle = currentCycle;
}

void CassetteDevice::clearLiveAudioState()
{
    std::lock_guard<std::mutex> lock(audioMutex);
    audioSampleRemainder = 0.0;
    audioPlaybackSample = 0.0f;
    audioRampInSamplesRemaining = kAudioRampInSamples;
    audioQueue.clear();
}

void CassetteDevice::startSpeakerAtLeader()
{
    std::lock_guard<std::mutex> lock(audioMutex);
    if (loadedDurations.empty()) {
        speakerPlaybackActive = false;
        return;
    }
    speakerPlaybackActive = true;
    speakerIndex = 0;
    speakerLevel = loadedInitialLevel;
    speakerCyclesRemaining = static_cast<double>(loadedDurations[0]);
    audioPlaybackSample = 0.0f;
    audioRampInSamplesRemaining = kAudioRampInSamples;
    audioQueue.clear();
}

void CassetteDevice::stopSpeaker()
{
    std::lock_guard<std::mutex> lock(audioMutex);
    speakerPlaybackActive = false;
    speakerIndex = 0;
    speakerCyclesRemaining = 0.0;
    audioSampleRemainder = 0.0;
    audioPlaybackSample = 0.0f;
    audioRampInSamplesRemaining = kAudioRampInSamples;
    audioQueue.clear();
}

void CassetteDevice::playMechanicalClick()
{
    // ~70 ms damped thud + noise burst, synthesised into a buffer that
    // fillAudioBuffer mixes on top of the current deck output. Sits
    // above any speaker / recording audio so the user hears it even
    // while a tape is playing. Cheap enough (≈ 3 kB at 48 kHz) that we
    // can rebuild it every event without caching per sample rate.
    std::lock_guard<std::mutex> lock(audioMutex);
    const uint32_t rate = std::max<uint32_t>(1, audioOutputSampleRate);
    const uint32_t durSamples = rate / 14;  // ≈71 ms
    clickBuffer.assign(durSamples, 0.0f);
    uint32_t lcg = 0xC7E5A5B7u;
    constexpr float kTwoPi = 6.28318530718f;
    for (uint32_t i = 0; i < durSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rate);
        // Fast attack (<3 ms), then exponential decay.
        const float attack = std::min(1.0f, t * 400.0f);
        const float decay = std::exp(-t * 30.0f);
        lcg = lcg * 1664525u + 1013904223u;
        const float noise = (static_cast<float>(static_cast<int32_t>(lcg)) / 2147483648.0f);
        // ~95 Hz body resonance + mid-frequency "tick" + noise tail.
        const float thud = std::sin(kTwoPi * 95.0f * t);
        const float tick = std::sin(kTwoPi * 1300.0f * t) * std::exp(-t * 120.0f);
        clickBuffer[i] = (0.45f * thud + 0.30f * tick + 0.25f * noise) * attack * decay * 0.35f;
    }
    clickCursor = 0;
}

void CassetteDevice::fireClickIfModeChanged()
{
    const DeckMode m = getDeckMode();
    if (m == lastDeckMode) return;
    lastDeckMode = m;
    playMechanicalClick();
}

void CassetteDevice::reset()
{
    currentCycle = 0;
    outputLevel = false;
    recordedInitialLevel = false;
    lastOutputToggleCycle = 0;
    recordedDurations.clear();
    playbackPaused.store(false, std::memory_order_release);
    resetPlaybackState();
    stopSpeaker();
}

void CassetteDevice::setPlaybackPaused(bool paused)
{
    const bool prev = playbackPaused.exchange(paused, std::memory_order_acq_rel);
    if (prev == paused || paused) return;
    // Resume: reset ramp-in under the relevant mutex so the audio thread
    // observes a fresh fade-in and we don't click on un-pause.
    if (audioStreamMode) {
        std::lock_guard<std::mutex> lock(audioStreamMutex);
        audioRampInSamplesRemaining = kAudioRampInSamples;
    } else {
        std::lock_guard<std::mutex> lock(audioMutex);
        audioRampInSamplesRemaining = kAudioRampInSamples;
    }
}

void CassetteDevice::seekRelativeSeconds(double deltaSeconds)
{
    if (!audioStreamMode) return;
    std::lock_guard<std::mutex> lock(audioStreamMutex);
    if (!audioStreamDecoderOpen || audioOutputSampleRate == 0) return;

    const int64_t rate = static_cast<int64_t>(audioOutputSampleRate);
    int64_t newFrame = static_cast<int64_t>(audioStreamCursor) +
                       static_cast<int64_t>(std::llround(deltaSeconds * static_cast<double>(rate)));
    if (newFrame < 0) newFrame = 0;
    if (audioStreamTotalFrames > 0 &&
        newFrame >= static_cast<int64_t>(audioStreamTotalFrames)) {
        newFrame = static_cast<int64_t>(audioStreamTotalFrames) - 1;
    }
    if (ma_decoder_seek_to_pcm_frame(&audioStreamDecoder,
                                     static_cast<ma_uint64>(newFrame)) != MA_SUCCESS) {
        return;
    }
    audioStreamCursor = static_cast<uint64_t>(newFrame);
    audioRampInSamplesRemaining = kAudioRampInSamples;
}

double CassetteDevice::getPlaybackPositionSeconds() const
{
    if (!audioStreamMode) return 0.0;
    std::lock_guard<std::mutex> lock(audioStreamMutex);
    if (audioOutputSampleRate == 0) return 0.0;
    return static_cast<double>(audioStreamCursor) / static_cast<double>(audioOutputSampleRate);
}

double CassetteDevice::getPlaybackTotalSeconds() const
{
    if (!audioStreamMode) return 0.0;
    std::lock_guard<std::mutex> lock(audioStreamMutex);
    if (audioOutputSampleRate == 0) return 0.0;
    return static_cast<double>(audioStreamTotalFrames) / static_cast<double>(audioOutputSampleRate);
}

void CassetteDevice::queueAudioSegment(uint32_t cycles, bool level)
{
    if (!audioAvailable || cycles == 0) {
        return;
    }

    const uint32_t liveTimebaseHz = hardwareAccurateLiveAudio ? liveAudioTimebaseHz : kRealtimeAudioTimebaseHz;
    const double totalSamples = audioSampleRemainder +
        (static_cast<double>(cycles) * static_cast<double>(audioOutputSampleRate) / static_cast<double>(liveTimebaseHz));
    const uint32_t sampleCount = static_cast<uint32_t>(totalSamples);
    audioSampleRemainder = totalSamples - static_cast<double>(sampleCount);

    if (sampleCount == 0) {
        return;
    }

    const float sampleValue = level ? 0.22f : -0.22f;
    std::lock_guard<std::mutex> lock(audioMutex);
    if (!audioQueue.empty() && audioQueue.back().sampleValue == sampleValue) {
        audioQueue.back().remainingSamples += sampleCount;
    } else {
        audioQueue.push_back({sampleCount, sampleValue});
    }

    static constexpr size_t kMaxQueuedSegments = 8192;
    while (audioQueue.size() > kMaxQueuedSegments) {
        audioQueue.pop_front();
    }
}

void CassetteDevice::advancePlayback(uint32_t cycles)
{
    // Progressive rewind takes the whole slice budget while engaged —
    // the tape isn't playing, it's walking backward. advanceRewind does
    // its own pause check and its own EOF-at-start handling.
    if (rewinding) {
        advanceRewind(cycles);
        return;
    }
    // `cycles` is the slice budget in CPU cycles. `loadedDurations[i]` is
    // also a CPU-cycle count (kTapeFileTimebaseHz == POM1_CPU_CLOCK_HZ),
    // so we can subtract one from the other directly without any unit
    // conversion. Keep that invariant in mind when changing the tape file
    // format or the timebase constant.
    if (!playbackActive || loadedDurations.empty() || cycles == 0) {
        return;
    }
    if (playbackPaused.load(std::memory_order_acquire)) {
        return;
    }

    uint64_t remaining = cycles;
    while (remaining > 0 && playbackActive) {
        if (cyclesUntilInputToggle == 0) {
            if (playbackIndex >= loadedDurations.size()) {
                playbackActive = false;
                break;
            }
            cyclesUntilInputToggle = std::max<uint32_t>(1, loadedDurations[playbackIndex++]);
        }

        if (remaining < cyclesUntilInputToggle) {
            cyclesUntilInputToggle -= remaining;
            break;
        }

        remaining -= cyclesUntilInputToggle;
        // Audible speaker output used to be queued from here, which tied
        // playback pitch to the emulated CPU speed and silenced the deck
        // whenever the ACI ROM wasn't polling. The deck speaker is now
        // driven directly by fillAudioBuffer from `loadedDurations` at
        // wallclock pace (see startSpeakerAtLeader) — this loop only
        // needs to feed the ACI's `inputLevel`.
        cyclesUntilInputToggle = 0;
        inputLevel = !inputLevel;

        if (playbackIndex >= loadedDurations.size()) {
            playbackActive = false;
        }
    }
}

void CassetteDevice::advanceRewind(uint32_t cycles)
{
    // PAUSE during REW freezes the motor (matches a real deck's pause-
    // while-rewinding lockout).
    if (playbackPaused.load(std::memory_order_acquire)) {
        return;
    }
    if (!loadedTapeReady || loadedDurations.empty() || playbackIndex == 0) {
        // Nothing left to wind back — land clean.
        rewinding = false;
        rewCarryCycles = 0;
        resetPlaybackState();
        return;
    }

    // Spend the REW budget in units of forward-segment cycle-durations:
    // each whole segment consumed decrements playbackIndex by 1 and
    // flips inputLevel (forward playback toggles between segments; REW
    // is the temporal inverse). Partial-segment surplus carries into
    // the next slice via rewCarryCycles so we don't lose fractional
    // progress when slices don't align with segment boundaries.
    uint64_t budget = static_cast<uint64_t>(cycles) * kRewSpeedFactor + rewCarryCycles;
    while (playbackIndex > 0) {
        const uint32_t segDur = std::max<uint32_t>(1, loadedDurations[playbackIndex - 1]);
        if (budget < segDur) {
            rewCarryCycles = budget;
            return;
        }
        budget -= segDur;
        --playbackIndex;
        inputLevel = !inputLevel;
    }
    // Index reached 0 — REW complete. Rearm at the leader so the next
    // PLAY (or the next $C081 poll under B6, once that lands) starts
    // from the beginning. resetPlaybackState clears `rewinding` too.
    resetPlaybackState();
    clearLiveAudioState();
}

void CassetteDevice::advanceCycles(int cycles)
{
    if (cycles <= 0) {
        return;
    }

    advancePlayback(static_cast<uint32_t>(cycles));
    currentCycle += static_cast<uint32_t>(cycles);
}

void CassetteDevice::beginRecordingIfNeeded()
{
    if (lastOutputToggleCycle == 0 && recordedDurations.empty()) {
        clearLiveAudioState();
        recordedInitialLevel = outputLevel;
        lastOutputToggleCycle = currentCycle;
    }
}

CassetteDevice::quint8 CassetteDevice::toggleOutput()
{
    beginRecordingIfNeeded();

    if (currentCycle > lastOutputToggleCycle) {
        const uint64_t delta = currentCycle - lastOutputToggleCycle;
        if (!(recordedDurations.empty() && delta == 0)) {
            const uint32_t clampedDelta = static_cast<uint32_t>(std::max<uint64_t>(1, delta));
            recordedDurations.push_back(clampedDelta);

            // Very large gaps between ACI transitions represent silence or a new
            // session boundary in live playback. Keeping them as queued constant
            // level audio makes the GUI sound drift further behind each run.
            if (clampedDelta > kMaxRealtimeGapCycles) {
                clearLiveAudioState();
            } else {
                queueAudioSegment(clampedDelta, outputLevel);
            }
        }
    }

    lastOutputToggleCycle = currentCycle;
    outputLevel = !outputLevel;
    return outputLevel ? 0x80 : 0x00;
}

void CassetteDevice::armPlaybackAtStart()
{
    playbackIndex = 0;
    cyclesUntilInputToggle = 0;
    inputLevel = loadedInitialLevel;
    playbackActive = loadedTapeReady && !loadedDurations.empty();
    playbackArmed = false;
    clearLiveAudioState();
}

CassetteDevice::quint8 CassetteDevice::readTapeInput()
{
    // During progressive REW, the ACI sees a frozen tape input: the
    // signal reflects whatever `inputLevel` was at the moment REW was
    // last stepped (advanceRewind flips it on each consumed segment).
    // We don't update lastTapeInputCycle — the next post-REW read will
    // see a fresh stamp set by resetPlaybackState() when REW completes,
    // so the leader-rewind guard stays dormant across the REW window
    // regardless of how long REW took.
    if (rewinding) {
        return inputLevel ? 0x80 : 0x00;
    }
    // Leader-preservation: if nothing has polled $C081 for a while, the
    // user was almost certainly typing Wozmon commands (Wozmon reads
    // $D010/$D011, never touches the cassette input) while the tape was
    // freewheeling through pulses or even running all the way to the end.
    // Rewind + reactivate so the ACI READ routine — which just started
    // polling us — sees the leader from the start and can synchronize.
    // Without this:
    //   * At 1× CPU speed, ~10 s of typing eats through the leader.
    //   * At MAX speed, a 30 s tape finishes in <1 s of wallclock typing;
    //     playbackActive goes false and the tape would be stuck at EOF.
    // 500 ms gap is well above any inter-poll interval of the READ
    // routine (which polls at microsecond scale).
    constexpr uint64_t kLeaderRewindGapCycles = POM1_CPU_CLOCK_HZ / 2;  // 500 ms
    const bool leaderRewind =
        loadedTapeReady && !loadedDurations.empty() && playbackIndex > 0 &&
        (currentCycle - lastTapeInputCycle) > kLeaderRewindGapCycles;
    if (leaderRewind || playbackArmed) {
        armPlaybackAtStart();
    }
    lastTapeInputCycle = currentCycle;
    return inputLevel ? 0x80 : 0x00;
}

void CassetteDevice::rewindTape()
{
    if (audioStreamMode) {
        std::lock_guard<std::mutex> lock(audioStreamMutex);
        if (audioStreamDecoderOpen) {
            ma_decoder_seek_to_pcm_frame(&audioStreamDecoder, 0);
        }
        audioStreamCursor = 0;
        playbackActive = false;
        clearLiveAudioState();
        return;
    }
    // Pulse mode: simulate a physical deck rewinding. If the tape is
    // already at the leader (or there's nothing to wind back), snap to
    // a clean armed-at-start state — no reason to animate zero travel.
    // Otherwise enter the "rewinding" state and let advanceRewind walk
    // playbackIndex back over emulated time at kRewSpeedFactor× play
    // speed. The audio queue stays silent for the duration (head-lift
    // analogy); isRewinding() lets the UI paint a REW-in-progress state.
    if (!loadedTapeReady || loadedDurations.empty() || playbackIndex == 0) {
        resetPlaybackState();
        stopSpeaker();
        return;
    }
    rewinding = true;
    rewCarryCycles = 0;
    playbackActive = false;
    playbackArmed  = false;
    stopSpeaker();
}

void CassetteDevice::playTape()
{
    if (!loadedTapeReady) return;
    playbackPaused.store(false, std::memory_order_release);
    if (audioStreamMode) {
        std::lock_guard<std::mutex> lock(audioStreamMutex);
        if (!audioStreamDecoderOpen) return;
        // Resume from the current cursor — REW is what rewinds to 0.
        playbackActive = true;
        playbackArmed = false;
        audioRampInSamplesRemaining = kAudioRampInSamples;
        return;
    }
    if (loadedDurations.empty()) return;
    // B6 — play-on-first-read: pulse-mode PLAY arms the deck but does
    // NOT start consuming durations (CPU-side). The tape only begins
    // advancing toward the ACI when the ROM polls $C081 for the first
    // time (readTapeInput's armed→active transition). Without this,
    // advancePlayback burned through the full tape on every CPU slice
    // regardless of whether the ROM was actually reading — a 30 s tape
    // disappeared in <1 s at --cpu-max while the user was still typing
    // "C100R" at Wozmon. Leader-rewind papered over the symptom inside
    // a 500 ms window; this removes the coupling entirely.
    //
    // Stopped-deck rewind (B3): ALWAYS seek back to the leader on PLAY.
    // No mid-tape resume — the ACI READ routine always wants to see the
    // sync leader first. resetPlaybackState arms, zeroes the index,
    // stamps lastTapeInputCycle, and clears the REW flags.
    //
    // Speaker path is a separate concern: we start the audible square
    // wave immediately on PLAY, at wallclock pace, so the deck sounds
    // like a real cassette player the moment the user hits PLAY — and
    // stays at real speed regardless of CPU slider position. The ACI
    // arm-and-wait-for-poll behaviour above is untouched.
    resetPlaybackState();
    startSpeakerAtLeader();
}

void CassetteDevice::stopTape()
{
    playbackActive = false;
    playbackArmed = false;
    // STOP cancels an in-flight REW — the user pressed STOP, the motor
    // halts wherever the index currently is. They'll have to press REW
    // again to resume winding back.
    rewinding = false;
    rewCarryCycles = 0;
    cyclesUntilInputToggle = 0;
    stopSpeaker();
}

void CassetteDevice::ejectTape()
{
    closeAudioStream();
    // Halt the audio-thread speaker cursor BEFORE mutating
    // loadedDurations — stopSpeaker takes audioMutex, so any in-flight
    // fillAudioBuffer call completes before we touch the vector and no
    // subsequent call can race us (speakerPlaybackActive is false).
    stopSpeaker();
    audioStreamMode = false;
    loadedDurations.clear();
    loadedTapePath.clear();
    loadedTapeReady = false;
    loadedInitialLevel = false;
    resetPlaybackState();
    fireClickIfModeChanged();
}

void CassetteDevice::setAciActive(bool active)
{
    const bool wasActive = aciActive;
    aciActive = active;
    if (wasActive == active) return;
    // Plugging the ACI while a stream-mode tape is loaded would leave the
    // ACI ROM polling $C081 forever (the stream path has no pulse
    // transitions, so inputLevel stays flat). Eject the tape so the ROM
    // sees an empty deck and the user can load a proper program tape.
    if (!wasActive && active && audioStreamMode) {
        ejectTape();
        return;  // ejectTape already fired the mode-change clunk
    }
    // ACI toggled while a tape is in — mode didn't necessarily change
    // (pulse stays pulse on unplug; stream stays stream on plug-out), but
    // the physical act of plugging/unplugging the card is exactly the
    // kind of deck event the user asked for feedback on.
    if (loadedTapeReady) {
        playMechanicalClick();
    }
}

void CassetteDevice::clearRecordedTape()
{
    recordedDurations.clear();
    recordedInitialLevel = outputLevel;
    lastOutputToggleCycle = 0;
    clearLiveAudioState();
}

bool CassetteDevice::loadPlaybackDurations(std::vector<uint32_t> durations, bool initialLevel, const std::string& path)
{
    if (durations.empty()) {
        lastError = "Tape file does not contain any signal transitions";
        return false;
    }

    // Halt speaker + drop its cursor BEFORE replacing the durations
    // vector — stopSpeaker takes audioMutex so any in-flight audio
    // callback completes first, and speakerPlaybackActive=false keeps
    // subsequent callbacks from touching loadedDurations while we
    // reassign.
    stopSpeaker();
    loadedDurations = std::move(durations);
    loadedInitialLevel = initialLevel;
    loadedTapePath = path;
    loadedTapeReady = true;
    audioStreamMode = false;
    resetPlaybackState();
    fireClickIfModeChanged();
    lastError.clear();
    return true;
}

bool CassetteDevice::loadTape(const std::string& path)
{
    const std::string ext = lowerExtension(path);

    // .aci is always pulse data — there is no audio stream to decode.
    if (ext == ".aci") {
        closeAudioStream();
        audioStreamMode = false;
        return loadAciTape(path);
    }

    // ACI card plugged: user is loading a program tape. Use the pulse
    // path so the CPU can read bits from $C081. 30-minute cap applies
    // (pulse extraction allocates per-transition state).
    if (aciActive) {
        closeAudioStream();
        audioStreamMode = false;
        if (ext == ".wav") return loadWavTape(path);
        return loadMiniaudioTape(path);
    }

    // ACI unplugged: audio formats become raw playback through the deck
    // speaker (AUDIO STREAM mode). The zombie-state bug that motivated
    // the former refusal is prevented at the setACIEnabled() boundary —
    // toggling the ACI back on evicts any loaded tape — so entering
    // audio-stream mode here is safe.
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac") {
        return loadAudioStream(path);
    }

    lastError = "Unsupported tape extension (expected .aci/.wav/.ogg/.mp3/.flac).";
    return false;
}

bool CassetteDevice::saveTape(const std::string& path) const
{
    const std::string ext = lowerExtension(path);
    if (ext == ".wav") {
        return saveWavTape(path);
    }
    return saveAciTape(path);
}

bool CassetteDevice::loadAciTape(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        lastError = "Cannot open tape file: " + path;
        return false;
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() < 16 || std::memcmp(bytes.data(), kAciMagic, 8) != 0) {
        lastError = "Invalid .aci tape file";
        return false;
    }

    const uint8_t version = bytes[8];
    if (version != 1) {
        lastError = "Unsupported .aci tape version";
        return false;
    }

    const bool initialLevel = bytes[9] != 0;
    const uint32_t count = readLe32(bytes.data() + 12);
    if (bytes.size() < 16ull + static_cast<uint64_t>(count) * 4ull) {
        lastError = "Truncated .aci tape file";
        return false;
    }

    std::vector<uint32_t> durations;
    durations.reserve(count);
    size_t offset = 16;
    for (uint32_t i = 0; i < count; ++i) {
        durations.push_back(std::max<uint32_t>(1, readLe32(bytes.data() + offset)));
        offset += 4;
    }

    return loadPlaybackDurations(std::move(durations), initialLevel, path);
}

bool CassetteDevice::saveAciTape(const std::string& path) const
{
    if (recordedDurations.empty()) {
        lastError = "No cassette output has been recorded yet";
        return false;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        lastError = "Cannot write tape file: " + path;
        return false;
    }

    file.write(kAciMagic, 8);
    file.put(1);
    file.put(recordedInitialLevel ? 1 : 0);
    file.put(0);
    file.put(0);
    writeLe32(file, static_cast<uint32_t>(recordedDurations.size()));
    for (uint32_t duration : recordedDurations) {
        writeLe32(file, duration);
    }

    lastError.clear();
    return true;
}

bool CassetteDevice::loadWavTape(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        lastError = "Cannot open tape file: " + path;
        return false;
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        lastError = "Invalid WAV file";
        return false;
    }

    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    const uint8_t* dataChunk = nullptr;
    uint32_t dataSize = 0;

    size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const uint8_t* chunk = bytes.data() + offset;
        const uint32_t chunkSize = readLe32(chunk + 4);
        offset += 8;
        if (offset + chunkSize > bytes.size()) {
            break;
        }

        if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            audioFormat = readLe16(bytes.data() + offset + 0);
            channels = readLe16(bytes.data() + offset + 2);
            sampleRate = readLe32(bytes.data() + offset + 4);
            bitsPerSample = readLe16(bytes.data() + offset + 14);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            dataChunk = bytes.data() + offset;
            dataSize = chunkSize;
        }

        offset += chunkSize + (chunkSize & 1u);
    }

    if (dataChunk == nullptr || channels == 0 || sampleRate == 0) {
        lastError = "WAV file is missing format or data chunks";
        return false;
    }
    if (audioFormat != 1 && audioFormat != 3) {
        lastError = "Unsupported WAV format (only PCM and float are supported)";
        return false;
    }

    const size_t bytesPerSample = bitsPerSample / 8;
    if (bytesPerSample == 0 || dataSize < bytesPerSample * channels) {
        lastError = "Unsupported WAV sample format";
        return false;
    }

    const size_t frameCount = dataSize / (bytesPerSample * channels);
    std::vector<float> samples;
    samples.reserve(frameCount);

    for (size_t frame = 0; frame < frameCount; ++frame) {
        float mixed = 0.0f;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            const uint8_t* samplePtr = dataChunk + (frame * channels + ch) * bytesPerSample;
            float value = 0.0f;
            if (audioFormat == 1 && bitsPerSample == 8) {
                value = (static_cast<int>(samplePtr[0]) - 128) / 128.0f;
            } else if (audioFormat == 1 && bitsPerSample == 16) {
                const int16_t s = static_cast<int16_t>(readLe16(samplePtr));
                value = static_cast<float>(s) / 32768.0f;
            } else if (audioFormat == 3 && bitsPerSample == 32) {
                float f = 0.0f;
                std::memcpy(&f, samplePtr, sizeof(float));
                value = f;
            } else {
                lastError = "Only WAV PCM 8/16-bit and float32 are supported";
                return false;
            }
            mixed += value;
        }
        samples.push_back(mixed / static_cast<float>(channels));
    }

    std::vector<uint32_t> durations;
    bool initialLevel = false;
    if (!pcmToDurations(samples, sampleRate, durations, initialLevel, lastError)) {
        return false;
    }
    return loadPlaybackDurations(std::move(durations), initialLevel, path);
}

bool CassetteDevice::pcmToDurations(const std::vector<float>& mono,
                                    uint32_t sampleRate,
                                    std::vector<uint32_t>& outDurations,
                                    bool& outInitialLevel,
                                    std::string& outErr)
{
    // Output is in CPU-cycle units (kTapeFileTimebaseHz == POM1_CPU_CLOCK_HZ).
    // advancePlayback() consumes the resulting durations directly as CPU
    // cycles, and saveWavTape() rebuilds the WAV at kWavFileSampleRate
    // using the same constant — both paths stay symmetric.
    outDurations.clear();
    if (mono.empty()) {
        outErr = "Audio file does not contain samples";
        return false;
    }
    if (sampleRate == 0) {
        outErr = "Audio file has an invalid sample rate";
        return false;
    }

    static constexpr float kThreshold = 0.02f;
    size_t firstActive = 0;
    while (firstActive < mono.size() && std::fabs(mono[firstActive]) < kThreshold) {
        ++firstActive;
    }
    if (firstActive == mono.size()) {
        outErr = "Audio file does not contain a detectable cassette signal";
        return false;
    }

    outInitialLevel = mono[firstActive] >= 0.0f;
    bool currentLevel = outInitialLevel;
    size_t lastTransition = firstActive;

    for (size_t i = firstActive + 1; i < mono.size(); ++i) {
        bool newLevel = currentLevel;
        if (mono[i] >= kThreshold) {
            newLevel = true;
        } else if (mono[i] <= -kThreshold) {
            newLevel = false;
        }

        if (newLevel != currentLevel) {
            const size_t deltaSamples = i - lastTransition;
            const uint32_t cycles = std::max<uint32_t>(1, static_cast<uint32_t>(
                std::llround(static_cast<double>(deltaSamples) * static_cast<double>(kTapeFileTimebaseHz) /
                             static_cast<double>(sampleRate))));
            outDurations.push_back(cycles);
            currentLevel = newLevel;
            lastTransition = i;
        }
    }
    return true;
}

bool CassetteDevice::loadAudioStream(const std::string& path)
{
    closeAudioStream();
    loadedDurations.clear();
    std::lock_guard<std::mutex> lock(audioStreamMutex);

    // Decode to mono float32 at the device's output sample rate so the
    // audio callback can push samples straight to the mixer. miniaudio
    // resamples internally when the source rate differs.
    ma_decoder_config cfg = ma_decoder_config_init(
        ma_format_f32, 1, audioOutputSampleRate);
    if (ma_decoder_init_file(path.c_str(), &cfg, &audioStreamDecoder) != MA_SUCCESS) {
        lastError = "Cannot decode audio: " + path;
        return false;
    }
    audioStreamDecoderOpen = true;
    audioStreamCursor = 0;
    ma_uint64 total = 0;
    if (ma_decoder_get_length_in_pcm_frames(&audioStreamDecoder, &total) != MA_SUCCESS) {
        total = 0; // some formats (Ogg streams) can't report a length
    }
    audioStreamTotalFrames = total;
    audioStreamMode   = true;
    loadedTapePath    = path;
    loadedTapeReady   = true;
    playbackArmed     = false;
    playbackActive    = false;
    loadedInitialLevel = false;
    // Entering stream mode — if a pulse tape was previously loaded and its
    // speaker cursor is still active, stop it before the next callback.
    stopSpeaker();
    fireClickIfModeChanged();
    lastError.clear();
    return true;
}

void CassetteDevice::closeAudioStream()
{
    std::lock_guard<std::mutex> lock(audioStreamMutex);
    if (audioStreamDecoderOpen) {
        ma_decoder_uninit(&audioStreamDecoder);
        audioStreamDecoderOpen = false;
    }
    audioStreamCursor = 0;
    audioStreamTotalFrames = 0;
}

bool CassetteDevice::loadMiniaudioTape(const std::string& path)
{
    // 30-minute cap — real Apple-1 tapes are minutes long; anything
    // longer is almost certainly a wrong file. Prevents accidental
    // 2-hour podcast loads from chewing memory.
    static constexpr uint64_t kMaxFrames = 30ull * 60ull * 96000ull;

    // Decode straight to mono float32 so pcmToDurations gets the exact
    // format it expects. sampleRate=0 in the config means "keep source rate".
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 0);
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &cfg, &decoder) != MA_SUCCESS) {
        lastError = "Cannot decode audio file: " + path;
        return false;
    }

    const uint32_t sampleRate = decoder.outputSampleRate;
    if (sampleRate == 0) {
        ma_decoder_uninit(&decoder);
        lastError = "Decoded audio reports an invalid sample rate";
        return false;
    }

    // Ogg Vorbis often reports 0 total frames; we stream in chunks
    // instead of pre-allocating. 4096 frames ≈ 93 ms at 44.1 kHz.
    std::vector<float> samples;
    constexpr size_t kChunkFrames = 4096;
    float chunk[kChunkFrames];
    uint64_t totalFrames = 0;
    while (totalFrames < kMaxFrames) {
        ma_uint64 framesRead = 0;
        const ma_result r = ma_decoder_read_pcm_frames(&decoder, chunk, kChunkFrames, &framesRead);
        if (framesRead == 0) break;
        samples.insert(samples.end(), chunk, chunk + framesRead);
        totalFrames += framesRead;
        if (r != MA_SUCCESS) break;  // EOF or error — we consumed what we could
    }
    ma_decoder_uninit(&decoder);

    if (totalFrames >= kMaxFrames) {
        lastError = "Audio file exceeds 30-minute tape limit";
        return false;
    }

    std::vector<uint32_t> durations;
    bool initialLevel = false;
    if (!pcmToDurations(samples, sampleRate, durations, initialLevel, lastError)) {
        return false;
    }
    return loadPlaybackDurations(std::move(durations), initialLevel, path);
}

bool CassetteDevice::saveWavTape(const std::string& path) const
{
    if (recordedDurations.empty()) {
        lastError = "No cassette output has been recorded yet";
        return false;
    }

    std::vector<int16_t> pcm;
    bool level = recordedInitialLevel;
    for (uint32_t duration : recordedDurations) {
        const uint32_t sampleCount = std::max<uint32_t>(1, static_cast<uint32_t>(
            std::llround(static_cast<double>(duration) * static_cast<double>(kWavFileSampleRate) /
                         static_cast<double>(kTapeFileTimebaseHz))));
        const int16_t sample = level ? 14000 : -14000;
        pcm.insert(pcm.end(), sampleCount, sample);
        level = !level;
    }

    pcm.insert(pcm.end(), kWavFileSampleRate / 10, level ? 14000 : -14000);

    const size_t dataSizeFull = pcm.size() * sizeof(int16_t);
    if (dataSizeFull > UINT32_MAX - 36) {
        lastError = "Recording too large to save as WAV";
        return false;
    }
    const uint32_t dataSize = static_cast<uint32_t>(dataSizeFull);
    const uint32_t riffSize = 36 + dataSize;

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        lastError = "Cannot write tape file: " + path;
        return false;
    }

    file.write("RIFF", 4);
    writeLe32(file, riffSize);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    writeLe32(file, 16);
    writeLe16(file, 1);
    writeLe16(file, 1);
    writeLe32(file, kWavFileSampleRate);
    writeLe32(file, kWavFileSampleRate * sizeof(int16_t));
    writeLe16(file, sizeof(int16_t));
    writeLe16(file, 16);
    file.write("data", 4);
    writeLe32(file, dataSize);
    file.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(dataSize));

    lastError.clear();
    return true;
}
