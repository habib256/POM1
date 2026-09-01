// Fuzz target + bounded PR smoke for the cassette container parsers.
//
// This is the surface where POM1 reads a BINARY file a user picked: the deck
// accepts whatever is dropped on it, and two of the three paths are POM1's own
// hand-rolled chunk parsers (miniaudio covers only MP3/FLAC/Vorbis). Chunked
// binary formats are where length arithmetic goes wrong — a size field that
// wraps, an offset that points past its chunk, a frame count that claims more
// than the file holds — and every one of those fields comes from the file.
//
// Same dual shape as memory_image_fuzz_test.cpp: a deterministic driver by
// default (Apple clang ships no libFuzzer, so a fuzz-only gate would silently
// not run on macOS) and LLVMFuzzerTestOneInput under -DPOM1_FUZZERS=ON.
//
// The contract every result must hold:
//   1. a rejected parse yields no samples and says why;
//   2. an accepted parse has a plausible sample rate — the pulse decoder
//      divides by it;
//   3. the frame count is bounded by the cap AND by the input, so no crafted
//      header can make a small file allocate a large buffer;
//   4. samples are finite: NaN or infinity would poison the pulse decoder
//      rather than stopping at the parser;
//   5. truncation is never silent;
//   6. parsing is deterministic and side-effect free.

#include "PcmFile.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

void checkOneResult(const pom1::PcmAudio& a, size_t inputSize)
{
    if (!a.ok) {
        // (1) Nothing partial escapes, and the caller has something to show.
        assert(a.mono.empty());
        assert(!a.error.empty());
        return;
    }
    // (2) The rate reaches a divide in the pulse decoder.
    assert(a.sampleRate >= 1 && a.sampleRate <= 10000000u);

    // (3) Bounded twice over. The second bound is the one that matters for a
    //     hostile file: every frame needs at least one byte of payload, so a
    //     header claiming millions of frames over a few bytes of data must not
    //     be believed.
    assert(static_cast<uint64_t>(a.mono.size()) <= pom1::kMaxPcmFrames);
    assert(a.mono.size() <= inputSize);

    // (4) A NaN here would travel silently into the pulse decoder's
    //     comparisons, where every branch it takes is false.
    for (float s : a.mono) assert(std::isfinite(s));

    // (5) A tape cut short must say so.
    if (static_cast<uint64_t>(a.mono.size()) == pom1::kMaxPcmFrames)
        assert(!a.warning.empty());
}

void checkInvariants(const uint8_t* data, size_t size)
{
    const pom1::PcmAudio wav = pom1::parseWavPcm(data, size);
    checkOneResult(wav, size);
    const pom1::PcmAudio aiff = pom1::parseAiffPcm(data, size);
    checkOneResult(aiff, size);

    // A WAV is not an AIFF and vice versa: whatever this input is, at most one
    // of the two containers should claim it. Both accepting the same bytes
    // would mean a magic check is not doing its job.
    assert(!(wav.ok && aiff.ok));

    // (6) Pure means pure.
    const pom1::PcmAudio again = pom1::parseWavPcm(data, size);
    assert(again.ok == wav.ok && again.sampleRate == wav.sampleRate);
    assert(again.mono == wav.mono);
}

} // namespace

#if defined(POM1_LIBFUZZER)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size > 1u << 20) return 0;
    checkInvariants(data, size);
    return 0;
}

#else

namespace {

void putLe16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x & 0xFF); v.push_back(x >> 8); }
void putLe32(std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xFF);
}
void putBe16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x >> 8); v.push_back(x & 0xFF); }
void putBe32(std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 3; i >= 0; --i) v.push_back((x >> (8 * i)) & 0xFF);
}
void putTag(std::vector<uint8_t>& v, const char* t) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(t[i])); }

std::vector<uint8_t> seedWav(uint16_t format, uint16_t channels, uint16_t bits, size_t frames)
{
    std::vector<uint8_t> v;
    const size_t stride = channels * (bits / 8u);
    putTag(v, "RIFF"); putLe32(v, 0); putTag(v, "WAVE");
    putTag(v, "fmt "); putLe32(v, 16);
    putLe16(v, format); putLe16(v, channels); putLe32(v, 44100);
    putLe32(v, uint32_t(44100 * stride)); putLe16(v, uint16_t(stride)); putLe16(v, bits);
    putTag(v, "data"); putLe32(v, uint32_t(frames * stride));
    for (size_t i = 0; i < frames * stride; ++i) v.push_back(uint8_t(i * 37));
    return v;
}

