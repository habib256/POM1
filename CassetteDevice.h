#ifndef CASSETTEDEVICE_H
#define CASSETTEDEVICE_H

#include "CpuClock.h"
#include "POM1Build.h"
#include "AudioDevice.h"

#include <algorithm>
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
    void ejectTape();
    void clearRecordedTape();

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

    size_t getLoadedTransitionCount() const { return loadedDurations.size(); }
    size_t getRecordedTransitionCount() const { return recordedDurations.size(); }
    const std::string& getLoadedTapePath() const { return loadedTapePath; }
    const std::string& getLastError() const { return lastError; }

private:
    static constexpr uint32_t kRealtimeAudioTimebaseHz = static_cast<uint32_t>(POM1_CPU_CLOCK_HZ);
    static constexpr uint32_t kTapeFileTimebaseHz = 900000;
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
    bool playbackActive = false;
    uint64_t cyclesUntilInputToggle = 0;
    size_t playbackIndex = 0;
    std::vector<uint32_t> loadedDurations;
    std::string loadedTapePath;

    mutable std::string lastError;
};

#endif // CASSETTEDEVICE_H
