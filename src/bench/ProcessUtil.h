// Bench portable module — OS/process helpers, no emulator or ImGui dependency.
// Shared verbatim by POM1 / POM2 / NeoST. See bench/IBenchHost.h for the seam.
#ifndef BENCH_PROCESS_UTIL_H
#define BENCH_PROCESS_UTIL_H

#include <string>
#include <vector>

namespace bench {

// Quote a path/arg for a shell (single-quote on POSIX, double on Windows).
std::string shellQuote(const std::string& s);

// Run a command (with stdout+stderr merged) and capture its output. Returns the
// process exit code, or -1 if the pipe couldn't be opened. No-op on WASM.
int runCapture(const std::string& cmd, std::string& out);

// Absolute directory containing the running executable (Win: GetModuleFileNameA,
// macOS: _NSGetExecutablePath, Linux: /proc/self/exe). "" if unresolvable or on
// WASM (no real exe in the browser). Used to find a bundled toolchain next to a
// shipped binary, independent of the current working directory.
std::string executableDir();

// Resolve an executable. Searches `extraDirs` first (e.g. an exe-relative
// bundled-toolchain dir), then $PATH, then a couple of common toolchain dirs.
// Returns the full path, or "" if not found.
std::string whichExe(const char* name, const std::vector<std::string>& extraDirs = {});

} // namespace bench

#endif // BENCH_PROCESS_UTIL_H
