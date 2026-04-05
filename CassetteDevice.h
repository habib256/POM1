#ifndef CASSETTEDEVICE_H
#define CASSETTEDEVICE_H

#include "POM1Build.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#if !POM1_IS_WASM
struct ma_device;
#endif

class SID;

class CassetteDevice
{
public:
    using quint8 = uint8_t;
    using quint16 = uint16_t;

    CassetteDevice();
    ~CassetteDevice();

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

    void fillAudioBuffer(float* output, int frameCount);

    // P-LAB A1-SID: set external SID audio source for mixing
    void setSIDSource(SID* source) { sidSource = source; }

    size_t getLoadedTransitionCount() const { return loadedDurations.size(); }
    size_t getRecordedTransitionCount() const { return recordedDurations.size(); }
    const std::string& getLoadedTapePath() const { return loadedTapePath; }
    const std::string& getLastError() const { return lastError; }

private:
    // Live playback must stay aligned with the running emulation thread.
    static constexpr uint32_t kRealtimeAudioTimebaseHz = 1000000;
    // Export/import stays calibrated slightly below raw CPU clock so generated
    // WAV files match verified Apple-1 reference recordings better.
    static constexpr uint32_t kTapeFileTimebaseHz = 900000;
    static constexpr uint32_t kAudioSampleRate = 44100;

    bool initAudio();
    void shutdownAudio();
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
#if !POM1_IS_WASM
    ma_device* audioDevice = nullptr;
#endif
    bool audioAvailable = false;
    bool hardwareAccurateLiveAudio = false;
    uint32_t liveAudioTimebaseHz = kRealtimeAudioTimebaseHz;

#if !POM1_IS_WASM
    static void audioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount);
#endif
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

    SID* sidSource = nullptr;
};

#endif // CASSETTEDEVICE_H
