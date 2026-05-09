// One-shot helper to generate a small .d64 test fixture used by the IEC
// telnet harness and the d64 round-trip test. Run from the build directory:
//   cmake .. && make make_iec_fixture
//   ./tools/make_iec_fixture <out.d64>
//
// Output disk: label "POM1 IEC", id "01", one PRG file "HELLO" containing
// a 256-byte payload (load addr $0801 + 254 ASCII bytes). Useful for
// @$ / @L round-trips against the SD OS 1.3 firmware.

#include "D64Image.h"

#include <cstdio>
#include <cstring>
#include <fstream>

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "fixture.d64";

    pom1::D64Image disk;
    if (!disk.format("POM1 IEC", "01")) {
        std::fprintf(stderr, "format failed\n");
        return 1;
    }

    std::vector<uint8_t> hello;
    hello.push_back(0x01);   // load address $0801 (PRG header)
    hello.push_back(0x08);
    for (int i = 0; i < 254; ++i) {
        hello.push_back(static_cast<uint8_t>('A' + (i % 26)));
    }
    if (!disk.writeFile("HELLO", hello, pom1::D64Image::FileType::Prg)) {
        std::fprintf(stderr, "writeFile HELLO failed\n");
        return 1;
    }

    std::ofstream f(out, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", out); return 1; }
    f.write(reinterpret_cast<const char*>(disk.rawBytes().data()),
            static_cast<std::streamsize>(disk.rawBytes().size()));
    if (!f) { std::fprintf(stderr, "write failed\n"); return 1; }

    std::printf("Wrote %s (%zu bytes) — label='%s', %d / %d blocks free\n",
                out, disk.rawBytes().size(),
                disk.labelAscii().c_str(),
                disk.blocksFree(), disk.totalBlocks());
    return 0;
}
