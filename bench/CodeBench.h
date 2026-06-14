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
    char filePath_[512] = {0};
    char rawAddr_[8] = "0300";
};

} // namespace bench

#endif // BENCH_CODE_BENCH_H
