// d64_parse_smoke — D64Image format/write/read/delete round-trip.
//
// Pins:
//   - 174848 byte image, 35 tracks, geometry 21/19/18/17 sectors.
//   - format() initialises BAM (664 free blocks excluding track 18).
//   - writeFile allocates sectors + appends dir entry.
//   - readFile follows the chain and returns exact bytes.
//   - deleteFile zeros the dir slot and frees the chain.

#include "D64Image.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// Side-effect-safe check macro — assert() is a no-op under NDEBUG, which
// silently drops the wrapped expression. Use REQUIRE() for any expression
// whose evaluation must happen (writeFile, mount, save…).
#define REQUIRE(expr)                                                          \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "REQUIRE failed: %s (%s:%d)\n",               \
                         #expr, __FILE__, __LINE__);                           \
            std::abort();                                                      \
        }                                                                     \
    } while (0)

using pom1::D64Image;

static void check_geometry() {
    assert(D64Image::sectorsOnTrack(1)  == 21);
    assert(D64Image::sectorsOnTrack(17) == 21);
    assert(D64Image::sectorsOnTrack(18) == 19);
    assert(D64Image::sectorsOnTrack(24) == 19);
    assert(D64Image::sectorsOnTrack(25) == 18);
    assert(D64Image::sectorsOnTrack(30) == 18);
    assert(D64Image::sectorsOnTrack(31) == 17);
    assert(D64Image::sectorsOnTrack(35) == 17);

    int total = 0;
    for (uint8_t t = 1; t <= 35; ++t) total += D64Image::sectorsOnTrack(t);
    assert(total == 683);
    assert(683 * 256 == D64Image::kImageSize);

    // Track 1 sector 0 at offset 0; track 2 sector 0 at offset 21*256.
    assert(D64Image::sectorOffset(1, 0)  == 0);
    assert(D64Image::sectorOffset(2, 0)  == 21 * 256);
    assert(D64Image::sectorOffset(18, 0) == 17 * 21 * 256);
}

static void check_wildcard() {
    auto m = [](const char* p, const char* n) {
        return D64Image::wildcardMatch(reinterpret_cast<const uint8_t*>(p), std::strlen(p),
                                        reinterpret_cast<const uint8_t*>(n), std::strlen(n));
    };
    assert(m("BASIC", "BASIC"));
    assert(!m("BASIC", "BASIK"));
    assert(m("BAS*", "BASIC"));
    assert(m("*", "ANYTHING"));
    assert(m("B%SIC", "BASIC"));
    assert(!m("BA?", "BASIC"));   // ? not a wildcard in CBM (we treat as %, single char only)
}

static void check_format_writeread_delete() {
    D64Image img;
    bool ok = img.format("MYDISK", "A1");
    REQUIRE(ok);

    // 664 blocks free on a 35-track disk (track 18 is reserved/ignored in the count).
    assert(img.blocksFree() == img.totalBlocks());
    assert(img.totalBlocks() == 664);

    // Label/id round-trip.
    auto lbl = img.labelRaw();
    assert(lbl.size() == 6);
    assert(lbl[0] == 'M' && lbl[5] == 'K');

    auto idr = img.idRaw();
    assert(idr.size() == 2);
    assert(idr[0] == 'A' && idr[1] == '1');

    // Write a small PRG (300 bytes).
    std::vector<uint8_t> data;
    data.reserve(300);
    data.push_back(0x01); data.push_back(0x08);  // load addr $0801
    for (int i = 0; i < 298; ++i) data.push_back(static_cast<uint8_t>(i & 0xFF));
    REQUIRE(img.writeFile("HELLO", data));

    // Directory has one entry, blocks free decreased.
    auto dir = img.directory("*");
    REQUIRE(dir.size() == 1);
    assert(dir[0].name.size() == 5);
    assert(dir[0].name[0] == 'H' && dir[0].name[4] == 'O');
    assert(dir[0].blocks >= 2);    // 300 bytes / 254 = 2 blocks
    assert(img.blocksFree() == 664 - dir[0].blocks);

    // Read back, byte-compare.
    auto roundtrip = img.readFile("HELLO");
    REQUIRE(roundtrip.size() == data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        if (roundtrip[i] != data[i]) {
            std::fprintf(stderr, "Mismatch at %zu: got %02X, expected %02X\n",
                         i, roundtrip[i], data[i]);
            std::abort();
        }
    }

    // Wildcard read.
    auto byPat = img.readFile("HEL*");
    REQUIRE(byPat == data);

    // Refuse to overwrite.
    REQUIRE(!img.writeFile("HELLO", data));

    // Delete + re-add.
    REQUIRE(img.deleteFile("HELLO"));
    auto dirAfter = img.directory("*");
    REQUIRE(dirAfter.empty());
    REQUIRE(img.blocksFree() == 664);
    REQUIRE(img.writeFile("HELLO", data));
    REQUIRE(img.directory("*").size() == 1);
}

