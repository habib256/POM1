// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#ifndef POM1_FILE_BYTES_H
#define POM1_FILE_BYTES_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace pom1 {

/// Read a whole file, refusing anything past `maxBytes` BEFORE reading it.
///
/// The order is the point. Every parser POM1 feeds from disk — memory images,
/// cassette containers, snapshots — is bounded, but a bound checked after the
/// read has already lost: slurping the file IS the allocation the limit exists
/// to prevent. Three call sites had grown the same size-then-slurp preamble;
/// one function makes the rule impossible to forget at a fourth.
///
/// `what` names the kind of file for the error message ("memory image",
/// "cassette tape", "snapshot"). Returns false with `error` set on a file that
/// is too large or cannot be opened; `bytes` is untouched in that case.
inline bool readFileBounded(const std::string& path, std::size_t maxBytes,
                            const char* what, std::vector<std::uint8_t>& bytes,
                            std::string& error)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    // An unreadable size (a pipe, a race, a platform quirk) is not a refusal:
    // fall through and let the read itself decide, exactly as it did before.
    if (!ec && size > maxBytes) {
        error = std::filesystem::path(path).filename().string() + " is " +
                std::to_string(size / (1024 * 1024)) + " MB — too large to be a " +
                what + " (limit " + std::to_string(maxBytes / (1024 * 1024)) + " MB)";
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = std::string("cannot open ") + what + ": " + path;
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(file),
                 std::istreambuf_iterator<char>());
    return true;
}

} // namespace pom1

#endif // POM1_FILE_BYTES_H
