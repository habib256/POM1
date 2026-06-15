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
    lastSavedText_ = editor_->GetText(); dirty_ = false;
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

    // Default tall enough that the toolbar, tab, editor, console and status bar
    // all fit without the editor being squeezed to its 100 px floor; clamp the
    // minimum so the user can't shrink it into a clipped, unusable stack.
    ImGui::SetNextWindowSize(ImVec2(660, 720), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(520, 480), ImVec2(FLT_MAX, FLT_MAX));
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
        lastSavedText_ = editor_->GetText(); dirty_ = false;
        status_ = "New sketch"; statusOk_ = true;
    };
    auto openFile = [&](const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) { status_ = "Open failed: " + path; statusOk_ = false; return; }
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        editor_->SetText(data);
        editor_->SetErrorMarkers({}); errorLines_.clear();
        loadedPath_ = path;
        lastSavedText_ = editor_->GetText(); dirty_ = false;
        status_ = "Opened " + path + " (" + std::to_string(data.size()) + " B)";
        statusOk_ = true;
    };
    auto saveFile = [&](const std::string& path) {
        std::ofstream out(path, std::ios::binary);
        if (!out) { status_ = "Save failed: " + path; statusOk_ = false; return; }
        const std::string text = editor_->GetText();
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        loadedPath_ = path;
        lastSavedText_ = text; dirty_ = false;
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
    // Labelled pill (icon + text) for the two primary actions, so newcomers can
    // tell "compile" from "run" without hovering for a tooltip.
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
    // Thin vertical divider between toolbar groups (build · CPU controls · file).
    auto sep = [&]() {
        ImGui::SameLine(0, 6);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = 11.0f;                       // total reserved width
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
    // CPU-control group, bracketed by dividers from the build pills on its left
    // and the file actions on its right. One play/stop TOGGLE reflecting the
    // live CPU state (▶ green to resume when halted, ■ red to halt when running)
    // plus single-step. The "Run" pill above builds + deploys; this toggle just
    // resumes/halts the already-loaded program.
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
            std::string s = host_->cpuStep();   // "Stepped - PC: 0x...."
            status_ = s.empty() ? "Stepped" : s; statusOk_ = true;
        }
    }
    sep();
    ImGui::SameLine(0, 6);
    if (circleBtn(ICON_FA_FILE,          "##benchnew",      "New sketch (pick language + target)")) doNewChoose();
    ImGui::SameLine(0, 6);
    if (circleBtn(ICON_FA_FOLDER_OPEN,   "##benchopen",     "Open file (browse dev/)")) browse(false);
    ImGui::SameLine(0, 6);
    if (circleBtn(ICON_FA_FLOPPY_DISK,   "##benchsave",     "Save file (browse)"))      browse(true);
    if (!host_->examples().empty()) {
        ImGui::SameLine(0, 6);
        if (circleBtn(ICON_FA_BOOK,      "##benchexamples", "Examples")) openExamplesPopup = true;
    }

    // Right-aligned cluster: the cc65 toolchain status is pinned to the far
    // right; the Serial Monitor (when present) sits just to its left. The
    // toolchain icon + ring turn red when the toolchain is incomplete for the
    // current target, so a missing cc65 / dev/ tree is visible at a glance
    // without opening the popup.
    const bool toolchainOk = host_->toolchainReady(targetIndex_);
    const ImU32 tcCol = toolchainOk ? IM_COL32_WHITE : IM_COL32(235, 80, 60, 255);
    const float rightX = ImGui::GetWindowWidth() - 42;   // 34px button + 8px margin
    if (host_->hasSerial()) {
        ImGui::SameLine(rightX - 40);                    // 34px button + 6px gap
        if (circleBtn(ICON_FA_MAGNIFYING_GLASS, "##benchserial", "Serial Monitor")) host_->openSerial();
    }
    ImGui::SameLine(rightX);
    if (circleBtn(ICON_FA_SCREWDRIVER_WRENCH, "##benchtoolchain",
                  toolchainOk ? "Toolchain status (cc65 / dev/)"
                              : "Toolchain incomplete — cc65 / dev/ not found (click for details)",
                  tcCol, tcCol))
        openToolchainPopup = true;
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
                    lastSavedText_ = editor_->GetText(); dirty_ = false;
                    if (el.targetIndex >= 0) { targetIndex_ = el.targetIndex; applyTargetSyntax(); }
                }
                status_ = el.status; statusOk_ = el.ok;
            }
        }
        ImGui::EndPopup();
    }

    // ---- Toolchain status popup (what the cc65 probe found) ----
    if (openToolchainPopup) ImGui::OpenPopup("##benchtoolchainpopup");
    if (ImGui::BeginPopup("##benchtoolchainpopup")) {
        const std::string rep = host_->toolchainReport();
        ImGui::TextUnformatted(rep.empty() ? "No toolchain info." : rep.c_str());
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
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Target:"); ImGui::SameLine(96);
        ImGui::SetNextItemWidth(300);
        if (ImGui::BeginCombo("##newmach", newMachine_ < (int)machs.size() ? machs[newMachine_].c_str() : "")) {
            for (int i = 0; i < (int)machs.size(); ++i)
                if (ImGui::Selectable(machs[i].c_str(), i == newMachine_)) newMachine_ = i;
            ImGui::EndCombo();
        }
        // Inline (non-floating) descriptions for the current selection.
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
            applyTarget(t);
            editor_->SetText(host_->starterSketch(t));
            editor_->SetErrorMarkers({}); errorLines_.clear();
            loadedPath_.clear();
            lastSavedText_ = editor_->GetText(); dirty_ = false;
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
    if (dirty_) tabName += " *";
    if (ImGui::BeginTabBar("##benchtabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem((tabName + "  ###benchtab").c_str())) ImGui::EndTabItem();
        ImGui::EndTabBar();
    }

    // ---- Editor (fills all vertical space down to the bottom status bar) ----
    // `avail` already excludes the tab bar above. Below the editor we stack, with
    // one ItemSpacing.y gap each: the optional console block, then the status bar.
    // Reserve exactly that so the status bar sits flush at the window bottom and
    // no vertical space is wasted.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float spacingY       = ImGui::GetStyle().ItemSpacing.y;
    const float rowH           = ImGui::GetFrameHeightWithSpacing();   // console header row
    const float kStatusBarH    = 24.0f;
    const float kConsoleChildH = 124.0f;
    const float consoleBlock   = showConsole_ ? (rowH + kConsoleChildH + spacingY) : 0.0f;
    float editorH = avail.y - consoleBlock - kStatusBarH - spacingY;
    if (editorH < 100.0f) editorH = 100.0f;
    editor_->Render("##benchsrc", ImVec2(avail.x, editorH), true);
    if (editor_->IsTextChanged()) dirty_ = (editor_->GetText() != lastSavedText_);

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
