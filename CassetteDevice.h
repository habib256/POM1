#ifndef CASSETTEDEVICE_H
#define CASSETTEDEVICE_H

#include "CpuClock.h"
#include "POM1Build.h"
#include "AudioDevice.h"
#include "third_party/miniaudio.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

class CassetteDevice : public AudioSource
{
public:
    using quint8 = uint8_t;
    using quint16 = uint16_t;

    CassetteDevice();
    ~CassetteDevice() override = default;

    void reset();
    void advanceCycles(int cycles);

    quint8 readTapeInput();
    quint8 toggleOutput();

    bool loadTape(const std::string& path);
    bool saveTape(const std::string& path) const;

    void rewindTape();
    /// Start loaded-tape playback from the beginning (virtual tape advances with CPU cycles).
    void playTape();
    /// Halt playback without resetting position or ejecting. Calling playTape()
    /// afterwards restarts from the beginning (playback position is not a
    /// first-class concept in the ACI pulse model).
    void stopTape();
    void ejectTape();
    void clearRecordedTape();

    /// Freeze/unfreeze playback. Silences the audio output and halts pulse
    /// advance (ACI mode) or frame consumption (stream mode). Resume resets
    /// the ramp-in to avoid a click on un-pause.
    void setPlaybackPaused(bool paused);
    bool isPlaybackPaused() const { return playbackPaused.load(std::memory_order_relaxed); }

    /// Stream-mode only seek (no-op in ACI pulse mode). Clamps at [0, total-1].
    void seekRelativeSeconds(double deltaSeconds);

    /// Stream-mode only: current cursor / total length in seconds. Both
    /// return 0 in ACI pulse mode (pulses have no wall-clock position).
    double getPlaybackPositionSeconds() const;
    double getPlaybackTotalSeconds() const;

    bool hasLoadedTape() const { return loadedTapeReady; }
    bool hasRecordedTape() const { return !recordedDurations.empty(); }
    bool isPlaybackActive() const { return playbackActive; }
    bool isAudioAvailable() const { return audioAvailable; }
    bool isHardwareAccurateLiveAudio() const { return hardwareAccurateLiveAudio; }
    double getQueuedAudioSeconds() const;
    void setHardwareAccurateLiveAudio(bool enabled);
    void setLiveAudioTimebaseHz(uint32_t hz);

    /// AudioSource interface — generates cassette audio samples.
    void fillAudioBuffer(float* output, int frameCount) override;

    /// Called by AudioDevice after init to signal audio is available.
    void setAudioAvailable(bool available) { audioAvailable = available; }

    /// Actual rate of the audio device (may differ from 44.1 kHz — miniaudio
    /// often negotiates 48 kHz on Apple Silicon; the browser AudioContext
    /// can ignore the requested rate entirely). Must be set after the
    /// AudioDevice has negotiated its rate, otherwise live cassette
    /// playback runs at the wrong speed by the rate ratio.
    void setAudioOutputSampleRate(uint32_t hz) { audioOutputSampleRate = std::max<uint32_t>(1, hz); }

    /// Cassette playback volume multiplier in [0, 2]. Applied to the final
    /// audio sample (ACI pulse mode AND audio-stream mode). UI thread sets,
    /// audio thread reads — std::atomic<float> with relaxed memory order is
    /// fine: a single-frame stale value is inaudible.
    void  setVolume(float v);
    float getVolume() const { return volume.load(std::memory_order_relaxed); }

    /// Tells the device whether the Apple Cassette Interface (ACI) card is
    /// currently plugged. Loading a tape while ACI is active uses the
    /// pulse/zero-crossing path so the CPU can read program data from
    /// $C081; with ACI unplugged, the device switches to a direct audio
    /// streaming path that plays the file as-is (mp3/ogg/flac/wav) with no
    /// length or pulse-extraction limit — the cassette becomes a simple
    /// audio player. The mode is latched at load time; toggling ACI
    /// afterwards does not re-mode an already-loaded tape.
    void setAciActive(bool active) { aciActive = active; }

    size_t getLoadedTransitionCount() const {
        return audioStreamMode ? static_cast<size_t>(audioStreamTotalFrames) : loadedDurations.size();
    }
    bool isAudioStreamMode() const { return audioStreamMode; }
    size_t getRecordedTransitionCount() const { return recordedDurations.size(); }
    const std::string& getLoadedTapePath() const { return loadedTapePath; }
    const std::string& getLastError() const { return lastError; }

private:
    static constexpr uint32_t kRealtimeAudioTimebaseHz = static_cast<uint32_t>(POM1_CPU_CLOCK_HZ);
    // Tape-file durations are stored in CPU-cycle units so they feed
    // advancePlayback() and saveWavTape() without unit conversion. Aligning
    // the constant on POM1_CPU_CLOCK_HZ also makes WAV exports round-trip
    // correctly against real Apple-1 hardware: a 770 Hz sync tone now lands
    // on the right number of cycles instead of being ~13 % off (the
    // pre-fix 900 kHz constant scaled every duration to ~88 % of its CPU-
    // cycle equivalent, pushing the "1" half-period dangerously close to
    // the Woz READBIT threshold and breaking OGG cassette loads).
    static constexpr uint32_t kTapeFileTimebaseHz = static_cast<uint32_t>(POM1_CPU_CLOCK_HZ);
    /// Sample rate written into saved .wav files — independent of the live
    /// audio device's rate so saved tapes stay portable regardless of the
    /// host's native rate.
    static constexpr uint32_t kWavFileSampleRate = 44100;

