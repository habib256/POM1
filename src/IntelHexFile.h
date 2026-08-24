#pragma once
// IntelHexFile.h -- Intel HEX detection + parser.
//
// POM1's ".hex" extension routes to Memory::loadHexDump, whose parser reads the
// WOZMON dialect ("AAAA: HH HH ... / AAAAR"). Intel HEX is a completely
// different container that also gets published as ".hex":
//
//     :10030000D8A2FF9AA92A851A204604A97C8518A9C3
//      ^ ^   ^ ^                                ^
//      | |   | |                                checksum (two's complement)
//      | |   | record type (00 data, 01 EOF, 02/04 base, 03/05 start)
//      | |   load address, big-endian
//      | byte count
//      start-of-record marker
//
// Fed to the WOZMON parser, that line has no "AAAA:" address, so every digit
// (count, address, type AND checksum included) is written as DATA at whatever
// address happened to be current -- no error, no warning. Exactly the class of
// silent corruption that the ".apl" routing bug produced before HexDumpFile.h
// existed (see that header), which is why detection lives in its own predicate
// rather than as an inline special case in the parser.
//
// Detection is deliberately structural, never extension-based: a file's SHAPE
// decides, so an Intel HEX published as ".txt" or ".apl" is read correctly too,
// and a WOZMON dump named ".hex" keeps its behaviour byte for byte.
//
// The 6502 address space is 16 bits; the type 02/04 base-address records exist
// only because Intel HEX was designed for segmented x86. They are honoured (a
// generator may emit a leading ":020000040000FA") and any record that ends up
// beyond $FFFF is reported as an error by the caller rather than wrapped.

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace pom1 {

struct IntelHexRecord {
    uint32_t addr = 0;          // base + per-record offset, full linear address
    std::vector<uint8_t> data;
};

struct IntelHexImage {
    std::vector<IntelHexRecord> records;  // type-00 records, in file order
    bool hasStart = false;                // a type 03/05 start record was present
    uint32_t start = 0;
    bool sawEof = false;                  // a type-01 record terminated the stream
};

namespace detail {

inline bool ihxIsHex(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

inline int ihxVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Strip leading/trailing whitespace (\r included, so CRLF files parse).
inline std::string ihxTrim(const std::string& s)
{
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// One record, fully validated: ':' + an even run of hex digits, a byte count
// consistent with the line length, a record type in range, and a checksum that
// makes the whole record sum to zero mod 256. Returns false on any violation.
// `bytes` receives count/addrHi/addrLo/type/data.../checksum.
inline bool ihxDecodeRecord(const std::string& line, std::vector<uint8_t>& bytes)
{
    if (line.size() < 11 || line[0] != ':') return false;
    const size_t digits = line.size() - 1;
    if (digits % 2 != 0) return false;
    bytes.clear();
    bytes.reserve(digits / 2);
    unsigned sum = 0;
    for (size_t i = 1; i + 1 < line.size(); i += 2) {
        const int hi = ihxVal(line[i]);
        const int lo = ihxVal(line[i + 1]);
        if (hi < 0 || lo < 0) return false;
        const uint8_t b = static_cast<uint8_t>((hi << 4) | lo);
        bytes.push_back(b);
        sum += b;
    }
    // count + addr(2) + type + data + checksum
    if (bytes.size() != static_cast<size_t>(bytes[0]) + 5u) return false;
    if (bytes[3] > 0x05) return false;
    return (sum & 0xFFu) == 0u;
}

} // namespace detail

// True when `content`'s FIRST non-blank line is a structurally valid Intel HEX
// record. Used to decide whether a parse failure further down is a broken Intel
// HEX file (loud error) or simply "this was never Intel HEX" (fall through to
// the WOZMON parser).
//
// A WOZMON continuation line (":85 15 85 12 ...") or a TurboType data line
// (":D8A2FF9A...") also opens with ':', so the discriminator is not the colon
// but the three structural constraints checked together -- byte count matching
// the line length, record type <= 5, and a valid checksum. A random hex run
// clearing all three is on the order of 1 in 10 000, and the outcome is a
// refusal to load with a message, never a silent bad write.
inline bool looksLikeIntelHex(const std::string& content)
{
    size_t pos = 0;
    bool first = true;
    while (pos <= content.size()) {
        const size_t nl = content.find('\n', pos);
        std::string line =
            detail::ihxTrim(content.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        // A UTF-8 BOM only ever precedes the very first line, and an editor on
        // Windows adds one without asking. Left in place it made the first
        // record fail to decode, so the whole file fell through to the WOZMON
        // parser — which writes the record HEADERS into RAM as data.
        if (first && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line = detail::ihxTrim(line.substr(3));
        }
        first = false;
        // Skip the comment lines assemblers and converters put at the top
        // (`; built by ca65`, `# srec_cat …`). The WOZMON parser strips these
        // too, so a commented Intel HEX file used to reach it looking like a
        // hex dump and get silently mis-written: byte count, load address and
        // record type all land in RAM as data, at whatever address happens to
        // be current, and the load reports SUCCESS. Detection has to see past
        // them for the "commit, then fail loudly" contract above to hold.
        if (!line.empty() && line[0] != ';' && line[0] != '#' &&
            line.compare(0, 2, "//") != 0) {
            std::vector<uint8_t> bytes;
            return detail::ihxDecodeRecord(line, bytes);
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return false;
}

// Full parse. Returns false when `content` is not a well-formed Intel HEX file;
// `*error` (optional) then describes the first violation, with a 1-based line
// number. Blank lines are skipped; anything after a type-01 EOF record is
// ignored, as the format specifies.
inline bool parseIntelHex(const std::string& content, IntelHexImage& out, std::string* error = nullptr)
{
    out = IntelHexImage();
    uint32_t base = 0;
    int lineNo = 0;
    size_t pos = 0;
    bool done = false;

    auto fail = [&](const char* what) {
        if (error) *error = "line " + std::to_string(lineNo) + ": " + what;
        return false;
    };

    while (pos <= content.size() && !done) {
        const size_t nl = content.find('\n', pos);
        const std::string raw =
            content.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? content.size() + 1 : nl + 1;
        lineNo++;

        std::string line = detail::ihxTrim(raw);
        // Strip a leading UTF-8 BOM on line 1 and skip comment lines, exactly
        // as looksLikeIntelHex does — the two MUST agree on what counts as a
        // record, or a file the detector accepts dies here on its first line.
        if (lineNo == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line = detail::ihxTrim(line.substr(3));
        }
        if (line.empty() || line[0] == ';' || line[0] == '#' ||
            line.compare(0, 2, "//") == 0) continue;

        std::vector<uint8_t> b;
        if (!detail::ihxDecodeRecord(line, b))
            return fail("not a valid Intel HEX record (count, type or checksum)");

        const uint8_t count = b[0];
        const uint32_t offset = static_cast<uint32_t>((b[1] << 8) | b[2]);
        const uint8_t type = b[3];

        switch (type) {
        case 0x00: {                              // data
            IntelHexRecord rec;
            rec.addr = base + offset;
            rec.data.assign(b.begin() + 4, b.begin() + 4 + count);
            out.records.push_back(std::move(rec));
            break;
        }
        case 0x01:                                // end of file
            out.sawEof = true;
            done = true;
            break;
        case 0x02:                                // extended SEGMENT address
            if (count != 2) return fail("type 02 record must carry 2 bytes");
            base = (static_cast<uint32_t>((b[4] << 8) | b[5])) << 4;
            break;
        case 0x04:                                // extended LINEAR address
            if (count != 2) return fail("type 04 record must carry 2 bytes");
            base = (static_cast<uint32_t>((b[4] << 8) | b[5])) << 16;
            break;
        case 0x03:                                // start segment address (CS:IP)
            if (count != 4) return fail("type 03 record must carry 4 bytes");
            out.start = ((static_cast<uint32_t>((b[4] << 8) | b[5])) << 4) +
                        static_cast<uint32_t>((b[6] << 8) | b[7]);
            out.hasStart = true;
            break;
        case 0x05:                                // start linear address (EIP)
            if (count != 4) return fail("type 05 record must carry 4 bytes");
            out.start = (static_cast<uint32_t>(b[4]) << 24) | (static_cast<uint32_t>(b[5]) << 16) |
                        (static_cast<uint32_t>(b[6]) << 8) | static_cast<uint32_t>(b[7]);
            out.hasStart = true;
            break;
        default:
            return fail("unknown record type");
        }
    }

    if (out.records.empty()) {
        if (error) *error = "no data record";
        return false;
    }
    return true;
}

}  // namespace pom1
