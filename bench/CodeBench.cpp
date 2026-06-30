// Bench portable module — see CodeBench.h.
#include "CodeBench.h"

#include "IBenchHost.h"
#include "BenchLang.h"
#include "Markdown.h"
#include "TextEditor.h"
#include "imgui.h"
#include "IconsFontAwesome6.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace bench {

CodeBench::CodeBench(IBenchHost* host) : host_(host) {}
CodeBench::~CodeBench() = default;

bool CodeBench::pathIsMarkdown(const std::string& path)
{
    auto ends = [&](const char* ext) {
        const size_t n = std::strlen(ext);
        if (path.size() < n) return false;
        for (size_t i = 0; i < n; ++i)
            if (std::tolower(static_cast<unsigned char>(path[path.size() - n + i])) != ext[i]) return false;
        return true;
    };
    return ends(".md") || ends(".markdown") || ends(".mdown") || ends(".mkd");
}

CodeBench::Doc* CodeBench::cur()
{
    if (docs_.empty()) return nullptr;
    if (active_ < 0) active_ = 0;
    if (active_ >= static_cast<int>(docs_.size())) active_ = static_cast<int>(docs_.size()) - 1;
    return docs_[active_].get();
}

void CodeBench::applyDocSyntax(Doc& d)
{
    const auto& targets = host_->targets();
    std::string lang = d.isMarkdown ? "markdown"
                     : (d.targetIndex >= 0 && d.targetIndex < static_cast<int>(targets.size()))
                         ? targets[d.targetIndex].language : "";
    if (lang != d.language) {
        d.editor->SetLanguageDefinition(langDef(lang));
        d.language = lang;
    }
    // BASIC programs carry their own line numbers (10, 20, …) — the editor's
    // gutter line numbers are just noise, so hide them for BASIC documents.
    d.editor->SetShowLineNumbers(lang != "BASIC");
}

CodeBench::Doc& CodeBench::newDoc(const std::string& path, const std::string& text, int targetIndex)
{
    auto d = std::make_unique<Doc>();
    d->uid = nextUid_++;
    d->editor = std::make_unique<TextEditor>();
    d->editor->SetPalette(TextEditor::GetLightPalette());
    d->editor->SetShowWhitespaces(false);
    d->editor->SetHandleRightClickCopy(false);   // the host owns right-click (context menu)
    d->path = path;
    d->targetIndex = targetIndex;
    d->isMarkdown = !path.empty() && pathIsMarkdown(path);
    d->mdPreview = d->isMarkdown;                 // open .md in presentation mode
    if (!path.empty()) {
        const size_t slash = path.find_last_of("/\\");
        d->title = (slash == std::string::npos) ? path : path.substr(slash + 1);
    } else {
        d->title = "untitled-" + std::to_string(++untitledSeq_);
    }
    applyDocSyntax(*d);
    d->editor->SetText(text);
    d->lastSavedText = d->editor->GetText();
    d->dirty = false;
    docs_.push_back(std::move(d));
    active_ = static_cast<int>(docs_.size()) - 1;
    focusUid_ = docs_.back()->uid;
    return *docs_.back();
}

void CodeBench::ensureDocs()
{
    if (inited_) return;
    inited_ = true;
    const int ti = host_->defaultTargetIndex();
    newDoc("", host_->starterSketch(ti), ti);
}

int CodeBench::findDocByPath(const std::string& path) const
{
    if (path.empty()) return -1;
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i)
        if (docs_[i]->path == path) return i;
    return -1;
}

void CodeBench::closeDoc(int uid)
{
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i) {
        if (docs_[i]->uid != uid) continue;
        docs_.erase(docs_.begin() + i);
        if (active_ >= i) active_ = active_ > 0 ? active_ - 1 : 0;
        break;
    }
    if (docs_.empty()) pendingNewChoose_ = true;  // empty bench → render() opens the New chooser
}

void CodeBench::closeOtherDocs(int keepUid)
{
    for (int i = static_cast<int>(docs_.size()) - 1; i >= 0; --i)
        if (docs_[i]->uid != keepUid) docs_.erase(docs_.begin() + i);
    active_ = 0;
    focusUid_ = keepUid;                          // keep the survivor selected
}

void CodeBench::closeAllDocs()
{
    docs_.clear();
    active_ = 0;
    pendingNewChoose_ = true;                      // empty bench → render() opens the New chooser
}

bool CodeBench::loadStarterForTargetIfClean(int targetIndex)
{
    ensureDocs();
    Doc* d = cur();
    const auto& targets = host_->targets();
    if (!d || targetIndex < 0 || targetIndex >= static_cast<int>(targets.size())) return false;
    d->targetIndex = targetIndex;
    applyDocSyntax(*d);
    if (d->dirty) return false;
    d->isMarkdown = false; d->mdPreview = false;
    d->editor->SetText(host_->starterSketch(targetIndex));
    d->editor->SetErrorMarkers({}); d->errorLines.clear();
    d->path.clear();
    d->lastSavedText = d->editor->GetText(); d->dirty = false;
    status_ = "Starter loaded for new preset"; statusOk_ = true;
    return true;
}

void CodeBench::applyResult(const BuildResult& r)
{
    console_ = r.console;
    if (r.showConsole) showConsole_ = true;
    status_  = r.status;
    statusOk_ = r.ok;
    Doc* d = cur();
    if (!d) return;
    TextEditor::ErrorMarkers em;
    d->errorLines.clear();
    for (const auto& e : r.errors) { em[e.first] = e.second; d->errorLines.push_back(e.first); }
    d->editor->SetErrorMarkers(em);
}

