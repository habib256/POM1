// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// The three memory-image dialects POM1 reads, as pure functions. Lifted out of
// Memory::loadHexDump(), which used to interleave parsing with mem[] writes and
// pom1::log() calls; the behaviour of each dialect is preserved byte for byte,
// and the traps each branch exists to avoid are documented where they live.

#include "MemoryImageLoader.h"

#include "HexDumpFile.h"
#include "IntelHexFile.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace pom1 {

std::vector<std::pair<uint16_t, uint16_t>> MemoryImage::zones() const
{
    std::vector<std::pair<uint16_t, uint16_t>> out;
    out.reserve(writes.size());
    for (const MemoryImageSpan& span : writes)
        if (!span.bytes.empty()) out.push_back({span.start, span.end()});
    return out;
}

namespace {

/// The refusal message for an input past kMaxMemoryImageBytes. Only reachable
/// through a caller that already holds a buffer — a caller reading from disk is
/// stopped earlier, and more cheaply, by pom1::readFileBounded().
std::string tooLargeMessage(std::string_view fileName, std::size_t size)
{
    std::ostringstream oss;
    oss << fileName << " is " << std::dec << (size / (1024 * 1024))
        << " MB — too large to be a memory image (limit "
        << (kMaxMemoryImageBytes / (1024 * 1024))
        << " MB; the 6502 addresses 64 KB).";
    return oss.str();
}

bool isHexDigit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

/// Read a hex token as a 16-bit address. False when it names something outside
/// the 6502's 64 KB, so the caller can refuse the line instead of guessing.
///
/// Deliberately NOT strtol. `long` is 64 bits on macOS and Linux and 32 bits on
/// Windows, and an over-wide token saturates differently on each: a TurboType
/// address line reading "100000000" truncated to $0000 on the 64-bit hosts —
/// putting the following bytes into PAGE ZERO — while the 32-bit host clamped
/// to $7FFFFFFF and dropped them. Same malformed file, two machines, two
/// outcomes, neither of them the file's intent. Accumulating explicitly makes
/// the answer the same everywhere and makes "too wide" a decision rather than
/// an accident.
bool parseHexAddress(const std::string& token, uint16_t& out)
{
    std::size_t i = 0;
    while (i < token.size() && token[i] == '0') ++i;   // leading zeros are free
    if (i == token.size()) { out = 0; return true; }
    if (token.size() - i > 4) return false;
    uint32_t v = 0;
    for (; i < token.size(); ++i)
        v = (v << 4) | static_cast<uint32_t>(hexVal(token[i]));
    out = static_cast<uint16_t>(v);
    return true;
}

/// Accumulates writes as maximal contiguous runs, and tracks the first address
/// the file established. Shared by all three dialects — the address bookkeeping
/// is the one thing they genuinely have in common.
class ImageBuilder {
public:
    void writeByte(uint8_t v)
    {
        if (currentAddr_ >= 0x10000) return;   // a 6502 has 64 KB and no more
        if (!spanOpen_) {
            image_.writes.push_back({static_cast<uint16_t>(currentAddr_), {}});
            spanOpen_ = true;
        }
        image_.writes.back().bytes.push_back(v);
        ++currentAddr_;
        ++image_.byteCount;
    }

    /// Move the write pointer. An address line that picks up exactly where the
    /// previous one left off stays in the SAME run: breaking on every address
    /// line would shred chess.txt into one zone per 8-byte row.
    void setAddr(unsigned int addr)
    {
        if (spanOpen_ && addr != currentAddr_) spanOpen_ = false;
        currentAddr_ = addr;
        if (!addressSeen_) {
            image_.startAddress = static_cast<uint16_t>(currentAddr_);
            addressSeen_ = true;
        }
    }

    void setRunAddress(uint16_t addr)   // "AAAAR" — the last one wins
    {
        runAddr_ = addr;
        image_.hasRunAddress = true;
    }

    /// A trailing nibble with nowhere to go. Record the address it would have
    /// landed at: "3 odd runs" sends a user hunting through the whole file,
    /// "first near $0312" points at the line.
    void noteOddDigits()
    {
        if (oddDigitsDropped_ == 0)
            firstOddAddr_ = static_cast<uint16_t>(currentAddr_ & 0xFFFF);
        ++oddDigitsDropped_;
    }

    uint16_t firstOddAddr() const { return firstOddAddr_; }

    bool addressSeen() const { return addressSeen_; }
    bool hasRunAddress() const { return image_.hasRunAddress; }
    int oddDigitsDropped() const { return oddDigitsDropped_; }

