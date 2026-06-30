// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// NativeFileDialog.cpp — desktop OS file picker integration.
//
// Per-platform backends:
//   * Windows : Common Item Dialog via legacy comdlg32 GetOpenFileNameW /
//               GetSaveFileNameW. Wide-string round-trip through MultiByte
//               <-> WideChar so callers stay UTF-8.
//   * macOS   : implemented in NativeFileDialog_Mac.mm (Objective-C++).
//   * Linux   : forks `zenity --file-selection ...` (GNOME / generic) or
//               `kdialog --getopenfilename ...` (KDE), captures stdout via
//               pipe. The first call probes $PATH and caches the choice.
//   * WASM    : everything stubs out (isAvailable() = false), the existing
//               ImGui dialog stays as the only option.

#include "NativeFileDialog.h"
#include "POM1Build.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#if POM1_IS_WASM
// ── WASM: no native picker available, every call returns false. ─────────────
namespace pom1 {
bool NativeFileDialog::isAvailable() { return false; }
bool NativeFileDialog::openFile(GLFWwindow*, const std::string&,
                                const std::string&,
                                const std::vector<FileFilter>&, std::string&)
{ return false; }
bool NativeFileDialog::saveFile(GLFWwindow*, const std::string&,
                                const std::string&, const std::string&,
                                const std::vector<FileFilter>&, std::string&)
{ return false; }
} // namespace pom1

#elif defined(_WIN32)
// ── Windows: GetOpenFileNameW / GetSaveFileNameW. ───────────────────────────
//
// Uses the legacy comdlg32 API rather than the IFileDialog COM interface
// because GetOpenFileNameW is already initialised by every Win32 process and
// avoids the CoInitializeEx dance — POM1's main thread runs the GL context
// and we don't want to risk shifting its COM apartment under glfw.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace pom1 {

namespace {

std::wstring utf8ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                   nullptr, 0);
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), wlen);
    return w;
}

std::string wideToUtf8(const wchar_t* w)
{
    if (!w || !*w) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0,
                                  nullptr, nullptr);
    if (len <= 1) return std::string();
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

// commdlg expects a double-NUL terminated buffer of "Label\0*.ext;*.ext2\0..."
// pairs. Build it from FileFilter list and append an "All files" trailer.
std::wstring buildFilterBuffer(const std::vector<FileFilter>& filters)
{
    std::wstring buf;
    auto append = [&](const std::wstring& label, const std::wstring& pattern) {
        buf.append(label);
        buf.push_back(L'\0');
        buf.append(pattern);
        buf.push_back(L'\0');
    };
    for (const auto& f : filters) {
        std::wstring pat;
        if (f.extensions.empty()) {
            pat = L"*.*";
        } else {
            for (size_t i = 0; i < f.extensions.size(); ++i) {
                if (i) pat.push_back(L';');
                pat.append(L"*.").append(utf8ToWide(f.extensions[i]));
            }
        }
        std::wstring label = utf8ToWide(f.description);
        if (label.empty()) label = pat;
        append(label, pat);
    }
    append(L"All files (*.*)", L"*.*");
    buf.push_back(L'\0'); // final double-NUL terminator
    return buf;
}

HWND parentHwnd(GLFWwindow* w)
{
    return w ? glfwGetWin32Window(w) : nullptr;
}

// Win32 paths can exceed MAX_PATH (260) — long OneDrive / AppData targets
// commonly do. OFN_EXPLORER + a large lpstrFile buffer up to 32 768 wchars
// is the documented way to make GetOpenFileNameW / GetSaveFileNameW return
// the full path. Using a stock MAX_PATH buffer makes GetSaveFileNameW
// return FALSE with CDERR_*/FNERR_BUFFERTOOSMALL, which we previously
// treated as a silent cancel — the user saw their Save click do nothing.
constexpr size_t kWin32PathBufWChars = 32768;

} // namespace

bool NativeFileDialog::isAvailable() { return true; }

bool NativeFileDialog::openFile(GLFWwindow* parent,
                                const std::string& title,
                                const std::string& defaultDir,
                                const std::vector<FileFilter>& filters,
                                std::string& outPath)
{
    std::vector<wchar_t> buf(kWin32PathBufWChars, L'\0');
    std::wstring wtitle  = utf8ToWide(title);
    std::wstring wdir    = utf8ToWide(defaultDir);
    std::wstring wfilter = buildFilterBuffer(filters);

    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = parentHwnd(parent);
    ofn.lpstrFilter = wfilter.c_str();
    ofn.lpstrFile   = buf.data();
    ofn.nMaxFile    = (DWORD)buf.size();
    ofn.lpstrTitle  = wtitle.empty() ? nullptr : wtitle.c_str();
    ofn.lpstrInitialDir = wdir.empty() ? nullptr : wdir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER
              | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) return false;
    outPath = wideToUtf8(buf.data());
    return !outPath.empty();
}

