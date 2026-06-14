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
    std::string status_;
    char rawAddr_[8] = "0300";
    // In-app file browser (Open/Save), rooted at the host's browseDir().
    std::string browseDir_;          // current directory shown in the browser
    std::string loadedPath_;         // full path of the open file ("" = untitled)
    bool browseSave_ = false;        // current popup is Save (vs Open)
    char saveName_[128] = "sketch.s";
};

} // namespace bench

#endif // BENCH_CODE_BENCH_H
