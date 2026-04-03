#include "CassetteDevice.h"
#include "POM1Build.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

#define MINIAUDIO_IMPLEMENTATION
#include "third_party/miniaudio.h"

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
    initAudio();
    reset();
}

CassetteDevice::~CassetteDevice()
{
    shutdownAudio();
}

void CassetteDevice::audioDataCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, uint32_t frameCount)
{
    CassetteDevice* self = static_cast<CassetteDevice*>(pDevice->pUserData);
    float* output = static_cast<float*>(pOutput);
    if (self == nullptr) {
        std::fill(output, output + frameCount, 0.0f);
        return;
    }

    // A small low-pass makes the live ACI output much less harsh than a raw
    // square wave while preserving the melody and timing.
    static constexpr float kFilterAlpha = 0.33f;
    std::lock_guard<std::mutex> lock(self->audioMutex);
    for (uint32_t i = 0; i < frameCount; ++i) {
        float targetSample = 0.0f;
        if (!self->audioQueue.empty()) {
            targetSample = self->audioQueue.front().sampleValue;
            if (self->audioQueue.front().remainingSamples > 0) {
                self->audioQueue.front().remainingSamples--;
            }
            if (self->audioQueue.front().remainingSamples == 0) {
                self->audioQueue.pop_front();
            }
        }
        if (self->audioRampInSamplesRemaining > 0) {
            const float ramp = 1.0f - (static_cast<float>(self->audioRampInSamplesRemaining) /
                                       static_cast<float>(kAudioRampInSamples));
            targetSample *= ramp;
            self->audioRampInSamplesRemaining--;
        }
        self->audioPlaybackSample += (targetSample - self->audioPlaybackSample) * kFilterAlpha;
        output[i] = self->audioPlaybackSample;
    }
}

bool CassetteDevice::initAudio()
{
#if POM1_IS_WASM
    // Pas de fil audio miniaudio sur le Web (pas de pthreads par défaut, AudioContext après geste utilisateur).
    audioAvailable = false;
    return false;
#else
    shutdownAudio();

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = kAudioSampleRate;
    config.periodSizeInFrames = 256;
    config.periods = 3;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.dataCallback = &CassetteDevice::audioDataCallback;
    config.pUserData = this;

    audioDevice = new ma_device();
    if (ma_device_init(nullptr, &config, audioDevice) != MA_SUCCESS) {
        delete audioDevice;
        audioDevice = nullptr;
        audioAvailable = false;
        return false;
    }

    if (ma_device_start(audioDevice) != MA_SUCCESS) {
        ma_device_uninit(audioDevice);
        delete audioDevice;
        audioDevice = nullptr;
        audioAvailable = false;
        return false;
    }

    audioAvailable = true;
    return true;
#endif
}