static void check_save_load_round_trip() {
    D64Image img;
    img.format("SAVED", "Z9");
    std::vector<uint8_t> data{0x00, 0x03, 0xAA, 0xBB, 0xCC, 0xDD};
    REQUIRE(img.writeFile("FOO", data));

    // Save to a temp path, mount fresh, read. Test-only path; no security context.
    auto base = std::filesystem::temp_directory_path() / "pom1_d64_smoke.d64";
    std::string path = base.string();
    std::error_code rmEc;
    std::filesystem::remove(path, rmEc);

    // Manual file write — D64Image::save needs path_, set via mount() but that requires existing file.
    // Workaround: write raw bytes ourselves, then mount.
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        REQUIRE(f);
        std::fwrite(img.rawBytes().data(), 1, img.rawBytes().size(), f);
        std::fclose(f);
    }

    D64Image img2;
    REQUIRE(img2.mount(path));
    REQUIRE(img2.blocksFree() == img.blocksFree());
    auto roundtrip = img2.readFile("FOO");
    REQUIRE(roundtrip == data);

    // save() round-trip after mount.
    REQUIRE(img2.deleteFile("FOO"));
    REQUIRE(img2.save());

    D64Image img3;
    REQUIRE(img3.mount(path));
    REQUIRE(img3.directory("*").empty());

    std::remove(path.c_str());
}

