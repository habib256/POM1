// Bench portable module — see CodeBench.h.
#include "CodeBench.h"

#include "IBenchHost.h"
#include "BenchLang.h"
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

void CodeBench::ensureEditor()
{
    if (inited_) return;
    inited_ = true;
    targetIndex_ = host_->defaultTargetIndex();
    editor_ = std::make_unique<TextEditor>();
    editor_->SetPalette(TextEditor::GetLightPalette());
    editor_->SetShowWhitespaces(false);
    applyTargetSyntax();
    editor_->SetText(host_->starterSketch(targetIndex_));
}

void CodeBench::applyTargetSyntax()
{
    const auto& targets = host_->targets();
    if (targetIndex_ < 0 || targetIndex_ >= static_cast<int>(targets.size())) return;
    const std::string& lang = targets[targetIndex_].language;
    if (lang != lastLanguage_) {
        editor_->SetLanguageDefinition(langDef(lang));
        lastLanguage_ = lang;
    }
}

void CodeBench::applyResult(const BuildResult& r)
{
    console_ = r.console;
    if (r.showConsole) showConsole_ = true;
    status_  = r.status;
    statusOk_ = r.ok;
    TextEditor::ErrorMarkers em;
    errorLines_.clear();
    for (const auto& e : r.errors) { em[e.first] = e.second; errorLines_.push_back(e.first); }
    editor_->SetErrorMarkers(em);
}

