// Bench portable module — see CodeBench.h.
#include "CodeBench.h"

#include "IBenchHost.h"
#include "BenchLang.h"
#include "TextEditor.h"
#include "imgui.h"
#include "IconsFontAwesome6.h"

#include <cstdio>
#include <cstring>
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
    for (const auto& e : r.errors) em[e.first] = e.second;
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
        editor_->SetErrorMarkers({});
        filePath_[0] = '\0';
        status_ = "New sketch"; statusOk_ = true;
    };
    auto doOpen = [&]() {
        if (filePath_[0] == '\0') { status_ = "Open: enter a file path first"; statusOk_ = false; return; }
        std::ifstream in(filePath_, std::ios::binary);
        if (!in) { status_ = "Open failed: " + std::string(filePath_); statusOk_ = false; return; }
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        editor_->SetText(data);
        editor_->SetErrorMarkers({});
        status_ = "Opened " + std::string(filePath_) + " (" + std::to_string(data.size()) + " B)";
        statusOk_ = true;
    };
    auto doSave = [&]() {
        if (filePath_[0] == '\0') { status_ = "Save: enter a file path first"; statusOk_ = false; return; }
        std::ofstream out(filePath_, std::ios::binary);
        if (!out) { status_ = "Save failed: " + std::string(filePath_); statusOk_ = false; return; }
        const std::string text = editor_->GetText();
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        status_ = "Saved " + std::string(filePath_) + " (" + std::to_string(text.size()) + " B)";
        statusOk_ = true;
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
    if (circleBtn(ICON_FA_FILE,          "##benchnew",      "New sketch"))              doNew();
    ImGui::SameLine(0, 6);
    if (circleBtn(ICON_FA_FOLDER_OPEN,   "##benchopen",     "Open (path field below)")) doOpen();
    ImGui::SameLine(0, 6);
    if (circleBtn(ICON_FA_FLOPPY_DISK,   "##benchsave",     "Save (path field below)")) doSave();
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
                    editor_->SetErrorMarkers({});
                    filePath_[0] = '\0';
                    if (el.targetIndex >= 0) { targetIndex_ = el.targetIndex; applyTargetSyntax(); }
                }
                status_ = el.status; statusOk_ = el.ok;
            }
        }
        ImGui::EndPopup();
    }

    // ---- Target row ----
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Target:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("##benchtarget", targets[targetIndex_].label.c_str())) {
        for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
            const bool sel = (i == targetIndex_);
            if (ImGui::Selectable(targets[i].label.c_str(), sel)) applyTarget(i);
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (targets[targetIndex_].wantsAddr) {
        ImGui::SameLine(); ImGui::TextUnformatted("@ $"); ImGui::SameLine();
        ImGui::SetNextItemWidth(56);
        ImGui::InputText("##benchaddr", rawAddr_, sizeof(rawAddr_), ImGuiInputTextFlags_CharsHexadecimal);
    }
    {
        const std::string hint = host_->toolchainHint(targetIndex_);
        if (!hint.empty()) {
            ImGui::SameLine(0, 16);
            const bool ok = host_->toolchainReady(targetIndex_);
            ImGui::TextColored(ok ? ImVec4(0.4f, 0.85f, 0.4f, 1.0f) : ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                               "%s %s", ok ? ICON_FA_MICROCHIP : ICON_FA_HAMMER, hint.c_str());
        }
    }

    // ---- Sketch tab ----
    std::string tabName = "sketch";
    if (filePath_[0]) {
        std::string p(filePath_);
        size_t slash = p.find_last_of("/\\");
        tabName = (slash == std::string::npos) ? p : p.substr(slash + 1);
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

    // ---- Path field ----
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##benchpath", "path to Open / Save...", filePath_, sizeof(filePath_));

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
