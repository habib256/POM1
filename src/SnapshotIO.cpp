// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SnapshotIO — see SnapshotIO.h for format documentation.

#include "SnapshotIO.h"

#include <algorithm>
#include <cstring>

namespace pom1 {

namespace {

void writeFixedName(std::ostream& out, std::string_view name) {
    char buf[kSectionNameLen]{};
    const std::size_t copy = std::min(name.size(), kSectionNameLen);
    std::memcpy(buf, name.data(), copy);
    // remaining bytes already zeroed by aggregate init.
    out.write(buf, kSectionNameLen);
}

std::string readFixedName(std::istream& in) {
    char buf[kSectionNameLen]{};
    in.read(buf, kSectionNameLen);
    // NUL-trim — sections shorter than 8 chars are right-padded.
    std::size_t len = 0;
    while (len < kSectionNameLen && buf[len] != '\0') ++len;
    return std::string(buf, len);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────
// Writer
// ─────────────────────────────────────────────────────────────────────
SnapshotWriter::SnapshotWriter(const std::string& path)
    : owned(std::make_unique<std::ofstream>(path, std::ios::binary | std::ios::trunc)),
      out(*owned)
{
    if (!out.good()) return;
    writeMagicHeader();
}

SnapshotWriter::SnapshotWriter()
    : owned(std::make_unique<std::ostringstream>(std::ios::binary | std::ios::out)),
      mem(static_cast<std::ostringstream*>(owned.get())),
      out(*owned)
{
    writeMagicHeader();
}

void SnapshotWriter::writeMagicHeader() {
    out.write(kSnapshotMagic, sizeof(kSnapshotMagic));
    writeU32(kSnapshotVersion);
    writeU32(0);  // flags reserved
}

std::vector<uint8_t> SnapshotWriter::takeBuffer() {
    if (!mem) return {};
    out.flush();
    const std::string s = mem->str();
    return std::vector<uint8_t>(s.begin(), s.end());
}

void SnapshotWriter::writeU8(uint8_t v) {
    out.put(static_cast<char>(v));
}
void SnapshotWriter::writeU16(uint16_t v) {
    char buf[2] = { static_cast<char>(v & 0xFF),
                    static_cast<char>((v >> 8) & 0xFF) };
    out.write(buf, 2);
}
void SnapshotWriter::writeU32(uint32_t v) {
    char buf[4] = { static_cast<char>(v & 0xFF),
                    static_cast<char>((v >> 8) & 0xFF),
                    static_cast<char>((v >> 16) & 0xFF),
                    static_cast<char>((v >> 24) & 0xFF) };
    out.write(buf, 4);
}
void SnapshotWriter::writeU64(uint64_t v) {
    writeU32(static_cast<uint32_t>(v & 0xFFFFFFFFu));
    writeU32(static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu));
}
void SnapshotWriter::writeBytes(const void* data, std::size_t length) {
    if (length > 0) out.write(static_cast<const char*>(data),
                              static_cast<std::streamsize>(length));
}

void SnapshotWriter::writeString(std::string_view s) {
    writeU32(static_cast<uint32_t>(s.size()));
    writeBytes(s.data(), s.size());
}

void SnapshotWriter::writeByteVector(const std::vector<uint8_t>& v) {
    writeU32(static_cast<uint32_t>(v.size()));
    writeBytes(v.data(), v.size());
}

SnapshotWriter::SectionHandle SnapshotWriter::beginSection(std::string_view name) {
    SectionHandle h{};
    writeFixedName(out, name);
    h.lengthSlot = out.tellp();
    writeU32(0);                  // placeholder length
    h.payloadStart = out.tellp();
    return h;
}

void SnapshotWriter::endSection(SectionHandle h) {
    const std::streampos endPos = out.tellp();
    const auto length = static_cast<uint32_t>(endPos - h.payloadStart);
    out.seekp(h.lengthSlot);
    writeU32(length);
    out.seekp(endPos);
}

void SnapshotWriter::writeSection(std::string_view name, const void* data, std::size_t length) {
    SectionHandle h = beginSection(name);
    writeBytes(data, length);
    endSection(h);
}

// ─────────────────────────────────────────────────────────────────────
// Reader
// ─────────────────────────────────────────────────────────────────────
SnapshotReader::SnapshotReader(const std::string& path)
    : owned(std::make_unique<std::ifstream>(path, std::ios::binary)),
      in(*owned)
{
    if (!in.good()) {
        errorMsg = "cannot open snapshot file: " + path;
        return;
    }
    measureStreamSize();
    readMagicHeader();
}

SnapshotReader::SnapshotReader(const std::vector<uint8_t>& buffer)
    : owned(std::make_unique<std::istringstream>(
          std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()),
          std::ios::binary | std::ios::in)),
      in(*owned)
{
    measureStreamSize();
    readMagicHeader();
}

void SnapshotReader::measureStreamSize() {
    in.seekg(0, std::ios::end);
    const std::streampos end = in.tellg();
    in.seekg(0, std::ios::beg);
    streamSize = (end > std::streampos(0)) ? static_cast<std::streamoff>(end) : 0;
}

std::streamoff SnapshotReader::remainingBytes() {
    const std::streampos pos = in.tellg();
    if (pos < std::streampos(0)) return 0;
    const std::streamoff rem = streamSize - static_cast<std::streamoff>(pos);
    return rem > 0 ? rem : 0;
}

void SnapshotReader::readMagicHeader() {
    char magic[sizeof(kSnapshotMagic)]{};
    in.read(magic, sizeof(kSnapshotMagic));
    if (!in.good() || std::memcmp(magic, kSnapshotMagic, sizeof(kSnapshotMagic)) != 0) {
        errorMsg = "snapshot magic mismatch (not a POM1 snapshot)";
        return;
    }

    ver = readU32();
    (void)readU32();              // flags reserved
    if (ver == 0 || ver > kSnapshotVersion) {
        errorMsg = "unsupported snapshot version " + std::to_string(ver);
        return;
    }
    ok = true;
    cursor     = in.tellg();
    sectionEnd = cursor;
}

uint8_t SnapshotReader::readU8() {
    char c = 0;
    in.read(&c, 1);
    return static_cast<uint8_t>(c);
}
uint16_t SnapshotReader::readU16() {
    unsigned char buf[2]{};
    in.read(reinterpret_cast<char*>(buf), 2);
    return static_cast<uint16_t>(buf[0] | (uint16_t(buf[1]) << 8));
}
uint32_t SnapshotReader::readU32() {
    unsigned char buf[4]{};
    in.read(reinterpret_cast<char*>(buf), 4);
    return static_cast<uint32_t>(buf[0])
         | (static_cast<uint32_t>(buf[1]) << 8)
         | (static_cast<uint32_t>(buf[2]) << 16)
         | (static_cast<uint32_t>(buf[3]) << 24);
}
uint64_t SnapshotReader::readU64() {
    uint64_t lo = readU32();
    uint64_t hi = readU32();
    return lo | (hi << 32);
}
void SnapshotReader::readBytes(void* data, std::size_t length) {
    if (length > 0) in.read(static_cast<char*>(data),
                            static_cast<std::streamsize>(length));
}

std::string SnapshotReader::readString() {
    const uint32_t len = readU32();
    // Reject a length that can't fit in the bytes still present — a corrupt or
    // truncated snapshot must fail cleanly, not attempt a huge allocation.
    if (static_cast<std::streamoff>(len) > remainingBytes()) {
        in.setstate(std::ios::failbit);
        return {};
    }
    std::string s(len, '\0');
    if (len) readBytes(s.data(), len);
    return s;
}

std::vector<uint8_t> SnapshotReader::readByteVector() {
    const uint32_t len = readU32();
    if (static_cast<std::streamoff>(len) > remainingBytes()) {
        in.setstate(std::ios::failbit);
        return {};
    }
    std::vector<uint8_t> v(len);
    if (len) readBytes(v.data(), len);
    return v;
}

bool SnapshotReader::nextSection(std::string& name, std::uint32_t& length) {
    if (!ok) return false;
    // Skip any unread tail of the previous section so the caller can
    // safely loop without remembering to call skipCurrentSection.
    if (cursor != sectionEnd) {
        in.seekg(sectionEnd);
        cursor = sectionEnd;
    }
    if (in.peek() == EOF) return false;

    name = readFixedName(in);
    if (!in.good()) return false;
    length = readU32();
    if (!in.good()) return false;

    cursor = in.tellg();
    // Reject a forged section length that overruns the bytes still present, the
    // same guard readString()/readByteVector() apply. Without it a handler that
    // consumes `length` bytes directly would read past the buffer.
    if (static_cast<std::streamoff>(length) > remainingBytes()) {
        in.setstate(std::ios::failbit);
        return false;
    }
    sectionEnd = cursor + static_cast<std::streamoff>(length);
    return true;
}

void SnapshotReader::skipCurrentSection() {
    in.seekg(sectionEnd);
    cursor = sectionEnd;
}

const SnapshotOutline::Section* SnapshotOutline::find(std::string_view name) const
{
    for (const Section& s : sections)
        if (s.name == name) return &s;
    return nullptr;
}

SnapshotOutline outlineSnapshot(const std::uint8_t* data, std::size_t size)
{
    SnapshotOutline out;
    auto fail = [&out](std::string message) {
        out.ok = false;
        out.sections.clear();
        out.error = std::move(message);
        return out;
    };

    if (size > kMaxSnapshotBytes)
        return fail("snapshot is too large to be a POM1 machine state");
    // magic(8) + version(4) + flags(4)
    constexpr std::size_t kHeaderLen = sizeof(kSnapshotMagic) + 4 + 4;
    if (size < kHeaderLen || std::memcmp(data, kSnapshotMagic, sizeof(kSnapshotMagic)) != 0)
        return fail("snapshot magic mismatch (not a POM1 snapshot)");

    auto readU32At = [data](std::size_t at) {
        return static_cast<std::uint32_t>(data[at]) |
               (static_cast<std::uint32_t>(data[at + 1]) << 8) |
               (static_cast<std::uint32_t>(data[at + 2]) << 16) |
               (static_cast<std::uint32_t>(data[at + 3]) << 24);
    };

    out.version = readU32At(sizeof(kSnapshotMagic));
    if (out.version == 0 || out.version > kSnapshotVersion)
        return fail("unsupported snapshot version " + std::to_string(out.version));

    std::size_t at = kHeaderLen;
    while (at < size) {
        // A trailing fragment too short to hold a section header is a truncated
        // file, not a section-free tail: say so rather than stopping quietly.
        if (size - at < kSectionNameLen + 4)
            return fail("truncated snapshot: incomplete section header");

        const char* raw = reinterpret_cast<const char*>(data + at);
        std::size_t nameLen = 0;
        while (nameLen < kSectionNameLen && raw[nameLen] != '\0') ++nameLen;
        SnapshotOutline::Section section;
        section.name.assign(raw, nameLen);
        section.length = readU32At(at + kSectionNameLen);
        section.payloadOffset = at + kSectionNameLen + 4;

        // The whole point of the walk. Compare against the bytes REMAINING —
        // payloadOffset + length would wrap on a 32-bit size_t.
        if (section.length > size - section.payloadOffset)
            return fail("truncated snapshot: section \"" + section.name +
                        "\" declares " + std::to_string(section.length) +
                        " bytes but only " +
                        std::to_string(size - section.payloadOffset) + " remain");

        at = section.payloadOffset + section.length;
        out.sections.push_back(std::move(section));
    }

    if (out.sections.empty())
        return fail("snapshot contains no sections");

    out.ok = true;
    return out;
}

bool validateSnapshot(const std::uint8_t* data, std::size_t size, std::string& error)
{
    const SnapshotOutline outline = outlineSnapshot(data, size);
    if (!outline.ok) {
        error = outline.error;
        return false;
    }

    // MEM is a fixed 64 KB RAM image plus its trailing scalars. A shorter
    // declared length would make the apply pass consume bytes belonging to the
    // next section and load garbage into RAM while reporting success.
    if (const auto* mem = outline.find("MEM")) {
        if (mem->length != kMemSectionLen && mem->length != kMemSectionLenV5) {
            error = "corrupt snapshot: MEM section length " +
                    std::to_string(mem->length) + " (expected " +
                    std::to_string(kMemSectionLen) + ", or " +
                    std::to_string(kMemSectionLenV5) + " pre-v6)";
            return false;
        }
    }

    // GEN2VID (v5+) carries a count-then-elements journal, and the count drives
    // a reserve(). Bound it by the payload that is actually there: each event is
    // emuCycle(8) + kind(1) + value(1), so a count larger than the section can
    // hold is corruption whatever the machine's own per-frame cap happens to be.
    if (outline.version >= 5) {
        if (const auto* vid = outline.find("GEN2VID")) {
            constexpr std::uint32_t kCountOffset = 4 + 1 + 8;   // state + 50 Hz + cycle
            constexpr std::uint32_t kEventLen = 8 + 1 + 1;
            if (vid->length >= kCountOffset + 4) {
                const std::uint8_t* p = data + vid->payloadOffset + kCountOffset;
                const std::uint32_t n = static_cast<std::uint32_t>(p[0]) |
                                        (static_cast<std::uint32_t>(p[1]) << 8) |
                                        (static_cast<std::uint32_t>(p[2]) << 16) |
                                        (static_cast<std::uint32_t>(p[3]) << 24);
                const std::uint32_t room = vid->length - kCountOffset - 4;
                if (static_cast<std::uint64_t>(n) * kEventLen > room) {
                    error = "corrupt snapshot: GEN2VID declares " +
                            std::to_string(n) + " video events but carries " +
                            std::to_string(room) + " bytes";
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace pom1
