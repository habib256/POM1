// pom1::parseWavPcm / parseAiffPcm — the two cassette containers POM1 decodes
// itself, as pure functions.
//
// Lifted out of CassetteDevice, which read the file, parsed it, mixed it and
// stored the result in one pass. Neither parser could be tested without a whole
// audio device, and the AIFF one — POM1's own, because miniaudio has no AIFF
// backend and AIFF is what Uncle Bernie's ACIace synthesiser emits — was gated
// by exactly one end-to-end test.
//
// Covered:
//   §1  WAV 8-bit is UNSIGNED, AIFF 8-bit is SIGNED — the one width where the
//       two formats disagree, and a silent full-scale DC offset if confused;
//   §2  WAV 16-bit and float32, and the channel mixdown;
//   §3  AIFF 16/24/32-bit big-endian, and AIFF-C `sowt` (byte-swapped);
//   §4  the 80-bit extended sample rate, including the absurd-value refusal;
//   §5  SSND's alignment offset, and a malformed one;
//   §6  COMM's frame count wins when it is the SMALLER of the two;
//   §7  chunk sizes are checked against the REMAINING bytes, so a crafted size
//       cannot wrap past the guard on a 32-bit size_t;
//   §8  the frame cap — the one the MP3 path always had and these two never
//       did — truncates and SAYS so;
//   §9  the file-size cap refuses rather than allocates;
//   §10 a rejected parse yields no samples, and unsupported widths are refused
//       BEFORE any frame is decoded;
//   §11 the sample rate must be plausible — found by fuzzing;
//   §12 NaN and infinity in a float container become silence — found by fuzzing;
//   §13 …and a non-finite sample BUILT from finite channels by the mixdown —
//       also found by fuzzing, and the per-channel scrub alone missed it.

#include "PcmFile.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using pom1::parseAiffPcm;
using pom1::parseWavPcm;
using pom1::PcmAudio;

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

// A minimal RIFF/WAVE around `data`. `format` is 1 (PCM) or 3 (float).
std::vector<uint8_t> wav(uint16_t format, uint16_t channels, uint32_t rate,
                         uint16_t bits, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> v;
    putTag(v, "RIFF"); putLe32(v, 0); putTag(v, "WAVE");
    putTag(v, "fmt "); putLe32(v, 16);
    putLe16(v, format); putLe16(v, channels); putLe32(v, rate);
    putLe32(v, rate * channels * (bits / 8u));          // byte rate
    putLe16(v, uint16_t(channels * (bits / 8u)));       // block align
    putLe16(v, bits);
    putTag(v, "data"); putLe32(v, uint32_t(data.size()));
    v.insert(v.end(), data.begin(), data.end());
    if (data.size() & 1u) v.push_back(0);
    return v;
}

// The 80-bit IEEE extended encoding of a positive integer rate: no compiler
// exposes the type, so the test builds what the parser must read.
void putExtended(std::vector<uint8_t>& v, uint32_t rate)
{
    uint64_t mantissa = rate;
    int exp = 16383 + 63;
    while (mantissa && !(mantissa & (1ull << 63))) { mantissa <<= 1; --exp; }
    putBe16(v, uint16_t(exp));
    for (int i = 7; i >= 0; --i) v.push_back(uint8_t((mantissa >> (8 * i)) & 0xFF));
}

