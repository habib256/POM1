// pom1::outlineSnapshot / validateSnapshot — the structural gate a machine
// state must pass BEFORE any of it is applied.
//
// Applying a snapshot writes straight into the live machine, section by
// section. A file that went bad halfway therefore used to leave a HYBRID and
// report a clean failure: truncating a real 116 933-byte snapshot to 200 bytes
// restored the CPU section — program counter and all — over RAM that was never
// replaced. The caller saw `false`; the machine had silently changed, and a PC
// pointing into a program that is not in memory is a machine that will run
// garbage. Rewind replays these same blobs, so it inherited the same hazard.
//
// These functions are pure — bytes in, a verdict out — so the gate is testable
// without a Memory, a CPU or a file.
//
// Covered:
//   §1  a well-formed snapshot outlines into its sections, in order;
//   §2  magic and version are checked, and a future version is refused rather
//       than half-understood;
//   §3  truncation is caught wherever it lands: mid-header, mid-payload, and
//       at a section boundary with a trailing fragment;
//   §4  a forged section length is rejected against the bytes REMAINING, so it
//       cannot wrap past the guard;
//   §5  the MEM section's two legal lengths, and nothing else;
//   §6  GEN2VID's event count is bounded by the payload that is actually
//       there — the count drives a reserve();
//   §7  a rejected outline reports no sections at all.

#include "SnapshotIO.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using pom1::outlineSnapshot;
using pom1::SnapshotOutline;
using pom1::validateSnapshot;

namespace {

void putU32(std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xFF);
}

