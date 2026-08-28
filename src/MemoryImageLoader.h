// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#ifndef POM1_MEMORY_IMAGE_LOADER_H
#define POM1_MEMORY_IMAGE_LOADER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pom1 {

/// Largest input parseMemoryImage() will look at, in bytes.
///
/// The 6502 addresses 64 KB, and the fattest dialect spends about four
/// characters per byte, so a full-address-space image runs to a few hundred KB;
/// the largest program POM1 ships is ~100 KB. 8 MB is far above anything
/// legitimate and far below what hurts: parsing peaks at roughly three times
/// the input (the content, the comment-stripped join, and the line vector), so
/// this bounds the loader at ~24 MB however hostile the file. Beyond it the
/// answer is a refusal with a message, never an allocation the user did not
/// ask for — `Memory::loadHexDump()` checks the file's SIZE before reading it.
inline constexpr std::size_t kMaxMemoryImageBytes = 8u * 1024u * 1024u;

/// Which of the three dialects a file turned out to be.
///
/// The choice is made from the file's SHAPE plus, for TurboType only, its
/// extension — never from the extension alone. `.hex` names both a WOZMON dump
/// and Intel HEX, and an Intel HEX published as `.txt` must still be read as
/// Intel HEX; routing on the name would silently write record headers into RAM.
enum class MemoryImageFormat {
    WozmonHex,   ///< "AAAA: HH HH ..." + trailing "AAAAR". The legacy dialect.
    IntelHex,    ///< ":LLAAAATT<data>CC" — detected structurally.
    TurboType,   ///< Uncle Bernie's `.TUR`: line-structured, ':' OPENS a data line.
};

/// One contiguous run of bytes destined for `start`.
///
/// A run is exactly a maximal sequence of writes at consecutive addresses: it
/// breaks only where the source moves the write pointer somewhere that is not
/// where it already was. Two runs may overlap (a dump that rewrites an address
/// it already filled); applying them in order reproduces the source's intent.
struct MemoryImageSpan {
    uint16_t start = 0;
    std::vector<uint8_t> bytes;

    uint16_t end() const   // inclusive; never called on an empty span
    {
        return static_cast<uint16_t>(start + bytes.size() - 1);
    }
};

/// Something the caller should tell the user about. The loader never logs: it
/// has no business deciding whether this run is a UI action, a CLI verb or a
/// test, so it reports and the caller routes.
struct MemoryImageDiagnostic {
    enum class Severity { Info, Warning, Error };
    Severity severity = Severity::Info;
    std::string message;
};

/// The complete result of reading a memory image — every write the file asks
/// for, where to start it, and what was odd about it.
struct MemoryImage {
    /// False when the file yielded nothing usable: no address was ever
    /// established (and, for the WOZMON/TurboType dialects, no run address
    /// either). A rejected image carries diagnostics and NO writes, which is
    /// the whole point — see parseMemoryImage().
    bool ok = false;
    MemoryImageFormat format = MemoryImageFormat::WozmonHex;

    /// Apply in order. Empty whenever `ok` is false.
    std::vector<MemoryImageSpan> writes;

    /// Where execution should begin: the file's run address when it names one
    /// (WOZMON `AAAAR`, Intel HEX type 03/05), otherwise the first address the
    /// file wrote to.
    uint16_t startAddress = 0;
    bool hasRunAddress = false;

    /// Total bytes across every span, counting a rewritten address twice —
    /// this is "how many bytes did the file ask us to store", not "how many
    /// distinct cells changed".
    int byteCount = 0;

    std::vector<MemoryImageDiagnostic> diagnostics;

    /// One (first, last) inclusive pair per span, for the Memory Map. Spans
    /// ARE the zones: both are maximal contiguous runs, which is why a
    /// sequential address line ("0288:" right after eight bytes from "0280:")
    /// stays inside its predecessor instead of shredding chess.txt into one
    /// zone per row.
    std::vector<std::pair<uint16_t, uint16_t>> zones() const;
};

/// Parse a memory image. PURE: no `Memory`, no filesystem, no UI, no logging,
/// no global state — bytes and a name in, a complete description out.
///
/// `fileName` is never opened. It supplies the display name for diagnostics and
/// the one extension rule the formats genuinely need: a `.tur` takes the
/// line-structured TurboType parser whatever its content, because the lone `T`
/// marker that normally selects it is optional in the wild and such a file fed
/// to the legacy joined-lines parser is shredded across memory (each data
/// line's trailing digits sliced off as the next line's address — that is how
/// the 15 Puzzle's $0300 block used to land in 60+ zones at $18A9, $4159,
/// $F460...). Every other extension still needs its `T`.
///
/// **Nothing partial ever escapes.** The result is complete or it is rejected:
/// a malformed record makes `ok` false and drops every span, so a caller that
/// applies `writes` cannot leave the machine holding half a program. The
/// in-place loader this replaced wrote each Intel HEX record into RAM as it
/// went and only then discovered the record that lay outside the 6502's 64 KB —
/// by which point the earlier records were already in memory.
MemoryImage parseMemoryImage(std::string_view content, std::string_view fileName);

} // namespace pom1

#endif // POM1_MEMORY_IMAGE_LOADER_H
