// Bench portable module — the emulator-agnostic editor window (Arduino-style):
// teal toolbar (Verify/Upload/New/Open/Save/Examples/Serial), syntax-highlighted
// editor, Target picker, build-output console, status bar. Multiple files open as
// tabs; Markdown files get an edit/preview toggle. All emulator work is delegated
// to an IBenchHost. Copy bench/ verbatim into POM2/NeoST and supply a host.
// Depends only on ImGui, ImGuiColorTextEdit and IconsFontAwesome6.
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

    // Switch the ACTIVE tab's target (syntax highlighter + starter sketch) WITHOUT
    // notifying the host — used when the bench is being driven BY a preset change
    // (so we don't bounce back through onTargetSelected). Returns true if the
    // starter was loaded; false (leaving the editor alone) if the buffer is dirty
    // or the index is out of range. Safe to call before the first render — lazily
    // creates the first document.
    bool loadStarterForTargetIfClean(int targetIndex);

private:
    // One open file = one editor tab.
    struct Doc {
        std::unique_ptr<TextEditor> editor;
        int         uid = 0;             // stable id (tab ###, focus, close)
        std::string path;                // "" = untitled
        std::string title;               // tab label
        std::string language;            // last applied lang id (syntax)
        std::string lastSavedText;       // dirty check baseline
        int  targetIndex = 0;            // build/syntax target
        bool dirty = false;
        std::vector<int> errorLines;     // 1-based, mirrored onto the scrollbar
        bool isMarkdown = false;
        bool mdPreview  = true;          // markdown: rendered preview vs edit
    };

    void  ensureDocs();
    Doc*  cur();
    Doc&  newDoc(const std::string& path, const std::string& text, int targetIndex);
    void  applyDocSyntax(Doc& d);
    void  applyResult(const BuildResult& r);
    void  closeDoc(int uid);
    void  closeOtherDocs(int keepUid);   // close every tab except `keepUid`
    void  closeAllDocs();                // close every tab → New chooser (see renderNewPhase)
    void  drawNewDialog();               // the "New sketch" chooser popup body
    void  renderNewPhase(const char* title, bool* open);  // empty-bench state: New chooser only
    int   findDocByPath(const std::string& path) const;
    static bool pathIsMarkdown(const std::string& path);

    IBenchHost* host_;
    std::vector<std::unique_ptr<Doc>> docs_;
    int  active_   = 0;
    int  nextUid_  = 1;
    int  untitledSeq_ = 0;
    int  focusUid_ = -1;             // request to programmatically select a tab
    int  lastActiveUid_ = -1;        // detect tab switches → refresh host mode mapping
    bool pendingNewChoose_ = false;  // bench just emptied → auto-open the New chooser

    // ---- Global UI state (not per-document) ----
    std::string console_;
    bool statusOk_ = true;
    bool showConsole_ = false;
    bool inited_ = false;
    bool buildPolling_ = false;      // an async (web/WASM) build is in flight
    std::string status_;
    char fallbackAddr_[8] = "0300";
    // In-app file browser (Open/Save), rooted at the host's browseDir().
    std::string browseDir_;          // current directory shown in the browser
    bool browseSave_ = false;        // current popup is Save (vs Open)
    char saveName_[128] = "sketch.s";
    int newLang_ = 0;                // New-dialog selection: language x machine
    int newMachine_ = 0;
};

} // namespace bench

#endif // BENCH_CODE_BENCH_H