    void queueAudioSegment(uint32_t cycles, bool level);
    void advancePlayback(uint32_t cycles);

    bool loadAciTape(const std::string& path);
    bool saveAciTape(const std::string& path) const;
    bool loadWavTape(const std::string& path);
    bool saveWavTape(const std::string& path) const;
    // Decodes MP3 / Ogg Vorbis / FLAC via miniaudio's ma_decoder, feeds
    // the resulting mono PCM into pcmToDurations, and commits via
    // loadPlaybackDurations. Returns false with lastError set on any
    // decoder failure or empty/too-long input.
    bool loadMiniaudioTape(const std::string& path);
    // Opens the file as a live-streaming audio source — no pulse
    // extraction, no length cap. The decoder is kept alive until the tape
    // is ejected; the audio callback pulls mono float32 frames directly
    // at the device's output sample rate (miniaudio resamples internally).
    bool loadAudioStream(const std::string& path);
    void closeAudioStream();

    // Shared PCM → transition durations core (zero-crossing with
    // hysteresis + 900 kHz tape-file timebase). Extracted from
    // loadWavTape so both WAV and miniaudio paths hit the same math —
    // single source of truth for the threshold and timebase rounding.
    static bool pcmToDurations(const std::vector<float>& mono,
                               uint32_t sampleRate,
                               std::vector<uint32_t>& outDurations,
                               bool& outInitialLevel,
                               std::string& outErr);

    bool loadPlaybackDurations(std::vector<uint32_t> durations, bool initialLevel, const std::string& path);

    void resetPlaybackState();
    void beginRecordingIfNeeded();
    void clearLiveAudioState();

private:
    bool audioAvailable = false;
    bool hardwareAccurateLiveAudio = false;
    uint32_t liveAudioTimebaseHz = kRealtimeAudioTimebaseHz;
    /// Live output sample rate (set by AudioDevice after negotiation).
    /// Defaults to kWavFileSampleRate so existing callers still work before
    /// the real rate is known.
    uint32_t audioOutputSampleRate = kWavFileSampleRate;

    struct AudioSegment {
        uint32_t remainingSamples;
        float sampleValue;
    };

    mutable std::mutex audioMutex;
    std::deque<AudioSegment> audioQueue;
    float audioPlaybackSample = 0.0f;
    uint32_t audioRampInSamplesRemaining = 0;

    uint64_t currentCycle = 0;
    double audioSampleRemainder = 0.0;

    bool outputLevel = false;
    bool recordedInitialLevel = false;
    uint64_t lastOutputToggleCycle = 0;
    std::vector<uint32_t> recordedDurations;

    bool inputLevel = false;
    bool loadedInitialLevel = false;
    bool loadedTapeReady = false;
    bool playbackArmed = false;
    // CPU cycle of the most recent $C081 read. Used to detect a large gap
    // (the user was typing Wozmon commands during which nothing polls the
    // tape input): the next $C081 read rewinds the tape to the start so
    // the ACI READ routine sees the leader intact.
    uint64_t lastTapeInputCycle = 0;
    bool playbackActive = false;
    uint64_t cyclesUntilInputToggle = 0;
    size_t playbackIndex = 0;
    std::vector<uint32_t> loadedDurations;
    std::string loadedTapePath;

    // ACI-card state mirrored from Memory — determines whether a newly
    // loaded tape is treated as pulse data (ACI plugged) or as a direct
    // audio stream (ACI unplugged).
    bool aciActive = false;

    // Cassette volume multiplier — see setVolume(). 1.0 = no change.
    std::atomic<float> volume{1.0f};

    // Deck PAUSE — toggled by the UI via EmulationController::pauseTape().
    // Audio thread reads it every buffer; CPU-side advancePlayback checks
    // it too, so pause truly freezes both modes.
    std::atomic<bool> playbackPaused{false};

    // Direct audio streaming state. When audioStreamMode is true, playback
    // reads PCM frames from audioDecoder in fillAudioBuffer instead of
    // synthesising square waves from loadedDurations. Protected by
    // audioStreamMutex because the audio callback competes with UI-thread
    // load/eject/seek calls.
    mutable std::mutex audioStreamMutex;
    bool audioStreamMode = false;
    bool audioStreamDecoderOpen = false;
    ma_decoder audioStreamDecoder{};
    uint64_t audioStreamCursor = 0;       // frames consumed so far
    uint64_t audioStreamTotalFrames = 0;  // reported by decoder; 0 if unknown

    mutable std::string lastError;
};

#endif // CASSETTEDEVICE_H
