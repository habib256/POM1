// Bench portable module — OS/process helpers. See ProcessUtil.h.
#include "ProcessUtil.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
  #include <sys/wait.h>
#endif

#if defined(_WIN32)
  #include <windows.h>
#elif defined(__APPLE__) && !defined(__EMSCRIPTEN__)
  #include <mach-o/dyld.h>
  #include <climits>
#endif

namespace bench {

std::string shellQuote(const std::string& s)
{
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
    out += "'";
    return out;
#endif
}

int runCapture(const std::string& cmd, std::string& out)
{
    out.clear();
#if defined(__EMSCRIPTEN__)
    (void)cmd;
    return -1;   // no subprocesses in the browser
#else
  #ifdef _WIN32
    // _popen runs the command through `cmd.exe /c`, which strips the OUTERMOST
    // quote pair from its argument. Our commands start with a quoted program path
    // AND carry quoted arguments (-I "<dir>", "<src>", -o "<obj>"), so that strip
    // desyncs the remaining quotes: a cc65 path containing a space — e.g.
    // "C:\...\POM1-Windows-v1.9.2 (1)\cc65\bin\ca65.exe", a OneDrive "Bureau",
    // "Program Files", … — then gets split at the space and cmd reports
    // "'C:\...' n'est pas reconnu en tant que commande …". Wrapping the whole
    // command in one extra quote pair makes cmd strip THOSE and pass the original
    // command through intact (the same `cmd /c "…"` idiom the packaging
    // PowerShell already uses — see packaging/windows/fetch_cc65.ps1).
    const std::string full = "\"" + cmd + " 2>&1\"";
    FILE* pipe = _popen(full.c_str(), "r");
  #else
    const std::string full = cmd + " 2>&1";
    FILE* pipe = popen(full.c_str(), "r");
  #endif
    if (!pipe) return -1;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0) out.append(buf, n);
  #ifdef _WIN32
    return _pclose(pipe);
  #else
    int rc = pclose(pipe);
    if (rc != -1 && WIFEXITED(rc)) return WEXITSTATUS(rc);
    return rc;
  #endif
#endif
}

std::string executableDir()
{
    namespace fs = std::filesystem;
    std::error_code ec;
#if defined(__EMSCRIPTEN__)
    return "";   // no real executable path in the browser
#elif defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return "";
    return fs::path(std::string(buf, n)).parent_path().string();
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t n = sizeof(buf);
    if (_NSGetExecutablePath(buf, &n) != 0) return "";   // buffer too small
    fs::path p = fs::canonical(buf, ec);
    if (ec) p = fs::path(buf);
    return p.parent_path().string();
#else  // Linux / other POSIX
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return "";
    return p.parent_path().string();
#endif
}

std::string whichExe(const char* name, const std::vector<std::string>& extraDirs)
{
    namespace fs = std::filesystem;
    std::vector<std::string> dirs;
    // Caller-supplied dirs win (e.g. an exe-relative bundled toolchain).
    for (const auto& d : extraDirs)
        if (!d.empty()) dirs.push_back(d);
    if (const char* pathEnv = std::getenv("PATH")) {
#ifdef _WIN32
        const char sep = ';';
#else
        const char sep = ':';
#endif
        std::string p(pathEnv);
        size_t start = 0;
        while (start <= p.size()) {
            size_t e = p.find(sep, start);
            if (e == std::string::npos) e = p.size();
            if (e > start) dirs.push_back(p.substr(start, e - start));
            start = e + 1;
        }
    }
    dirs.push_back("/usr/local/bin");
    dirs.push_back("/opt/cc65/bin");
    std::string exe = name;
#ifdef _WIN32
    exe += ".exe";
#endif
    std::error_code ec;
    for (const auto& d : dirs) {
        fs::path cand = fs::path(d) / exe;
        if (fs::exists(cand, ec) && !fs::is_directory(cand, ec))
            return cand.string();
    }
    return "";
}

} // namespace bench
