// pom1::parseMemoryImage — the three memory-image dialects, as pure functions.
//
// Lifted out of Memory::loadHexDump(), which interleaved parsing with mem[]
// writes and log() calls. That shape had two costs. It could not be tested
// without a Memory (so the dialects were only ever covered end to end, through
// a whole emulated machine), and it could not validate before mutating: an
// Intel HEX whose fifth record lay outside the 6502's 64 KB left the first four
// already in RAM before the error came back.
//
// Covered here, all without a Memory, a file or a log sink:
//   §1  WOZMON: addresses, run address, comment stripping, zone merging;
//   §2  the 1-2 digit token before ':' — data, not an address;
//   §3  merged data+address and merged data+run, artefacts of line joining;
//   §4  Intel HEX detected by SHAPE, never by extension;
//   §5  a broken Intel HEX yields NO writes — the no-partial-mutation promise;
//   §6  TurboType selected by extension OR by a lone 'T', and the shredding
//       that the line-structured parse exists to prevent;
//   §7  the 'X' end-of-stream marker keeps its trailing run address;
//   §8  odd-length hex runs are reported, not swallowed;
//   §9  a file that says nothing is rejected and writes nothing;
//   §10 addresses are clamped to the 6502's 64 KB;
//   §11 address arithmetic is checked, not left to strtol's platform-dependent
//       saturation;
//   §12 an odd-length run reports where it was;
//   §13 the size cap refuses rather than allocates.

#include "MemoryImageLoader.h"

#include <cassert>
#include <cstdio>
#include <map>
#include <string>

using pom1::MemoryImage;
using pom1::MemoryImageDiagnostic;
using pom1::MemoryImageFormat;
using pom1::parseMemoryImage;

namespace {

// Flatten the spans the way Memory::loadHexDump applies them: in order, later
// writes winning. What the emulated machine would actually end up holding.
std::map<uint16_t, uint8_t> flatten(const MemoryImage& img)
{
    std::map<uint16_t, uint8_t> out;
    for (const auto& span : img.writes) {
        unsigned int addr = span.start;
        for (uint8_t b : span.bytes) {
            if (addr >= 0x10000) break;
            out[static_cast<uint16_t>(addr++)] = b;
        }
    }
    return out;
}

bool has(const MemoryImage& img, MemoryImageDiagnostic::Severity sev)
{
    for (const auto& d : img.diagnostics)
        if (d.severity == sev) return true;
    return false;
}

int severityCount(const MemoryImage& img, MemoryImageDiagnostic::Severity sev)
{
    int n = 0;
    for (const auto& d : img.diagnostics)
        if (d.severity == sev) ++n;
    return n;
}

} // namespace

