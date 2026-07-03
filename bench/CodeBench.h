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
    void  drawModeMenu();                // status-bar "Mode" click → switch profile popup
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
    // A close action (tab X, Close, Close others/all) that would discard unsaved
    // changes, held until the user answers the Discard/Cancel confirmation popup.
    enum class CloseReq { None, One, Others, All };
    CloseReq pendingCloseReq_ = CloseReq::None;
    int      pendingCloseUid_ = -1;  // subject tab for One / survivor for Others
    bool anyDirty(int exceptUid) const;   // any open doc dirty (skip `exceptUid`, -1 = none)

    // ---- Global UI state (not per-document) ----
    std::string console_;
    bool statusOk_ = true;
    bool showConsole_ = false;
    bool inited_ = false;
    bool buildPolling_ = false;      // an async (web/WASM) build is in flight
    std::string status_;
    std::string lastForwardedStatus_;  // last status pushed to host_->onStatus (dedup)
    char fallbackAddr_[8] = "0300";
    // In-app file browser (Open/Save), rooted at the host's browseDir().
    std::string browseDir_;          // current directory shown in the browser
    bool browseSave_ = false;        // current popup is Save (vs Open)
    char saveName_[128] = "sketch.s";
    int newLang_ = 0;                // New-dialog selection: language x machine
    int newMachine_ = 0;
    // Markdown link navigation: stack of source paths to return to via the Back
    // arrow in the preview/edit toolbar (each link jump pushes the doc it left).
    std::vector<std::string> mdNavBack_;
    // Tab "Rename…" context-menu action: target doc + the in-flight input buffer.
    int  renameUid_      = -1;
    char renameBuf_[128] = "";
    // Interactive REPL (shown when host_->replActive()): one-line input + command
    // history (Up/Down recall). Sent lines are echoed into console_ and fed to the
    // host's resident interpreter via replSend().
    char replInput_[128] = "";
    std::vector<std::string> replHistory_;   // submitted lines, oldest first
    int  replHistoryPos_ = -1;               // -1 = editing a fresh line
    bool replFocus_      = false;            // request keyboard focus next frame
    int  onReplHistory(void* data);          // ImGuiInputTextCallbackData* (Up/Down)
};

} // namespace bench

#endif // BENCH_CODE_BENCH_H
