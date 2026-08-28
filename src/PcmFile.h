// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#ifndef POM1_PCM_FILE_H
#define POM1_PCM_FILE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pom1 {

/// Largest cassette file the hand-rolled parsers will look at, in bytes.
///
/// Holds thirty minutes of 44.1 kHz 16-bit mono — the same outer bound
/// kMaxPcmFrames expresses, and far past any real Apple-1 tape (the longest one
/// POM1 ships is a 2.5 MB recording of Woz talking). Checked before the file is
/// read, because reading it is the allocation the limit exists to prevent.
inline constexpr std::size_t kMaxPcmFileBytes = 256u * 1024u * 1024u;

/// Longest tape accepted, in frames: thirty minutes at 96 kHz.
///
/// This number is not new — it is the cap `CassetteDevice::loadMiniaudioTape()`
/// has always applied to MP3/FLAC/Vorbis, whose comment says it "prevents
/// accidental 2-hour podcast loads from chewing memory". The two hand-rolled
/// parsers had no cap at all, so a 2-hour WAV or AIFF sailed straight past the
/// very case that comment describes: a 1.2 GB file became a 1.2 GB byte vector
/// plus 1.3 GB of floats plus the duration list. Same rule for all three paths.
inline constexpr std::uint64_t kMaxPcmFrames = 30ull * 60ull * 96000ull;

/// Plausible sample-rate window for a cassette recording, in Hz.
///
/// Real rips are 8 kHz to 192 kHz; this is wide enough for anything a converter
/// emits and narrow enough that a garbage header field is refused instead of
/// divided by. Both bounds earn their place:
///
///   * the WAV parser had NO upper bound at all (the AIFF one has always been
///     bounded by its 80-bit float decoder), so a file could declare four
///     billion Hz and be accepted — an asymmetry a fuzz run turned up;
///   * the lower bound keeps the downstream conversion in range. Cassette
///     durations are computed as `deltaSamples * CPU_HZ / sampleRate` and then
///     narrowed to uint32; at 1 Hz a 5000-sample gap already yields 5.1e9,
///     whose conversion is undefined behaviour (UBSan: "outside the range of
///     representable values of type 'unsigned int'").
inline constexpr std::uint32_t kMinPcmSampleRate = 1000u;
inline constexpr std::uint32_t kMaxPcmSampleRate = 1000000u;

/// Decoded cassette audio: one mono float channel plus its rate.
///
/// The parsers below mix every channel down, because that is what the pulse
/// decoder wants — a cassette carries one signal, and a stereo rip of one is
/// two copies of it.
struct PcmAudio {
    bool ok = false;
    std::uint32_t sampleRate = 0;
    std::vector<float> mono;

    /// Set when `ok` is false, and then the only thing worth reading.
    std::string error;
    /// Set when the file was accepted with a reservation — currently only
    /// "longer than kMaxPcmFrames, tail ignored".
    std::string warning;
};

/// Parse RIFF/WAVE: PCM 8/16-bit and float32, any channel count.
///
/// PURE: bytes in, audio out. No file, no device, no log sink. Nothing partial
/// escapes — a rejected result carries no samples.
///
/// `maxFrames` exists so the truncation path can be exercised without
/// allocating the better part of a gigabyte; production callers take the
/// default.
PcmAudio parseWavPcm(const std::uint8_t* data, std::size_t size,
                     std::uint64_t maxFrames = kMaxPcmFrames);

/// Parse AIFF and AIFF-C: PCM 8/16/24/32-bit, plus AIFF-C `sowt` (byte-swapped)
/// and `fl32`. Same purity and same all-or-nothing contract as parseWavPcm.
///
/// POM1 parses AIFF by hand because miniaudio has no AIFF backend, and AIFF is
/// what Uncle Bernie's `ACIace` synthesiser emits — every extended-ACI recording
/// circulating on Applefritter arrives as one.
PcmAudio parseAiffPcm(const std::uint8_t* data, std::size_t size,
                      std::uint64_t maxFrames = kMaxPcmFrames);

} // namespace pom1

#endif // POM1_PCM_FILE_H