bool NativeFileDialog::saveFile(GLFWwindow* parent,
                                const std::string& title,
                                const std::string& defaultDir,
                                const std::string& defaultName,
                                const std::vector<FileFilter>& filters,
                                std::string& outPath)
{
    std::vector<wchar_t> buf(kWin32PathBufWChars, L'\0');
    std::wstring wname = utf8ToWide(defaultName);
    if (!wname.empty()) {
        size_t n = std::min<size_t>(wname.size(), buf.size() - 1);
        std::memcpy(buf.data(), wname.data(), n * sizeof(wchar_t));
        buf[n] = L'\0';
    }
    std::wstring wtitle  = utf8ToWide(title);
    std::wstring wdir    = utf8ToWide(defaultDir);
    std::wstring wfilter = buildFilterBuffer(filters);

    // Default extension drives Explorer's auto-append when the user types a
    // bare name. Take it from the first filter's first extension if available.
    std::wstring defExt;
    if (!filters.empty() && !filters.front().extensions.empty())
        defExt = utf8ToWide(filters.front().extensions.front());

    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = parentHwnd(parent);
    ofn.lpstrFilter = wfilter.c_str();
    ofn.lpstrFile   = buf.data();
    ofn.nMaxFile    = (DWORD)buf.size();
    ofn.lpstrTitle  = wtitle.empty() ? nullptr : wtitle.c_str();
    ofn.lpstrInitialDir = wdir.empty() ? nullptr : wdir.c_str();
    ofn.lpstrDefExt = defExt.empty() ? nullptr : defExt.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER
              | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&ofn)) return false;
    outPath = wideToUtf8(buf.data());
    return !outPath.empty();
}

} // namespace pom1

#elif defined(__APPLE__)
// ── macOS: implementation lives in NativeFileDialog_Mac.mm so we can call
// AppKit. Nothing to do in this file.

#else
// ── Linux (and other Unixes): zenity / kdialog fork+exec. ───────────────────
//
// We never link against GTK/Qt directly — that would drag a heavy compile-
// time dependency for what amounts to two CLI calls. The first
// isAvailable() call probes $PATH and caches "zenity", "kdialog", or "none".

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string>

