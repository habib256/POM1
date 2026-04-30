// chess_engine_perft_smoke_test.cpp -- pin the chess engine's depth-1 perft.
//
// Loads software/games/Chess.{bin.lo,bin.hi}, parses the VICE-format symbol
// file produced by ld65 -Ln (Chess.sym), installs a tiny RAM trampoline
// that calls init_board then perft1 then traps via JMP-to-self, drives the
// CPU until the trap, and asserts perft_count_lo:hi == 20.
//
// 20 is the well-known depth-1 perft count for the initial chess position.
// Any silent regression in:
//   - move generation (pawn / knight / bishop / rook / queen / king)
//   - is_pseudo_legal dispatch
//   - the ce_piece / atk_piece aliasing fix from v0.4
//   - the ai_play_move / perft1 enumeration loop
// will move this number off 20 and fail the test.
//
// The test also sanity-checks board[$00] == white rook + board[$77] == black
// rook so init_board itself is exercised, not just the perft scan.
//
// Symbols extracted from Chess.sym at runtime (not hard-coded) — the engine
// can be re-laid-out without touching this test.

#include "M6502.h"
#include "Memory.h"

// Memory holds unique_ptrs to peripheral classes that need their full type
// at this site (same pattern as klaus_6502_functional_test.cpp).
#include "A1IO_RTC.h"
#include "CFFA1.h"
#include "MicroSD.h"
#include "SID.h"
#include "TMS9918.h"
#include "TerminalCard.h"
#include "WiFiModem.h"
#include "PR40Printer.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr uint16_t kTrampoline   = 0x0200;
constexpr long     kMaxSteps     = 50'000'000L;  // perft1 ≈ a few hundred kcy

// Piece encoding from chess_common.inc (only the codes we assert on).
constexpr uint8_t kWhiteRook = 0x04;             // PIECE_ROOK
constexpr uint8_t kBlackRook = 0x84;             // PIECE_ROOK | COLOR_BLACK

bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)),
               std::istreambuf_iterator<char>());
    return true;
}

// Parse Chess.sym (VICE label file). Lines look like:
//     al 00E000 .init_board
// Strip leading "al ", read 6 hex digits, then ".name" (we drop the dot).
bool parseSymbols(const std::string& path,
                  std::map<std::string, uint16_t>& out)
{
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < 12 || line.substr(0, 3) != "al ") continue;
        unsigned addr = 0;
        char namebuf[128] = {0};
        // sscanf: skip "al ", read 6 hex, skip space-dot, read name.
        if (std::sscanf(line.c_str(), "al %x .%127s", &addr, namebuf) == 2) {
            out[namebuf] = static_cast<uint16_t>(addr & 0xFFFF);
        }
    }
    return true;
}

uint16_t requireSymbol(const std::map<std::string, uint16_t>& syms,
                       const char* name)
{
    auto it = syms.find(name);
    if (it == syms.end()) {
        std::fprintf(stderr,
            "FAIL — symbol '%s' not found in Chess.sym (rebuild chess?)\n",
            name);
        std::exit(2);
    }
    return it->second;
}

} // namespace