void CodeBench::render(const char* title, bool* open)
{
    ensureEditor();

    const auto& targets = host_->targets();
    if (targetIndex_ < 0 || targetIndex_ >= static_cast<int>(targets.size())) targetIndex_ = 0;

    // Arduino IDE palette (teal toolbar/status, dark console).
    const ImVec4 kTeal     = ImVec4(0.000f, 0.592f, 0.616f, 1.0f);
    const ImVec4 kTealDark = ImVec4(0.000f, 0.353f, 0.369f, 1.0f);
    const ImVec4 kWhite    = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    const ImVec4 kConsoleBg= ImVec4(0.10f, 0.10f, 0.11f, 1.0f);

    ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

    // ---- Actions ----
    auto applyTarget = [&](int i) {
        if (i < 0 || i >= static_cast<int>(targets.size())) return;
        targetIndex_ = i;
        host_->onTargetSelected(i);
        applyTargetSyntax();
    };
    auto doNew = [&]() {
        editor_->SetText(host_->starterSketch(targetIndex_));
        editor_->SetErrorMarkers({}); errorLines_.clear();
        loadedPath_.clear();
        status_ = "New sketch"; statusOk_ = true;
    };
    auto openFile = [&](const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) { status_ = "Open failed: " + path; statusOk_ = false; return; }
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        editor_->SetText(data);
        editor_->SetErrorMarkers({}); errorLines_.clear();
        loadedPath_ = path;
        status_ = "Opened " + path + " (" + std::to_string(data.size()) + " B)";
        statusOk_ = true;
    };
    auto saveFile = [&](const std::string& path) {
        std::ofstream out(path, std::ios::binary);
        if (!out) { status_ = "Save failed: " + path; statusOk_ = false; return; }
        const std::string text = editor_->GetText();
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        loadedPath_ = path;
        status_ = "Saved " + path + " (" + std::to_string(text.size()) + " B)";
        statusOk_ = true;
    };
    // New: pick a (language x machine) target, then load its HELLO WORLD. If the
    // host offers no axes, New just reloads the current target's starter.
    bool openNew = false;
    auto doNewChoose = [&]() {
        if (host_->languages().empty() || host_->machines().empty()) doNew();
        else openNew = true;
    };
    // Open the file browser popup (deferred OpenPopup after the toolbar child).
    bool openBrowse = false;
    auto browse = [&](bool save) {
        if (browseDir_.empty()) browseDir_ = host_->browseDir();
        browseSave_ = save;
        openBrowse = true;
    };
    auto doVerify = [&]() { applyResult(host_->verify(targetIndex_, editor_->GetText(), rawAddr_)); };
    auto doUpload = [&]() { applyResult(host_->upload(targetIndex_, editor_->GetText(), rawAddr_)); };

    // ---- Teal toolbar with circular icon buttons ----
    bool openExamplesPopup = false;
    auto circleBtn = [&](const char* icon, const char* id, const char* tip,
                         ImU32 iconCol = IM_COL32_WHITE) -> bool {
        const float d = 34.0f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(d, d));
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
        if (hov) dl->AddCircleFilled(c, d * 0.5f, ImGui::GetColorU32(kTealDark), 32);
        const ImVec2 ts = ImGui::CalcTextSize(icon);
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), iconCol, icon);
        dl->AddCircle(c, d * 0.5f - 1.0f, IM_COL32_WHITE, 32, hov ? 2.5f : 1.5f);
        if (hov && tip) ImGui::SetTooltip("%s", tip);
        return clicked;
    };

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kTeal);
    ImGui::BeginChild("##benchtoolbar", ImVec2(0, 44), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos(ImVec2(8, 5));
    if (circleBtn(ICON_FA_CHECK,         "##benchverify",   "Verify (compile)"))        doVerify();
    ImGui::SameLine(0, 6);
    if (circleBtn(ICON_FA_ARROW_RIGHT,   "##benchupload",   "Upload (build + run)"))    doUpload();
    if (host_->hasStop()) {
        ImGui::SameLine(0, 6);
        if (circleBtn(ICON_FA_STOP, "##benchstop", "Stop (halt the CPU)", IM_COL32(235, 80, 60, 255))) {
            host_->stop();
            status_ = "Stopped"; statusOk_ = true;
        }
    }
    ImGui::SameLine(0, 18);
    if (circleBtn(ICON_FA_FILE,          "##benchnew",      "New sketch (pick language + target)")) doNewChoose();
    ImGui::SameLine(0, 6);
    if (circleBtn(ICON_FA_FOLDER_OPEN,   "##benchopen",     "Open file (browse dev/)")) browse(false);
    ImGui::SameLine(0, 6);
    if (circleBtn(ICON_FA_FLOPPY_DISK,   "##benchsave",     "Save file (browse)"))      browse(true);
    if (!host_->examples().empty()) {
        ImGui::SameLine(0, 6);
        if (circleBtn(ICON_FA_BOOK,      "##benchexamples", "Examples")) openExamplesPopup = true;
    }
    if (host_->hasSerial()) {
        ImGui::SameLine(ImGui::GetWindowWidth() - 42);
        if (circleBtn(ICON_FA_MAGNIFYING_GLASS, "##benchserial", "Serial Monitor")) host_->openSerial();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // ---- Examples popup ----
    if (openExamplesPopup) ImGui::OpenPopup("##benchexamplespopup");
    if (ImGui::BeginPopup("##benchexamplespopup")) {
        ImGui::TextDisabled("Examples"); ImGui::Separator();
        const auto& ex = host_->examples();
        for (int i = 0; i < static_cast<int>(ex.size()); ++i) {
            if (ImGui::Selectable(ex[i].label.c_str())) {
                ExampleLoad el = host_->loadExample(i);
                if (el.ok) {
                    editor_->SetText(el.source);
                    editor_->SetErrorMarkers({}); errorLines_.clear();
                    loadedPath_.clear();
                    if (el.targetIndex >= 0) { targetIndex_ = el.targetIndex; applyTargetSyntax(); }
                }
                status_ = el.status; statusOk_ = el.ok;
            }
        }
        ImGui::EndPopup();
    }

    // ---- File browser popup (Open / Save), rooted at the host's browseDir() ----
    if (openBrowse) ImGui::OpenPopup("##benchbrowse");
    if (ImGui::BeginPopup("##benchbrowse")) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path cur(browseDir_);
        ImGui::TextUnformatted(browseSave_ ? ICON_FA_FLOPPY_DISK " Save to" : ICON_FA_FOLDER_OPEN " Open");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", cur.string().c_str());
        ImGui::Separator();
        ImGui::BeginChild("##browselist", ImVec2(480, 300), true);
        if (cur.has_parent_path() && cur.parent_path() != cur) {
            if (ImGui::Selectable(ICON_FA_FOLDER_OPEN " .."))
                browseDir_ = cur.parent_path().string();
        }
        std::vector<std::string> dirs, files;
        for (const auto& e : fs::directory_iterator(cur, ec)) {
            if (e.is_directory(ec)) dirs.push_back(e.path().filename().string());
            else                    files.push_back(e.path().filename().string());
        }
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());
        for (const auto& d : dirs) {
            const std::string label = ICON_FA_FOLDER_OPEN " " + d;
            if (ImGui::Selectable(label.c_str())) browseDir_ = (cur / d).string();
        }
        for (const auto& f : files) {
            const std::string label = ICON_FA_FILE " " + f;
            if (ImGui::Selectable(label.c_str())) {
                if (browseSave_) {
                    std::snprintf(saveName_, sizeof(saveName_), "%s", f.c_str());
                } else {
                    openFile((cur / f).string());
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
                saveFile((cur / saveName_).string());
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- New-sketch dialog: pick language + machine, load its HELLO WORLD ----
    if (openNew) ImGui::OpenPopup("##benchnewdlg");
    if (ImGui::BeginPopup("##benchnewdlg")) {
        const auto& langs = host_->languages();
        const auto& machs = host_->machines();
        ImGui::TextDisabled("New sketch"); ImGui::Separator();
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Language:"); ImGui::SameLine(96);
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##newlang", newLang_ < (int)langs.size() ? langs[newLang_].c_str() : "")) {
            for (int i = 0; i < (int)langs.size(); ++i)
                if (ImGui::Selectable(langs[i].c_str(), i == newLang_)) newLang_ = i;
            ImGui::EndCombo();
        }
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Target:"); ImGui::SameLine(96);
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##newmach", newMachine_ < (int)machs.size() ? machs[newMachine_].c_str() : "")) {
            for (int i = 0; i < (int)machs.size(); ++i)
                if (ImGui::Selectable(machs[i].c_str(), i == newMachine_)) newMachine_ = i;
            ImGui::EndCombo();
        }
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
            applyTarget(t);
            editor_->SetText(host_->starterSketch(t));
            editor_->SetErrorMarkers({}); errorLines_.clear();
            loadedPath_.clear();
            status_ = "New: " + host_->targets()[t].label; statusOk_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- Sketch tab ----
    std::string tabName = "sketch";
    if (!loadedPath_.empty()) {
        size_t slash = loadedPath_.find_last_of("/\\");
        tabName = (slash == std::string::npos) ? loadedPath_ : loadedPath_.substr(slash + 1);
    }
    if (ImGui::BeginTabBar("##benchtabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem((tabName + "  ").c_str())) ImGui::EndTabItem();
        ImGui::EndTabBar();
    }

    // ---- Editor (reserve room so the status bar is never clipped) ----
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float spacingY       = ImGui::GetStyle().ItemSpacing.y;
    const float rowH           = ImGui::GetFrameHeightWithSpacing();
    const float kStatusBarH    = 24.0f;
    const float kConsoleChildH = 124.0f;
    const float consoleBlock   = showConsole_ ? (rowH + kConsoleChildH + spacingY) : 0.0f;
    float editorH = avail.y - consoleBlock - rowH - (kStatusBarH + spacingY) - spacingY;
    if (editorH < 100.0f) editorH = 100.0f;
    editor_->Render("##benchsrc", ImVec2(avail.x, editorH), true);

    // Mirror error lines onto the editor's scrollbar lane (VS Code-style overview
    // ruler): a red tick at each error's proportional vertical position.
    if (!errorLines_.empty()) {
        const ImVec2 mn = ImGui::GetItemRectMin();   // editor child rect
        const ImVec2 mx = ImGui::GetItemRectMax();
        const int total = editor_->GetTotalLines();
        if (total > 0) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float sb = ImGui::GetStyle().ScrollbarSize;
            const float x0 = mx.x - sb + 1.0f;
            const float x1 = mx.x - 1.0f;
            const float trackH = mx.y - mn.y - 2.0f;
            const ImU32 col = IM_COL32(235, 80, 60, 230);
            for (int ln : errorLines_) {
                float t = static_cast<float>(ln - 1) / static_cast<float>(total);
                if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
                const float y = mn.y + 1.0f + t * trackH;
                dl->AddRectFilled(ImVec2(x0, y - 1.5f), ImVec2(x1, y + 1.5f), col, 1.0f);
            }
        }
    }

    // ---- Build output console (error lines orange + click → editor line) ----
    if (showConsole_) {
        ImGui::TextUnformatted("Build output");
        ImGui::SameLine();
        if (ImGui::SmallButton("Hide"))      showConsole_ = false;
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##bc")) console_.clear();
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
                    editor_->SetCursorPosition(TextEditor::Coordinates(jumpLine - 1, 0));
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

    // ---- Teal status bar (selected target on the right) ----
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kTeal);
    ImGui::BeginChild("##benchstatus", ImVec2(0, 24), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
    ImGui::SetCursorPos(ImVec2(8, 4));
    ImGui::TextUnformatted(status_.empty() ? "Ready" : status_.c_str());
    const std::string right = targets[targetIndex_].label + " on host";
    const float rw = ImGui::CalcTextSize(right.c_str()).x;
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - rw - 8, 4));
    ImGui::TextUnformatted(right.c_str());
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::End();
}

} // namespace bench
