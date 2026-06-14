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
    const std::string full = cmd + " 2>&1";
  #ifdef _WIN32
    FILE* pipe = _popen(full.c_str(), "r");
  #else
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

std::string whichExe(const char* name)
{
    namespace fs = std::filesystem;
    std::vector<std::string> dirs;
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
