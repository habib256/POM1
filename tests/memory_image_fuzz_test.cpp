// Fuzz target + bounded PR smoke for pom1::parseMemoryImage.
//
// The loader is the widest hostile-input surface POM1 has: every File > Load,
// every --load, every DevBench Wozmon-hex run feeds it a file a user picked.
// Now that it is pure — bytes in, a described result out — it can be fuzzed
// directly, with no emulated machine, no filesystem and no log sink in the way.
//
// This file is BOTH:
//   * a libFuzzer target (`-DPOM1_FUZZERS=ON`, needs a clang that ships
//     libFuzzer — Apple clang does not), for the long ASan campaign; and
//   * a deterministic standalone driver (the default) that CTest runs on every
//     platform: it replays a corpus taken from the real shipped programs, then
//     mutates it with a FIXED seed so a failure reproduces exactly.
//
// What it checks is not merely "did not crash". A parser that survives by
// returning nonsense is no better than one that crashes, so every result is
// held to the contract in MemoryImageLoader.h:
//
//   1. no span may describe a write outside the 6502's 64 KB;
//   2. byteCount must equal the bytes actually spanned;
//   3. a rejected image carries NO writes — the no-partial-mutation promise
//      the in-place loader could not make;
//   4. an accepted image always explains itself (at least one diagnostic);
//   5. zones() agrees with writes, span for span;
//   6. the result is bounded by the input — a parser that amplifies is a DoS;
//   7. parsing is deterministic and side-effect free: same bytes, same result.

#include "MemoryImageLoader.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

// Every input is parsed under both name shapes: the extension is the one thing
// besides content that steers the dialect choice (".tur" forces the
// line-structured parser), so a fuzzer that only ever passes ".txt" never
// reaches the TurboType branch at all.
const char* const kNames[] = {"fuzz.txt", "fuzz.tur", "fuzz.hex", "noext"};

void checkOneResult(const pom1::MemoryImage& img, size_t inputSize)
{
    size_t spanned = 0;
    for (const auto& span : img.writes) {
        // (1) A 6502 has 64 KB. A span that runs off the top would make the
        //     caller wrap onto page zero.
        assert(!span.bytes.empty());
        assert(static_cast<size_t>(span.start) + span.bytes.size() <= 0x10000u);
        assert(span.end() >= span.start);
        spanned += span.bytes.size();
    }
    // (2) byteCount is what the caller reports to the user and what decides
    //     whether the page-dirty bitmap is touched at all.
    assert(spanned == static_cast<size_t>(img.byteCount));

    // (3) THE promise. A rejected image must be inert.
    if (!img.ok) {
        assert(img.writes.empty());
        assert(img.byteCount == 0);
    } else {
        // (4) Every accepted load says something — the caller logs it.
        assert(!img.diagnostics.empty());
    }

    // (5) The Memory Map reads zones(); it must not disagree with what was
    //     actually written.
    const auto zones = img.zones();
    assert(zones.size() == img.writes.size());
    for (size_t i = 0; i < zones.size(); ++i) {
        assert(zones[i].first == img.writes[i].start);
        assert(zones[i].second == img.writes[i].end());
    }

    // (6) Bounded by the input. Two hex digits are the shortest possible byte,
    //     so a file can never legitimately yield more bytes than it has
    //     characters; and a new span needs at least an address token to open
    //     it. Amplification beyond that is a denial-of-service shape, not a
    //     parse.
    assert(static_cast<size_t>(img.byteCount) <= inputSize);
    assert(img.writes.size() <= inputSize + 1);
}

// The whole contract, for one input. Shared by the libFuzzer entry point and
// the standalone driver so both hold the parser to exactly the same rules.
void checkInvariants(const uint8_t* data, size_t size)
{
    const std::string content(reinterpret_cast<const char*>(data), size);
    for (const char* name : kNames) {
        const pom1::MemoryImage first = pom1::parseMemoryImage(content, name);
        checkOneResult(first, size);

        // (7) Pure means pure: no accumulated state, no dependence on call
        //     order. Re-parsing the same bytes must give the same answer.
        const pom1::MemoryImage again = pom1::parseMemoryImage(content, name);
        assert(again.ok == first.ok);
        assert(again.format == first.format);
        assert(again.startAddress == first.startAddress);
        assert(again.hasRunAddress == first.hasRunAddress);
        assert(again.byteCount == first.byteCount);
        assert(again.writes.size() == first.writes.size());
        for (size_t i = 0; i < again.writes.size(); ++i) {
            assert(again.writes[i].start == first.writes[i].start);
            assert(again.writes[i].bytes == first.writes[i].bytes);
        }
    }
}

} // namespace

#if defined(POM1_LIBFUZZER)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    // Keep the campaign in the shape a real load has. Beyond this the parser is
    // exercising allocator throughput, not parsing logic.
    if (size > 1u << 20) return 0;
    checkInvariants(data, size);
    return 0;
}

#else

