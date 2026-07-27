#pragma once
// HexDumpFile.h -- which file extensions name a Wozmon hex dump.
//
// Uncle Bernie's canonical Apple-1 convention (Applefritter) calls a file of
// plain WOZMON keystrokes ".APL". In practice every published .APL is
// "AAAA: HH HH ..." plus a trailing "R" -- exactly what Memory::loadHexDump
// parses, which is why the ~20 bundled "*.apl.txt" programs load correctly.
// POM1 used to key the hex path on ".txt"/".hex" alone, so the same file named
// ".apl" fell through to the RAW BINARY loader and its ASCII text was written
// verbatim into RAM: no error, no warning, 6143 garbage bytes at $0280 instead
// of a 1962-byte program. Route every extension check through here so a new
// call site cannot reintroduce that.
//
// ".mon" is the same content under the other name seen in the wild (a WOZ
// monitor listing). Bundled programs keep their "*.apl.txt" names -- the double
// extension leaves them openable by plain-text tooling and keeps the test paths
// that reference them unchanged.
//
// ".tur" (TurboType) IS here, because the format turned out to be readable by
// the same parser. A .TUR is:
//
//     0003 / :A2 FF 9A 4C 1A FF / 0003R      stack-reset stub, WOZMON syntax
//     0100 / :A2 FE E8 ... / 0100R           the serial RECEIVER, autotyped
//     T                                      switch to turbo mode
//     0300                                   block address, own line
//     :D8A2FF9AA92A851A204604A97C8518A9      16 bytes/line, UNSPACED
//     ...                                    more blocks, same shape
//     X                                      end of stream
//     015ER                                  run
//
// The turbo payload is not compressed -- just denser than WOZMON's 3 chars per
// byte plus a per-line address -- so Memory::loadHexDump reads it as-is, and
// the merged data+address branch handles the block transitions.
//
// Direct injection skips the transfer entirely: the $0100 receiver is written
// to RAM but never executed (nothing is arriving over a wire), and the file's
// LAST run address is the CRC checker, which validates the injected image on
// the emulated 6502 and jumps to the program itself on match, or prints "EE".
// So a .TUR load is self-checking -- see tests/hex_dump_turbotype_test.cpp.

#include <cctype>
#include <string>

namespace pom1 {

// Extensions WITHOUT the leading dot, lowercase. Order is the file-picker order.
inline const char* const kHexDumpExtensions[] = { "txt", "hex", "apl", "mon", "tur" };
inline constexpr int kHexDumpExtensionCount = 5;

// "Dir.v2/Prog.APL" -> "apl". Empty when the filename has no extension.
// Only the last path component is considered, so a dot in a directory name
// cannot masquerade as an extension.
inline std::string lowerExtension(const std::string& path)
{
    const std::size_t slash = path.find_last_of("/\\");
    const std::size_t nameStart = (slash == std::string::npos) ? 0 : slash + 1;
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot < nameStart || dot + 1 >= path.size())
        return std::string();
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

// True when `ext` (no leading dot, any case) names a Wozmon hex dump.
inline bool isHexDumpExtension(const std::string& ext)
{
    std::string lower = ext;
    if (!lower.empty() && lower.front() == '.') lower.erase(0, 1);
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (int i = 0; i < kHexDumpExtensionCount; ++i)
        if (lower == kHexDumpExtensions[i]) return true;
    return false;
}

// True when `path` names a Wozmon hex dump by its extension.
inline bool isHexDumpPath(const std::string& path)
{
    return isHexDumpExtension(lowerExtension(path));
}

}  // namespace pom1
