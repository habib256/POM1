// process_util_smoke — pins the bench toolchain-discovery helpers that make a
// bundled cc65 work next to a shipped binary:
//   * executableDir()            resolves the running exe's directory
//   * whichExe(name, extraDirs)  searches caller dirs FIRST, then $PATH
// The exe-relative + POM1_CC65_DIR probe in Pom1BenchHost::probe() is built on
// these, so a regression here is what would silently break a packaged DevBench.
#include "ProcessUtil.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// Platform exe suffix whichExe() appends to the bare tool name.
#ifdef _WIN32
static const char* kExeSuffix = ".exe";
#else
static const char* kExeSuffix = "";
#endif

static fs::path makeFakeTool(const fs::path& dir, const std::string& bareName)
{
    fs::create_directories(dir);
    fs::path p = dir / (bareName + kExeSuffix);
    std::ofstream(p, std::ios::binary) << "#!/bin/sh\nexit 0\n";
    return p;
}

int main()
{
    // ---- executableDir(): non-empty + a real directory ---------------------
    const std::string exeDir = bench::executableDir();
    assert(!exeDir.empty() && "executableDir() returned empty");
    assert(fs::is_directory(exeDir) && "executableDir() is not a directory");
    // The test binary itself must live there.
    bool sawAnExe = false;
    for (const auto& e : fs::directory_iterator(exeDir))
        if (e.is_regular_file()) { sawAnExe = true; break; }
    assert(sawAnExe && "executableDir() holds no files");

    // ---- whichExe(): unknown tool resolves to "" ---------------------------
    assert(bench::whichExe("pom1_no_such_tool_xyzzy").empty());

    // ---- whichExe(): extraDirs are searched and FIRST one wins -------------
    fs::path base = fs::temp_directory_path() / "pom1_procutil_smoke";
    fs::remove_all(base);
    fs::path dirA = base / "a";   // higher priority (listed first)
    fs::path dirB = base / "b";   // lower priority
    fs::path toolA = makeFakeTool(dirA, "faketool");
    fs::path toolB = makeFakeTool(dirB, "faketool");

    std::string hit = bench::whichExe("faketool", {dirA.string(), dirB.string()});
    assert(!hit.empty() && "extraDirs lookup failed to find the fake tool");
    assert(fs::weakly_canonical(hit) == fs::weakly_canonical(toolA) &&
           "first extraDir did not win over the second");

    // Order reversed -> the other dir wins (precedence is positional).
    std::string hit2 = bench::whichExe("faketool", {dirB.string(), dirA.string()});
    assert(fs::weakly_canonical(hit2) == fs::weakly_canonical(toolB) &&
           "extraDirs precedence is not positional");

    // An empty / missing extraDir entry is skipped, not crashing.
    std::string hit3 = bench::whichExe("faketool", {"", dirA.string()});
    assert(fs::weakly_canonical(hit3) == fs::weakly_canonical(toolA));

    // A tool only on PATH is still found with no extraDirs (POSIX: /bin/sh).
#ifndef _WIN32
    assert(!bench::whichExe("sh").empty() && "sh not found on PATH");
#endif

    fs::remove_all(base);
    std::printf("process_util_smoke: OK\n");
    return 0;
}