int main()
{
    // -----------------------------------------------------------------
    // §1 WOZMON: the dialect ~100 bundled dumps use.
    //
    // Also pins zone merging: "0288:" picks up exactly where "0280:" stopped,
    // so the two lines are ONE run. Breaking on every address line is what
    // used to shred chess.txt into a zone per 8-byte row.
    // -----------------------------------------------------------------
    {
        const std::string src =
            "// a comment line\n"
            "0280: A9 0B 8D 02 B0 A9 1E 8D   // inline comment\n"
            "0288: 03 B0 A2 00\n"
            "; another comment\n"
            "0280R\n";
        const MemoryImage img = parseMemoryImage(src, "prog.txt");

        assert(img.ok);
        assert(img.format == MemoryImageFormat::WozmonHex);
        assert(img.byteCount == 12);
        assert(img.hasRunAddress);
        assert(img.startAddress == 0x0280);
        assert(img.writes.size() == 1);          // merged, not one span per line
        assert(img.zones().size() == 1);
        assert(img.zones()[0].first == 0x0280);
        assert(img.zones()[0].second == 0x028B);

        const auto mem = flatten(img);
        assert(mem.at(0x0280) == 0xA9);
        assert(mem.at(0x0287) == 0x8D);
        assert(mem.at(0x0288) == 0x03);
        assert(mem.at(0x028B) == 0x00);
        assert(has(img, MemoryImageDiagnostic::Severity::Info));
        assert(!has(img, MemoryImageDiagnostic::Severity::Warning));

        // A '#' comment must be stripped too: without that branch the
        // hex-looking tail below tokenises as data and corrupts memory.
        const MemoryImage sharp = parseMemoryImage("0300: AB # BADC0DE\n", "p.txt");
        assert(sharp.ok && sharp.byteCount == 1);
        assert(flatten(sharp).at(0x0300) == 0xAB);
    }

    // -----------------------------------------------------------------
    // §2 A 1-2 digit token before ':' is DATA, not an address.
    //
    // mandelbrot-65, 2048, cat, cellular and 50th format a contiguous line as
    // "0280:4C 5F ... 5E:3D 2B ..." with a ':' every 8th byte. Reading the
    // trailing "5E" as address $005E scattered the whole program across zero
    // page (zones=166) and it crashed to $0000 on the first JMP.
    // -----------------------------------------------------------------
    {
        const MemoryImage img =
            parseMemoryImage("0280:4C 5F 03 2E 2E 2C 27 5E:3D 2B 20 21\n", "m.txt");
        assert(img.ok);
        assert(img.byteCount == 12);
        assert(img.writes.size() == 1);            // ONE zone, not two
        assert(img.zones()[0].first == 0x0280);
        assert(img.zones()[0].second == 0x028B);
        assert(flatten(img).at(0x0287) == 0x5E);   // the 8th byte, still data
        assert(flatten(img).at(0x0288) == 0x3D);
    }

    // -----------------------------------------------------------------
    // §3 Merged tokens — artefacts of joining every line into one string, and
    //    the reason the joining parser needs these two branches at all.
    // -----------------------------------------------------------------
    {
        // "ED0300:" = data ED at the previous address, then address $0300.
        const MemoryImage img = parseMemoryImage("0200: AA ED0300: BB CC\n", "m.txt");
        assert(img.ok);
        assert(img.byteCount == 4);
        const auto mem = flatten(img);
        assert(mem.at(0x0200) == 0xAA);
        assert(mem.at(0x0201) == 0xED);   // the merged prefix stayed data
        assert(mem.at(0x0300) == 0xBB);
        assert(mem.at(0x0301) == 0xCC);
        assert(img.writes.size() == 2);   // $0200 run and $0300 run
    }
    {
        // "FFE2B3R" = data FF, then run address $E2B3.
        const MemoryImage img = parseMemoryImage("0200: AA\nFFE2B3R\n", "m.txt");
        assert(img.ok);
        assert(img.hasRunAddress);
        assert(img.startAddress == 0xE2B3);
        assert(img.byteCount == 2);
        assert(flatten(img).at(0x0201) == 0xFF);
    }

    // -----------------------------------------------------------------
    // §4 Intel HEX is detected by SHAPE. Extension-based routing would write
    //    every record header — count, address, type, checksum — into RAM as
    //    data, silently.
    // -----------------------------------------------------------------
    {
        // :10028000 <16 bytes> CS  then a type-01 EOF.
        const std::string src =
            ":10028000A90B8D02B0A91E8D03B0A200BDBC020057\n"
            ":00000001FF\n";
        const MemoryImage img = parseMemoryImage(src, "prog.txt");   // .txt on purpose
        assert(img.ok);
        assert(img.format == MemoryImageFormat::IntelHex);
        assert(img.byteCount == 16);
        assert(img.startAddress == 0x0280);
        assert(!img.hasRunAddress);            // no type 03/05 record here
        assert(flatten(img).at(0x0280) == 0xA9);
        assert(!has(img, MemoryImageDiagnostic::Severity::Warning));

        // ...and a missing EOF record warns but still loads.
        const MemoryImage noEof =
            parseMemoryImage(":10028000A90B8D02B0A91E8D03B0A200BDBC020057\n", "p.hex");
        assert(noEof.ok && noEof.format == MemoryImageFormat::IntelHex);
        assert(has(noEof, MemoryImageDiagnostic::Severity::Warning));
    }
    {
        // A WOZMON dump named ".hex" must NOT be taken for Intel HEX.
        const MemoryImage img = parseMemoryImage("0280: A9 0B\n0280R\n", "prog.hex");
        assert(img.ok && img.format == MemoryImageFormat::WozmonHex);
    }

    // -----------------------------------------------------------------
    // §5 THE no-partial-mutation pin.
    //
    // Once the first record validates, the parser COMMITS to Intel HEX — a
    // later failure is a broken file, reported, never retried through the
    // WOZMON parser (which would resume writing record headers into RAM).
    // And the rejected image carries NO writes: the caller cannot half-apply
    // it, which the in-place loader could not promise.
    // -----------------------------------------------------------------
    {
        // Record 1 is valid and in range; record 2 sits at $FFF8 with 16 bytes,
        // i.e. past the 6502's 64 KB (the type 02/04 base records are an x86
        // inheritance). Wrapping it would scribble over page zero.
        const std::string src =
            ":10028000A90B8D02B0A91E8D03B0A200BDBC020057\n"
            ":10FFF800A90B8D02B0A91E8D03B0A200BDBC0200E2\n"
            ":00000001FF\n";
        const MemoryImage img = parseMemoryImage(src, "bad.hex");
        assert(!img.ok);
        assert(img.format == MemoryImageFormat::IntelHex);
        assert(img.writes.empty());        // <- the promise
        assert(img.byteCount == 0);
        assert(severityCount(img, MemoryImageDiagnostic::Severity::Error) == 1);
    }
    {
        // A bad checksum on a later record fails the load the same way.
        const std::string src =
            ":10028000A90B8D02B0A91E8D03B0A200BDBC020057\n"
            ":10029000A90B8D02B0A91E8D03B0A200BDBC020000\n"
            ":00000001FF\n";
        const MemoryImage img = parseMemoryImage(src, "bad.hex");
        assert(!img.ok);
        assert(img.writes.empty());
        assert(has(img, MemoryImageDiagnostic::Severity::Error));
    }

    // -----------------------------------------------------------------
    // §6 TurboType — line-structured, because ':' OPENS a data line there.
    //
    // Joined, each 32-digit run is followed by the next line's ':', so the
    // legacy rule sliced off its last four digits as an address and scattered
    // the program (the 15 Puzzle's $0300 block landed in 60+ zones at $18A9,
    // $4159, $F460...). Selected by the '.tur' extension OR a lone 'T' — the
    // marker is optional in the wild, and a marker-less .tur left to the legacy
    // parser hits exactly that shredding.
    // -----------------------------------------------------------------
    {
        const std::string turbo =
            "0300\n"
            ":D8A2FF9AA92A851A204604A97C8518A9\n"
            ":05851920AD0320CB03D00EA9AF8518A9\n"
            "X\n"
            "015ER\n";

        const MemoryImage byExt = parseMemoryImage(turbo, "puzzle.tur");
        assert(byExt.ok);
        assert(byExt.format == MemoryImageFormat::TurboType);
        assert(byExt.byteCount == 32);
        assert(byExt.writes.size() == 1);        // ONE run, not 60+
        assert(byExt.zones()[0].first == 0x0300);
        assert(byExt.zones()[0].second == 0x031F);
        assert(byExt.hasRunAddress && byExt.startAddress == 0x015E);
        assert(flatten(byExt).at(0x0300) == 0xD8);
        assert(flatten(byExt).at(0x0310) == 0x05);

        // Same content, named .txt, but carrying the lone 'T' marker.
        const MemoryImage byMarker = parseMemoryImage("T\n" + turbo, "puzzle.txt");
        assert(byMarker.format == MemoryImageFormat::TurboType);
        assert(byMarker.writes.size() == 1);
        assert(byMarker.startAddress == 0x015E);

        // ...and without either, the same bytes go to the legacy parser and are
        // shredded. Pinned so the selection rule cannot quietly widen: this is
        // the FAILURE the .tur branch exists to prevent, not a supported mode.
        const MemoryImage shredded = parseMemoryImage(turbo, "puzzle.txt");
        assert(shredded.format == MemoryImageFormat::WozmonHex);
        assert(shredded.writes.size() > 1);
    }

    // -----------------------------------------------------------------
    // §7 'X' is a BARE end-of-stream marker.
    //
    // It used to swallow the hex run behind it, eating the run address of
    // every real .TUR: the files end "X / 015ER", so "015E" was consumed as
    // the X's operand and the orphaned "R" fell through. The load then
    // reported the PREVIOUS R — the "0100R" that starts the serial receiver on
    // real hardware — and POM1 jumped into a transfer loader waiting for a
    // stream that direct injection never sends.
    // -----------------------------------------------------------------
    {
        // The legacy (joined) parser must handle it too — a .txt can carry it.
        const MemoryImage img =
            parseMemoryImage("0300: AA BB\nX\n015ER\n", "p.txt");
        assert(img.ok);
        assert(img.format == MemoryImageFormat::WozmonHex);
        assert(img.hasRunAddress);
        assert(img.startAddress == 0x015E);
        assert(img.byteCount == 2);          // nothing past X became data
    }

    // -----------------------------------------------------------------
    // §8 A lone trailing nibble is dropped — but reported. Silence there masks
    //    real bugs in hand-edited dumps.
    // -----------------------------------------------------------------
    {
        const MemoryImage img = parseMemoryImage("0300: AA BB C\n", "p.txt");
        assert(img.ok);
        assert(img.byteCount == 2);
        assert(has(img, MemoryImageDiagnostic::Severity::Warning));
    }

    // -----------------------------------------------------------------
    // §9 A file that establishes no address and names no run address says
    //    nothing. Rejected, and — unlike the in-place loader, which wrote its
    //    stray bytes to $0000 and THEN returned an error — it writes nothing.
    // -----------------------------------------------------------------
    {
        const MemoryImage img = parseMemoryImage("// just a comment\n", "empty.txt");
        assert(!img.ok);
        assert(img.writes.empty());
        assert(img.byteCount == 0);

        const MemoryImage stray = parseMemoryImage("AA BB CC\n", "stray.txt");
        assert(!stray.ok);
        assert(stray.writes.empty());   // no silent dump into page zero
    }

    // -----------------------------------------------------------------
    // §10 The 6502 has 64 KB. Data running off the top is dropped, not wrapped
    //     around onto page zero.
    // -----------------------------------------------------------------
    {
        const MemoryImage img = parseMemoryImage("FFFE: AA BB CC DD\n", "p.txt");
        assert(img.ok);
        assert(img.byteCount == 2);
        const auto mem = flatten(img);
        assert(mem.at(0xFFFE) == 0xAA);
        assert(mem.at(0xFFFF) == 0xBB);
        assert(mem.find(0x0000) == mem.end());
        assert(img.zones()[0].second == 0xFFFF);
    }

    // -----------------------------------------------------------------
    // §11 Address arithmetic is CHECKED, not delegated to strtol.
    //
    // `long` is 64-bit on macOS/Linux and 32-bit on Windows, and an over-wide
    // token saturates differently on each. A TurboType address line reading
    // "100000000" truncated to $0000 on the 64-bit hosts — putting the bytes
    // that followed into PAGE ZERO — while the 32-bit host clamped elsewhere
    // and dropped them. One file, two machines, two outcomes, neither of them
    // what the file said.
    //
    // The line-structured parser has no merged-token rule to fall back on, so
    // an address it cannot hold is a malformed line: skipped, and reported.
    // -----------------------------------------------------------------
    {
        const MemoryImage img = parseMemoryImage("100000000\n:AABBCC\n", "x.tur");
        assert(!img.ok);                     // no address was ever established
        assert(img.writes.empty());          // and nothing went to page zero
        assert(has(img, MemoryImageDiagnostic::Severity::Warning));

        // Leading zeros do not make a token over-wide: "000300" is $0300.
        const MemoryImage zeros = parseMemoryImage("000300\n:AABB\n", "x.tur");
        assert(zeros.ok);
        assert(zeros.startAddress == 0x0300);
        assert(zeros.writes.size() == 1 && zeros.writes[0].start == 0x0300);

        // The JOINED parser reads the same token as its documented merged
        // data+address ("100000000:" = data 10 00, then address $0000). That
        // is a rule, not an accident, so it keeps its behaviour — with the
        // dropped nibble reported.
        const MemoryImage joined = parseMemoryImage("100000000\n:AABBCC\n", "x.txt");
        assert(joined.ok);
        assert(joined.startAddress == 0x0000);
        assert(has(joined, MemoryImageDiagnostic::Severity::Warning));
    }

    // -----------------------------------------------------------------
    // §12 An odd-length run says WHERE it was. "3 odd runs" sends a user
    //     hunting through the whole file; an address points at the line.
    // -----------------------------------------------------------------
    {
        const MemoryImage img = parseMemoryImage("0300: AA BB\n0310: CC D\n", "p.txt");
        assert(img.ok);
        bool located = false;
        for (const auto& d : img.diagnostics)
            if (d.severity == MemoryImageDiagnostic::Severity::Warning)
                located = d.message.find("0x311") != std::string::npos;
        assert(located);
    }

    // -----------------------------------------------------------------
    // §13 Past the size cap the answer is a refusal with a message, never an
    //     allocation the user did not ask for. Memory::loadHexDump() checks
    //     the file's size before reading it; this is the backstop for every
    //     other caller.
    // -----------------------------------------------------------------
    {
        const std::string huge(pom1::kMaxMemoryImageBytes + 1, 'A');
        const MemoryImage img = parseMemoryImage(huge, "huge.txt");
        assert(!img.ok);
        assert(img.writes.empty());
        assert(severityCount(img, MemoryImageDiagnostic::Severity::Error) == 1);

        // ...and a file right at the limit is still parsed.
        std::string atLimit = "0300: AA\n";
        atLimit.resize(pom1::kMaxMemoryImageBytes, '\n');
        const MemoryImage ok = parseMemoryImage(atLimit, "big.txt");
        assert(ok.ok && ok.byteCount == 1);
    }

    std::printf("memory_image_loader_smoke: OK\n");
    return 0;
}