    /// Hand over the finished image. `accepted` is the dialect's own verdict on
    /// whether the file said anything at all.
    MemoryImage finish(MemoryImageFormat format, bool accepted)
    {
        image_.format = format;
        image_.ok = accepted;
        if (image_.hasRunAddress) image_.startAddress = runAddr_;
        if (!accepted) {
            // A file that never established an address still produced writes —
            // at $0000, where the pointer starts. The in-place loader dumped
            // those into page zero and only THEN returned its error. Rejected
            // means rejected: nothing escapes for a caller to apply.
            image_.writes.clear();
            image_.byteCount = 0;
        }
        return std::move(image_);
    }

    /// Reject everything parsed so far. The caller gets diagnostics and no
    /// writes, so it cannot half-apply a broken file.
    MemoryImage reject(MemoryImageFormat format, std::string message)
    {
        MemoryImage out;
        out.format = format;
        out.ok = false;
        out.diagnostics.push_back({MemoryImageDiagnostic::Severity::Error,
                                   std::move(message)});
        return out;
    }

    void diagnose(MemoryImageDiagnostic::Severity sev, std::string message)
    {
        image_.diagnostics.push_back({sev, std::move(message)});
    }

private:
    MemoryImage image_;
    unsigned int currentAddr_ = 0;
    bool spanOpen_ = false;
    bool addressSeen_ = false;
    uint16_t runAddr_ = 0;
    int oddDigitsDropped_ = 0;
    uint16_t firstOddAddr_ = 0;
};

/// "12 bytes starting at 0x280" — the tail every summary shares.
std::string summaryTail(int bytes, const char* what, uint16_t addr)
{
    std::ostringstream oss;
    oss << " (" << std::dec << bytes << " bytes " << what << " 0x"
        << std::hex << addr << ")";
    return oss.str();
}

// ---------------------------------------------------------------------------
// Intel HEX — ":LLAAAATT<data>CC".
//
// A completely different container that is also published as ".hex". Fed to the
// WOZMON parser it has no "AAAA:" address, so every digit (byte count, load
// address, record type, checksum) was written as DATA at whatever address
// happened to be current — silent, same class as the old ".apl" fall-through.
//
// looksLikeIntelHex() validates the FIRST record only. Once it says yes we
// COMMIT: a later bad checksum, an unknown record type or a record past $FFFF
// fails the load loudly rather than falling back to the WOZMON parser, which
// would resume writing record headers into RAM.
// ---------------------------------------------------------------------------
MemoryImage parseIntelHexImage(const std::string& content, std::string_view name)
{
    ImageBuilder b;
    const auto kFmt = MemoryImageFormat::IntelHex;

    IntelHexImage ihx;
    std::string error;
    if (!parseIntelHex(content, ihx, &error)) {
        return b.reject(kFmt, "Intel HEX file " + std::string(name) +
                                  " is malformed (" + error + ")");
    }
    for (const IntelHexRecord& rec : ihx.records) {
        // The 6502 has a 16-bit bus. A record past $FFFF means the file targets
        // something else entirely (the type 02/04 base records are an x86
        // inheritance); wrapping it would scribble over page zero.
        if (rec.addr + rec.data.size() > 0x10000) {
            std::ostringstream oss;
            oss << "Intel HEX file " << name << ": record at 0x" << std::hex << rec.addr
                << " lies outside the 6502's 64 KB address space";
            return b.reject(kFmt, oss.str());
        }
        b.setAddr(rec.addr);
        for (uint8_t byte : rec.data) b.writeByte(byte);
    }

    // A type 03/05 start record is this format's "AAAAR". Absent one, the first
    // data address stands.
    if (ihx.hasStart && ihx.start <= 0xFFFF)
        b.setRunAddress(static_cast<uint16_t>(ihx.start));

    const bool accepted = b.addressSeen();
    if (!ihx.sawEof) {
        b.diagnose(MemoryImageDiagnostic::Severity::Warning,
                   "Intel HEX file " + std::string(name) +
                       " has no ':00000001FF' end-of-file record — loaded anyway, "
                       "but it may be truncated.");
    }
    MemoryImage out = b.finish(kFmt, accepted);
    out.diagnostics.insert(
        out.diagnostics.begin(),
        {MemoryImageDiagnostic::Severity::Info,
         "Intel HEX loaded: " + std::string(name) +
             summaryTail(out.byteCount, ihx.hasStart ? "run at" : "start at",
                         out.startAddress)});
    return out;
}

// ---------------------------------------------------------------------------
// TurboType (.TUR) — LINE-STRUCTURED parse.
//
// The legacy parser concatenates every line and recovers boundaries
// heuristically ("a hex token before ':' is an address"). That rule is exactly
// INVERTED in a turbo stream, where ':' OPENS a data line instead of closing an
// address:
//
//     0300                                  <- address, own line, no ':'
//     :D8A2FF9AA92A851A204604A97C8518A9     <- 16 bytes, ':' first
//     :05851920AD0320CB03D00EA9AF8518A9
//
// Concatenated, each 32-digit data run is followed by the next line's ':' — so
// the legacy rule split off its last four digits as an address and scattered
// the program across memory (the 15 Puzzle's $0300 block landed in 60+ zones at
// $18A9, $4159, $F460, ...). Keeping the lines apart makes every case
// unambiguous and needs none of the merge hacks.
// ---------------------------------------------------------------------------
MemoryImage parseTurboTypeImage(const std::vector<std::string>& lines,
                                std::string_view name)
{
    ImageBuilder b;
    // 'X' = end of stream. Past it the only thing a .TUR may still carry is the
    // "AAAAR" run line, so stray hex must NOT be written as data.
    bool endMarkerSeen = false;
    int badAddressLines = 0;

    auto emitData = [&](const std::string& s, size_t from) {
        std::string digits;
        for (size_t k = from; k < s.size(); ++k)
            if (isHexDigit(s[k])) digits.push_back(s[k]);
        if (digits.size() % 2 != 0) b.noteOddDigits();
        for (size_t j = 0; j + 1 < digits.size(); j += 2)
            b.writeByte(static_cast<uint8_t>((hexVal(digits[j]) << 4) | hexVal(digits[j + 1])));
    };

    for (const std::string& s : lines) {
        const char c0 = s[0];
        // Bare markers: 'T' switches the SENDER to turbo mode, 'X' ends the
        // stream. Neither carries an operand — the run address that follows 'X'
        // is its own "AAAAR" line.
        if (s.size() == 1 && (c0 == 'T' || c0 == 't')) continue;
        if (s.size() == 1 && (c0 == 'X' || c0 == 'x')) { endMarkerSeen = true; continue; }
        // ":data" — turbo block line, or a WOZMON continuation line.
        if (c0 == ':') { if (!endMarkerSeen) emitData(s, 1); continue; }

        size_t p = 0;
        while (p < s.size() && isHexDigit(s[p])) p++;
        if (p == 0) continue;                       // not a line we understand

        // Unlike the joined parser below, this one has no merged-token rule to
        // fall back on: a token here is meant to BE an address. One that cannot
        // be is a malformed line, so skip it and say so rather than truncate it
        // to some 16-bit remainder and write the data that follows there.
        uint16_t tok = 0;
        if (!parseHexAddress(s.substr(0, p), tok)) {
            ++badAddressLines;
            continue;
        }

        if (p < s.size() && (s[p] == 'R' || s[p] == 'r')) {
            b.setRunAddress(tok);
            continue;
        }
        if (p < s.size() && s[p] == ':') {          // WOZMON "AAAA: HH HH ..."
            b.setAddr(tok);
            if (!endMarkerSeen) emitData(s, p + 1);
            continue;
        }
        if (p == s.size()) { b.setAddr(tok); continue; }   // bare address line
    }

    const bool accepted = b.addressSeen() || b.hasRunAddress();
    if (b.oddDigitsDropped() > 0) {
        std::ostringstream oss;
        oss << "TurboType dump " << name << ": " << std::dec << b.oddDigitsDropped()
            << " odd-length hex run(s) — trailing nibble(s) dropped, first near 0x"
            << std::hex << b.firstOddAddr() << ".";
        b.diagnose(MemoryImageDiagnostic::Severity::Warning, oss.str());
    }
    if (badAddressLines > 0) {
        std::ostringstream oss;
        oss << "TurboType dump " << name << ": " << std::dec << badAddressLines
            << " line(s) name an address outside the 6502's 64 KB — skipped.";
        b.diagnose(MemoryImageDiagnostic::Severity::Warning, oss.str());
    }
    MemoryImage out = b.finish(MemoryImageFormat::TurboType, accepted);
    out.diagnostics.insert(
        out.diagnostics.begin(),
        {MemoryImageDiagnostic::Severity::Info,
         "TurboType dump loaded: " + std::string(name) +
             summaryTail(out.byteCount, "run at", out.startAddress)});
    return out;
}

// ---------------------------------------------------------------------------
// WOZMON hex — the legacy dialect, and the one ~100 bundled dumps use.
//
// Joins every surviving line into ONE string and recovers boundaries
// heuristically. The "merged data + address" and "merged data + run" branches
// exist precisely to undo that joining; they must stay, because real dumps
// depend on them.
// ---------------------------------------------------------------------------
MemoryImage parseWozmonImage(const std::string& cleaned, std::string_view name)
{
    ImageBuilder b;
    bool endMarkerSeen = false;
    size_t i = 0;

    while (i < cleaned.size()) {
        const char c = cleaned[i];

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }

        // 'T' = TurboType mode switch, on its own line between the autotyped
        // WOZMON prologue and the first turbo block. A bare marker: skip it and
        // let the "AAAA:" that follows parse as an address.
        if ((c == 'T' || c == 't') && i + 1 < cleaned.size() && isHexDigit(cleaned[i + 1])) {
            i++; continue;
        }

        // 'X' = TurboType end-of-stream marker, likewise on its own line. It is
        // a BARE marker — skip only the X itself.
        //
        // This used to also swallow the hex run behind it, which silently ate
        // the run address of every real .TUR: the files end
        //
        //     X
        //     015ER
        //
        // so "015E" was consumed as if it belonged to the X and the orphaned
        // "R" fell through to the unknown-character branch. The load then
        // reported the PREVIOUS R (the "0100R" that starts the serial receiver
        // on real hardware) and POM1 jumped into the transfer loader, which
        // sits waiting for a stream that direct injection never sends.
        if ((c == 'X' || c == 'x') && i + 1 < cleaned.size() && isHexDigit(cleaned[i + 1])) {
            endMarkerSeen = true;
            i++; continue;
        }

        if (c == ':') { i++; continue; }   // continuation — hex data follows

        if (isHexDigit(c)) {
            const size_t hexStart = i;
            while (i < cleaned.size() && isHexDigit(cleaned[i])) i++;
            std::string hexStr = cleaned.substr(hexStart, i - hexStart);

            // What follows decides the meaning: 'R' = run, ':' = address,
            // anything else = data. Skip whitespace to find it.
            size_t peek = i;
            while (peek < cleaned.size() &&
                   (cleaned[peek] == ' ' || cleaned[peek] == '\t' ||
                    cleaned[peek] == '\r' || cleaned[peek] == '\n')) peek++;

            auto emitMergedPrefix = [&](size_t keepDigits) {
                if (hexStr.size() <= keepDigits) return;
                const size_t dataLen = hexStr.size() - keepDigits;
                if (dataLen % 2 != 0) b.noteOddDigits();
                for (size_t j = 0; j + 1 < dataLen; j += 2)
                    b.writeByte(static_cast<uint8_t>((hexVal(hexStr[j]) << 4) |
                                                     hexVal(hexStr[j + 1])));
                hexStr = hexStr.substr(dataLen);
            };

            if (i < cleaned.size() && (cleaned[i] == 'R' || cleaned[i] == 'r')) {
                emitMergedPrefix(4);    // "FFE2B3R" = data FF, run E2B3
                // emitMergedPrefix leaves at most four digits, so this cannot
                // fail; go through the checked accumulator anyway so no future
                // edit to the trim rule can reintroduce strtol's platform-
                // dependent saturation (see parseHexAddress).
                uint16_t addr = 0;
                if (parseHexAddress(hexStr, addr)) b.setRunAddress(addr);
                i++;                    // skip the R
                continue;
            }

            // A hex token before ':' is an ADDRESS only when it is >= 3 hex
            // digits (real addresses in these dumps are 4; 3 covers a rare
            // "300:"; the merged data+address case ">4" is split below).
            //
            // A 1-2 digit token before ':' is a DATA byte followed by a
            // group-separator colon — several bundled programs (mandelbrot-65,
            // 2048, cat, cellular, 50th) format one contiguous line as
            // "0280:4C 5F 03 2E 2E 2C 27 5E:3D 2B ..." with a ':' every 8th
            // byte. Treating that trailing "5E" as address $005E used to
            // scatter the whole program across zero page (zones=166) so it
            // crashed to $0000 on the very first JMP. Chess/Connect4 etc. put
            // ':' only after their 4-digit line address, so they are unaffected.
            if (peek < cleaned.size() && cleaned[peek] == ':' && hexStr.size() >= 3) {
                emitMergedPrefix(4);    // "ED0300:" = data ED, address 0300
                uint16_t addr = 0;
                if (parseHexAddress(hexStr, addr)) b.setAddr(addr);
                i = peek + 1;           // skip the ':'
                continue;
            }

            // Past the 'X' end-of-stream marker there is no more data — only a
            // possible run line, and that took the 'R' branch above. Dropping
            // the token here (rather than writing it) keeps a trailing
            // address-shaped word from landing in RAM at currentAddr.
            if (endMarkerSeen) continue;

            // Data bytes, in pairs. A lone trailing nibble would otherwise be
            // dropped in silence, so count it for the warning below — it masks
            // real bugs in hand-edited dumps.
            if (hexStr.size() % 2 != 0) b.noteOddDigits();
            for (size_t j = 0; j + 1 < hexStr.size(); j += 2)
                b.writeByte(static_cast<uint8_t>((hexVal(hexStr[j]) << 4) |
                                                 hexVal(hexStr[j + 1])));
            continue;
        }

        i++;   // unknown character — skip
    }