void CodeBench::drawNewDialog()
{
    if (!ImGui::BeginPopup("##benchnewdlg")) return;
    const auto& langs     = host_->languages();
    const auto& machs     = host_->machines();
    const auto& langHints = host_->languageHints();
    const auto& machHints = host_->machineHints();
    auto hintFor = [](const std::vector<std::string>& h, int i) -> const char* {
        return (i >= 0 && i < (int)h.size() && !h[i].empty()) ? h[i].c_str() : nullptr;
    };
    ImGui::TextDisabled("New sketch"); ImGui::Separator();
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Language:"); ImGui::SameLine(96);
    ImGui::SetNextItemWidth(300);
    if (ImGui::BeginCombo("##newlang", newLang_ < (int)langs.size() ? langs[newLang_].c_str() : "")) {
        for (int i = 0; i < (int)langs.size(); ++i)
            if (ImGui::Selectable(langs[i].c_str(), i == newLang_)) newLang_ = i;
        ImGui::EndCombo();
    }
    if (host_->targetFor(newLang_, newMachine_) < 0) {
        for (int i = 0; i < (int)machs.size(); ++i)
            if (host_->targetFor(newLang_, i) >= 0) { newMachine_ = i; break; }
    }
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Target:"); ImGui::SameLine(96);
    ImGui::SetNextItemWidth(300);
    if (ImGui::BeginCombo("##newmach", newMachine_ < (int)machs.size() ? machs[newMachine_].c_str() : "")) {
        for (int i = 0; i < (int)machs.size(); ++i) {
            if (host_->targetFor(newLang_, i) < 0) continue;
            if (ImGui::Selectable(machs[i].c_str(), i == newMachine_)) newMachine_ = i;
        }
        ImGui::EndCombo();
    }
    if (const char* h = hintFor(langHints, newLang_))
        ImGui::TextDisabled("%s", h);
    if (const char* h = hintFor(machHints, newMachine_))
        ImGui::TextDisabled("%s", h);
    ImGui::Separator();
    const int t = host_->targetFor(newLang_, newMachine_);
    if (t >= 0) {
        const std::string hint = host_->toolchainHint(t);
        const bool ok = host_->toolchainReady(t);
        if (!hint.empty())
            ImGui::TextColored(ok ? ImVec4(0.4f, 0.85f, 0.4f, 1.0f) : ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                               "%s %s", ok ? ICON_FA_MICROCHIP : ICON_FA_HAMMER, hint.c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "combination unavailable");
    }
    if (ImGui::Button("Create") && t >= 0) {
        host_->onTargetSelected(t);
        newDoc("", host_->starterSketch(t), t);
        status_ = "New: " + host_->targets()[t].label; statusOk_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Quick profile switcher: opened by clicking the "Mode:" label in the status bar.
// Same language x machine axes as the New dialog, but it switches the ACTIVE tab's
// target in place — one click, no new tab. The starter is reloaded only on a clean
// buffer (loadStarterForTargetIfClean keeps the user's code if the tab is dirty).
void CodeBench::drawModeMenu()
{
    if (!ImGui::BeginPopup("##benchmodepopup")) return;
    const Doc* d = cur();
    const int  curTarget = d ? d->targetIndex : -1;

    auto pick = [&](int t) {
        if (t < 0) return;
        // Switch the profile AND prepare the target's runtime (cold-start the matching
        // BASIC / ready the toolchain). The editor's code is left untouched.
        BuildResult pr = host_->selectTargetExplicit(t);
        if (Doc* dd = cur()) {                 // retarget the tab IN PLACE: the code in
            dd->targetIndex = t;               // the editor stays exactly as it is —
            applyDocSyntax(*dd);               // only the build target + syntax change.
        }
        status_   = pr.status.empty() ? ("Mode: " + host_->targets()[t].label) : pr.status;
        statusOk_ = pr.ok || pr.status.empty();
        if (pr.showConsole) { console_ = pr.console; showConsole_ = true; }
        ImGui::CloseCurrentPopup();
    };

    ImGui::TextDisabled("Switch mode / profile"); ImGui::Separator();

    const auto& langs = host_->languages();
    const auto& machs = host_->machines();
    if (langs.empty() || machs.empty()) {
        // Host without language/machine axes: flat list of every target.
        const auto& targets = host_->targets();
        for (int t = 0; t < (int)targets.size(); ++t) {
            std::string ml = host_->modeLabel(t);
            if (ml.empty()) ml = targets[t].label;
            if (ImGui::Selectable(ml.c_str(), t == curTarget)) pick(t);
        }
        ImGui::EndPopup();
        return;
    }

    // Short language name: trim the "  —  toolchain" tail (e.g. "Assembly").
    auto shortLang = [&](int l) {
        std::string s = langs[l];
        const size_t dash = s.find("  \xE2\x80\x94");   // "  —"
        if (dash != std::string::npos) s.resize(dash);
        return s;
    };

    // One section per language, one full-width Selectable per machine it can build
    // for. Uniform rows (no inline buttons) keep the switcher clean and scannable;
    // the active target is highlighted via the Selectable's selected state. asm and
    // C list the same three machines, each under its own header; BASIC's "machines"
    // are its interpreters.
    bool firstSection = true;
    for (int l = 0; l < (int)langs.size(); ++l) {
        bool anyMachine = false;
        for (int m = 0; m < (int)machs.size(); ++m)
            if (host_->targetFor(l, m) >= 0) { anyMachine = true; break; }
        if (!anyMachine) continue;   // language builds for nothing — no header

        if (!firstSection) ImGui::Spacing();
        firstSection = false;
        ImGui::TextDisabled("%s", shortLang(l).c_str());

        ImGui::Indent();
        for (int m = 0; m < (int)machs.size(); ++m) {
            const int t = host_->targetFor(l, m);
            if (t < 0) continue;
            const std::string lbl = machs[m] + "##mode" + std::to_string(t);
            if (ImGui::Selectable(lbl.c_str(), t == curTarget)) pick(t);
        }
        ImGui::Unindent();
    }
    ImGui::EndPopup();
}

// Empty bench (every tab closed): render the window with only the "New sketch"
// chooser. Create makes the first tab and normal rendering resumes next frame.
void CodeBench::renderNewPhase(const char* title, bool* open)
{
    // A host without language/machine axes has no chooser to show — fall back to a
    // default starter so the bench is never left unusable.
    if (host_->languages().empty() || host_->machines().empty()) {
        const int ti = host_->defaultTargetIndex();
        newDoc("", host_->starterSketch(ti), ti);
        pendingNewChoose_ = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(660, 720), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(520, 480), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::Dummy(ImVec2(0, avail.y * 0.40f));
    const char* msg = ICON_FA_FILE "  No open sketch — start a new one.";
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - ImGui::CalcTextSize(msg).x) * 0.5f);
    ImGui::TextDisabled("%s", msg);
    const char* btn = "New sketch...";
    const float bw = ImGui::CalcTextSize(btn).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - bw) * 0.5f);
    if (ImGui::Button(btn)) ImGui::OpenPopup("##benchnewdlg");

    // Auto-open the chooser the first frame after the bench emptied.
    if (pendingNewChoose_) { ImGui::OpenPopup("##benchnewdlg"); pendingNewChoose_ = false; }
    drawNewDialog();
    ImGui::End();
}

