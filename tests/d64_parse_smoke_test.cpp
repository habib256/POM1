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

int main() {
    check_geometry();
    check_wildcard();
    check_format_writeread_delete();
    check_save_load_round_trip();
    std::printf("d64_parse_smoke: OK\n");
    return 0;
}