namespace pom1 {

namespace {

enum class Backend { Unprobed, None, Zenity, Kdialog };

Backend& backend()
{
    static Backend b = Backend::Unprobed;
    return b;
}

bool onPath(const char* binary)
{
    // Walk $PATH ourselves rather than fork+exec a `which` probe. Avoids
    // depending on coreutils being on minimal containers.
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string dir;
    auto check = [&](const std::string& d) {
        std::string full = d.empty() ? std::string(binary)
                                     : d + "/" + binary;
        return access(full.c_str(), X_OK) == 0;
    };
    while (*path) {
        if (*path == ':') {
            if (check(dir)) return true;
            dir.clear();
        } else {
            dir.push_back(*path);
        }
        ++path;
    }
    return check(dir);
}

Backend probeBackend()
{
    static std::once_flag once;
    std::call_once(once, []() {
        // Prefer zenity (GNOME / generic). Most desktops ship it; KDE users
        // typically have kdialog instead.
        if (onPath("zenity"))       backend() = Backend::Zenity;
        else if (onPath("kdialog")) backend() = Backend::Kdialog;
        else                         backend() = Backend::None;
    });
    return backend();
}

// Run argv[], capture stdout up to 64 KB. Returns "" when the child exited
// non-zero (which both zenity and kdialog do on Cancel) or could not be
// spawned. Newlines are stripped — these tools terminate the path with \n.
std::string runChildCapture(const std::vector<std::string>& argv)
{
    int fds[2];
    if (pipe(fds) != 0) return {};

    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return {}; }
    if (pid == 0) {
        // Child: redirect stdout to the pipe, drop stderr to /dev/null so the
        // GTK/KDE plug-init chatter doesn't pollute POM1's console.
        dup2(fds[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        std::vector<char*> raw;
        raw.reserve(argv.size() + 1);
        for (auto& s : argv) raw.push_back(const_cast<char*>(s.c_str()));
        raw.push_back(nullptr);
        execvp(raw[0], raw.data());
        _exit(127); // exec failed
    }

    close(fds[1]);
    std::string out;
    char buf[1024];
    while (out.size() < 64 * 1024) {
        ssize_t n = read(fds[0], buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, buf + n);
    }
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return {};
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
}

// "*.bin *.txt" — zenity's --file-filter pattern syntax (space-separated).
std::string zenityPatterns(const std::vector<std::string>& exts)
{
    std::string out;
    if (exts.empty()) return "*";
    for (size_t i = 0; i < exts.size(); ++i) {
        if (i) out.push_back(' ');
        out.append("*.").append(exts[i]);
    }
    return out;
}

// "*.bin|*.txt|" — kdialog's filter syntax (pipe-separated patterns, then
// a description after the leftmost '|' — see `kdialog --help-all`).
std::string kdialogFilter(const std::vector<FileFilter>& filters)
{
    if (filters.empty()) return "*|All files";
    std::string out;
    for (size_t i = 0; i < filters.size(); ++i) {
        if (i) out.push_back('\n');
        const auto& f = filters[i];
        if (f.extensions.empty()) {
            out.append("*");
        } else {
            for (size_t j = 0; j < f.extensions.size(); ++j) {
                if (j) out.push_back(' ');
                out.append("*.").append(f.extensions[j]);
            }
        }
        out.push_back('|');
        out.append(f.description.empty() ? std::string("Files")
                                         : f.description);
    }
    out.append("\n*|All files");
    return out;
}

std::string runZenityOpen(const std::string& title,
                          const std::string& defaultDir,
                          const std::vector<FileFilter>& filters)
{
    std::vector<std::string> argv = {
        "zenity", "--file-selection",
        "--title=" + (title.empty() ? std::string("Open File") : title),
    };
    if (!defaultDir.empty()) {
        // Trailing slash hints zenity to enter the dir rather than preselect.
        argv.push_back("--filename=" + defaultDir + "/");
    }
    for (const auto& f : filters) {
        std::string spec = "--file-filter=";
        spec += f.description.empty() ? "Files" : f.description;
        spec += " | ";
        spec += zenityPatterns(f.extensions);
        argv.push_back(spec);
    }
    argv.push_back("--file-filter=All files | *");
    return runChildCapture(argv);
}

std::string runZenitySave(const std::string& title,
                          const std::string& defaultDir,
                          const std::string& defaultName,
                          const std::vector<FileFilter>& filters)
{
    std::vector<std::string> argv = {
        "zenity", "--file-selection", "--save", "--confirm-overwrite",
        "--title=" + (title.empty() ? std::string("Save File") : title),
    };
    std::string filenameArg = "--filename=";
    if (!defaultDir.empty()) filenameArg += defaultDir + "/";
    filenameArg += defaultName;
    if (filenameArg != "--filename=") argv.push_back(filenameArg);
    for (const auto& f : filters) {
        std::string spec = "--file-filter=";
        spec += f.description.empty() ? "Files" : f.description;
        spec += " | ";
        spec += zenityPatterns(f.extensions);
        argv.push_back(spec);
    }
    argv.push_back("--file-filter=All files | *");
    return runChildCapture(argv);
}

std::string runKdialogOpen(const std::string& title,
                           const std::string& defaultDir,
                           const std::vector<FileFilter>& filters)
{
    std::vector<std::string> argv = {
        "kdialog", "--getopenfilename",
        defaultDir.empty() ? std::string(":") : defaultDir,
        kdialogFilter(filters),
    };
    if (!title.empty()) {
        argv.push_back("--title");
        argv.push_back(title);
    }
    return runChildCapture(argv);
}

std::string runKdialogSave(const std::string& title,
                           const std::string& defaultDir,
                           const std::string& defaultName,
                           const std::vector<FileFilter>& filters)
{
    std::string start = defaultDir.empty() ? std::string(":")
                                           : (defaultDir + "/" + defaultName);
    if (defaultDir.empty() && !defaultName.empty()) start = defaultName;
    std::vector<std::string> argv = {
        "kdialog", "--getsavefilename", start, kdialogFilter(filters),
    };
    if (!title.empty()) {
        argv.push_back("--title");
        argv.push_back(title);
    }
    return runChildCapture(argv);
}

// When the user types a bare basename in a save dialog, append the first
// configured extension so callers don't need to. Mirrors the Win32 lpstrDefExt
// behaviour for the GTK side, which doesn't auto-suffix.
std::string ensureExtension(std::string path,
                            const std::vector<FileFilter>& filters)
{
    if (path.empty() || filters.empty()) return path;
    auto slash = path.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? path
                                                    : path.substr(slash + 1);
    if (base.find('.') != std::string::npos) return path;
    const auto& exts = filters.front().extensions;
    if (exts.empty()) return path;
    path.push_back('.');
    path.append(exts.front());
    return path;
}

} // namespace

bool NativeFileDialog::isAvailable()
{
    return probeBackend() != Backend::None;
}

bool NativeFileDialog::openFile(GLFWwindow* /*parent*/,
                                const std::string& title,
                                const std::string& defaultDir,
                                const std::vector<FileFilter>& filters,
                                std::string& outPath)
{
    switch (probeBackend()) {
    case Backend::Zenity:
        outPath = runZenityOpen(title, defaultDir, filters);
        break;
    case Backend::Kdialog:
        outPath = runKdialogOpen(title, defaultDir, filters);
        break;
    default:
        return false;
    }
    return !outPath.empty();
}

bool NativeFileDialog::saveFile(GLFWwindow* /*parent*/,
                                const std::string& title,
                                const std::string& defaultDir,
                                const std::string& defaultName,
                                const std::vector<FileFilter>& filters,
                                std::string& outPath)
{
    switch (probeBackend()) {
    case Backend::Zenity:
        outPath = runZenitySave(title, defaultDir, defaultName, filters);
        break;
    case Backend::Kdialog:
        outPath = runKdialogSave(title, defaultDir, defaultName, filters);
        break;
    default:
        return false;
    }
    if (outPath.empty()) return false;
    outPath = ensureExtension(std::move(outPath), filters);
    return true;
}

} // namespace pom1

#endif