void putSection(std::vector<uint8_t>& v, const char* name,
                const std::vector<uint8_t>& payload)
{
    for (size_t i = 0; i < pom1::kSectionNameLen; ++i)
        v.push_back(i < std::strlen(name) ? uint8_t(name[i]) : 0);
    putU32(v, uint32_t(payload.size()));
    v.insert(v.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> header(uint32_t version = pom1::kSnapshotVersion)
{
    std::vector<uint8_t> v(pom1::kSnapshotMagic,
                           pom1::kSnapshotMagic + sizeof(pom1::kSnapshotMagic));
    putU32(v, version);
    putU32(v, 0);          // flags, reserved
    return v;
}

// A minimal but structurally complete machine state: CPU, a full-length MEM,
// FLAGS. Enough for the validator to have real work to do.
std::vector<uint8_t> wellFormed()
{
    std::vector<uint8_t> v = header();
    putSection(v, "CPU", std::vector<uint8_t>(17, 0));
    putSection(v, "MEM", std::vector<uint8_t>(pom1::kMemSectionLen, 0));
    putSection(v, "FLAGS", std::vector<uint8_t>(4, 0));
    return v;
}

bool rejects(const std::vector<uint8_t>& v)
{
    std::string error;
    if (validateSnapshot(v.data(), v.size(), error)) return false;
    assert(!error.empty());        // a refusal always explains itself
    return true;
}

} // namespace

int main()
{
    // -----------------------------------------------------------------
    // §1 A well-formed snapshot outlines into its sections, in file order,
    //    with usable payload offsets.
    // -----------------------------------------------------------------
    {
        const auto v = wellFormed();
        const SnapshotOutline o = outlineSnapshot(v.data(), v.size());
        assert(o.ok);
        assert(o.version == pom1::kSnapshotVersion);
        assert(o.sections.size() == 3);
        assert(o.sections[0].name == "CPU");
        assert(o.sections[1].name == "MEM");
        assert(o.sections[2].name == "FLAGS");
        assert(o.find("MEM") != nullptr);
        assert(o.find("MEM")->length == pom1::kMemSectionLen);
        assert(o.find("GEN2VID") == nullptr);
        // The offsets must actually point at the payloads.
        assert(o.sections[1].payloadOffset + o.sections[1].length <= v.size());

        std::string error;
        assert(validateSnapshot(v.data(), v.size(), error));
        assert(error.empty());
    }

    // -----------------------------------------------------------------
    // §2 Magic and version. A version from the future is refused outright —
    //    half-understanding a format is how a restore silently drops state.
    // -----------------------------------------------------------------
    {
        auto bad = wellFormed();
        bad[0] = 'X';
        assert(rejects(bad));

        auto future = header(pom1::kSnapshotVersion + 1);
        putSection(future, "MEM", std::vector<uint8_t>(pom1::kMemSectionLen, 0));
        assert(rejects(future));

        auto zero = header(0);
        putSection(zero, "MEM", std::vector<uint8_t>(pom1::kMemSectionLen, 0));
        assert(rejects(zero));

        // Nothing at all, and a header with no sections behind it.
        assert(rejects({}));
        assert(rejects(header()));
    }

    // -----------------------------------------------------------------
    // §3 Truncation, wherever it lands. This is what a damaged file or a
    //    clipped rewind blob actually looks like.
    // -----------------------------------------------------------------
    {
        const auto full = wellFormed();

        // Mid-payload: the case that produced the hybrid machine.
        assert(rejects({full.begin(), full.begin() + 200}));
        // Mid-header, just past a section boundary.
        assert(rejects({full.begin(), full.begin() + 16 + 8 + 4 + 17 + 3}));
        // One byte short of complete.
        assert(rejects({full.begin(), full.end() - 1}));
        // A trailing fragment too small to be a section header must be called
        // out, not quietly treated as "no more sections".
        auto stub = full;
        stub.push_back(0x00);
        assert(rejects(stub));
    }

    // -----------------------------------------------------------------
    // §4 A forged length is checked against the bytes REMAINING. Comparing
    //    offset+length instead would wrap on a 32-bit size_t and slip past.
    // -----------------------------------------------------------------
    {
        auto v = header();
        putSection(v, "CPU", std::vector<uint8_t>(17, 0));
        // Overwrite CPU's declared length with 0xFFFFFFFF.
        const size_t lenAt = 16 + pom1::kSectionNameLen;
        v[lenAt] = 0xFF; v[lenAt + 1] = 0xFF; v[lenAt + 2] = 0xFF; v[lenAt + 3] = 0xFF;
        assert(rejects(v));
    }

    // -----------------------------------------------------------------
    // §5 MEM has exactly two legal lengths: the v6 layout and the pre-v6 one.
    //    A shorter declared length would make the apply pass consume bytes
    //    belonging to the NEXT section and load garbage into RAM — while
    //    reporting success.
    // -----------------------------------------------------------------
    {
        for (uint32_t len : {pom1::kMemSectionLen, pom1::kMemSectionLenV5}) {
            auto v = header();
            putSection(v, "MEM", std::vector<uint8_t>(len, 0));
            std::string error;
            assert(validateSnapshot(v.data(), v.size(), error));
        }
        for (uint32_t len : {0u, 16u, pom1::kMemSectionLenV5 - 1,
                             pom1::kMemSectionLen + 1}) {
            auto v = header();
            putSection(v, "MEM", std::vector<uint8_t>(len, 0));
            putSection(v, "FLAGS", std::vector<uint8_t>(4, 0));
            assert(rejects(v));
        }
    }

    // -----------------------------------------------------------------
    // §6 GEN2VID (v5+) is a count-then-elements journal and the count drives a
    //    reserve(). Bound it by the payload that is really there: each event is
    //    emuCycle(8) + kind(1) + value(1).
    // -----------------------------------------------------------------
    {
        auto payload = [](uint32_t events, uint32_t declared) {
            std::vector<uint8_t> p(4 + 1 + 8, 0);      // state + 50 Hz + cycle
            putU32(p, declared);
            for (uint32_t i = 0; i < events; ++i) p.insert(p.end(), 10, 0);
            p.insert(p.end(), 4, 0);                   // frame-start state
            return p;
        };

        auto good = header();
        putSection(good, "GEN2VID", payload(3, 3));
        std::string error;
        assert(validateSnapshot(good.data(), good.size(), error));

        // A count the section cannot possibly hold.
        auto forged = header();
        putSection(forged, "GEN2VID", payload(3, 1000000));
        assert(rejects(forged));

        // Pre-v5 snapshots carry no journal at all, so the check must not fire.
        auto v4 = header(4);
        putSection(v4, "GEN2VID", std::vector<uint8_t>(4 + 1 + 8, 0));
        assert(validateSnapshot(v4.data(), v4.size(), error));
    }

    // -----------------------------------------------------------------
    // §7 A rejected outline reports NO sections. A caller must not be able to
    //    act on a partial reading of a file that was refused.
    // -----------------------------------------------------------------
    {
        const auto full = wellFormed();
        const std::vector<uint8_t> cut(full.begin(), full.begin() + 200);
        const SnapshotOutline o = outlineSnapshot(cut.data(), cut.size());
        assert(!o.ok);
        assert(o.sections.empty());
        assert(!o.error.empty());
        assert(o.find("CPU") == nullptr);
    }

    std::printf("snapshot_validate_smoke: OK\n");
    return 0;
}
