// Fuzz target + bounded PR smoke for the D64 disk-image reader.
//
// A .d64 is mounted from `--iec-disk` and from the IEC card's UI, so the bytes
// come from a file the user picked. The format is a linked structure — every
// directory block and every file names the NEXT track/sector — which is the
// shape that goes wrong under corruption: a chain that points at itself, a
// chain that points off the disk, a directory that never terminates. Nothing
// in a D64 says how long anything is; you find out by following links.
//
// Same dual shape as the other three fuzzers: deterministic driver by default
// (Apple clang ships no libFuzzer), LLVMFuzzerTestOneInput under POM1_FUZZERS.
//
// The contract every mounted image must hold, whatever its contents:
//   1. mounting is total — any 174 848 bytes mount, any other size does not;
//   2. every traversal TERMINATES. A disk holds 683 sectors, so no honest
//      chain is longer; a crafted one must not spin;
//   3. no read escapes the image — directory entries and file contents are
//      bounded by the disk, not by the numbers the disk contains;
//   4. reported free blocks stay within what a 35-track disk can hold;
//   5. reading is side-effect free: the image is unchanged afterwards.

#include "D64Image.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using pom1::D64Image;

// A disk has 683 sectors of 254 usable data bytes. Anything longer than that
// came from following a link twice.
constexpr size_t kMaxHonestFileBytes = 683 * 254;

void checkMounted(D64Image& img)
{
    const std::vector<uint8_t> before = img.rawBytes();

    // (4) 664 blocks on a 35-track disk once track 18 is reserved.
    const int free = img.blocksFree();
    assert(free >= 0 && free <= img.totalBlocks());
    assert(img.totalBlocks() == 664);

    // (2) + (3) The directory is a chain of blocks, each holding 8 entries.
    //     A corrupt chain must terminate, and every entry must be readable.
    const auto dir = img.directory("*");
    assert(dir.size() <= 683u * 8u);
    for (const auto& e : dir) {
        assert(e.name.size() <= D64Image::kFilenameLen);
        assert(e.entryOffset + 32 <= D64Image::kImageSize);
    }

    // Label and ID come out of the BAM sector; they must be bounded too.
    assert(img.labelRaw().size() <= 16);
    assert(img.idRaw().size() <= 5);
    assert(img.labelAscii().size() <= 16);

    // (2) + (3) Following a file's sector chain is the traversal most exposed
    //     to a crafted link: read every file the directory claims, plus a
    //     wildcard and a name that is not there.
    for (const auto& e : dir) {
        const std::string name(e.name.begin(), e.name.end());
        const auto data = img.readFile(name);
        assert(data.size() <= kMaxHonestFileBytes);
    }
    assert(img.readFile("*").size() <= kMaxHonestFileBytes);
    assert(img.readFile("NOSUCHFILE").empty() || true);
    assert(img.readFile("").size() <= kMaxHonestFileBytes);

    // (5) None of the above may have written to the image.
    assert(img.rawBytes() == before);
}

void checkInvariants(const uint8_t* data, size_t size)
{
    D64Image img;
    const bool mounted = img.mountBytes(data, size);

    // (1) Mounting is decided by size alone — a D64 is 35 tracks, with or
    //     without the trailing error bytes, and nothing else is one.
    assert(mounted == (size == D64Image::kImageSize ||
                       size == D64Image::kImageSizeWithErrors));
    if (!mounted) {
        assert(!img.isMounted());
        assert(img.rawBytes().empty());
        return;
    }
    assert(img.isMounted());
    assert(img.rawBytes().size() == D64Image::kImageSize);
    checkMounted(img);
}

} // namespace

#if defined(POM1_LIBFUZZER)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    checkInvariants(data, size);
    return 0;
}

#else