// A minimal FORM/AIFF (or AIFC when `compression` is non-null) around `data`.
std::vector<uint8_t> aiff(uint16_t channels, uint32_t frames, uint16_t bits,
                          uint32_t rate, const std::vector<uint8_t>& data,
                          const char* compression = nullptr,
                          uint32_t ssndOffset = 0)
{
    std::vector<uint8_t> v;
    putTag(v, "FORM"); putBe32(v, 0); putTag(v, compression ? "AIFC" : "AIFF");

    std::vector<uint8_t> comm;
    putBe16(comm, channels); putBe32(comm, frames); putBe16(comm, bits);
    putExtended(comm, rate);
    if (compression) { putTag(comm, compression); comm.push_back(0); comm.push_back(0); }
    putTag(v, "COMM"); putBe32(v, uint32_t(comm.size()));
    v.insert(v.end(), comm.begin(), comm.end());
    if (comm.size() & 1u) v.push_back(0);

    std::vector<uint8_t> ssnd;
    putBe32(ssnd, ssndOffset); putBe32(ssnd, 0);        // offset, blockSize
    ssnd.insert(ssnd.end(), ssndOffset, 0);             // alignment padding
    ssnd.insert(ssnd.end(), data.begin(), data.end());
    putTag(v, "SSND"); putBe32(v, uint32_t(ssnd.size()));
    v.insert(v.end(), ssnd.begin(), ssnd.end());
    if (ssnd.size() & 1u) v.push_back(0);
    return v;
}

bool near(float a, float b) { return std::fabs(a - b) < 1.0f / 200.0f; }

void assertRejected(const PcmAudio& a)
{
    assert(!a.ok);
    assert(a.mono.empty());          // nothing partial escapes
    assert(!a.error.empty());        // ...and it says why
}

} // namespace

