// Fuzz target + bounded PR smoke for the snapshot pre-flight.
//
// A snapshot is the largest structured binary POM1 reads, it arrives from
// File > Load snapshot and --load-snapshot (files POM1 did not write), and the
// same blobs are replayed by rewind many times a second. It is also the format
// whose failure mode is the nastiest: applying one writes straight into the
// live machine, so anything the gate lets through and the apply pass then
// rejects leaves a half-restored hybrid.
//
// Same dual shape as the other two fuzzers: deterministic driver by default
// (Apple clang ships no libFuzzer), LLVMFuzzerTestOneInput under POM1_FUZZERS.
//
// The contract:
//   1. the verdict is total — validate() true implies outline() ok, and a
//      rejected outline reports no sections at all;
//   2. every reported section lies inside the buffer, offsets and lengths both
//      — the apply pass indexes the buffer with them;
//   3. sections tile the file in order without overlapping;
//   4. a refusal always explains itself;
//   5. validation is deterministic and side-effect free.

#include "SnapshotIO.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

void checkInvariants(const uint8_t* data, size_t size)
{
    std::string error;
    const bool valid = pom1::validateSnapshot(data, size, error);
    const pom1::SnapshotOutline o = pom1::outlineSnapshot(data, size);

    // (1) The two must agree in one direction: a snapshot cannot be valid
    //     without being structurally sound in the first place.
    if (valid) assert(o.ok);
    // (4) ...and a refusal is never mute.
    if (!valid) assert(!error.empty());

    if (!o.ok) {
        assert(o.sections.empty());
        assert(!o.error.empty());
        return;
    }

    // (2) + (3) Every section must be indexable, and they must tile the file in
    //     order. An overlap would mean the apply pass reads one section's bytes
    //     as another's.
    size_t at = sizeof(pom1::kSnapshotMagic) + 4 + 4;
    for (const auto& s : o.sections) {
        assert(s.payloadOffset >= at + pom1::kSectionNameLen + 4);
        assert(s.payloadOffset <= size);
        assert(static_cast<uint64_t>(s.payloadOffset) + s.length <= size);
        assert(s.name.size() <= pom1::kSectionNameLen);
        at = s.payloadOffset + s.length;
    }
    assert(at == size);        // no unaccounted tail

    // (5) Pure means pure.
    std::string again;
    assert(pom1::validateSnapshot(data, size, again) == valid);
    assert(again == error);
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

void putU32(std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xFF);
}

void putSection(std::vector<uint8_t>& v, const char* name, size_t payloadLen)
{
    for (size_t i = 0; i < pom1::kSectionNameLen; ++i)
        v.push_back(i < std::strlen(name) ? uint8_t(name[i]) : 0);
    putU32(v, uint32_t(payloadLen));
    for (size_t i = 0; i < payloadLen; ++i) v.push_back(uint8_t(i * 31));
}

std::vector<uint8_t> header(uint32_t version)
{
    std::vector<uint8_t> v(pom1::kSnapshotMagic,
                           pom1::kSnapshotMagic + sizeof(pom1::kSnapshotMagic));
    putU32(v, version);
    putU32(v, 0);
    return v;
}

std::vector<std::vector<uint8_t>> buildCorpus()
{
    std::vector<std::vector<uint8_t>> out;

    // The shape POM1 actually writes.
    {
        auto v = header(pom1::kSnapshotVersion);
        putSection(v, "CPU", 17);
        putSection(v, "MEM", pom1::kMemSectionLen);
        putSection(v, "FLAGS", 4);
        putSection(v, "GEN2VID", 4 + 1 + 8 + 4 + 4);
        putSection(v, "SCREEN", 960);
        out.push_back(std::move(v));
    }
    // A pre-v6 machine state — still supported, shorter MEM.
    {
        auto v = header(5);
        putSection(v, "MEM", pom1::kMemSectionLenV5);
        putSection(v, "FLAGS", 2);
        out.push_back(std::move(v));
    }
    // An unknown section: forward-compat, must outline cleanly.
    {
        auto v = header(pom1::kSnapshotVersion);
        putSection(v, "MEM", pom1::kMemSectionLen);
        putSection(v, "FUTURE", 32);
        out.push_back(std::move(v));
    }
    out.push_back(header(pom1::kSnapshotVersion));
    out.push_back({});
    out.push_back({'P', 'O', 'M', '1', 'S', 'N', 'A', 'P'});
    return out;
}

// Section lengths are 32-bit fields, and length fields are where chunked
// formats break — so the mutator aims squarely at them.
const uint8_t kInterestingBytes[] = {
    0x00, 0x01, 0x04, 0x10, 0x7F, 0x80, 0xFF,
    'P', 'O', 'M', '1', 'S', 'N', 'A', 'C', 'U', 'E', 'F', 'L', 'G', 'V', 'I', 'D',
};

std::vector<uint8_t> mutate(std::vector<uint8_t> v, std::mt19937& rng)
{
    const int op = static_cast<int>(rng() % 6);
    const uint8_t c = kInterestingBytes[rng() % sizeof(kInterestingBytes)];
    switch (op) {
    case 0: if (!v.empty()) v[rng() % v.size()] = c; break;
    case 1: v.insert(v.begin() + (v.empty() ? 0 : rng() % v.size()), c); break;
    case 2: if (!v.empty()) v.erase(v.begin() + rng() % v.size()); break;
    case 3: if (!v.empty()) v.resize(rng() % v.size()); break;   // truncation
    case 4:
        // A whole 32-bit field: the shape of a forged section length.
        if (v.size() >= 4) {
            const size_t at = rng() % (v.size() - 3);
            for (int i = 0; i < 4; ++i)
                v[at + i] = kInterestingBytes[rng() % sizeof(kInterestingBytes)];
        }
        break;
    case 5: v.insert(v.end(), 32, c); break;
    }
    return v;
}

} // namespace

int main()
{
    const auto corpus = buildCorpus();
    for (const auto& entry : corpus) checkInvariants(entry.data(), entry.size());

    // Every truncation of the real shape, byte by byte: the exact family that
    // produced the hybrid machine, and cheap enough to enumerate exhaustively
    // rather than sample.
    for (size_t cut = 0; cut <= corpus[0].size(); ++cut)
        checkInvariants(corpus[0].data(), cut);

    std::mt19937 rng(0x534E4150u);   // "SNAP"
    int inputs = 0;
    for (int round = 0; round < 20000; ++round) {
        std::vector<uint8_t> v = corpus[rng() % corpus.size()];
        const int depth = 1 + static_cast<int>(rng() % 6);
        for (int d = 0; d < depth; ++d) v = mutate(std::move(v), rng);
        checkInvariants(v.data(), v.size());
        ++inputs;
    }

    std::printf("snapshot_fuzz_smoke: OK (%zu corpus + %zu truncations + %d mutated)\n",
                corpus.size(), corpus[0].size() + 1, inputs);
    return 0;
}

#endif
