// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// The two cassette containers POM1 decodes itself, as pure functions. Lifted
// out of CassetteDevice, which read the file, parsed it, mixed it and stored
// the result in one pass — so neither parser could be tested or fuzzed without
// a whole audio device, and neither could bound what it was about to allocate.
//
// Both formats are chunked: a 12-byte header, then a sequence of
// (4-byte tag, 4-byte size, payload) with word alignment. WAV is
// little-endian, AIFF big-endian, and that is very nearly the whole difference
// until the sample decoding.

#include "PcmFile.h"

#include <cmath>
#include <cstring>
#include <sstream>

namespace pom1 {
namespace {

std::uint16_t readLe16(const std::uint8_t* d) { return static_cast<std::uint16_t>(d[0] | (d[1] << 8)); }
std::uint32_t readLe32(const std::uint8_t* d)
{
    return static_cast<std::uint32_t>(d[0]) | (static_cast<std::uint32_t>(d[1]) << 8) |
           (static_cast<std::uint32_t>(d[2]) << 16) | (static_cast<std::uint32_t>(d[3]) << 24);
}
std::uint16_t readBe16(const std::uint8_t* d) { return static_cast<std::uint16_t>((d[0] << 8) | d[1]); }
std::uint32_t readBe32(const std::uint8_t* d)
{
    return (static_cast<std::uint32_t>(d[0]) << 24) | (static_cast<std::uint32_t>(d[1]) << 16) |
           (static_cast<std::uint32_t>(d[2]) << 8) | static_cast<std::uint32_t>(d[3]);
}

/// AIFF stores its sample rate as an 80-bit IEEE 754 extended float, a type no
/// mainstream compiler exposes. Decode the fields by hand and refuse anything
/// that is not plausibly a rate rather than trusting an out-of-range shift.
std::uint32_t readBe80Float(const std::uint8_t* data)
{
    const std::uint16_t expField = readBe16(data);
    const int exponent = static_cast<int>(expField & 0x7FFF);
    std::uint64_t mantissa = 0;
    for (int i = 0; i < 8; ++i) mantissa = (mantissa << 8) | data[2 + i];
    if (exponent == 0 || exponent == 0x7FFF || mantissa == 0) return 0;

    const int shift = exponent - 16383 - 63;
    // Sample rates live in [1, 1e7]; anything needing a shift outside this
    // window is not a rate, so bail rather than commit UB on the shift itself.
    if (shift > 0 || shift < -63) return 0;
    const double value = static_cast<double>(mantissa) * std::pow(2.0, shift);
    if (value < 1.0 || value > 10000000.0) return 0;
    return static_cast<std::uint32_t>(value + 0.5);
}

/// Float containers can carry any bit pattern, NaN and infinity included, and
/// nothing downstream is prepared for them: the pulse decoder compares each
/// sample against a threshold, and EVERY comparison against a NaN is false — so
/// a corrupt float WAV reads as a flat line and is reported as "no detectable
/// cassette signal", which tells the user nothing about what is wrong. Treat a
/// non-finite sample as silence, count it, and say so.
float sanitize(float v, std::size_t& badSamples)
{
    if (std::isfinite(v)) return v;
    ++badSamples;
    return 0.0f;
}

void noteBadSamples(std::size_t badSamples, std::string& warning)
{
    if (badSamples == 0) return;
    std::ostringstream oss;
    oss << badSamples << " non-finite sample(s) replaced with silence";
    warning = warning.empty() ? oss.str() : warning + "; " + oss.str();
}

PcmAudio fail(std::string message)
{
    PcmAudio out;
    out.ok = false;
    out.error = std::move(message);
    return out;
}

/// Apply the length cap and say so. Returns the number of frames to decode.
///
/// The cap is on FRAMES rather than bytes because that is what bounds the two
/// vectors that actually grow: the mixed-down samples here and the duration
/// list downstream.
std::size_t capFrames(std::size_t frameCount, std::uint64_t maxFrames, std::string& warning)
{
    if (static_cast<std::uint64_t>(frameCount) <= maxFrames) return frameCount;
    std::ostringstream oss;
    oss << "tape is longer than " << (maxFrames / (60ull * 96000ull))
        << " minutes — only the first part was loaded";
    warning = oss.str();
    return static_cast<std::size_t>(maxFrames);
}

} // namespace

PcmAudio parseWavPcm(const std::uint8_t* bytes, std::size_t size,
                     std::uint64_t maxFrames)
{
    if (size > kMaxPcmFileBytes)
        return fail("WAV file is too large to be a cassette tape");
    // 44 = the shortest possible RIFF header + fmt + data headers.
    if (size < 44 || std::memcmp(bytes, "RIFF", 4) != 0 ||
        std::memcmp(bytes + 8, "WAVE", 4) != 0)
        return fail("Invalid WAV file");

    std::uint16_t audioFormat = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    const std::uint8_t* dataChunk = nullptr;
    std::uint32_t dataSize = 0;

    std::size_t offset = 12;
    while (offset + 8 <= size) {
        const std::uint8_t* chunk = bytes + offset;
        const std::uint32_t chunkSize = readLe32(chunk + 4);
        offset += 8;
        // Compare against the REMAINING byte count, never offset+chunkSize: on a
        // 32-bit size_t (wasm32) a crafted chunkSize near 0xFFFFFFFF wraps the
        // sum below size and slips straight past the guard.
        if (chunkSize > size - offset) break;

        if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            audioFormat   = readLe16(bytes + offset + 0);
            channels      = readLe16(bytes + offset + 2);
            sampleRate    = readLe32(bytes + offset + 4);
            bitsPerSample = readLe16(bytes + offset + 14);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            dataChunk = bytes + offset;
            dataSize  = chunkSize;
        }

        offset += chunkSize + (chunkSize & 1u);   // RIFF chunks are word-aligned
    }

