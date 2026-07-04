#include "CassetteDevice.h"
#include "POM1Build.h"
#include "SnapshotIO.h"

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

std::string CassetteDevice::lookupTapeInfo(const std::string& path)
{
    namespace fs = std::filesystem;
    const fs::path tapePath(path);
    const fs::path dir = tapePath.parent_path();
    if (dir.empty()) return {};

    const fs::path infoFile = dir / "tapeinfo.txt";
    std::ifstream file(infoFile);
    if (!file.is_open()) return {};

    const std::string baseName = tapePath.filename().string();
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and blank lines.
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        // Trim key and value.
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!val.empty() && val.front() == ' ') val.erase(val.begin());
        if (key == baseName) return val;
    }
    return {};
}

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
    for (int i = 0; i < frameCount; ++i) {
        // Pulse-mode audio: CPU-side advancePlayback() and toggleOutput()
        // push segments into audioQueue at the emulated CPU's pace, so
        // the deck speaker stays locked to the 6502 cycle count.  This
        // covers both program-tape playback and recording feedback.
        float targetSample = 0.0f;
        if (!audioQueue.empty()) {
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
    // Always leave the deck DISARMED after a playback-state reset. Arming
    // is the user's responsibility — it must come from an explicit PLAY
    // (piano key in the UI, or `playTape()` call in tests). Previously
    // this routine auto-armed whenever `loadedTapeReady && !empty`, which
    // meant simply inserting a cassette while the ACI was plugged caused
    // the deck to silently latch into "waiting for C100R" without the user
    // ever pressing PLAY — surprising behaviour for anyone used to a real
    // tape deck where loading a cassette just opens the compartment.
    playbackArmed = false;
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
    // audioRampInSamplesRemaining is atomic — safe to store here even though
    // some callers (e.g. rewindTape) already hold audioStreamMutex; no extra
    // lock and no two-mutex race with the audio-callback thread.
    audioRampInSamplesRemaining = kAudioRampInSamples;
    audioQueue.clear();
}

void CassetteDevice::dropLiveAudio()
{
    clearLiveAudioState();
}

void CassetteDevice::serialize(pom1::SnapshotWriter& w) const
{
    w.writeU8 (outputLevel ? 1 : 0);
    w.writeU8 (recordedInitialLevel ? 1 : 0);
    w.writeU64(currentCycle);
    w.writeU64(lastOutputToggleCycle);
    // Recorded transitions: u32 count + u32 each. Bounded by user behaviour
    // (fresh recording = a few seconds = ~1k entries typical).
    w.writeU32(static_cast<uint32_t>(recordedDurations.size()));
    if (!recordedDurations.empty()) {
        w.writeBytes(recordedDurations.data(),
                     recordedDurations.size() * sizeof(uint32_t));
    }
}

void CassetteDevice::deserialize(pom1::SnapshotReader& r)
{
    outputLevel              = r.readU8() != 0;
    recordedInitialLevel     = r.readU8() != 0;
    currentCycle             = r.readU64();
    lastOutputToggleCycle    = r.readU64();
    const uint32_t count     = r.readU32();
    // Validate the declared count against the bytes actually present before
    // allocating — a forged/corrupt snapshot could otherwise drive a multi-GB
    // assign(). Mirrors readByteVector()'s length guard.
    if (static_cast<std::streamoff>(count) * static_cast<std::streamoff>(sizeof(uint32_t))
            > r.bytesAvailable()) {
        r.fail();
        return;
    }
    recordedDurations.assign(count, 0);
    if (count) {
        r.readBytes(recordedDurations.data(), count * sizeof(uint32_t));
    }
    // recordingStarted_ isn't serialized (would change the section layout);
    // reconstruct it from the restored state — a recording is in progress iff
    // its start was stamped at a non-zero cycle or an edge interval was already
    // captured. (The only unreconstructable case is a session started at exactly
    // cycle 0 with no interval yet — the same near-unreachable edge the flag
    // fixes — which harmlessly re-initialises on the next toggle.)
    recordingStarted_ = (lastOutputToggleCycle != 0) || !recordedDurations.empty();
    // Quiesce playback — PR2 deliberately doesn't restore in-flight tape
    // position (see header comment). The user re-presses PLAY.
    stopPulseAudio();
    resetPlaybackState();
}

void CassetteDevice::stopPulseAudio()
{
    clearLiveAudioState();
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
    recordingStarted_ = false;   // recording cleared → next toggle starts fresh
    playbackPaused.store(false, std::memory_order_release);
    resetPlaybackState();
    stopPulseAudio();
}

void CassetteDevice::resetApple1Side()
{
    // Only touch fields that the Apple 1 hardware reset line would
    // physically clobber: the output flip-flop latched off $C000, and
    // the CPU-cycle timebase we share with the 6502 core (which the
    // hard reset also zeroes). Everything else is mechanical state
    // inside the deck and must survive a host reset — a real tape
    // doesn't rewind and the capstan doesn't stop just because the
    // Apple 1 got power-cycled.
    currentCycle = 0;
    outputLevel = false;
    lastOutputToggleCycle = 0;
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
        // Queue audio at the current inputLevel (before toggle) for the
        // segment that was just fully consumed. The audio callback drains
        // the queue at the hardware sample rate, so the deck speaker
        // stays locked to the 6502 cycle count.
        queueAudioSegment(
            std::max<uint32_t>(1, loadedDurations[playbackIndex - 1]),
            inputLevel);
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
    // Track session start with an explicit flag: `lastOutputToggleCycle == 0`
    // cannot be the "not yet recording" sentinel because 0 is a valid toggle
    // time (a $C000 write on the very first instruction after a reset lands at
    // cycle 0). The old guard then re-fired on the *second* toggle — clobbering
    // recordedInitialLevel with the already-flipped level and dropping the first
    // edge interval [0, T].
    if (!recordingStarted_) {
        recordingStarted_ = true;
        clearLiveAudioState();
        recordedInitialLevel = outputLevel;
        lastOutputToggleCycle = currentCycle;
    }
}

uint8_t CassetteDevice::toggleOutput()
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
    const bool becameActive = loadedTapeReady && !loadedDurations.empty();
    playbackActive = becameActive;
    playbackArmed = false;
    clearLiveAudioState();
    // The audio queue was cleared by clearLiveAudioState() above.
    // advancePlayback() will start pushing segments into the queue as
    // the CPU consumes pulse durations — the deck speaker stays silent
    // until the ACI ROM actually polls $C081 and drives the tape forward.
}

