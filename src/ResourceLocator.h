// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#ifndef POM1_RESOURCE_LOCATOR_H
#define POM1_RESOURCE_LOCATOR_H

#include <filesystem>
#include <string>
#include <vector>

namespace pom1 {

/// Where POM1's data lives: `roms/`, `software/`, `cassettes/`, `sdcard/`,
/// `disks/`, `cfcard/`, `fonts/`.
///
/// Every consumer used to re-implement the same "try `x`, then `../x`, then
/// `../../x`" walk — and they did not agree on how far to climb. `loadROM()`
/// went up ONE level, the sdcard/disks/cfcard probes TWO, the CodeTank probe
/// THREE. Run POM1 from `build/tests/` and it found the disk images but not the
/// ROMs, then quietly substituted its built-in Woz Monitor and carried on:
/// "[Mem] WARN: WOZ Monitor loaded from built-in fallback". One search order,
/// in one place, is what stops that.
///
/// A locator is a VALUE, not a global. `Memory` takes one (defaulted, so
/// existing callers are unchanged), which is what lets a test build the core
/// against a directory it controls instead of against whatever the working
/// directory happens to be.
class ResourceLocator {
public:
    /// The shipped search order: the working directory and its ancestors,
    /// then the executable's directory and the layouts packagers put around it
    /// (macOS `.app` `Resources/`, AppImage `share/POM1/`).
    ///
    /// Returned BY VALUE and recomputed per call, because the working directory
    /// is live state: `pom1_macos_provision_user_data_dir()` chdirs during
    /// startup, and tests chdir to sandboxes. Caching it made resolution depend
    /// on *when* the locator was first touched — `rom_fallback_smoke` caught
    /// exactly that, still finding the repo's ROMs from inside its sandbox. The
    /// executable-derived half IS cached; that one cannot move.
    static ResourceLocator defaultLocator();

    /// A locator that considers `root` and nothing else. Everything outside it
    /// is invisible — the point being that a test can assert what was touched.
    static ResourceLocator rootedAt(std::filesystem::path root);

    /// Build from an explicit, ordered root list. Non-existent roots are kept:
    /// a root may be created after the locator is (the macOS user-data
    /// directory is provisioned at startup), and `find()` checks existence per
    /// call anyway.
    explicit ResourceLocator(std::vector<std::filesystem::path> roots);

    /// Resolve a repo-relative path — "roms/basic.rom", "cassettes/BASIC.aci".
    /// Returns the first root under which it exists, or an EMPTY path.
    ///
    /// An absolute `relative` is returned as-is when it exists: callers pass
    /// user-chosen paths through here too, and rewriting those would be wrong.
    std::filesystem::path find(const std::string& relative) const;

    /// Same, but the result must be a directory. `find()` accepts either.
    std::filesystem::path findDirectory(const std::string& relative) const;

    /// True when `relative` resolves anywhere. Convenience over `find`.
    bool exists(const std::string& relative) const { return !find(relative).empty(); }

    const std::vector<std::filesystem::path>& roots() const { return roots_; }

private:
    std::vector<std::filesystem::path> roots_;
};

/// Directory holding the running executable, or empty when it cannot be
/// determined (and always under Emscripten, which has no such thing).
///
/// `bench::executableDir()` is a deliberate second copy: `src/bench/` is kept
/// standalone for POM2 reuse and must not depend on POM1 headers — the same
/// rule that has `CodeBench.cpp` repeating the hex-dump extensions literally.
std::filesystem::path executableDirectory();

} // namespace pom1

#endif // POM1_RESOURCE_LOCATOR_H
