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
    readMagicHeader();
}

SnapshotReader::SnapshotReader(const std::vector<uint8_t>& buffer)
    : owned(std::make_unique<std::istringstream>(
          std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()),
          std::ios::binary | std::ios::in)),
      in(*owned)
{
    readMagicHeader();
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
    std::string s(len, '\0');
    if (len) readBytes(s.data(), len);
    return s;
}

std::vector<uint8_t> SnapshotReader::readByteVector() {
    const uint32_t len = readU32();
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

    cursor     = in.tellg();
    sectionEnd = cursor + static_cast<std::streamoff>(length);
    return true;
}

void SnapshotReader::skipCurrentSection() {
    in.seekg(sectionEnd);
    cursor = sectionEnd;
}

} // namespace pom1