std::vector<uint8_t> seedAiff(const char* compression, uint16_t bits, size_t frames,
                              uint32_t ssndOffset)
{
    std::vector<uint8_t> v;
    putTag(v, "FORM"); putBe32(v, 0); putTag(v, compression ? "AIFC" : "AIFF");
    std::vector<uint8_t> comm;
    putBe16(comm, 1); putBe32(comm, uint32_t(frames)); putBe16(comm, bits);
    // 44100 Hz as an 80-bit extended float.
    putBe16(comm, 0x400E);
    for (unsigned b : {0xACu, 0x44u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u})
        comm.push_back(static_cast<uint8_t>(b));
    if (compression) { putTag(comm, compression); comm.push_back(0); comm.push_back(0); }
    putTag(v, "COMM"); putBe32(v, uint32_t(comm.size()));
    v.insert(v.end(), comm.begin(), comm.end());
    if (comm.size() & 1u) v.push_back(0);

    std::vector<uint8_t> ssnd;
    putBe32(ssnd, ssndOffset); putBe32(ssnd, 0);
    ssnd.insert(ssnd.end(), ssndOffset, 0);
    for (size_t i = 0; i < frames * (bits / 8u); ++i) ssnd.push_back(uint8_t(i * 53));
    putTag(v, "SSND"); putBe32(v, uint32_t(ssnd.size()));
    v.insert(v.end(), ssnd.begin(), ssnd.end());
    if (ssnd.size() & 1u) v.push_back(0);
    return v;
}

std::vector<std::vector<uint8_t>> buildCorpus()
{
    return {
        seedWav(1, 1, 8, 16),
        seedWav(1, 2, 16, 16),
        seedWav(3, 1, 32, 8),
        seedWav(1, 1, 24, 8),                  // unsupported width, still a WAV
        seedAiff(nullptr, 8, 16, 0),
        seedAiff(nullptr, 16, 16, 0),
        seedAiff(nullptr, 24, 8, 2),
        seedAiff(nullptr, 32, 8, 0),
        seedAiff("sowt", 16, 16, 0),
        seedAiff("fl32", 32, 8, 0),
        seedAiff("NONE", 16, 8, 0),
        {},
        {'R', 'I', 'F', 'F'},
    };
}

// Chunked binary formats break on their LENGTH fields, so the mutator aims
// there: sizes, counts and offsets, biased to the boundary values that wrap.
const uint8_t kInterestingBytes[] = {
    0x00, 0x01, 0x02, 0x08, 0x10, 0x7F, 0x80, 0xFE, 0xFF,
    'R', 'I', 'F', 'W', 'A', 'V', 'E', 'F', 'O', 'M', 'C', 'S', 'N', 'D', 'd', 't', 'a', 'f', 'm',
};

std::vector<uint8_t> mutate(std::vector<uint8_t> v, std::mt19937& rng)
{
    const int op = static_cast<int>(rng() % 6);
    const uint8_t c = kInterestingBytes[rng() % (sizeof(kInterestingBytes))];
    switch (op) {
    case 0: if (!v.empty()) v[rng() % v.size()] = c; break;
    case 1: v.insert(v.begin() + (v.empty() ? 0 : rng() % v.size()), c); break;
    case 2: if (!v.empty()) v.erase(v.begin() + rng() % v.size()); break;
    case 3: if (!v.empty()) v.resize(rng() % v.size()); break;
    case 4:
        // Overwrite a whole 32-bit field — the shape of a crafted chunk size,
        // frame count or SSND offset.
        if (v.size() >= 4) {
            const size_t at = rng() % (v.size() - 3);
            for (int i = 0; i < 4; ++i) v[at + i] = kInterestingBytes[rng() % sizeof(kInterestingBytes)];
        }
        break;
    case 5: v.insert(v.end(), 64, c); break;
    }
    return v;
}

} // namespace

int main()
{
    const auto corpus = buildCorpus();
    for (const auto& entry : corpus)
        checkInvariants(entry.data(), entry.size());

    // Fixed seed: a failure here reproduces byte for byte.
    std::mt19937 rng(0x50434D31u);
    int inputs = 0;
    for (int round = 0; round < 20000; ++round) {
        std::vector<uint8_t> v = corpus[rng() % corpus.size()];
        const int depth = 1 + static_cast<int>(rng() % 6);
        for (int d = 0; d < depth; ++d) v = mutate(std::move(v), rng);
        checkInvariants(v.data(), v.size());
        ++inputs;
    }

    // A header that claims far more frames than the file can hold. Believing it
    // is how a 60-byte file asks for a gigabyte.
    {
        auto v = seedAiff(nullptr, 16, 4, 0);
        for (size_t i = 0; i + 4 <= v.size(); ++i) {
            if (std::memcmp(&v[i], "COMM", 4) == 0) {   // frames field is at +10
                v[i + 10] = 0xFF; v[i + 11] = 0xFF; v[i + 12] = 0xFF; v[i + 13] = 0xFF;
                break;
            }
        }
        const auto a = pom1::parseAiffPcm(v.data(), v.size());
        checkOneResult(a, v.size());
        assert(!a.ok || a.mono.size() <= v.size());
    }

    std::printf("pcm_file_fuzz_smoke: OK (%zu corpus + %d mutated inputs)\n",
                corpus.size(), inputs);
    return 0;
}

#endif
