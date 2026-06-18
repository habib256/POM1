// Bench portable module — the emulator-agnostic editor window (Arduino-style):
// teal toolbar (Verify/Upload/New/Open/Save/Examples/Serial), syntax-highlighted
// editor, Target picker, build-output console, status bar. All emulator work is
// delegated to an IBenchHost. Copy bench/ verbatim into POM2/NeoST and supply a
// host. Depends only on ImGui, ImGuiColorTextEdit and IconsFontAwesome6.
#ifndef BENCH_CODE_BENCH_H
#define BENCH_CODE_BENCH_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class TextEditor;

namespace bench {

class IBenchHost;
struct BuildResult;

class CodeBench {
public:
    explicit CodeBench(IBenchHost* host);
    ~CodeBench();

    // Render the Bench window. `open` is the show flag (toggled by the close box).
    void render(const char* title, bool* open);

    // Switch the active target (syntax highlighter + starter sketch) WITHOUT
    // notifying the host — used when the bench is being driven BY a preset
    // change (so we don't bounce back through onTargetSelected and re-trigger
    // applyMachineConfig). Returns true if the starter was loaded; returns
    // false (and leaves the editor alone) if the buffer is dirty or the index
    // is out of range. Safe to call before the first render — lazily creates
    // the editor.
    bool loadStarterForTargetIfClean(int targetIndex);

private:
    void ensureEditor();
    void applyTargetSyntax();
    void applyResult(const BuildResult& r);

    IBenchHost* host_;
    std::unique_ptr<TextEditor> editor_;
    int  targetIndex_ = -1;
    std::string lastLanguage_;
    std::string console_;
    bool statusOk_ = true;
    bool showConsole_ = false;
    bool inited_ = false;
    bool buildPolling_ = false;      // an async (web/WASM) build is in flight
    std::string status_;
    char rawAddr_[8] = "0300";
    // In-app file browser (Open/Save), rooted at the host's browseDir().
    std::string browseDir_;          // current directory shown in the browser
    std::string loadedPath_;         // full path of the open file ("" = untitled)
    bool browseSave_ = false;        // current popup is Save (vs Open)
    char saveName_[128] = "sketch.s";
    std::vector<int> errorLines_;    // 1-based lines, mirrored onto the scrollbar
    int newLang_ = 0;                // New-dialog selection: language x machine
    int newMachine_ = 0;
    std::string lastSavedText_;      // editor text at the last save/new/open
    bool dirty_ = false;             // editor changed since lastSavedText_ ('*' on tab)
};

} // namespace bench

#endif // BENCH_CODE_BENCH_H
