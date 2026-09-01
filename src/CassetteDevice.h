#ifndef CASSETTEDEVICE_H
#define CASSETTEDEVICE_H

#include "CpuClock.h"
#include "POM1Build.h"
#include "AudioDevice.h"
#include "LockOrder.h"
#include "RealtimeDiagnostics.h"
#include "Peripheral.h"
// Vendored miniaudio: /W0 for the include only (same reason as the GCC/clang
// pragmas in AudioDevice.cpp — it is not ours to keep clean, and -DPOM1_WERROR=ON
// would otherwise fail on it in every TU that pulls this header).
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "third_party/miniaudio.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class CassetteDevice : public AudioSource, public pom1::Peripheral
{
public:
    std::string_view name() const override { return "ACI"; }

    /// Logical deck mode surfaced to the UI. Derived from whether a tape
    /// is loaded and which playback path it's on — the deck UI shows this
    /// as a big label, and a mechanical "clunk" fires on every transition.
    enum class DeckMode { NoTape, ProgramTape, AudioStream };
    DeckMode getDeckMode() const {
        if (!loadedTapeReady) return DeckMode::NoTape;
        return audioStreamMode ? DeckMode::AudioStream : DeckMode::ProgramTape;
    }

    CassetteDevice();
    /// Not `= default`: a mounted stream tape owns a live `ma_decoder`, and
    /// only `ma_decoder_uninit` releases the backend's buffers and file handle.
    /// Destroying the owning pointer alone leaks them (the nightly ASan job
    /// runs with detect_leaks=1). Defined in the .cpp, next to closeAudioStream.
    ~CassetteDevice() override;

    /// Full reset of every cassette-side state (loaded tape preserved,
    /// recording and playback progress WIPED). Called at construction
    /// only — the Apple 1 hard-reset path uses `resetApple1Side()` so
    /// the tape doesn't rewind itself when the user resets the computer
    /// (a real deck keeps playing regardless of the host's power state).
    void reset() override;
    /// Reset only the bits of the ACI that are actually wired to the
    /// Apple 1's hardware reset line: the `$C000` output flip-flop and
    /// the CPU-cycle timebase. Loaded tape, playback position, recording
    /// buffer, speaker loop, paused flag and rewinding flag all survive.
    void resetApple1Side();

    // Round-trip cassette state through a .snap file. Captures: $C000 flip-
    // flop (`outputLevel`), CPU-cycle timebase (`currentCycle`,
    // `lastOutputToggleCycle`), and the recorded transitions buffer. NOT
    // captured (deliberate scope of PR2 — see TODO.md "Snapshot save/load"):
    // loaded tape file (re-load via UI/CLI before snapshot-load), in-flight
    // playback position, audio-stream decoder state. Loading a snapshot
    // therefore preserves recordings but quiesces playback — the user
    // re-presses PLAY after resuming.
    void serialize(pom1::SnapshotWriter& writer) const override;
    void deserialize(pom1::SnapshotReader& reader) override;
    void advanceCycles(int cycles);

    uint8_t readTapeInput();
    uint8_t toggleOutput();

    /// Beeper SFX editor live preview: synthesise a 1-bit square wave straight
    /// into the pulse-audio queue — no CPU, no $C030 toggles. `pulses` is a
    /// (cpu-cycles, speaker-level) segment list (from sfxbeep::sfxToPulses);
    /// each segment is queued at the same timebase toggleOutput() uses, so the
    /// preview pitch matches the real beeper. Non-blocking: the audio callback
    /// plays it out. No-op without an audio device or while an audio-stream tape
    /// is loaded. Published through the pulse SPSC ring; call under the
    /// EmulationController state lock so there remains exactly one producer.
    void previewBeep(const std::vector<std::pair<uint32_t, bool>>& pulses);

    /// Beeper preview "stop": drop any queued preview pulses so the speaker goes
    /// silent immediately (the editor's Stop button). Safe when nothing is
    /// queued; only affects the pulse-audio queue.
    void stopPreviewBeep() { stopPulseAudio(); }

    bool loadTape(const std::string& path);
    bool loadProgramTape(const std::string& path);
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
    /// True when PLAY has been pressed but no $C081 poll has arrived yet
    /// (B6 play-on-first-read). To a UI observing the deck, armed looks
    /// like "playing, waiting for the ACI ROM to start reading" — if the
    /// UI conflates armed with EOF it'll auto-stop immediately after PLAY.
    bool isPlaybackArmed() const { return playbackArmed; }
    /// True while rewindTape() is physically walking playbackIndex back
    /// to 0. Pulse mode only — stream mode's ma_decoder seek is instant.
    /// Cleared the moment REW reaches index 0 (armed-at-start state).
    bool isRewinding() const { return rewinding; }
    bool isAudioAvailable() const { return audioAvailable; }
    bool isHardwareAccurateLiveAudio() const { return hardwareAccurateLiveAudio; }
    double getQueuedAudioSeconds() const;
    void setHardwareAccurateLiveAudio(bool enabled);
    void setLiveAudioTimebaseHz(uint32_t hz);

    /// Drop any pending live-audio samples (the queue recording feedback
    /// for `$C000` toggles). Used by the emulation loop when we're
    /// running faster than real time — letting the queue grow at 60×
    /// drain rate would otherwise park the emulation thread in its
    /// audio-lead throttle and defeat `--cpu-max`.
    void dropLiveAudio();

    /// AudioSource interface — generates cassette audio samples.
    void fillAudioBuffer(float* output, int frameCount) override;
    void copyRealtimeDiagnostics(pom1::RealtimeDiagnostics& out) const;

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
    /// afterwards does not re-mode an already-loaded tape. Transitioning
    /// from unplugged → plugged while a stream-mode tape is loaded evicts
    /// it (prevents the ACI ROM from polling $C081 forever against a flat
    /// input that carries no transitions).
    void setAciActive(bool active);

    size_t getLoadedTransitionCount() const {
        return audioStreamMode ? static_cast<size_t>(audioStreamTotalFrames.load(std::memory_order_relaxed))
                               : loadedDurations.size();
    }
    bool isAudioStreamMode() const { return audioStreamMode; }
    size_t getRecordedTransitionCount() const { return recordedDurations.size(); }
    const std::string& getLoadedTapePath() const { return loadedTapePath; }
    /// Human-readable load info read from a companion tapeinfo.txt file
    /// sitting next to the tape (e.g. "E000.EFFF" or "BASIC > LOAD").
    /// Empty when no metadata was found — the UI falls back to the
    /// transition count in that case.
    const std::string& getLoadInfo() const { return loadInfo; }
    const std::string& getLastError() const { return lastError; }

    // ── Interpreting loadInfo ────────────────────────────────────────────
    // tapeinfo.txt entries come in two shapes and the UI must not conflate
    // them:
    //
    //   a RANGE      "E000.EFFF"          the operator types it, then C100R
    //   a COMMAND    "C500R then RX RX"   the whole entry sequence, because
    //                                     Uncle Bernie's extended format
    //                                     carries its own from/to headers
    //                                     and needs no typed range
    //
    // A space is the discriminator: a load range never contains one. Both
    // helpers are pure string work and live here, next to the field they
    // interpret, so the deck widget and the tests agree by construction —
    // the deck used to hardcode "C100R" in its ARMED banner and to append a
    // bare "R" to whatever loadInfo said, which turned the extended entry
    // into the uncopyable "Type C500R then RX RXR".

    /// What the cassette label should tell the operator to type.
    static std::string tapeLabelCommand(const std::string& info)
    {
        if (info.empty()) return std::string();
        // A range needs the Woz Monitor's "R" to run; a command already has
        // every keystroke it needs.
        return info.find(' ') == std::string::npos ? info + "R" : info;
    }

    /// The command that ends the deck's ARMED wait by making the ACI poll
    /// $C081. For a range-style tape that is C100R (the range is typed
    /// first and does not read the tape); for a command-style tape the
    /// sequence itself already says it.
    static std::string tapeArmingCommand(const std::string& info)
    {
        return info.find(' ') == std::string::npos ? std::string("C100R") : info;
    }

    // Arm recording without requiring a CPU $C000 toggle. The deck's REC
    // button and the CLI `--rec` verb both use this so a scripted run can
    // capture output that the program emits as soon as it reaches the ACI
    // without having to inject a first toggle by hand.
    void armRecording() { beginRecordingIfNeeded(); }

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
    // Stop pulse-mode audio: publishes a ring clear boundary so the audio
    // callback goes silent. Called from stop/eject/rewind/load paths that need to
    // halt audible output before mutating playback state.
    void stopPulseAudio();
    void advancePlayback(uint32_t cycles);
    // Pulse-mode progressive rewind. Walks playbackIndex down toward 0
    // at kRewSpeedFactor× play speed. Called from advancePlayback while
    // `rewinding` is true. When index reaches 0 the tape re-arms at the
    // leader (resetPlaybackState).
    void advanceRewind(uint32_t cycles);
    // How much faster the tape winds back than it plays forward. 20×
    // makes a ~30 s tape rewind in ~1.5 s of emulated time — close to
    // CassetteDeck_ImGui's kWindDurationSeconds visual latch at 1×. At
    // --cpu-max the operation still races, which is consistent with the
    // rest of the emulator's "emulated time" semantics.
    static constexpr uint32_t kRewSpeedFactor = 20;

    bool loadAciTape(const std::string& path);
    bool saveAciTape(const std::string& path) const;
    // Shared body of the two hand-rolled container paths: size-check, read,
    // hand the bytes to the pure parser in PcmFile.h, decode pulses.
    bool loadPcmTape(const std::string& path, bool aiff);
    bool loadWavTape(const std::string& path);
    // Hand-rolled AIFF/AIFF-C PCM reader — miniaudio has no AIFF backend and
    // AIFF is what Uncle Bernie's ACIace tape synthesiser emits, so extended-
    // ACI recordings would otherwise be unloadable. See the .cpp for the
    // supported subset.
    bool loadAiffTape(const std::string& path);
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
    void refillAudioStreamRing();
    void clearAudioStreamRing();

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
    // Seek playback to the start of the loaded pulse stream and activate
    // it. Shared by readTapeInput's leader-rewind guard and its armed→
    // active transition — both branches need the exact same side effects,
    // and having them as one helper removes the foot-gun of one branch
    // evolving away from the other.
    void armPlaybackAtStart();

private:
    bool audioAvailable = false;
    bool hardwareAccurateLiveAudio = false;
    uint32_t liveAudioTimebaseHz = kRealtimeAudioTimebaseHz;
    /// Live output sample rate (set by AudioDevice after negotiation).
    /// Defaults to kWavFileSampleRate so existing callers still work before
    /// the real rate is known.
    uint32_t audioOutputSampleRate = kWavFileSampleRate;
    // Mechanical-click waveform, synthesised once per sample rate outside the
    // realtime path (see playMechanicalClick), then copied into the click ring.
    std::vector<float> clickCache_;
    uint32_t clickCacheRate_ = 0;

    static constexpr size_t kPulseRingCapacity = 262144;
    static constexpr size_t kClickRingCapacity = 8192;
    std::array<float, kPulseRingCapacity> pulseRing{};
    std::atomic<size_t> pulseHead{0};
    std::atomic<size_t> pulseTail{0};
    std::atomic<size_t> pulseClearHead{0};
    std::atomic<uint64_t> pulseClearGeneration{0};
    uint64_t pulseConsumerGeneration = 0; // audio callback only
    std::array<float, kClickRingCapacity> clickRing{};
    std::atomic<size_t> clickHead{0};
    std::atomic<size_t> clickTail{0};
#if POM1_REALTIME_DIAGNOSTICS
    std::atomic<uint64_t> ringUnderruns{0};
    std::atomic<uint64_t> ringOverflows{0};
#endif
    float audioPlaybackSample = 0.0f;
    // Touched by the realtime audio-callback thread (decrement) and by main-thread resets
    // through several paths guarded by different mutexes — atomic so those can
    // never race regardless of which lock the caller holds. Only the audio
    // thread decrements; main thread only stores kAudioRampInSamples.
    std::atomic<uint32_t> audioRampInSamplesRemaining{0};

    // Mechanical "clunk" that fires when the deck mode transitions
    // (NoTape ↔ ProgramTape ↔ AudioStream). Pre-synthesised into
    // click ring once per event, then mixed on top of whatever the
    // speaker/queue paths are producing. Single producer (controller thread),
    // single consumer (audio callback), no lock or allocation in the callback.
    DeckMode lastDeckMode = DeckMode::NoTape;

    void playMechanicalClick();
    // Called after any state change that might flip the deck mode.
    // Self-edge transitions don't fire (loading a second .aci while the
    // first is in, etc.); only a real mode flip does.
    void fireClickIfModeChanged();

    uint64_t currentCycle = 0;
    double audioSampleRemainder = 0.0;

    bool outputLevel = false;
    bool recordedInitialLevel = false;
    uint64_t lastOutputToggleCycle = 0;
    // True once a recording session has captured its initial level. Explicit
    // sentinel because `lastOutputToggleCycle == 0` can't mean "not started" —
    // cycle 0 is a valid toggle time. Not serialized: reconstructed on
    // deserialize from lastOutputToggleCycle/recordedDurations.
    bool recordingStarted_ = false;
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
    // Read on the realtime audio thread (fillAudioBuffer) and written on the
    // CPU/UI thread — atomic to avoid a data race on the playback flag.
    std::atomic<bool> playbackActive{false};
    uint64_t cyclesUntilInputToggle = 0;
    size_t playbackIndex = 0;
    // Progressive-rewind state (pulse mode). `rewinding` is set by
    // rewindTape() when playbackIndex > 0 and cleared when REW reaches 0
    // or the tape is stopped/ejected. `rewCarryCycles` holds the
    // fractional REW budget carried over from a slice that couldn't
    // consume the next segment whole — accumulates across slices.
    bool rewinding = false;
    uint64_t rewCarryCycles = 0;
    // Threading invariant: loadedDurations is written by the UI thread
    // (loadTape / ejectTape → loadPlaybackDurations) and read by the CPU
    // thread (advancePlayback, which also queues audio segments from it).
    // Both sides run under EmulationController::stateMutex, so the
    // std::move assignment and the indexed read cannot overlap. The AUDIO
    // thread's fillAudioBuffer() must NOT touch this vector — it only
    // consumes the fixed pulse ring.
    std::vector<uint32_t> loadedDurations;
    std::string loadedTapePath;
    // Load info read from tapeinfo.txt next to the tape file. Shown on
    // the cassette label so the user knows what to type in Woz Monitor.
    std::string loadInfo;

    // Look up loadInfo from a tapeinfo.txt companion file sitting in the
    // same directory as `path`. Format: one "basename = info" per line.
    static std::string lookupTapeInfo(const std::string& path);

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

    // Direct audio streaming state. The emulation thread decodes into a fixed
    // SPSC ring; the realtime callback only consumes already-decoded PCM.
    // The mutex serialises decoder refill with UI-thread load/eject/seek.
    mutable pom1::RankedMutex<pom1::LockRank::Peripheral> audioStreamMutex;
    // Read on the realtime audio thread and in const mode queries while the
    // CPU/UI thread flips it on load/eject.
    std::atomic<bool> audioStreamMode{false};
    bool audioStreamDecoderOpen = false;
    /// Heap-owned so a fresh decoder can be built OUTSIDE audioStreamMutex and
    /// swapped in under a short lock (see loadAudioStream). A by-value
    /// ma_decoder could not be moved in after init — miniaudio's data-source
    /// base holds pointers into the object itself, so its address must not
    /// change once ma_decoder_init_file has run.
    std::unique_ptr<ma_decoder> audioStreamDecoder;
    static constexpr size_t kStreamRingCapacity = 262144;
    std::array<float, kStreamRingCapacity> streamRing{};
    std::atomic<size_t> streamHead{0};
    std::atomic<size_t> streamTail{0};
    std::atomic<size_t> streamClearHead{0};
    std::atomic<uint64_t> streamClearGeneration{0};
    uint64_t streamConsumerGeneration = 0; // audio callback only
    std::atomic<bool> audioStreamEof{false};
    // SnapshotPublisher reads these counters without the decoder mutex.
    std::atomic<uint64_t> audioStreamCursor{ 0 };      // frames consumed so far
    std::atomic<uint64_t> audioStreamTotalFrames{ 0 }; // decoder-reported; 0 if unknown

    mutable std::string lastError;
};

#endif // CASSETTEDEVICE_H