void CodeBench::render(const char* title, bool* open)
{
    ensureDocs();

    // Drive any in-flight async build (web/WASM cc65): poll until it finishes.
    if (buildPolling_) {
        BuildResult pr = host_->pollBuild();
        if (!pr.pending) { applyResult(pr); buildPolling_ = false; }
    }

    // All tabs closed: there is no active document. Show the bench window with the
    // "New sketch" chooser instead of silently re-creating a default tab.
    if (docs_.empty()) { renderNewPhase(title, open); return; }

    const auto& targets = host_->targets();
    Doc& doc = *cur();
    // targetIndex -1 is a valid "no build target" state (markdown / unknown file
    // type — "do nothing"); only an out-of-range positive index is invalid.
    if (doc.targetIndex >= static_cast<int>(targets.size())) doc.targetIndex = 0;

    // Tab switch (or first frame): refresh the host's active-source context and
    // re-derive this document's mode from its extension, so the hex/c/s/asm/md/
    // apf/bas mapping always matches the front tab. Path-less scratch docs keep
    // the target they were created with.
    if (doc.uid != lastActiveUid_) {
        lastActiveUid_ = doc.uid;
        host_->setActiveSourcePath(doc.path);
        if (!doc.path.empty()) {
            doc.isMarkdown = pathIsMarkdown(doc.path);
            if (!doc.isMarkdown) {
                const int ti = host_->targetForPath(doc.path);
                doc.targetIndex = (ti >= 0 && ti < static_cast<int>(targets.size())) ? ti : -1;
            }
            applyDocSyntax(doc);
        }
    }

    // Arduino IDE palette (teal toolbar/status, dark console).
    const ImVec4 kTeal     = ImVec4(0.000f, 0.592f, 0.616f, 1.0f);
    const ImVec4 kTealDark = ImVec4(0.000f, 0.353f, 0.369f, 1.0f);
    const ImVec4 kWhite    = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    const ImVec4 kConsoleBg= ImVec4(0.10f, 0.10f, 0.11f, 1.0f);

    ImGui::SetNextWindowSize(ImVec2(660, 720), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(520, 480), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

    // ---- Optional host banner (e.g. web build "desktop only" CTA) ----
    {
        const std::string note = host_->headerNote();
        if (!note.empty()) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.12f, 0.02f, 1.0f));
            ImGui::BeginChild("##benchnote", ImVec2(0, 0),
                              ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.35f, 1.0f));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(note.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }
    }

    // ---- Actions (operate on the active document) ----
    auto openFile = [&](const std::string& path) {
        const int existing = findDocByPath(path);
        if (existing >= 0) { active_ = existing; focusUid_ = docs_[existing]->uid; status_ = "Already open"; statusOk_ = true; return; }
        std::ifstream in(path, std::ios::binary);
        if (!in) { status_ = "Open failed: " + path; statusOk_ = false; return; }
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        // Tell the host which file is active BEFORE inferring the target (the
        // inference may depend on the path, e.g. a "portable" sketch).
        host_->setActiveSourcePath(path);
        // Extension drives the action: a known type (c/s/asm/hex/apf/bas) gets its
        // build target; markdown opens as a document; anything else is targetIndex
        // -1 ("do nothing" — Verify/Run are no-ops). No inheriting the old tab's
        // target, so opening a .png/.json/etc. never silently builds as asm.
        int ti = host_->targetForPath(path);
        if (ti >= static_cast<int>(targets.size())) ti = -1;
        Doc& nd = newDoc(path, data, ti);
        if (!nd.isMarkdown && ti >= 0) host_->onTargetSelected(ti);
        lastActiveUid_ = nd.uid;          // already current; don't re-derive next frame
        status_ = "Opened " + path + " (" + std::to_string(data.size()) + " B)";
        statusOk_ = true;
    };
    auto saveFile = [&](const std::string& path) {
        std::ofstream out(path, std::ios::binary);
        if (!out) { status_ = "Save failed: " + path; statusOk_ = false; return; }
        const std::string text = doc.editor->GetText();
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        doc.path = path;
        const size_t slash = path.find_last_of("/\\");
        doc.title = (slash == std::string::npos) ? path : path.substr(slash + 1);
        doc.isMarkdown = pathIsMarkdown(path);
        doc.lastSavedText = text; doc.dirty = false;
        status_ = "Saved " + path + " (" + std::to_string(text.size()) + " B)";
        statusOk_ = true;
    };
    // Rename the doc with `uid` to `rawName`. A path-backed doc is renamed on disk
    // (same directory); an untitled doc just gets a new tab label. The build target
    // + syntax + markdown mode are re-derived from the (possibly new) extension.
    auto renameDoc = [&](int uid, const char* rawName) {
        Doc* d = nullptr;
        for (auto& up : docs_) if (up->uid == uid) { d = up.get(); break; }
        if (!d) return;

        std::string name = rawName ? rawName : "";
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(name.begin());
        while (!name.empty() && (name.back()  == ' ' || name.back()  == '\t')) name.pop_back();
        if (name.empty()) { status_ = "Rename: empty name"; statusOk_ = false; return; }
        if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
            status_ = "Rename: name cannot contain a path separator"; statusOk_ = false; return;
        }
        if (name == d->title) return;   // unchanged

        if (!d->path.empty()) {
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path oldP(d->path);
            const fs::path newP = oldP.parent_path() / name;
            if (fs::exists(newP, ec)) { status_ = "Rename: '" + name + "' already exists"; statusOk_ = false; return; }
            fs::rename(oldP, newP, ec);
            if (ec) { status_ = "Rename failed: " + ec.message(); statusOk_ = false; return; }
            d->path = newP.string();
            status_ = "Renamed to " + name; statusOk_ = true;
        } else {
            status_ = "Tab renamed to " + name; statusOk_ = true;
        }
        d->title = name;
        // Re-derive mode + build target from the (possibly new) extension.
        const bool wasMd = d->isMarkdown;
        d->isMarkdown = pathIsMarkdown(d->path.empty() ? name : d->path);
        if (d->isMarkdown && !wasMd) d->mdPreview = true;
        if (!d->path.empty() && !d->isMarkdown) {
            const int ti = host_->targetForPath(d->path);
            d->targetIndex = (ti >= 0 && ti < static_cast<int>(targets.size())) ? ti : -1;
        }
        applyDocSyntax(*d);
        lastActiveUid_ = -1;   // force the front tab to re-sync host context next frame
    };
    // New: pick a (language x machine) target, then load its HELLO WORLD into a
    // NEW tab. If the host offers no axes, New just adds a starter tab.
    bool openNew = false;
    auto doNewChoose = [&]() {
        if (host_->languages().empty() || host_->machines().empty())
            newDoc("", host_->starterSketch(doc.targetIndex), doc.targetIndex);
        else openNew = true;
    };
    bool openBrowse = false;
    auto browse = [&](bool save) {
        if (browseDir_.empty()) browseDir_ = host_->browseDir();
        // Prefer the host's OS-native file picker; fall back to the in-process
        // ImGui browser below when the host has none (WASM, Linux without
        // zenity/kdialog). Matches the MainWindow dialogs' native+fallback.
        {
            const std::string title = save ? "Save file" : "Open file";
            const std::string desc  = "Source / data files";
            const std::string ext   = "c,h,s,asm,inc,bas,apf,int,hex,txt,md,json,cfg";
            const std::string defName = save ? doc.title : std::string();
            std::string picked;
            if (host_->pickFilePath(save, title, desc, ext, browseDir_, defName, picked)) {
                namespace fs = std::filesystem;
                const std::string dir = fs::path(picked).parent_path().string();
                if (!dir.empty()) browseDir_ = dir;
                if (save) saveFile(picked); else openFile(picked);
                return;
            }
        }
        browseSave_ = save;
        openBrowse = true;
    };
    auto doVerify = [&]() {
        if (doc.isMarkdown)      { status_ = "Markdown is a document — nothing to build"; statusOk_ = false; return; }
        if (doc.targetIndex < 0) { status_ = "Unknown file type — nothing to build"; statusOk_ = false; return; }
        host_->setActiveSourcePath(doc.path);
        BuildResult r = host_->verify(doc.targetIndex, doc.editor->GetText(), fallbackAddr_);
        buildPolling_ = r.pending; applyResult(r);
    };
    auto doUpload = [&]() {
        if (doc.isMarkdown)      { status_ = "Markdown is a document — nothing to build"; statusOk_ = false; return; }
        if (doc.targetIndex < 0) { status_ = "Unknown file type — nothing to run"; statusOk_ = false; return; }
        host_->setActiveSourcePath(doc.path);
        BuildResult r = host_->upload(doc.targetIndex, doc.editor->GetText(), fallbackAddr_);
        buildPolling_ = r.pending; applyResult(r);
    };

    // ---- Teal toolbar with labelled action pills + circular icon buttons ----
    bool openExamplesPopup = false;
    bool openToolchainPopup = false;
    auto circleBtn = [&](const char* icon, const char* id, const char* tip,
                         ImU32 iconCol = IM_COL32_WHITE, ImU32 ringCol = IM_COL32_WHITE) -> bool {
        const float d = 34.0f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(d, d));
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
        if (hov) dl->AddCircleFilled(c, d * 0.5f, ImGui::GetColorU32(kTealDark), 32);
        const ImVec2 ts = ImGui::CalcTextSize(icon);
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), iconCol, icon);
        dl->AddCircle(c, d * 0.5f - 1.0f, ringCol, 32, hov ? 2.5f : 1.5f);
        if (hov && tip) ImGui::SetTooltip("%s", tip);
        return clicked;
    };
    auto pillBtn = [&](const char* icon, const char* label, const char* id, const char* tip) -> bool {
        const std::string txt = (label && label[0]) ? std::string(icon) + "  " + label
                                                     : std::string(icon);
        const ImVec2 ts = ImGui::CalcTextSize(txt.c_str());
        const float padX = 11.0f, h = 34.0f;
        const ImVec2 sz(ts.x + padX * 2.0f, h);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(id, sz);
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y),
                          ImGui::GetColorU32(hov ? kTealDark : kTeal), 7.0f);
        dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32_WHITE, 7.0f, 0, hov ? 2.5f : 1.5f);
        dl->AddText(ImVec2(p.x + padX, p.y + (h - ts.y) * 0.5f), IM_COL32_WHITE, txt.c_str());
        if (hov && tip) ImGui::SetTooltip("%s", tip);
        return clicked;
    };
    auto sep = [&]() {
        ImGui::SameLine(0, 6);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = 11.0f;
        ImGui::Dummy(ImVec2(w, 34.0f));
        const float x = p.x + w * 0.5f;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(x - 1.0f, p.y + 4.0f), ImVec2(x + 1.0f, p.y + 30.0f),
            IM_COL32(255, 255, 255, 70));
    };

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kTeal);
    ImGui::BeginChild("##benchtoolbar", ImVec2(0, 44), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos(ImVec2(8, 5));
    if (pillBtn(ICON_FA_CHECK,       "", "##benchverify", "Verify - compile only")) doVerify();
    ImGui::SameLine(0, 6);
    if (pillBtn(ICON_FA_ARROW_RIGHT, "", "##benchupload", "Run - build and run on the emulator")) doUpload();
    sep();
    ImGui::SameLine(0, 6);
    if (circleBtn(ICON_FA_FILE,          "##benchnew",      "New sketch (pick language + target) — opens a tab")) doNewChoose();
    ImGui::SameLine(0, 6);
    if (circleBtn(ICON_FA_FOLDER_OPEN,   "##benchopen",     "Open file in a new tab (browse sketchs/)")) browse(false);
    ImGui::SameLine(0, 6);
    if (circleBtn(ICON_FA_FLOPPY_DISK,   "##benchsave",     "Save the active file (browse)"))      browse(true);
    if (!host_->examples().empty()) {
        ImGui::SameLine(0, 6);
        if (circleBtn(ICON_FA_BOOK,      "##benchexamples", "Examples")) openExamplesPopup = true;
    }
    if (host_->hasStop()) {
        sep();
        ImGui::SameLine(0, 6);
        if (host_->cpuIsRunning()) {
            if (circleBtn(ICON_FA_STOP, "##benchcputoggle", "Stop (halt the CPU)", IM_COL32(235, 80, 60, 255))) {
                host_->stop();
                status_ = "Stopped"; statusOk_ = true;
            }
        } else {
            if (circleBtn(ICON_FA_PLAY, "##benchcputoggle", "Run CPU (resume the processor)", IM_COL32(120, 230, 140, 255))) {
                host_->cpuRun();
                status_ = "CPU running"; statusOk_ = true;
            }
        }
        ImGui::SameLine(0, 6);
        if (circleBtn(ICON_FA_FORWARD_STEP, "##benchstep", "Step (single 6502 instruction)")) {
            std::string s = host_->cpuStep();
            status_ = s.empty() ? "Stepped" : s; statusOk_ = true;
        }
    }

    const bool toolchainOk = host_->toolchainReady(doc.targetIndex);
    const ImU32 tcCol = toolchainOk ? IM_COL32_WHITE : IM_COL32(235, 80, 60, 255);
    const float rightX = ImGui::GetWindowWidth() - 42;
    if (host_->hasSerial()) {
        ImGui::SameLine(rightX - 40);
        if (circleBtn(ICON_FA_PLUG, "##benchserial", "Serial Monitor")) host_->openSerial();
    }
    ImGui::SameLine(rightX);
    if (circleBtn(ICON_FA_SCREWDRIVER_WRENCH, "##benchtoolchain",
                  toolchainOk ? "Toolchain status (cc65 / dev/)"
                              : "Toolchain incomplete — cc65 / dev/ not found (click for details)",
                  tcCol, tcCol))
        openToolchainPopup = true;
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // ---- Examples popup (loads into the ACTIVE tab) ----
    if (openExamplesPopup) ImGui::OpenPopup("##benchexamplespopup");
    if (ImGui::BeginPopup("##benchexamplespopup")) {
        ImGui::TextDisabled("Examples"); ImGui::Separator();
        const auto& ex = host_->examples();
        for (int i = 0; i < static_cast<int>(ex.size()); ++i) {
            if (!ex[i].group.empty()) {
                if (i != 0) ImGui::Spacing();
                ImGui::TextDisabled("%s", ex[i].group.c_str());
            }
            if (ImGui::Selectable(ex[i].label.c_str())) {
                ExampleLoad el = host_->loadExample(i);
                if (el.ok) {
                    doc.isMarkdown = false; doc.mdPreview = false;
                    doc.editor->SetText(el.source);
                    doc.editor->SetErrorMarkers({}); doc.errorLines.clear();
                    doc.path.clear();
                    doc.lastSavedText = doc.editor->GetText(); doc.dirty = false;
                    if (el.targetIndex >= 0) { doc.targetIndex = el.targetIndex; applyDocSyntax(doc); }
                    doc.title = "untitled-" + std::to_string(++untitledSeq_);
                }
                status_ = el.status; statusOk_ = el.ok;
            }
        }
        ImGui::EndPopup();
    }

    // ---- Toolchain status popup ----
    if (openToolchainPopup) ImGui::OpenPopup("##benchtoolchainpopup");
    if (ImGui::BeginPopup("##benchtoolchainpopup")) {
        const std::string rep = host_->toolchainReport();
        ImGui::TextUnformatted(rep.empty() ? "No toolchain info." : rep.c_str());
        ImGui::EndPopup();
    }

    // ---- File browser popup (Open / Save) ----
    if (openBrowse) ImGui::OpenPopup("##benchbrowse");
    if (ImGui::BeginPopup("##benchbrowse")) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path curp(browseDir_);
        ImGui::TextUnformatted(browseSave_ ? ICON_FA_FLOPPY_DISK " Save to" : ICON_FA_FOLDER_OPEN " Open");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", curp.string().c_str());
        ImGui::Separator();
        ImGui::BeginChild("##browselist", ImVec2(480, 300), true);
        if (curp.has_parent_path() && curp.parent_path() != curp) {
            if (ImGui::Selectable(ICON_FA_FOLDER_OPEN " .."))
                browseDir_ = curp.parent_path().string();
        }
        std::vector<std::string> dirs, files;
        for (const auto& e : fs::directory_iterator(curp, ec)) {
            if (e.is_directory(ec)) dirs.push_back(e.path().filename().string());
            else                    files.push_back(e.path().filename().string());
        }
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());
        for (const auto& dn : dirs) {
            const std::string label = ICON_FA_FOLDER_OPEN " " + dn;
            if (ImGui::Selectable(label.c_str())) browseDir_ = (curp / dn).string();
        }
        for (const auto& f : files) {
            const std::string label = ICON_FA_FILE " " + f;
            if (ImGui::Selectable(label.c_str())) {
                if (browseSave_) {
                    std::snprintf(saveName_, sizeof(saveName_), "%s", f.c_str());
                } else {
                    openFile((curp / f).string());
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndChild();
        if (browseSave_) {
            ImGui::SetNextItemWidth(-160.0f);
            ImGui::InputText("##savename", saveName_, sizeof(saveName_));
            ImGui::SameLine();
            if (ImGui::Button("Save") && saveName_[0]) {
                saveFile((curp / saveName_).string());
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- New-sketch dialog: pick language + machine, open it in a new tab ----
    if (openNew) ImGui::OpenPopup("##benchnewdlg");
    drawNewDialog();

    // ---- Document tabs (one per open file) ----
    int  closeUid       = -1;
    int  closeOthersUid = -1;
    bool closeAllReq    = false;
    bool openRename     = false;
    if (ImGui::BeginTabBar("##benchtabs", ImGuiTabBarFlags_AutoSelectNewTabs |
                                          ImGuiTabBarFlags_Reorderable |
                                          ImGuiTabBarFlags_FittingPolicyScroll |
                                          ImGuiTabBarFlags_TabListPopupButton)) {
        const int tabCount = static_cast<int>(docs_.size());
        for (int i = 0; i < tabCount; ++i) {
            Doc& d = *docs_[i];
            ImGuiTabItemFlags flags = 0;
            if (d.dirty) flags |= ImGuiTabItemFlags_UnsavedDocument;
            if (focusUid_ == d.uid) flags |= ImGuiTabItemFlags_SetSelected;
            const std::string label = d.title + "###bdoc" + std::to_string(d.uid);
            bool tabOpen = true;
            if (ImGui::BeginTabItem(label.c_str(), &tabOpen, flags)) {
                active_ = i;
                ImGui::EndTabItem();
            }
            // Right-click the tab header for rename + close actions (the tab is the last item).
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(ICON_FA_PEN_TO_SQUARE "  Rename...")) {
                    renameUid_ = d.uid;
                    std::snprintf(renameBuf_, sizeof(renameBuf_), "%s", d.title.c_str());
                    openRename = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_XMARK "  Close"))                       closeUid = d.uid;
                if (ImGui::MenuItem("Close others", nullptr, false, tabCount > 1))  closeOthersUid = d.uid;
                if (ImGui::MenuItem("Close all"))                                   closeAllReq = true;
                ImGui::EndPopup();
            }
            if (!tabOpen) closeUid = d.uid;
        }
        // The trailing "+" opens a fresh BLANK tab in the current target (no
        // chooser) — quick scratch buffer. The toolbar's New button still offers
        // the language x machine picker for a different mode.
        if (ImGui::TabItemButton(ICON_FA_PLUS, ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
            int ti = cur()->targetIndex;
            if (ti < 0) ti = host_->defaultTargetIndex();   // markdown/unknown → sane default
            newDoc("", "", ti);
            status_ = "New blank tab"; statusOk_ = true;
        }
        ImGui::EndTabBar();
    }
    focusUid_ = -1;

    // Rename popup, opened from a tab's right-click context menu.
    if (openRename) ImGui::OpenPopup("##benchrename");
    if (ImGui::BeginPopupModal("##benchrename", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextDisabled(ICON_FA_PEN_TO_SQUARE "  Rename tab");
        ImGui::Separator();
        ImGui::SetNextItemWidth(280.0f);
        if (openRename) ImGui::SetKeyboardFocusHere();   // focus + select the field on open
        const bool enter = ImGui::InputText("##renamebuf", renameBuf_, sizeof(renameBuf_),
                                            ImGuiInputTextFlags_EnterReturnsTrue |
                                            ImGuiInputTextFlags_AutoSelectAll);
        ImGui::Spacing();
        bool doRename = enter;
        if (ImGui::Button("Rename", ImVec2(120, 0))) doRename = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        if (doRename) { renameDoc(renameUid_, renameBuf_); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    // Apply at most one structural change per frame, most destructive first.
    if (closeAllReq)              closeAllDocs();
    else if (closeOthersUid >= 0) closeOtherDocs(closeOthersUid);
    else if (closeUid >= 0)       closeDoc(closeUid);
    // The last tab may have just closed: finish this frame's window now; the empty
    // bench check at the top of render() opens the New chooser next frame.
    if (docs_.empty()) { ImGui::End(); return; }
    Doc& act = *cur();   // re-resolve: active_ / docs_ may have changed above

    // ---- Editor area (markdown gets an Edit/Preview toggle row) ----
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float spacingY       = ImGui::GetStyle().ItemSpacing.y;
    const float rowH           = ImGui::GetFrameHeightWithSpacing();
    const float kStatusBarH    = 24.0f;
    const float kConsoleChildH = 124.0f;
    const float consoleBlock   = showConsole_ ? (rowH + kConsoleChildH + spacingY) : 0.0f;
    const float mdToggleBlock  = act.isMarkdown ? rowH : 0.0f;
    float editorH = avail.y - consoleBlock - kStatusBarH - spacingY - mdToggleBlock;
    if (editorH < 100.0f) editorH = 100.0f;

    std::string mdBack;   // a Back-arrow click → reopen this path after render
    if (act.isMarkdown) {
        // Back arrow: return to the doc a link jumped from (disabled when the
        // navigation history is empty).
        const bool canBack = !mdNavBack_.empty();
        std::string backName;
        if (canBack) {
            const std::string& dest = mdNavBack_.back();
            const size_t slash = dest.find_last_of("/\\");
            backName = (slash == std::string::npos) ? dest : dest.substr(slash + 1);
        }
        ImGui::BeginDisabled(!canBack);
        if (ImGui::Button(ICON_FA_ARROW_LEFT "##mdback")) {
            mdBack = mdNavBack_.back();
            mdNavBack_.pop_back();
        }
        ImGui::EndDisabled();
        if (canBack && ImGui::IsItemHovered())
            ImGui::SetTooltip("Retour : %s", backName.c_str());
        ImGui::SameLine();
        if (ImGui::RadioButton("Preview", act.mdPreview)) act.mdPreview = true;
        ImGui::SameLine();
        if (ImGui::RadioButton("Edit", !act.mdPreview)) act.mdPreview = false;
        ImGui::SameLine();
        ImGui::TextDisabled(ICON_FA_FILE_LINES "  Markdown");
    }

    std::string mdFollow;   // a markdown link the user clicked → open it after render
    if (act.isMarkdown && act.mdPreview) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.13f, 0.13f, 0.15f, 1.0f));
        ImGui::BeginChild("##mdpreview", ImVec2(avail.x, editorH), true);
        ImGui::PushTextWrapPos(0.0f);
        const std::string clicked = RenderMarkdown(act.editor->GetText());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        if (!clicked.empty()) {
            namespace fs = std::filesystem;
            std::string url = clicked;
            const size_t hash = url.find('#');      // drop any #section anchor
            if (hash != std::string::npos) url = url.substr(0, hash);
            const bool external = url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 ||
                                  url.rfind("mailto:", 0) == 0;
            if (url.empty()) {
                // pure in-page anchor — nothing to open
            } else if (external) {
                ImGui::SetClipboardText(clicked.c_str());
                status_ = "External link copied: " + clicked; statusOk_ = true;
            } else {
                // Resolve relative to the current document's directory.
                std::error_code ec;
                fs::path base = act.path.empty() ? fs::current_path(ec)
                                                 : fs::path(act.path).parent_path();
                fs::path tgt(url);
                if (tgt.is_relative()) tgt = base / tgt;
                tgt = fs::weakly_canonical(tgt, ec);
                if (!ec && fs::exists(tgt, ec)) mdFollow = tgt.string();
                else { status_ = "Link not found: " + url; statusOk_ = false; }
            }
        }
    } else {
        act.editor->Render("##benchsrc", ImVec2(avail.x, editorH), true);

        // Right-click context menu on the editor.
        if (ImGui::BeginPopupContextItem("##benchsrcctx")) {
            const bool ro   = act.editor->IsReadOnly();
            const bool selc = act.editor->HasSelection();
            const char* clip = ImGui::GetClipboardText();
            const bool hasClip = clip && clip[0] != '\0';
            if (ImGui::MenuItem("Cut",   "Ctrl+X", false, selc && !ro))     act.editor->Cut();
            if (ImGui::MenuItem("Copy",  "Ctrl+C", false, selc))            act.editor->Copy();
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, !ro && hasClip))  act.editor->Paste();
            if (ImGui::MenuItem("Delete", nullptr, false, selc && !ro))     act.editor->Delete();
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A"))                    act.editor->SelectAll();
            ImGui::Separator();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !ro && act.editor->CanUndo())) act.editor->Undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !ro && act.editor->CanRedo())) act.editor->Redo();
            ImGui::EndPopup();
        }

        if (act.editor->IsTextChanged()) act.dirty = (act.editor->GetText() != act.lastSavedText);

        // Mirror error lines onto the editor's scrollbar lane (overview ruler).
        if (!act.errorLines.empty()) {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            const int total = act.editor->GetTotalLines();
            if (total > 0) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float sb = ImGui::GetStyle().ScrollbarSize;
                const float x0 = mx.x - sb + 1.0f;
                const float x1 = mx.x - 1.0f;
                const float trackH = mx.y - mn.y - 2.0f;
                const ImU32 col = IM_COL32(235, 80, 60, 230);
                for (int ln : act.errorLines) {
                    float t = static_cast<float>(ln - 1) / static_cast<float>(total);
                    if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
                    const float y = mn.y + 1.0f + t * trackH;
                    dl->AddRectFilled(ImVec2(x0, y - 1.5f), ImVec2(x1, y + 1.5f), col, 1.0f);
                }
            }
        }
    }

    // ---- Build output console ----
    if (showConsole_) {
        ImGui::TextUnformatted("Build output");
        ImGui::SameLine();
        if (ImGui::SmallButton("Hide"))      showConsole_ = false;
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##bc")) console_.clear();
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy##bc") && !console_.empty())
            ImGui::SetClipboardText(console_.c_str());
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kConsoleBg);
        ImGui::BeginChild("##benchconsole", ImVec2(avail.x, kConsoleChildH), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        size_t start = 0;
        while (start <= console_.size()) {
            size_t nl = console_.find('\n', start);
            const size_t end = (nl == std::string::npos) ? console_.size() : nl;
            const char* b = console_.data() + start;
            const char* e = console_.data() + end;
            const std::string line(b, e);
            const bool err = line.find("rror") != std::string::npos ||
                             line.find("ailed") != std::string::npos;
            int jumpLine = 0;
            if (err) {
                const size_t lp = line.find('(');
                if (lp != std::string::npos) {
                    const size_t rp = line.find(')', lp);
                    if (rp != std::string::npos)
                        try { jumpLine = std::stoi(line.substr(lp + 1, rp - lp - 1)); } catch (...) { jumpLine = 0; }
                }
            }
            ImGui::PushStyleColor(ImGuiCol_Text, err ? ImVec4(0.96f, 0.55f, 0.22f, 1.0f)
                                                     : ImVec4(0.82f, 0.82f, 0.82f, 1.0f));
            if (jumpLine > 0) {
                const std::string id = line + "##bcl" + std::to_string(start);
                if (ImGui::Selectable(id.c_str()))
                    act.editor->SetCursorPosition(TextEditor::Coordinates(jumpLine - 1, 0));
            } else {
                ImGui::TextUnformatted(b, e);
            }
            ImGui::PopStyleColor();
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // Mirror the bench status line into the host's MAIN status bar (full width —
    // the bench's own bar is too narrow for long file paths). Forward only on
    // change so it doesn't re-arm the host's status timeout every frame.
    if (status_ != lastForwardedStatus_) {
        lastForwardedStatus_ = status_;
        host_->onStatus(status_, statusOk_);
    }

    // ---- Teal status bar ----
    // The left half is intentionally empty: status text now lives in the app's
    // main status bar (see onStatus above). This bar carries only the clickable
    // Mode/profile switcher on the right.
    bool openModePopup = false;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kTeal);
    ImGui::BeginChild("##benchstatus", ImVec2(0, 24), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
    std::string right = act.isMarkdown      ? std::string("Markdown")
                      : act.targetIndex < 0 ? std::string("Document (no build)")
                                            : host_->modeLabel(act.targetIndex);
    if (right.empty() && act.targetIndex >= 0) right = targets[act.targetIndex].label;
    // A real build target makes the Mode label a one-click profile switcher (see
    // drawModeMenu). Markdown / unknown files have nothing to switch → static text.
    const bool modeSwitchable = !act.isMarkdown && act.targetIndex >= 0 && targets.size() > 1;
    if (modeSwitchable) {
        const std::string label = std::string(ICON_FA_CARET_UP " ") + right;
        const float lw = ImGui::CalcTextSize(label.c_str()).x;
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - lw - 8, 2));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kTealDark);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  kTealDark);
        if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(lw, 20))) openModePopup = true;
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch mode / profile");
    } else {
        const float rw = ImGui::CalcTextSize(right.c_str()).x;
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - rw - 8, 4));
        ImGui::TextUnformatted(right.c_str());
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (openModePopup) ImGui::OpenPopup("##benchmodepopup");
    drawModeMenu();

    // Follow a clicked markdown link last: openFile() mutates docs_/active_, so do
    // it after every reference to `act`/`doc` for this frame is done. A link jump
    // remembers the doc it left (for the Back arrow); Back reopens without pushing.
    if (!mdFollow.empty()) {
        if (!act.path.empty() && mdFollow != act.path) {
            mdNavBack_.push_back(act.path);
            if (mdNavBack_.size() > 128) mdNavBack_.erase(mdNavBack_.begin());
        }
        openFile(mdFollow);
    } else if (!mdBack.empty()) {
        openFile(mdBack);
    }

    ImGui::End();
}

} // namespace bench