double CassetteDevice::getQueuedAudioSeconds() const
{
    std::lock_guard<std::mutex> lock(audioMutex);
    uint64_t queuedSamples = 0;
    for (const auto& segment : audioQueue) {
        queuedSamples += segment.remainingSamples;
    }
    return static_cast<double>(queuedSamples) / static_cast<double>(kAudioSampleRate);
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

void CassetteDevice::shutdownAudio()
{
    if (audioDevice != nullptr) {
        ma_device_uninit(audioDevice);
        delete audioDevice;
        audioDevice = nullptr;
    }
    audioAvailable = false;
}

void CassetteDevice::resetPlaybackState()
{
    playbackArmed = loadedTapeReady && !loadedDurations.empty();
    playbackActive = false;
    playbackIndex = 0;
    cyclesUntilInputToggle = 0;
    inputLevel = loadedInitialLevel;
}

void CassetteDevice::clearLiveAudioState()
{
    std::lock_guard<std::mutex> lock(audioMutex);
    audioSampleRemainder = 0.0;
    audioPlaybackSample = 0.0f;
    audioRampInSamplesRemaining = kAudioRampInSamples;
    audioQueue.clear();
}

void CassetteDevice::reset()
{
    currentCycle = 0;
    outputLevel = false;
    recordedInitialLevel = false;
    lastOutputToggleCycle = 0;
    recordedDurations.clear();
    resetPlaybackState();
    clearLiveAudioState();
}

void CassetteDevice::queueAudioSegment(uint32_t cycles, bool level)
{
    if (!audioAvailable || cycles == 0) {
        return;
    }

    const uint32_t liveTimebaseHz = hardwareAccurateLiveAudio ? liveAudioTimebaseHz : kRealtimeAudioTimebaseHz;
    const double totalSamples = audioSampleRemainder +
        (static_cast<double>(cycles) * static_cast<double>(kAudioSampleRate) / static_cast<double>(liveTimebaseHz));
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
    if (!playbackActive || loadedDurations.empty() || cycles == 0) {
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
        cyclesUntilInputToggle = 0;
        inputLevel = !inputLevel;

        if (playbackIndex >= loadedDurations.size()) {
            playbackActive = false;
        }
    }
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

CassetteDevice::quint8 CassetteDevice::readTapeInput()
{
    if (playbackArmed) {
        playbackArmed = false;
        playbackActive = loadedTapeReady && !loadedDurations.empty();
        playbackIndex = 0;
        cyclesUntilInputToggle = 0;
        inputLevel = loadedInitialLevel;
    }
    return inputLevel ? 0x80 : 0x00;
}

void CassetteDevice::rewindTape()
{
    resetPlaybackState();
    clearLiveAudioState();
}

void CassetteDevice::ejectTape()
{
    loadedDurations.clear();
    loadedTapePath.clear();
    loadedTapeReady = false;
    loadedInitialLevel = false;
    resetPlaybackState();
    clearLiveAudioState();
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

    loadedDurations = std::move(durations);
    loadedInitialLevel = initialLevel;
    loadedTapePath = path;
    loadedTapeReady = true;
    resetPlaybackState();
    clearLiveAudioState();
    lastError.clear();
    return true;
}

bool CassetteDevice::loadTape(const std::string& path)
{
    const std::string ext = lowerExtension(path);
    if (ext == ".wav") {
        return loadWavTape(path);
    }
    return loadAciTape(path);
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
        const_cast<CassetteDevice*>(this)->lastError = "No cassette output has been recorded yet";
        return false;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        const_cast<CassetteDevice*>(this)->lastError = "Cannot write tape file: " + path;
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

    const_cast<CassetteDevice*>(this)->lastError.clear();
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

    if (samples.empty()) {
        lastError = "WAV file does not contain audio samples";
        return false;
    }

    static constexpr float kThreshold = 0.02f;
    size_t firstActive = 0;
    while (firstActive < samples.size() && std::fabs(samples[firstActive]) < kThreshold) {
        ++firstActive;
    }
    if (firstActive == samples.size()) {
        lastError = "WAV file does not contain a detectable cassette signal";
        return false;
    }

    bool level = samples[firstActive] >= 0.0f;
    bool currentLevel = level;
    size_t lastTransition = firstActive;
    std::vector<uint32_t> durations;

    for (size_t i = firstActive + 1; i < samples.size(); ++i) {
        bool newLevel = currentLevel;
        if (samples[i] >= kThreshold) {
            newLevel = true;
        } else if (samples[i] <= -kThreshold) {
            newLevel = false;
        }

        if (newLevel != currentLevel) {
            const size_t deltaSamples = i - lastTransition;
            const uint32_t cycles = std::max<uint32_t>(1, static_cast<uint32_t>(
                std::llround(static_cast<double>(deltaSamples) * static_cast<double>(kTapeFileTimebaseHz) /
                             static_cast<double>(sampleRate))));
            durations.push_back(cycles);
            currentLevel = newLevel;
            lastTransition = i;
        }
    }

    return loadPlaybackDurations(std::move(durations), level, path);
}

bool CassetteDevice::saveWavTape(const std::string& path) const
{
    if (recordedDurations.empty()) {
        const_cast<CassetteDevice*>(this)->lastError = "No cassette output has been recorded yet";
        return false;
    }

    std::vector<int16_t> pcm;
    bool level = recordedInitialLevel;
    for (uint32_t duration : recordedDurations) {
        const uint32_t sampleCount = std::max<uint32_t>(1, static_cast<uint32_t>(
            std::llround(static_cast<double>(duration) * static_cast<double>(kAudioSampleRate) /
                         static_cast<double>(kTapeFileTimebaseHz))));
        const int16_t sample = level ? 14000 : -14000;
        pcm.insert(pcm.end(), sampleCount, sample);
        level = !level;
    }

    pcm.insert(pcm.end(), kAudioSampleRate / 10, level ? 14000 : -14000);

    const uint32_t dataSize = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t riffSize = 36 + dataSize;

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        const_cast<CassetteDevice*>(this)->lastError = "Cannot write tape file: " + path;
        return false;
    }

    file.write("RIFF", 4);
    writeLe32(file, riffSize);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    writeLe32(file, 16);
    writeLe16(file, 1);
    writeLe16(file, 1);
    writeLe32(file, kAudioSampleRate);
    writeLe32(file, kAudioSampleRate * sizeof(int16_t));
    writeLe16(file, sizeof(int16_t));
    writeLe16(file, 16);
    file.write("data", 4);
    writeLe32(file, dataSize);
    file.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(dataSize));

    const_cast<CassetteDevice*>(this)->lastError.clear();
    return true;
}