    if (dataChunk == nullptr || channels == 0 || sampleRate == 0)
        return fail("WAV file is missing format or data chunks");
    if (sampleRate < kMinPcmSampleRate || sampleRate > kMaxPcmSampleRate)
        return fail("WAV sample rate is not plausible for a cassette recording");
    if (audioFormat != 1 && audioFormat != 3)
        return fail("Unsupported WAV format (only PCM and float are supported)");

    // Decide the whole sample layout BEFORE decoding a single frame. The old
    // in-place version discovered an unsupported width from inside the decode
    // loop, after it had already mixed and stored every earlier frame.
    const std::size_t bytesPerSample = bitsPerSample / 8u;
    if (bytesPerSample == 0 ||
        dataSize < static_cast<std::uint64_t>(bytesPerSample) * channels)
        return fail("Unsupported WAV sample format");
    const bool pcm8   = audioFormat == 1 && bitsPerSample == 8;
    const bool pcm16  = audioFormat == 1 && bitsPerSample == 16;
    const bool flt32  = audioFormat == 3 && bitsPerSample == 32;
    if (!pcm8 && !pcm16 && !flt32)
        return fail("Only WAV PCM 8/16-bit and float32 are supported");

    PcmAudio out;
    const std::size_t stride = bytesPerSample * channels;
    const std::size_t frameCount = capFrames(dataSize / stride, maxFrames, out.warning);
    if (frameCount == 0) return fail("WAV file contains no audio frames");

    std::size_t badSamples = 0;
    out.mono.reserve(frameCount);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        float mixed = 0.0f;
        for (std::uint16_t ch = 0; ch < channels; ++ch) {
            const std::uint8_t* p = dataChunk + frame * stride + ch * bytesPerSample;
            if (pcm8) {
                // WAV's 8-bit samples are UNSIGNED — the one width where it
                // differs from AIFF, which is signed at every width.
                mixed += (static_cast<int>(p[0]) - 128) / 128.0f;
            } else if (pcm16) {
                mixed += static_cast<float>(static_cast<std::int16_t>(readLe16(p))) / 32768.0f;
            } else {
                float f = 0.0f;
                std::memcpy(&f, p, sizeof(float));
                mixed += sanitize(f, badSamples);
            }
        }
        // Sanitize the MIX, not only each channel. Two finite channels can
        // sum to a non-finite value — 3.4e38 + 3.4e38 overflows float — so a
        // per-channel scrub leaves a NaN/Inf in the output it just promised to
        // have removed. Found by the nightly fuzz job (pcm/crash-e086c0e6…, an
        // AIFF-C fl32 file): 5 samples scrubbed, and one still non-finite.
        out.mono.push_back(sanitize(mixed / static_cast<float>(channels), badSamples));
    }
    noteBadSamples(badSamples, out.warning);

    out.ok = true;
    out.sampleRate = sampleRate;
    return out;
}