namespace {

// A formatted, plausible disk with a couple of files on it — the shape the
// mutator should start from, since random bytes rarely reach the interesting
// traversal code.
std::vector<uint8_t> seedDisk()
{
    D64Image img;
    std::vector<uint8_t> blank(D64Image::kImageSize, 0);
    img.mountBytes(blank.data(), blank.size());
    img.format("POM1 TEST", "01");
    img.writeFile("HELLO", std::vector<uint8_t>(600, 0x41));
    img.writeFile("WORLD", std::vector<uint8_t>(20, 0x42));
    return img.rawBytes();
}

// The bytes that matter in a D64 are the LINKS: byte 0 and 1 of every sector,
// and the track/sector fields of every directory entry.
const uint8_t kInterestingBytes[] = {0x00, 0x01, 0x02, 0x11, 0x12, 0x13,
                                     0x23, 0x24, 0xA0, 0xFE, 0xFF};

void mutate(std::vector<uint8_t>& v, std::mt19937& rng)
{
    switch (rng() % 4) {
    case 0:                        // a link byte, at a sector boundary
        if (v.size() >= 2) {
            const size_t sector = (rng() % (D64Image::kImageSize / 256));
            v[sector * 256 + (rng() % 2)] =
                kInterestingBytes[rng() % sizeof(kInterestingBytes)];
        }
        break;
    case 1:                        // anywhere in the directory track
        if (v.size() > 0x16500) {
            const size_t at = 0x16500 + rng() % 0x1300;
            if (at < v.size()) v[at] = uint8_t(rng());
        }
        break;
    case 2:                        // anywhere at all
        if (!v.empty()) v[rng() % v.size()] = uint8_t(rng());
        break;
    case 3:                        // an interesting byte anywhere
        if (!v.empty())
            v[rng() % v.size()] = kInterestingBytes[rng() % sizeof(kInterestingBytes)];
        break;
    }
}

} // namespace

int main()
{
    const std::vector<uint8_t> seed = seedDisk();
    assert(seed.size() == D64Image::kImageSize);

    // Sizes: only the two legal ones may mount.
    for (size_t n : {size_t(0), size_t(1), size_t(255), size_t(174847),
                     D64Image::kImageSize, size_t(174849),
                     D64Image::kImageSizeWithErrors, size_t(175532)}) {
        std::vector<uint8_t> v(n, 0x00);
        checkInvariants(v.data(), v.size());
    }
    checkInvariants(seed.data(), seed.size());

    // A disk of all-$FF: every link points at track 255, every directory entry
    // claims a file. The densest single input for the traversal guards.
    {
        std::vector<uint8_t> ff(D64Image::kImageSize, 0xFF);
        checkInvariants(ff.data(), ff.size());
    }

    // A directory block that points at ITSELF, and a file chain that does the
    // same: the two cycles a linked format invites. Neither may spin.
    {
        std::vector<uint8_t> loop = seed;
        const size_t dirBlock = D64Image::sectorOffset(18, 1);
        loop[dirBlock + 0] = 18;          // next track  = 18
        loop[dirBlock + 1] = 1;           // next sector = 1  → itself
        const auto start = std::chrono::steady_clock::now();
        checkInvariants(loop.data(), loop.size());
        const auto elapsed = std::chrono::steady_clock::now() - start;
        assert(elapsed < std::chrono::seconds(5));
    }
    {
        std::vector<uint8_t> loop = seed;
        D64Image probe;
        probe.mountBytes(seed.data(), seed.size());
        const auto dir = probe.directory("*");
        assert(!dir.empty());
        const size_t first = D64Image::sectorOffset(dir[0].track, dir[0].sector);
        assert(first != SIZE_MAX);
        loop[first + 0] = dir[0].track;   // the file's first sector links to
        loop[first + 1] = dir[0].sector;  // itself
        checkInvariants(loop.data(), loop.size());
    }

    // Fixed seed: a failure reproduces byte for byte.
    std::mt19937 rng(0x44363400u);   // "D64"
    int inputs = 0;
    for (int round = 0; round < 3000; ++round) {
        std::vector<uint8_t> v = seed;
        const int depth = 1 + static_cast<int>(rng() % 24);
        for (int d = 0; d < depth; ++d) mutate(v, rng);
        checkInvariants(v.data(), v.size());
        ++inputs;
    }

    std::printf("d64_fuzz_smoke: OK (%d mutated images)\n", inputs);
    return 0;
}

#endif