int main()
{
    // -----------------------------------------------------------------
    // §1 8-bit: WAV is UNSIGNED (128 = silence), AIFF is SIGNED (0 = silence).
    //
    // The one width where the two formats disagree. Reading an AIFF byte as
    // unsigned would put every sample a full half-scale off — a DC offset the
    // pulse decoder would read as one long unbroken level.
    // -----------------------------------------------------------------
    {
        const PcmAudio w = parseWavPcm(nullptr, 0);  // guard: no deref on empty
        assert(!w.ok);

        const auto wv = wav(1, 1, 44100, 8, {128, 255, 0, 64});
        const PcmAudio a = parseWavPcm(wv.data(), wv.size());
        assert(a.ok && a.sampleRate == 44100 && a.mono.size() == 4);
        assert(near(a.mono[0], 0.0f));        // 128 is WAV silence
        assert(near(a.mono[1], 0.9921875f));
        assert(near(a.mono[2], -1.0f));

        const auto af = aiff(1, 4, 8, 44100, {0, 127, 0x80, 64});
        const PcmAudio b = parseAiffPcm(af.data(), af.size());
        assert(b.ok && b.sampleRate == 44100 && b.mono.size() == 4);
        assert(near(b.mono[0], 0.0f));        // 0 is AIFF silence
        assert(near(b.mono[1], 0.9921875f));
        assert(near(b.mono[2], -1.0f));       // 0x80 = -128, not +128
    }

    // -----------------------------------------------------------------
    // §2 WAV 16-bit and float32, plus the stereo mixdown: a cassette carries
    //    ONE signal, so a stereo rip of one is two copies to average.
    // -----------------------------------------------------------------
    {
        std::vector<uint8_t> d;
        putLe16(d, uint16_t(int16_t(16384)));
        putLe16(d, uint16_t(int16_t(-16384)));
        const auto v = wav(1, 1, 22050, 16, d);
        const PcmAudio a = parseWavPcm(v.data(), v.size());
        assert(a.ok && a.sampleRate == 22050 && a.mono.size() == 2);
        assert(near(a.mono[0], 0.5f) && near(a.mono[1], -0.5f));
    }
    {
        // Stereo: one frame of (+1.0, -1.0) must average to silence.
        std::vector<uint8_t> d;
        const float l = 1.0f, r = -1.0f;
        d.resize(8);
        std::memcpy(d.data(), &l, 4);
        std::memcpy(d.data() + 4, &r, 4);
        const auto v = wav(3, 2, 48000, 32, d);
        const PcmAudio a = parseWavPcm(v.data(), v.size());
        assert(a.ok && a.mono.size() == 1);
        assert(near(a.mono[0], 0.0f));
    }

    // -----------------------------------------------------------------
    // §3 AIFF is big-endian throughout; AIFF-C 'sowt' is the same PCM with the
    //    sample bytes swapped, which some converters emit when re-wrapping
    //    ACIace output.
    // -----------------------------------------------------------------
    {
        std::vector<uint8_t> be;
        putBe16(be, uint16_t(int16_t(16384)));
        const auto v = aiff(1, 1, 16, 44100, be);
        const PcmAudio a = parseAiffPcm(v.data(), v.size());
        assert(a.ok && near(a.mono[0], 0.5f));

        std::vector<uint8_t> le;
        putLe16(le, uint16_t(int16_t(16384)));
        const auto v2 = aiff(1, 1, 16, 44100, le, "sowt");
        const PcmAudio b = parseAiffPcm(v2.data(), v2.size());
        assert(b.ok && near(b.mono[0], 0.5f));
    }
    {
        // 24-bit, big-endian, negative: sign extension from bit 23.
        const std::vector<uint8_t> d = {0xFF, 0x00, 0x00};   // -65536 of 8388608
        const auto v = aiff(1, 1, 24, 44100, d);
        const PcmAudio a = parseAiffPcm(v.data(), v.size());
        assert(a.ok && a.mono[0] < 0.0f && near(a.mono[0], -0.0078125f));
    }
    {
        // 32-bit big-endian PCM.
        std::vector<uint8_t> d;
        putBe32(d, uint32_t(int32_t(1073741824)));           // +0.5
        const auto v = aiff(1, 1, 32, 44100, d);
        const PcmAudio a = parseAiffPcm(v.data(), v.size());
        assert(a.ok && near(a.mono[0], 0.5f));
    }
    {
        // AIFF-C fl32 — big-endian IEEE floats.
        std::vector<uint8_t> d(4);
        const float f = 0.25f;
        uint8_t le[4];
        std::memcpy(le, &f, 4);
        d[0] = le[3]; d[1] = le[2]; d[2] = le[1]; d[3] = le[0];
        const auto v = aiff(1, 1, 32, 44100, d, "fl32");
        const PcmAudio a = parseAiffPcm(v.data(), v.size());
        assert(a.ok && near(a.mono[0], 0.25f));

        // fl32 at any other width is a contradiction.
        const auto bad = aiff(1, 1, 16, 44100, {0, 0}, "fl32");
        assertRejected(parseAiffPcm(bad.data(), bad.size()));

        // ...and a compression POM1 cannot decode is refused, not guessed at.
        const auto ulaw = aiff(1, 1, 16, 44100, {0, 0}, "ulaw");
        assertRejected(parseAiffPcm(ulaw.data(), ulaw.size()));
    }

    // -----------------------------------------------------------------
    // §4 The 80-bit extended sample rate — the field that makes AIFF annoying
    //    to parse by hand. A rate that decodes to something absurd is treated
    //    as "not a rate", which fails the file rather than dividing by it.
    // -----------------------------------------------------------------
    {
        for (uint32_t rate : {8000u, 22050u, 44100u, 48000u, 96000u}) {
            const auto v = aiff(1, 1, 16, rate, {0, 0});
            const PcmAudio a = parseAiffPcm(v.data(), v.size());
            assert(a.ok && a.sampleRate == rate);
        }
        // Exponent 0x7FFF (Inf/NaN) is not a sample rate.
        auto v = aiff(1, 1, 16, 44100, {0, 0});
        const size_t rateAt = 12 + 8 + 6;    // FORM hdr + COMM hdr + ch/frames/bits
        v[rateAt] = 0x7F; v[rateAt + 1] = 0xFF;
        assertRejected(parseAiffPcm(v.data(), v.size()));
    }

    // -----------------------------------------------------------------
    // §5 SSND leads with offset + blockSize, and the frames start after
    //    `offset` bytes of alignment padding. An offset that runs past the
    //    chunk is malformed, not a reason to read outside it.
    // -----------------------------------------------------------------
    {
        std::vector<uint8_t> d;
        putBe16(d, uint16_t(int16_t(16384)));
        const auto v = aiff(1, 1, 16, 44100, d, nullptr, /*ssndOffset=*/6);
        const PcmAudio a = parseAiffPcm(v.data(), v.size());
        assert(a.ok && near(a.mono[0], 0.5f));   // padding skipped, not decoded

        // Now corrupt the offset so it exceeds the chunk.
        auto bad = v;
        for (size_t i = 0; i + 4 <= bad.size(); ++i) {
            if (std::memcmp(&bad[i], "SSND", 4) == 0) {
                bad[i + 8] = 0xFF; bad[i + 9] = 0xFF;
                bad[i + 10] = 0xFF; bad[i + 11] = 0xFF;
                break;
            }
        }
        assertRejected(parseAiffPcm(bad.data(), bad.size()));
    }

    // -----------------------------------------------------------------
    // §6 COMM's frame count wins when it is the SMALLER of the two: trailing
    //    garbage after the last frame is common in synthesised files.
    // -----------------------------------------------------------------
    {
        std::vector<uint8_t> d;
        for (int i = 0; i < 8; ++i) putBe16(d, 0);
        const auto few = aiff(1, /*frames=*/3, 16, 44100, d);
        const PcmAudio a = parseAiffPcm(few.data(), few.size());
        assert(a.ok && a.mono.size() == 3);      // COMM 3 < SSND's 8

        // ...but a COMM claiming MORE than the data holds must not be believed.
        const auto many = aiff(1, /*frames=*/9999, 16, 44100, d);
        const PcmAudio b = parseAiffPcm(many.data(), many.size());
        assert(b.ok && b.mono.size() == 8);
    }

    // -----------------------------------------------------------------
    // §7 A chunk size is checked against the bytes REMAINING, never
    //    offset+size: on a 32-bit size_t (wasm32) a crafted size near
    //    0xFFFFFFFF wraps the sum below the file length and slips past.
    // -----------------------------------------------------------------
    {
        auto v = wav(1, 1, 44100, 8, {128, 128, 128, 128});
        for (size_t i = 0; i + 4 <= v.size(); ++i) {
            if (std::memcmp(&v[i], "data", 4) == 0) {
                v[i + 4] = 0xFF; v[i + 5] = 0xFF; v[i + 6] = 0xFF; v[i + 7] = 0xFF;
                break;
            }
        }
        const PcmAudio a = parseWavPcm(v.data(), v.size());
        assertRejected(a);      // the chunk is abandoned, so no data chunk found
    }

    // -----------------------------------------------------------------
    // §8 The length cap. loadMiniaudioTape() has capped MP3/FLAC/Vorbis at
    //    thirty minutes since it was written — "prevents accidental 2-hour
    //    podcast loads from chewing memory" — while these two parsers had no
    //    cap at all, so the very case that comment describes was refused as an
    //    .mp3 and accepted as a .wav. Same rule now, and it reports itself
    //    instead of silently dropping the tail.
    // -----------------------------------------------------------------
    {
        // Exercised through the maxFrames parameter: the real cap is 172.8
        // million frames, and crossing it for real would allocate the better
        // part of a gigabyte inside a unit test. The logic under test is the
        // same either way.
        std::vector<uint8_t> d(100, 128);
        const auto v = wav(1, 1, 96000, 8, d);
        const PcmAudio a = parseWavPcm(v.data(), v.size(), /*maxFrames=*/40);
        assert(a.ok);
        assert(a.mono.size() == 40);
        assert(!a.warning.empty());          // truncation is never silent

        // Under the cap, no warning at all.
        const PcmAudio b = parseWavPcm(v.data(), v.size(), /*maxFrames=*/1000);
        assert(b.ok && b.mono.size() == 100 && b.warning.empty());

        std::vector<uint8_t> ad;
        for (int i = 0; i < 20; ++i) putBe16(ad, 0);
        const auto av = aiff(1, 20, 16, 44100, ad);
        const PcmAudio c = parseAiffPcm(av.data(), av.size(), /*maxFrames=*/5);
        assert(c.ok && c.mono.size() == 5 && !c.warning.empty());
    }

    // -----------------------------------------------------------------
    // §9 Past the file-size cap the answer is a refusal, not an allocation.
    //    CassetteDevice checks the file's size before reading it; this is the
    //    backstop for every other caller.
    // -----------------------------------------------------------------
    {
        // A size, not a buffer: the parser must refuse on the length alone,
        // before it touches a single byte.
        const PcmAudio a = parseWavPcm(nullptr, pom1::kMaxPcmFileBytes + 1);
        assertRejected(a);
        const PcmAudio b = parseAiffPcm(nullptr, pom1::kMaxPcmFileBytes + 1);
        assertRejected(b);
    }

    // -----------------------------------------------------------------
    // §10 Rejections are complete. In particular an unsupported width is
    //     refused BEFORE any frame is decoded — the in-place version
    //     discovered it from inside the decode loop, having already mixed and
    //     stored every earlier frame.
    // -----------------------------------------------------------------
    {
        const auto v24 = wav(1, 1, 44100, 24, {0, 0, 0, 0, 0, 0});
        assertRejected(parseWavPcm(v24.data(), v24.size()));

        const auto vfmt = wav(/*format=*/7, 1, 44100, 16, {0, 0});
        assertRejected(parseWavPcm(vfmt.data(), vfmt.size()));

        const auto vbits = aiff(1, 1, /*bits=*/12, 44100, {0, 0});
        assertRejected(parseAiffPcm(vbits.data(), vbits.size()));

        const std::vector<uint8_t> junk = {'N', 'O', 'P', 'E'};
        assertRejected(parseWavPcm(junk.data(), junk.size()));
        assertRejected(parseAiffPcm(junk.data(), junk.size()));

        // A header with no data chunk at all.
        std::vector<uint8_t> hdrOnly;
        putTag(hdrOnly, "RIFF"); putLe32(hdrOnly, 0); putTag(hdrOnly, "WAVE");
        hdrOnly.resize(48, 0);
        assertRejected(parseWavPcm(hdrOnly.data(), hdrOnly.size()));
    }

    // -----------------------------------------------------------------
    // §11 The sample rate must be plausible — the pulse decoder divides by it.
    //
    // Found by fuzzing: the WAV parser had NO upper bound at all, while the
    // AIFF one has always been bounded by its 80-bit float decoder. A WAV could
    // declare four billion Hz and be accepted. The lower bound matters more:
    // durations are `deltaSamples * CPU_HZ / sampleRate` narrowed to uint32, and
    // at 1 Hz a 5000-sample gap already yields 5.1e9 — a conversion UBSan calls
    // "outside the range of representable values of type 'unsigned int'".
    // -----------------------------------------------------------------
    {
        for (uint32_t bad : {1u, 999u, 2000000u, 4000000000u}) {
            const auto v = wav(1, 1, bad, 8, {128, 200, 60, 128});
            assertRejected(parseWavPcm(v.data(), v.size()));
        }
        for (uint32_t good : {8000u, 44100u, 96000u, 192000u}) {
            const auto v = wav(1, 1, good, 8, {128, 200, 60, 128});
            assert(parseWavPcm(v.data(), v.size()).ok);
        }
        const auto slow = aiff(1, 1, 16, 100, {0, 0});
        assertRejected(parseAiffPcm(slow.data(), slow.size()));
    }

    // -----------------------------------------------------------------
    // §12 Float containers can carry any bit pattern, NaN and infinity
    //     included — and NOTHING downstream is prepared for them: the pulse
    //     decoder compares each sample against a threshold, and every
    //     comparison against a NaN is false. A corrupt float WAV therefore read
    //     as a flat line and was reported as "no detectable cassette signal",
    //     which tells the user nothing about what is actually wrong.
    //
    //     Also found by fuzzing. Non-finite samples become silence, are
    //     counted, and are reported.
    // -----------------------------------------------------------------
    {
        const float nan = std::nanf("");
        const float inf = HUGE_VALF;
        std::vector<uint8_t> d(12);
        std::memcpy(d.data() + 0, &nan, 4);
        std::memcpy(d.data() + 4, &inf, 4);
        const float half = 0.5f;
        std::memcpy(d.data() + 8, &half, 4);

        const auto v = wav(3, 1, 44100, 32, d);
        const PcmAudio a = parseWavPcm(v.data(), v.size());
        assert(a.ok && a.mono.size() == 3);
        assert(a.mono[0] == 0.0f && a.mono[1] == 0.0f);   // silenced
        assert(near(a.mono[2], 0.5f));                    // the good one survives
        assert(!a.warning.empty());                       // and it is never silent
        for (float x : a.mono) assert(std::isfinite(x));

        // Same for AIFF-C fl32, where the bytes are big-endian.
        std::vector<uint8_t> ad(4);
        uint8_t le[4];
        std::memcpy(le, &nan, 4);
        ad[0] = le[3]; ad[1] = le[2]; ad[2] = le[1]; ad[3] = le[0];
        const auto av = aiff(1, 1, 32, 44100, ad, "fl32");
        const PcmAudio b = parseAiffPcm(av.data(), av.size());
        assert(b.ok && b.mono.size() == 1 && b.mono[0] == 0.0f);
        assert(!b.warning.empty());
    }

    // -----------------------------------------------------------------
    // §13 A non-finite sample can be BUILT from finite ones. Scrubbing each
    //     channel is not enough: the mixdown SUMS them, and two floats near
    //     the top of the range add to infinity. Every channel passes
    //     isfinite() and the frame that comes out does not — so the parser
    //     promised to have removed what it had just created.
    //
    //     Found by the nightly fuzz job, as an AIFF-C fl32 file
    //     (pcm/crash-e086c0e6…): five samples scrubbed, and one still
    //     non-finite in the output. The fix sanitizes the MIX as well, which
    //     is why the counts below include the frame.
    // -----------------------------------------------------------------
    {
        const float huge = 3.0e38f;                 // finite; 2 x huge is not
        assert(std::isfinite(huge));
        std::vector<uint8_t> d(8);
        std::memcpy(d.data() + 0, &huge, 4);
        std::memcpy(d.data() + 4, &huge, 4);

        // WAV: one STEREO frame whose two finite channels overflow when summed.
        // Format 3 = IEEE float; format 1 would read these bytes as integers.
        const auto v = wav(3, 2, 44100, 32, d);
        const PcmAudio a = parseWavPcm(v.data(), v.size());
        assert(a.ok && a.mono.size() == 1);
        assert(std::isfinite(a.mono[0]));
        assert(a.mono[0] == 0.0f);                  // silenced, like any other
        assert(!a.warning.empty());                 // and reported

        // AIFF-C fl32, big-endian, same shape.
        std::vector<uint8_t> ad(8);
        uint8_t le[4];
        std::memcpy(le, &huge, 4);
        for (int f = 0; f < 2; ++f) {
            ad[f * 4 + 0] = le[3]; ad[f * 4 + 1] = le[2];
            ad[f * 4 + 2] = le[1]; ad[f * 4 + 3] = le[0];
        }
        const auto bv = aiff(2, 1, 32, 44100, ad, "fl32");   // 2 channels, 1 frame
        const PcmAudio c = parseAiffPcm(bv.data(), bv.size());
        assert(c.ok && c.mono.size() == 1);
        assert(std::isfinite(c.mono[0]) && c.mono[0] == 0.0f);
        assert(!c.warning.empty());
    }

    std::printf("pcm_file_smoke: OK\n");
    return 0;
}