int main(int argc, char** argv)
{
    // Resolve software/games/ path. CMake sets WORKING_DIRECTORY to repo root,
    // so a relative path is safe; allow override via argv[1] for ad-hoc runs.
    std::string baseDir = "software/games";
    if (argc >= 2) baseDir = argv[1];

    const std::string loPath  = baseDir + "/Chess.bin.lo";
    const std::string hiPath  = baseDir + "/Chess.bin.hi";
    const std::string symPath = baseDir + "/Chess.sym";

    std::vector<uint8_t> lo, hi;
    if (!readFile(loPath, lo)) {
        std::fprintf(stderr,
            "FAIL — cannot read %s. Run `make` in dev/projects/games_chess/ first.\n",
            loPath.c_str());
        return 2;
    }
    if (!readFile(hiPath, hi)) {
        std::fprintf(stderr,
            "FAIL — cannot read %s. Run `make` in dev/projects/games_chess/ first.\n",
            hiPath.c_str());
        return 2;
    }

    std::map<std::string, uint16_t> syms;
    if (!parseSymbols(symPath, syms)) {
        std::fprintf(stderr,
            "FAIL — cannot read %s. Run `make` in dev/projects/games_chess/ first.\n",
            symPath.c_str());
        return 2;
    }

    const uint16_t addr_init   = requireSymbol(syms, "init_board");
    const uint16_t addr_perft  = requireSymbol(syms, "perft1");
    const uint16_t addr_pcl    = requireSymbol(syms, "perft_count_lo");
    const uint16_t addr_pch    = requireSymbol(syms, "perft_count_hi");
    const uint16_t addr_board  = requireSymbol(syms, "board");

    // Flat 64 KB RAM. Chess accesses $D012 (cassette ECHO) only from the
    // user-facing renderer; the engine itself touches just RAM. Test mode
    // gives us bus-free RAM that perft1 can scribble through cleanly.
    Memory memory;
    memory.setTestMode(true);
    memory.setWriteInRom(true);
    uint8_t* m = memory.getMemoryPointerMutable();

    // Load the two binaries into their natural homes.
    std::memcpy(m + 0x0280, lo.data(), lo.size());
    std::memcpy(m + 0xE000, hi.data(), hi.size());

    // Trampoline at $0200:
    //   $0200: 20 LL HH       JSR init_board
    //   $0203: 20 LL HH       JSR perft1
    //   $0206: 4C 06 02       JMP $0206  (trap — Klaus-style PC-stuck detection)
    m[kTrampoline + 0] = 0x20;
    m[kTrampoline + 1] = static_cast<uint8_t>(addr_init & 0xFF);
    m[kTrampoline + 2] = static_cast<uint8_t>((addr_init >> 8) & 0xFF);
    m[kTrampoline + 3] = 0x20;
    m[kTrampoline + 4] = static_cast<uint8_t>(addr_perft & 0xFF);
    m[kTrampoline + 5] = static_cast<uint8_t>((addr_perft >> 8) & 0xFF);
    m[kTrampoline + 6] = 0x4C;
    m[kTrampoline + 7] = 0x06;
    m[kTrampoline + 8] = 0x02;

    M6502 cpu(&memory);
    cpu.setProgramCounter(kTrampoline);
    cpu.start();

    long steps = 0;
    uint16_t lastPc = cpu.getProgramCounter();
    int stuck = 0;
    while (steps < kMaxSteps) {
        cpu.step();
        ++steps;
        const uint16_t pc = cpu.getProgramCounter();
        if (pc == lastPc) {
            if (++stuck >= 2) break;
        } else {
            stuck = 0;
            lastPc = pc;
        }
    }

    const uint16_t finalPc = cpu.getProgramCounter();
    if (steps >= kMaxSteps) {
        std::fprintf(stderr,
            "TIMEOUT — perft1 did not terminate within %ld steps. PC=$%04X\n",
            kMaxSteps, finalPc);
        return 1;
    }
    if (finalPc != kTrampoline + 6) {
        std::fprintf(stderr,
            "FAIL — trapped at $%04X, expected $%04X (trampoline JMP-self)\n",
            finalPc, static_cast<uint16_t>(kTrampoline + 6));
        return 1;
    }

    // Verify init_board placed the rooks correctly. board[$00] = white rook,
    // board[$77] = black rook. This catches a corrupt starting_position copy
    // or an off-by-one in the rank-major → 0x88 transform without depending
    // on perft.
    const uint8_t pieceA1 = m[addr_board + 0x00];
    const uint8_t pieceH8 = m[addr_board + 0x77];
    if (pieceA1 != kWhiteRook) {
        std::fprintf(stderr,
            "FAIL — board[$00] = $%02X (expected $%02X = white rook)\n",
            pieceA1, kWhiteRook);
        return 1;
    }
    if (pieceH8 != kBlackRook) {
        std::fprintf(stderr,
            "FAIL — board[$77] = $%02X (expected $%02X = black rook)\n",
            pieceH8, kBlackRook);
        return 1;
    }

    const uint16_t perftCount =
        static_cast<uint16_t>(m[addr_pcl]) |
        (static_cast<uint16_t>(m[addr_pch]) << 8);

    if (perftCount != 20) {
        std::fprintf(stderr,
            "FAIL — perft1(initial position) = %u, expected 20\n",
            perftCount);
        return 1;
    }

    std::printf(
        "chess_engine_perft_smoke: OK (init_board=$%04X perft1=$%04X "
        "→ %u legal moves at depth 1, %ld CPU steps)\n",
        addr_init, addr_perft, perftCount, steps);
    return 0;
}