uint8_t CassetteDevice::readTapeInput()
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
        stopPulseAudio();
        return;
    }
    rewinding = true;
    rewCarryCycles = 0;
    playbackActive = false;
    playbackArmed  = false;
    stopPulseAudio();
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
    // sync leader first. resetPlaybackState zeroes the index, stamps
    // lastTapeInputCycle, clears the REW flags, and leaves the deck
    // DISARMED — we then explicitly arm it because this path is PLAY.
    //
    // Silence-while-armed: the speaker is NOT started here. PLAY only
    // declares the intent to play; the capstan stays quiet and the
    // counter stays frozen until the ACI ROM issues its first $C081
    // poll (the armed → active transition in `armPlaybackAtStart()`).
    // This keeps PLAY from "launching" playback in a way that contradicts
    // the ARMED banner.
    resetPlaybackState();
    playbackArmed = true;
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
    stopPulseAudio();
}

void CassetteDevice::ejectTape()
{
    closeAudioStream();
    // Clear the audio queue before mutating loadedDurations. The audio
    // thread never touches loadedDurations (it only drains audioQueue),
    // so there is no data race, but flushing avoids stale samples.
    stopPulseAudio();
    audioStreamMode = false;
    loadedDurations.clear();
    loadedTapePath.clear();
    loadInfo.clear();
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
    // Any tape in the deck is ejected on ACI toggle:
    //  - Plugging the ACI while a stream-mode tape is loaded would leave
    //    the ROM polling $C081 forever (no pulse transitions).
    //  - Unplugging the ACI while a program tape is loaded leaves a
    //    pulse-mode tape that can no longer be read by the ROM.
    // In both cases the cleanest UX is to eject so the user reloads a
    // tape that matches the new card state.
    if (loadedTapeReady) {
        ejectTape();
        return;  // ejectTape already fired the mode-change clunk
    }
}

void CassetteDevice::clearRecordedTape()
{
    recordedDurations.clear();
    recordedInitialLevel = outputLevel;
    lastOutputToggleCycle = 0;
    recordingStarted_ = false;   // recording cleared → next toggle starts fresh
    clearLiveAudioState();
}

bool CassetteDevice::loadPlaybackDurations(std::vector<uint32_t> durations, bool initialLevel, const std::string& path)
{
    if (durations.empty()) {
        lastError = "Tape file does not contain any signal transitions";
        return false;
    }

    // Flush the audio queue before replacing the durations vector.
    // The audio thread never touches loadedDurations directly.
    stopPulseAudio();
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
    loadInfo = lookupTapeInfo(path);

    // .aci is always pulse data — there is no audio stream to decode.
    if (ext == ".aci") {
        closeAudioStream();
        audioStreamMode = false;
        return loadAciTape(path);
    }

    // MP3 is treated as deck audio, never as an ACI program tape. Compressed MP3
    // music/speech should not flip the UI into PROGRAM mode just because the ACI
    // card is currently plugged.
    if (ext == ".mp3") {
        return loadAudioStream(path);
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

bool CassetteDevice::loadProgramTape(const std::string& path)
{
    const std::string ext = lowerExtension(path);
    loadInfo = lookupTapeInfo(path);
    if (ext == ".mp3") {
        return loadAudioStream(path);
    }
    closeAudioStream();
    audioStreamMode = false;

    if (ext == ".aci") return loadAciTape(path);
    if (ext == ".wav") return loadWavTape(path);
    if (ext == ".ogg" || ext == ".mp3" || ext == ".flac") {
        return loadMiniaudioTape(path);
    }

    lastError = "Unsupported program tape extension (expected .aci/.wav/.ogg/.mp3/.flac).";
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

    // Surface a write failure (disk full / media error / flush error) instead
    // of reporting success on a truncated file. Matches D64Image::save,
    // PR40Printer::savePaperRoll, Memory::saveSnapshot.
    file.flush();
    if (!file.good()) {
        lastError = "Write error on tape file: " + path;
        return false;
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
        // Compare against the remaining byte count rather than offset+chunkSize:
        // on 32-bit size_t (wasm32) a crafted chunkSize near 0xFFFFFFFF would wrap
        // offset+chunkSize below bytes.size() and slip past this guard.
        if (chunkSize > bytes.size() - offset) {
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
    // Entering stream mode — flush any pending pulse-mode audio segments.
    stopPulseAudio();
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

    // Surface a write failure rather than reporting success on a truncated file.
    file.flush();
    if (!file.good()) {
        lastError = "Write error on tape file: " + path;
        return false;
    }
    lastError.clear();
    return true;
}
