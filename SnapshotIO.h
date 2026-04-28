// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SnapshotIO — binary read/write primitives for the POM1 snapshot format.
//
// Format (versioned, little-endian throughout — POM1 is host-side LE only):
//
//   "POM1SNAP" (8 bytes)               magic
//   uint32_t  version                  format version (current = 1)
//   uint32_t  flags                    reserved, 0 for now
//
//   Section: "CPU\0\0\0\0\0" (8 bytes name) + uint32_t length + payload
//     payload: PC, A, X, Y, SP, status, IRQ, NMI, cycles  (M6502::serialize)
//
//   Section: "MEM\0\0\0\0\0" + uint32_t length + payload
//     payload: full 64 KB RAM, displayBusyCycles, last keyboard byte +
//              ready bit, ramSize, presetRamKB, oorAccessCount, all 12
//              card-enabled flags packed into 2 bytes.
//
//   Section per peripheral: name (8 bytes, NUL-padded) + uint32_t length +
//   peripheral->serialize() payload.  Unknown sections are skipped on load
//   (forward compat).  Cards that haven't migrated their state yet write
//   length==0 (no-op default in `Peripheral::serialize`).
//
// Everything is plain `fwrite`/`fread`. There is no compression, no checksum
// (yet — that's a v2 sweetener); the file is small (~70 KB) and the use case
// is "snapshot now, reload now" rather than archival cold storage. A
// SHA-256 footer would be a one-line append in v2 if a contributor wants it.

#ifndef POM1_SNAPSHOT_IO_H
#define POM1_SNAPSHOT_IO_H

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace pom1 {

/// Magic + version constants — exposed so tests can hand-write a snapshot
/// file and assert the reader recognises it.
inline constexpr char     kSnapshotMagic[8] = {'P','O','M','1','S','N','A','P'};
inline constexpr uint32_t kSnapshotVersion  = 1;

/// Section names are 8 bytes, NUL-padded. 8 bytes keeps the file aligned
/// and reads cheaply via fixed-size buffers. Names beyond 8 chars are
/// truncated by writeSection.
inline constexpr std::size_t kSectionNameLen = 8;

/// Append-only writer. The lifetime of the underlying ofstream is owned by
/// the writer; the destructor closes cleanly. Errors are reported via
/// `good()` after the final flush — POM1 does not throw from emulator-thread
/// code.
class SnapshotWriter {
public:
    explicit SnapshotWriter(const std::string& path);
    ~SnapshotWriter() = default;

    bool good() const { return out.good(); }

    void writeU8 (uint8_t  v);
    void writeU16(uint16_t v);
    void writeU32(uint32_t v);
    void writeU64(uint64_t v);
    void writeBytes(const void* data, std::size_t length);

    /// Begin a named section. Writes the 8-byte name and a placeholder
    /// length; returns a handle the caller passes back to `endSection()`
    /// once the payload has been streamed. Sections cannot nest.
    struct SectionHandle { std::streampos lengthSlot{}; std::streampos payloadStart{}; };
    SectionHandle beginSection(std::string_view name);
    void          endSection(SectionHandle handle);

    /// Convenience for sections backed by a contiguous buffer (the common
    /// case — most card snapshots are POD structs).
    void writeSection(std::string_view name, const void* data, std::size_t length);

private:
    std::ofstream out;
};

/// Read companion. Constructed against a path; `good()` reports the load
/// status after the magic + version handshake. Sections are read in order;
/// a reader-driven iteration model lets the caller dispatch to peripherals.
class SnapshotReader {
public:
    explicit SnapshotReader(const std::string& path);
    ~SnapshotReader() = default;

    /// True iff the file was a valid POM1 snapshot at construction *and*
    /// no read has set failbit/badbit since. EOF alone is NOT a failure —
    /// reaching EOF after consuming all sections is the normal loop
    /// terminator (`nextSection` returns false at EOF).
    bool     good() const { return ok && !in.fail(); }
    uint32_t version() const { return ver; }
    const std::string& error() const { return errorMsg; }

    uint8_t  readU8();
    uint16_t readU16();
    uint32_t readU32();
    uint64_t readU64();
    void     readBytes(void* data, std::size_t length);

    /// Advance to the next section. On success, returns true and fills
    /// `name` (NUL-trimmed) and `length`. The caller is then expected to
    /// either `readBytes(buf, length)` to consume the section, or call
    /// `skipCurrentSection()` to move past it. `nextSection` enforces
    /// that at most one of those two happened before the next call.
    bool nextSection(std::string& name, std::uint32_t& length);
    void skipCurrentSection();

    /// Did the previous nextSection's payload get consumed in full?
    /// Test/debug only.
    bool atSectionBoundary() const { return cursor == sectionEnd; }

private:
    std::ifstream in;
    bool          ok = false;
    uint32_t      ver = 0;
    std::string   errorMsg;
    std::streampos cursor{};
    std::streampos sectionEnd{};
};

} // namespace pom1

#endif // POM1_SNAPSHOT_IO_H