// A D64 is a LINKED structure: every directory block and every file sector
// names the next one, and nothing anywhere records how long a chain is — you
// find out by following links. A corrupt image can therefore point a chain at
// itself, and the only sound stop condition is "I have been here before".
//
// The counters that used to stand in for that were guesses, wrong in both
// directions: the file walk allowed 1000 hops on a disk holding 683 sectors, so
// a self-linking sector yielded a 254 KB "file" out of a 174 KB image, while
// the directory walk allowed only 256 — fewer than the 683 blocks a
// pathological but LEGAL directory chain could occupy. Found by fuzzing.
static void check_cyclic_chains_terminate() {
    D64Image seed;
    std::vector<uint8_t> blank(D64Image::kImageSize, 0);
    REQUIRE(seed.mountBytes(blank.data(), blank.size()));
    REQUIRE(seed.format("LOOP TEST", "01"));
    REQUIRE(seed.writeFile("VICTIM", std::vector<uint8_t>(600, 0x41)));
    const std::vector<uint8_t> good = seed.rawBytes();

    // The honest disk first, so the corrupted reads below have something to be
    // compared against.
    {
        D64Image img;
        REQUIRE(img.mountBytes(good.data(), good.size()));
        assert(img.directory("*").size() == 1);
        assert(img.readFile("VICTIM").size() == 600);
    }

    // A directory block that links to itself. The walk must stop, and must not
    // report the same eight entries over and over.
    {
        std::vector<uint8_t> loop = good;
        const size_t dirBlock = D64Image::sectorOffset(18, 1);
        REQUIRE(dirBlock != SIZE_MAX);
        loop[dirBlock + 0] = 18;
        loop[dirBlock + 1] = 1;
        D64Image img;
        REQUIRE(img.mountBytes(loop.data(), loop.size()));
        assert(img.directory("*").size() == 1);
    }

    // A file's first sector linking to itself. Before cycle detection this
    // returned 1000 sectors' worth of the same 254 bytes.
    {
        std::vector<uint8_t> loop = good;
        D64Image probe;
        REQUIRE(probe.mountBytes(good.data(), good.size()));
        const auto dir = probe.directory("*");
        assert(dir.size() == 1);
        const size_t first = D64Image::sectorOffset(dir[0].track, dir[0].sector);
        REQUIRE(first != SIZE_MAX);
        loop[first + 0] = dir[0].track;
        loop[first + 1] = dir[0].sector;

        D64Image img;
        REQUIRE(img.mountBytes(loop.data(), loop.size()));
        const auto data = img.readFile("VICTIM");
        // One pass over the sector, then the repeat is refused. A disk holds
        // 683 sectors of 254 data bytes, so nothing honest can exceed that.
        assert(data.size() == 254);
        assert(data.size() <= 683u * 254u);
    }

    // A chain pointing off the disk entirely (track 40 does not exist) must be
    // refused rather than followed.
    {
        std::vector<uint8_t> bad = good;
        D64Image probe;
        REQUIRE(probe.mountBytes(good.data(), good.size()));
        const auto dir = probe.directory("*");
        const size_t first = D64Image::sectorOffset(dir[0].track, dir[0].sector);
        bad[first + 0] = 40;
        bad[first + 1] = 0;
        D64Image img;
        REQUIRE(img.mountBytes(bad.data(), bad.size()));
        assert(img.readFile("VICTIM").size() == 254);
    }
}

// The BAM's per-track free count is a raw byte from the image. Believing it let
// an all-$FF disk report 8670 blocks free on a disk that holds 664 — a track
// cannot have more free sectors than it physically has. Also found by fuzzing.
static void check_blocks_free_is_clamped_to_geometry() {
    std::vector<uint8_t> ff(D64Image::kImageSize, 0xFF);
    D64Image img;
    REQUIRE(img.mountBytes(ff.data(), ff.size()));
    const int free = img.blocksFree();
    assert(free >= 0);
    assert(free <= img.totalBlocks());
    assert(img.totalBlocks() == 664);
}

// mount(path) reads the bytes and calls mountBytes(), so a file and a buffer
// pass exactly the same acceptance rules: a D64 is 35 tracks, with or without
// the 683 trailing error bytes, and nothing else is one.
static void check_mount_accepts_only_the_two_sizes() {
    for (size_t n : {size_t(0), size_t(1), size_t(174847), size_t(174849),
                     size_t(175530), size_t(175532)}) {
        std::vector<uint8_t> v(n, 0);
        D64Image img;
        assert(!img.mountBytes(v.data(), v.size()));
        assert(!img.isMounted());
    }
    for (size_t n : {D64Image::kImageSize, D64Image::kImageSizeWithErrors}) {
        std::vector<uint8_t> v(n, 0);
        D64Image img;
        REQUIRE(img.mountBytes(v.data(), v.size()));
        // The error bytes are dropped — POM1 emulates a healthy drive.
        assert(img.rawBytes().size() == D64Image::kImageSize);
        // An unmounted-from-memory image has no path, so it refuses to save
        // rather than writing somewhere surprising.
        assert(!img.save());
    }
    D64Image none;
    assert(!none.mountBytes(nullptr, D64Image::kImageSize));
}

int main() {
    check_geometry();
    check_wildcard();
    check_format_writeread_delete();
    check_save_load_round_trip();
    check_cyclic_chains_terminate();
    check_blocks_free_is_clamped_to_geometry();
    check_mount_accepts_only_the_two_sizes();
    std::printf("d64_parse_smoke: OK\n");
    return 0;
}