namespace {

// A corpus in the shape of the things POM1 actually loads. Deliberately small
// and literal rather than read off disk: the smoke must run identically in a
// packaging container, in CI and on a developer box with a half-checked-out
// tree, and a corpus that silently shrinks to nothing still "passes".
const char* const kCorpus[] = {
    // WOZMON, the dialect all 120 shipped dumps use.
    "0280: A9 0B 8D 02 B0 A9 1E 8D\n0288: 03 B0 A2 00\n0280R\n",
    // ...with the ':' group separator every 8th byte (mandelbrot-65 et al.).
    "0280:4C 5F 03 2E 2E 2C 27 5E:3D 2B 20 21\n",
    // ...and the merged tokens that line joining produces.
    "0200: AA ED0300: BB CC\nFFE2B3R\n",
    // TurboType: address on its own line, ':' OPENS the data.
    "0300\n:D8A2FF9AA92A851A204604A97C8518A9\nX\n015ER\n",
    "T\n0300\n:D8A2FF9AA92A851A204604A97C8518A9\nX\n015ER\n",
    // Intel HEX, the second '.hex' dialect, detected by shape.
    ":10028000A90B8D02B0A91E8D03B0A200BDBC020057\n:00000001FF\n",
    // Intel HEX behind the comment lines converters emit.
    "; built by srec_cat\n:10028000A90B8D02B0A91E8D03B0A200BDBC020057\n",
    // Degenerate but legal shapes.
    "// only a comment\n",
    "0300R\n",
    "FFFE: AA BB CC DD\n",
    "",
};

// Structured mutations: the ones that historically broke this parser are all
// about the punctuation that steers it, not about random bytes.
const char kInterestingChars[] = ":RrTtXx#;/\n\r 0123456789ABCDEFabcdef";

std::string mutate(std::string s, std::mt19937& rng)
{
    const int op = static_cast<int>(rng() % 5);
    const char c = kInterestingChars[rng() % (sizeof(kInterestingChars) - 1)];
    switch (op) {
    case 0:                                        // insert
        s.insert(s.empty() ? 0 : rng() % (s.size() + 1), 1, c);
        break;
    case 1:                                        // overwrite
        if (!s.empty()) s[rng() % s.size()] = c;
        break;
    case 2:                                        // delete
        if (!s.empty()) s.erase(rng() % s.size(), 1);
        break;
    case 3:                                        // truncate
        if (!s.empty()) s.resize(rng() % s.size());
        break;
    case 4:                                        // splice a second corpus entry
        s += kCorpus[rng() % (sizeof(kCorpus) / sizeof(kCorpus[0]))];
        break;
    }
    return s;
}

} // namespace

int main()
{
    constexpr int kCorpusCount = static_cast<int>(sizeof(kCorpus) / sizeof(kCorpus[0]));

    // Every seed input, unmutated: the corpus itself must hold the contract.
    for (const char* entry : kCorpus)
        checkInvariants(reinterpret_cast<const uint8_t*>(entry), std::strlen(entry));

    // Fixed seed — a failure here reproduces byte for byte, which is the whole
    // point of running a fuzzer as a PR gate rather than as a lottery.
    std::mt19937 rng(0x50 << 16 | 0x4D31);
    int inputs = 0;
    for (int round = 0; round < 20000; ++round) {
        std::string s = kCorpus[rng() % kCorpusCount];
        const int depth = 1 + static_cast<int>(rng() % 6);
        for (int d = 0; d < depth; ++d) s = mutate(std::move(s), rng);
        checkInvariants(reinterpret_cast<const uint8_t*>(s.data()), s.size());
        ++inputs;
    }

    // Pathological shapes a mutator will not stumble on but a user can produce:
    // a very long line, an address token far longer than four digits, and a
    // file that is nothing but separators.
    const std::string longLine = "0300:" + std::string(200000, 'A');
    checkInvariants(reinterpret_cast<const uint8_t*>(longLine.data()), longLine.size());
    const std::string longAddr = std::string(70000, 'F') + ":AA\n";
    checkInvariants(reinterpret_cast<const uint8_t*>(longAddr.data()), longAddr.size());
    const std::string colons(200000, ':');
    checkInvariants(reinterpret_cast<const uint8_t*>(colons.data()), colons.size());
    const std::string newlines(200000, '\n');
    checkInvariants(reinterpret_cast<const uint8_t*>(newlines.data()), newlines.size());

    // An address token no 16-bit bus can hold.
    //
    // In the TurboType parser this used to reach strtol, whose saturation
    // differs between a 64-bit `long` (macOS, Linux) and a 32-bit one
    // (Windows): the same line put three bytes into PAGE ZERO on one platform
    // and dropped them on the other. There is no merged-token rule on that
    // path, so the line is now refused outright.
    {
        const std::string wide = "100000000\n:AABBCC\n";
        const auto tur = pom1::parseMemoryImage(wide, "x.tur");
        assert(tur.writes.empty());
        assert(!tur.ok);

        // The joined WOZMON parser reads the same token as its DOCUMENTED
        // merged data+address ("100000000:" = data 10 00, then address $0000),
        // which is a rule, not an accident — so it keeps writing, and the
        // trailing nibble is reported rather than swallowed.
        const auto txt = pom1::parseMemoryImage(wide, "x.txt");
        assert(txt.ok);
        assert(txt.startAddress == 0x0000);
        bool warned = false;
        for (const auto& d : txt.diagnostics)
            warned |= d.severity == pom1::MemoryImageDiagnostic::Severity::Warning;
        assert(warned);
    }

    // Past the size cap the answer is a refusal, not an allocation.
    {
        const std::string huge(pom1::kMaxMemoryImageBytes + 1, 'A');
        const auto img = pom1::parseMemoryImage(huge, "huge.txt");
        assert(!img.ok);
        assert(img.writes.empty());
        assert(img.diagnostics.size() == 1);
        assert(img.diagnostics[0].severity == pom1::MemoryImageDiagnostic::Severity::Error);
    }

    std::printf("memory_image_fuzz_smoke: OK (%d corpus + %d mutated inputs)\n",
                kCorpusCount, inputs);
    return 0;
}

#endif
