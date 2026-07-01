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
    /// True when a native dialog should be used right now — i.e. the user
    /// preference is enabled AND this build can actually pop one. Every call
    /// site keys off this: when it is false the Load/Save flows fall back to
    /// POM1's in-process ImGui browser (instant, no XPC cold-start). Always
    /// false on Emscripten/WASM, and on Linux until a zenity or kdialog binary
    /// is detected on $PATH.
    static bool isAvailable();

    /// User preference: when false, isAvailable() reports false even on a
    /// platform that has a native picker, so the (faster) in-process ImGui
    /// browser is used everywhere. Default true. Wired to a Settings checkbox.
    static void setEnabled(bool enabled);
    static bool isEnabled();

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

    /// Convenience used by the portable paint/bench editors via their host
    /// seams: pick a file with ONE filter expressed as a human description plus
    /// a comma-separated extension list WITHOUT dots (e.g. "png,jpg,bmp" or
    /// "hgr"; empty matches everything). `forSave` routes to saveFile, else
    /// openFile. For loads an extra "All files (*.*)" filter is appended so the
    /// user is never boxed in; for saves only the typed filter is passed so the
    /// single-extension auto-append in saveFile still fires. Returns false when
    /// no native backend is available — the caller then falls back to its own
    /// in-process ImGui browser, so callers need not pre-check isAvailable().
    static bool pickFiltered(GLFWwindow* parent,
                             bool forSave,
                             const std::string& title,
                             const std::string& filterDesc,
                             const std::string& extCsv,
                             const std::string& defaultDir,
                             const std::string& defaultName,
                             std::string& outPath);

private:
    /// Per-platform probe: true when this build CAN pop a native dialog,
    /// ignoring the user preference. Defined in the platform backends
    /// (NativeFileDialog_Mac.mm on macOS; NativeFileDialog.cpp elsewhere).
    /// isAvailable() = isEnabled() && platformAvailable().
    static bool platformAvailable();
};

} // namespace pom1

#endif // POM1_NATIVE_FILE_DIALOG_H
