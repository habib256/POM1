// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "ResourceLocator.h"

#if defined(__EMSCRIPTEN__)
// No executable path in the browser; MEMFS is mounted at the root.
#elif defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#elif defined(__APPLE__)
#  include <climits>          // PATH_MAX
#  include <mach-o/dyld.h>
#endif

namespace pom1 {

std::filesystem::path executableDirectory()
{
    namespace fs = std::filesystem;
    std::error_code ec;
#if defined(__EMSCRIPTEN__)
    return {};
#elif defined(_WIN32)
    char buf[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return fs::path(std::string(buf, n)).parent_path();
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t n = sizeof(buf);
    if (_NSGetExecutablePath(buf, &n) != 0) return {};   // buffer too small
    fs::path p = fs::canonical(buf, ec);
    if (ec) p = fs::path(buf);
    return p.parent_path();
#else
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
    return p.parent_path();
#endif
}

ResourceLocator::ResourceLocator(std::vector<std::filesystem::path> roots)
    : roots_(std::move(roots))
{
}

ResourceLocator ResourceLocator::rootedAt(std::filesystem::path root)
{
    return ResourceLocator({std::move(root)});
}

namespace {

/// The executable-derived roots. Cached: unlike the working directory, the
/// binary does not move under a running process, and finding it costs a syscall.
const std::vector<std::filesystem::path>& packagedRoots()
{
    namespace fs = std::filesystem;
    static const std::vector<fs::path> roots = [] {
        std::vector<fs::path> out;
        const fs::path exe = executableDirectory();
        if (exe.empty()) return out;
        // A double-clicked app has a working directory nobody chose, so the
        // executable is the only reliable anchor.
        out.push_back(exe);                                  // alongside the binary
        out.push_back(exe.parent_path());                    // one level up
        out.push_back(exe.parent_path() / "Resources");      // macOS .app bundle
        out.push_back(exe.parent_path() / "share" / "POM1"); // Linux AppImage
        out.push_back(exe.parent_path().parent_path());
        return out;
    }();
    return roots;
}

} // namespace

ResourceLocator ResourceLocator::defaultLocator()
{
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;
    auto add = [&roots](fs::path p) {
        if (p.empty()) return;
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(p, ec);
        if (ec) canonical = std::move(p);
        for (const fs::path& existing : roots)
            if (existing == canonical) return;    // keep the order, drop dupes
        roots.push_back(std::move(canonical));
    };

    // The working directory first, then its ancestors: this is what makes a dev
    // build work from the repo root, from `build/`, and from `build/tests/`.
    // Three levels covers every layout in the tree; the old per-site walks
    // disagreed on exactly this number, which is the drift this class ends.
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    if (!ec) {
        add(cwd);
        for (int i = 0; i < 3 && cwd.has_parent_path(); ++i) {
            cwd = cwd.parent_path();
            add(cwd);
        }
    }
    for (const fs::path& p : packagedRoots()) add(p);
    return ResourceLocator(std::move(roots));
}

std::filesystem::path ResourceLocator::find(const std::string& relative) const
{
    namespace fs = std::filesystem;
    if (relative.empty()) return {};

    std::error_code ec;
    // A caller-supplied absolute path is an answer, not a question. `--iec-disk
    // /tmp/x.d64` and the file pickers both come through here, and rewriting
    // them against the roots would be actively wrong.
    const fs::path asGiven(relative);
    if (asGiven.is_absolute())
        return fs::exists(asGiven, ec) && !ec ? asGiven : fs::path{};

    for (const fs::path& root : roots_) {
        const fs::path candidate = root / asGiven;
        if (fs::exists(candidate, ec) && !ec) return candidate;
    }
    return {};
}

std::filesystem::path ResourceLocator::findDirectory(const std::string& relative) const
{
    namespace fs = std::filesystem;
    if (relative.empty()) return {};

    std::error_code ec;
    const fs::path asGiven(relative);
    if (asGiven.is_absolute())
        return fs::is_directory(asGiven, ec) && !ec ? asGiven : fs::path{};

    for (const fs::path& root : roots_) {
        const fs::path candidate = root / asGiven;
        if (fs::is_directory(candidate, ec) && !ec) return candidate;
    }
    return {};
}

} // namespace pom1
