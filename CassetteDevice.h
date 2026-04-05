#ifndef CASSETTEDEVICE_H
#define CASSETTEDEVICE_H

#include "POM1Build.h"
#include "AudioDevice.h"

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

    size_t getLoadedTransitionCount() const { return loadedDurations.size(); }
    size_t getRecordedTransitionCount() const { return recordedDurations.size(); }
    const std::string& getLoadedTapePath() const { return loadedTapePath; }
    const std::string& getLastError() const { return lastError; }

private:
    static constexpr uint32_t kRealtimeAudioTimebaseHz = 1000000;
    static constexpr uint32_t kTapeFileTimebaseHz = 900000;
    static constexpr uint32_t kAudioSampleRate = 44100;

    void queueAudioSegment(uint32_t cycles, bool level);
    void advancePlayback(uint32_t cycles);

    bool loadAciTape(const std::string& path);
    bool saveAciTape(const std::string& path) const;
    bool loadWavTape(const std::string& path);
    bool saveWavTape(const std::string& path) const;

    bool loadPlaybackDurations(std::vector<uint32_t> durations, bool initialLevel, const std::string& path);

    void resetPlaybackState();
    void beginRecordingIfNeeded();
    void clearLiveAudioState();

private:
    bool audioAvailable = false;
    bool hardwareAccurateLiveAudio = false;
    uint32_t liveAudioTimebaseHz = kRealtimeAudioTimebaseHz;

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