    const bool accepted = b.addressSeen() || b.hasRunAddress();
    if (b.oddDigitsDropped() > 0) {
        std::ostringstream oss;
        oss << "Hex dump " << name << ": " << std::dec << b.oddDigitsDropped()
            << " odd-length hex run(s) detected — trailing nibble(s) dropped, "
            << "first near 0x" << std::hex << b.firstOddAddr()
            << ". Check the source for a truncated byte.";
        b.diagnose(MemoryImageDiagnostic::Severity::Warning, oss.str());
    }
    MemoryImage out = b.finish(MemoryImageFormat::WozmonHex, accepted);
    out.diagnostics.insert(
        out.diagnostics.begin(),
        {MemoryImageDiagnostic::Severity::Info,
         "Hex dump loaded: " + std::string(name) +
             summaryTail(out.byteCount, "starting at", out.startAddress)});
    return out;
}

} // namespace

MemoryImage parseMemoryImage(std::string_view contentView, std::string_view fileName)
{
    const std::string name(fileName);

    // Refuse before allocating. Everything below makes at least two more copies
    // of the input, so the bound has to be checked here rather than discovered
    // by the allocator. Memory::loadHexDump() checks the file's size first and
    // never reads it at all; this is the backstop for every other caller.
    if (contentView.size() > kMaxMemoryImageBytes) {
        MemoryImage out;
        out.diagnostics.push_back(
            {MemoryImageDiagnostic::Severity::Error,
             tooLargeMessage(name, contentView.size())});
        return out;
    }

    const std::string content(contentView);

    // Structural detection FIRST, before any extension is consulted: an Intel
    // HEX named ".txt" must be read as Intel HEX, and a WOZMON dump named
    // ".hex" must not. No bundled dump even starts a line with ':'.
    if (looksLikeIntelHex(content))
        return parseIntelHexImage(content, name);

    // Strip comments (//, ; and #), in-line or whole-line. `cleaned` joins the
    // survivors WITHOUT a separator — the legacy parser's merge branches exist
    // precisely to undo that — while `lines` keeps them apart for TurboType,
    // which needs the line structure to tell ":data" from "address:".
    //
    // The '#' branch is not decoration: without it a hex-looking in-line
    // comment ("0300: AB # BADC0DE") tokenises "BADC0DE" as data and silently
    // corrupts memory.
    std::string cleaned;
    std::vector<std::string> lines;
    bool turboFile = lowerExtension(name) == "tur";

    std::istringstream lineStream(content);
    std::string line;
    while (std::getline(lineStream, line)) {
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        const char first = line[start];
        if (first == '#' || first == ';') continue;
        if (start + 1 < line.size() && first == '/' && line[start + 1] == '/') continue;
        for (const char* marker : {"//", ";", "#"}) {
            const size_t at = line.find(marker);
            if (at != std::string::npos) line = line.substr(0, at);
        }
        cleaned += line;

        const size_t b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos) continue;
        const size_t e = line.find_last_not_of(" \t\r");
        lines.push_back(line.substr(b, e - b + 1));
        // A lone "T" is TurboType's switch-to-turbo marker, and the only thing
        // besides the ".tur" extension that selects that parser.
        if (lines.back().size() == 1 && (lines.back()[0] == 'T' || lines.back()[0] == 't'))
            turboFile = true;
    }

    return turboFile ? parseTurboTypeImage(lines, name)
                     : parseWozmonImage(cleaned, name);
}

} // namespace pom1
