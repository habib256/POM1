// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// NativeFileDialog.h — thin portable wrapper around each desktop OS's
// localised file picker (Win32 GetOpenFileNameW, Cocoa NSOpenPanel/NSSavePanel,
// Linux zenity/kdialog). Returns false when no native backend is wired in
// (WASM, or a Linux box without zenity/kdialog on $PATH) so the caller can
// fall back to the in-process ImGui browser. Strings are UTF-8 throughout —
// the Win32 impl translates to/from UTF-16 internally.

#ifndef POM1_NATIVE_FILE_DIALOG_H
#define POM1_NATIVE_FILE_DIALOG_H

#include <string>
#include <vector>

struct GLFWwindow;

namespace pom1 {

struct FileFilter {
    /// Human-readable description shown in the dialog's filter dropdown,
    /// e.g. "Memory binary (*.bin)". On Win32 the parenthetical extension
    /// list is generated from `extensions` if missing.
    std::string description;
    /// File extensions WITHOUT the dot, e.g. {"bin"} or {"wav","mp3","flac"}.
    /// Empty list matches everything.
    std::vector<std::string> extensions;
};

class NativeFileDialog {
public:
    /// True when this build can pop a native dialog right now. Always false
    /// on Emscripten/WASM; on Linux returns false until a zenity or kdialog
    /// binary is detected on $PATH (probed and cached on first call).
    static bool isAvailable();

    /// Show an open-file dialog. Returns true and writes the selected path
    /// into `outPath` on success; false on cancel, error, or when no native
    /// backend is available. `parent` may be nullptr (then the dialog is
    /// non-modal w.r.t. the GLFW window). `defaultDir` may be empty.
    static bool openFile(GLFWwindow* parent,
                         const std::string& title,
                         const std::string& defaultDir,
                         const std::vector<FileFilter>& filters,
                         std::string& outPath);

    /// Show a save-file dialog. `defaultName` is the suggested filename
    /// (basename only). If the user picks a path without an extension and
    /// `filters` has exactly one entry with one extension, the impl auto-
    /// appends that extension so callers don't have to.
    static bool saveFile(GLFWwindow* parent,
                         const std::string& title,
                         const std::string& defaultDir,
                         const std::string& defaultName,
                         const std::vector<FileFilter>& filters,
                         std::string& outPath);
};

} // namespace pom1

#endif // POM1_NATIVE_FILE_DIALOG_H
