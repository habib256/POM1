// Bench portable module — OS/process helpers, no emulator or ImGui dependency.
// Shared verbatim by POM1 / POM2 / NeoST. See bench/IBenchHost.h for the seam.
#ifndef BENCH_PROCESS_UTIL_H
#define BENCH_PROCESS_UTIL_H

#include <string>

namespace bench {

// Quote a path/arg for a shell (single-quote on POSIX, double on Windows).
std::string shellQuote(const std::string& s);

// Run a command (with stdout+stderr merged) and capture its output. Returns the
// process exit code, or -1 if the pipe couldn't be opened. No-op on WASM.
int runCapture(const std::string& cmd, std::string& out);

// Resolve an executable by scanning $PATH (+ a couple of common toolchain dirs).
// Returns the full path, or "" if not found.
std::string whichExe(const char* name);

} // namespace bench

#endif // BENCH_PROCESS_UTIL_H