PcmAudio parseAiffPcm(const std::uint8_t* bytes, std::size_t size,
                      std::uint64_t maxFrames)
{
    if (size > kMaxPcmFileBytes)
        return fail("AIFF file is too large to be a cassette tape");

    const bool isAifc = size >= 12 && std::memcmp(bytes + 8, "AIFC", 4) == 0;
    if (size < 12 || std::memcmp(bytes, "FORM", 4) != 0 ||
        (std::memcmp(bytes + 8, "AIFF", 4) != 0 && !isAifc))
        return fail("Invalid AIFF file");

    std::uint16_t channels = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t commFrames = 0;
    bool haveComm = false;
    bool littleEndianSamples = false;   // AIFF-C 'sowt'
    bool floatSamples = false;          // AIFF-C 'fl32'
    const std::uint8_t* soundData = nullptr;
    std::uint32_t soundSize = 0;

    std::size_t offset = 12;
    while (offset + 8 <= size) {
        const std::uint8_t* chunk = bytes + offset;
        const std::uint32_t chunkSize = readBe32(chunk + 4);
        offset += 8;
        // Same overflow-safe form as the WAV parser above, for the same reason.
        if (chunkSize > size - offset) break;

        if (std::memcmp(chunk, "COMM", 4) == 0 && chunkSize >= 18) {
            const std::uint8_t* c = bytes + offset;
            channels      = readBe16(c + 0);
            commFrames    = readBe32(c + 2);
            bitsPerSample = readBe16(c + 6);
            sampleRate    = readBe80Float(c + 8);
            haveComm      = true;
            if (isAifc && chunkSize >= 22) {
                const std::uint8_t* comp = c + 18;
                if (std::memcmp(comp, "sowt", 4) == 0)      littleEndianSamples = true;
                else if (std::memcmp(comp, "fl32", 4) == 0 ||
                         std::memcmp(comp, "FL32", 4) == 0) floatSamples = true;
                else if (std::memcmp(comp, "NONE", 4) != 0)
                    return fail("Unsupported AIFF-C compression (expected NONE/sowt/fl32)");
            }
        } else if (std::memcmp(chunk, "SSND", 4) == 0 && chunkSize >= 8) {
            // SSND leads with offset + blockSize; the frames start after them,
            // displaced by `ssndOffset` bytes of alignment padding.
            const std::uint32_t ssndOffset = readBe32(bytes + offset);
            if (static_cast<std::uint64_t>(ssndOffset) + 8 > chunkSize)
                return fail("Malformed AIFF SSND chunk");
            soundData = bytes + offset + 8 + ssndOffset;
            soundSize = chunkSize - 8 - ssndOffset;
        }

        offset += chunkSize + (chunkSize & 1u);   // IFF chunks are word-aligned
    }

    if (!haveComm || soundData == nullptr || channels == 0 || sampleRate == 0)
        return fail("AIFF file is missing COMM or SSND chunks");
    if (sampleRate < kMinPcmSampleRate || sampleRate > kMaxPcmSampleRate)
        return fail("AIFF sample rate is not plausible for a cassette recording");
    if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 &&
        bitsPerSample != 32)
        return fail("Unsupported AIFF sample width (expected 8/16/24/32-bit)");
    if (floatSamples && bitsPerSample != 32)
        return fail("AIFF fl32 requires 32-bit samples");

    PcmAudio out;
    const std::size_t bytesPerSample = bitsPerSample / 8u;
    const std::size_t stride = bytesPerSample * channels;
    std::size_t frameCount = soundSize / stride;
    // COMM's frame count wins when it is the SMALLER of the two — trailing
    // garbage after the last frame is common in synthesised files.
    if (commFrames != 0 && commFrames < frameCount) frameCount = commFrames;
    frameCount = capFrames(frameCount, maxFrames, out.warning);
    if (frameCount == 0) return fail("AIFF file contains no audio frames");

    std::size_t badSamples = 0;
    out.mono.reserve(frameCount);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        float mixed = 0.0f;
        for (std::uint16_t ch = 0; ch < channels; ++ch) {
            const std::uint8_t* p = soundData + frame * stride + ch * bytesPerSample;
            float value = 0.0f;
            if (floatSamples) {
                std::uint8_t le[4] = {p[3], p[2], p[1], p[0]};   // fl32 is big-endian
                if (littleEndianSamples) std::memcpy(le, p, 4);
                std::memcpy(&value, le, sizeof(float));
                value = sanitize(value, badSamples);
            } else if (bitsPerSample == 8) {
                // AIFF PCM is SIGNED at every width, including 8-bit — the one
                // place it differs from WAV, whose 8-bit samples are unsigned.
                value = static_cast<float>(static_cast<std::int8_t>(p[0])) / 128.0f;
            } else if (bitsPerSample == 16) {
                const std::int16_t s = static_cast<std::int16_t>(
                    littleEndianSamples ? readLe16(p) : readBe16(p));
                value = static_cast<float>(s) / 32768.0f;
            } else if (bitsPerSample == 24) {
                const std::int32_t raw = littleEndianSamples
                    ? ((static_cast<std::int32_t>(p[2]) << 16) | (p[1] << 8) | p[0])
                    : ((static_cast<std::int32_t>(p[0]) << 16) | (p[1] << 8) | p[2]);
                const std::int32_t s = (raw & 0x800000) ? (raw - 0x1000000) : raw;
                value = static_cast<float>(s) / 8388608.0f;
            } else {
                const std::int32_t s = static_cast<std::int32_t>(
                    littleEndianSamples ? readLe32(p) : readBe32(p));
                value = static_cast<float>(s) / 2147483648.0f;
            }
            mixed += value;
        }
        // Sanitize the MIX, not only each channel. Two finite channels can
        // sum to a non-finite value — 3.4e38 + 3.4e38 overflows float — so a
        // per-channel scrub leaves a NaN/Inf in the output it just promised to
        // have removed. Found by the nightly fuzz job (pcm/crash-e086c0e6…, an
        // AIFF-C fl32 file): 5 samples scrubbed, and one still non-finite.
        out.mono.push_back(sanitize(mixed / static_cast<float>(channels), badSamples));
    }
    noteBadSamples(badSamples, out.warning);

    out.ok = true;
    out.sampleRate = sampleRate;
    return out;
}

} // namespace pom1
