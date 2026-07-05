// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "sfxbeep/SfxEditor.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "sfxbeep/SfxAsmExport.h"
#include "sfxbeep/SfxBank.h"

namespace sfxbeep {

SfxEditor::SfxEditor(ISfxHost* host) : host_(host) {
    std::strncpy(nameBuf_, "sfx", sizeof(nameBuf_) - 1);
    nameBuf_[sizeof(nameBuf_) - 1] = '\0';
    bank_ = parseSfxAsm(kSfxBank50);   // the built-in 50-cue catalogue
    if (!bank_.empty()) loadFromBank(0);
}

// Copy one built-in bank cue into the working model.
void SfxEditor::loadFromBank(int index) {
    if (index < 0 || index >= static_cast<int>(bank_.size())) return;
    const ParsedSfx& b = bank_[index];
    model_.clear();
    model_.setName(b.name);
    for (const Step& s : b.steps) model_.addStep(s);
    bankSel_ = index;
    selected_ = model_.empty() ? -1 : 0;
    std::strncpy(nameBuf_, b.name.c_str(), sizeof(nameBuf_) - 1);
    nameBuf_[sizeof(nameBuf_) - 1] = '\0';
}

void SfxEditor::render() {
    renderToolbar();
    ImGui::Separator();
    ImGui::Columns(2, "sfxcols", false);
    ImGui::SetColumnWidth(0, 150.0f);
    renderBank();                 // left: the 50-cue browser
    ImGui::NextColumn();
    renderCurve();                // right: editable curve + step editor
    ImGui::Separator();
    renderStepEditor();
    ImGui::Columns(1);
    if (!status_.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(status_.c_str());
    }
}

void SfxEditor::renderToolbar() {
    if (ImGui::Button("Play") && host_) host_->previewSfx(model_.steps());
    ImGui::SameLine();
    if (ImGui::Button("Stop") && host_) host_->stopPreview();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button("Export ASM...")) doExport();

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("Name", nameBuf_, sizeof(nameBuf_)))
        model_.setName(nameBuf_);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu steps, %u half-cycles", model_.size(),
                        model_.totalHalfCycles());
}

// Left panel: the built-in 50-cue bank. Click a name to load it; it also
// auditions immediately so browsing the bank is audible.
void SfxEditor::renderBank() {
    ImGui::TextDisabled("Bank (%zu)", bank_.size());
    if (ImGui::BeginListBox("##bank", ImVec2(-1.0f, 240.0f))) {
        for (int i = 0; i < static_cast<int>(bank_.size()); ++i) {
            const bool sel = (i == bankSel_);
            if (ImGui::Selectable(bank_[i].name.c_str(), sel)) {
                loadFromBank(i);
                if (host_) host_->previewSfx(model_.steps());   // audition on click
            }
        }
        ImGui::EndListBox();
    }
}

// Bars: one per step. Height encodes PITCH (a rest = flat baseline), width
// encodes duration. Click selects; vertical drag on a bar changes its pitch.
void SfxEditor::renderCurve() {
    const float h = 120.0f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float avail = std::max(120.0f, ImGui::GetContentRegionAvail().x);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // background
    dl->AddRectFilled(origin, ImVec2(origin.x + avail, origin.y + h),
                      IM_COL32(20, 20, 26, 255));

    const std::size_t n = model_.size();
    if (n == 0) {
        ImGui::Dummy(ImVec2(avail, h));
        ImGui::TextDisabled("(empty — add a step below)");
        return;
    }

    // Lay bars out proportional to their length (duration).
    unsigned total = std::max(1u, model_.totalHalfCycles());
    float x = origin.x;
    for (std::size_t i = 0; i < n; ++i) {
        const Step& s = model_.at(i);
        float w = avail * (static_cast<float>(s.length) / static_cast<float>(total));
        w = std::max(6.0f, w);
        // pitch → bar height: lower period = higher pitch = taller. period 0 = rest.
        float pitch01 = (s.period == 0) ? 0.0f : (1.0f - s.period / 255.0f);
        float barH = 4.0f + pitch01 * (h - 8.0f);
        ImVec2 p0(x, origin.y + h - barH);
        ImVec2 p1(x + w - 1.0f, origin.y + h);

        ImU32 col = (static_cast<int>(i) == selected_) ? IM_COL32(120, 200, 255, 255)
                    : (s.period == 0)                  ? IM_COL32(70, 70, 80, 255)
                                                       : IM_COL32(90, 160, 90, 255);
        dl->AddRectFilled(p0, p1, col);

        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(static_cast<int>(i));
        ImGui::InvisibleButton("bar", ImVec2(w - 1.0f, barH < 8.0f ? 8.0f : barH));
        if (ImGui::IsItemActivated()) selected_ = static_cast<int>(i);
        if (ImGui::IsItemActive() && s.period != 0) {
            float dy = ImGui::GetIO().MouseDelta.y;
            if (dy != 0.0f) {
                // drag up = higher pitch = smaller period
                int np = std::clamp(static_cast<int>(s.period) + static_cast<int>(dy), 1, 255);
                Step ns = s; ns.period = static_cast<uint8_t>(np);
                model_.setStep(i, ns);
            }
        }
        ImGui::PopID();
        x += w;
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + h + 2.0f));
    ImGui::Dummy(ImVec2(avail, 1.0f));
}

void SfxEditor::renderStepEditor() {
    if (ImGui::Button("+ Add step")) {
        Step s = (selected_ >= 0) ? model_.at(static_cast<std::size_t>(selected_))
                                  : Step{0x40, 0x10};
        model_.insertStep(selected_ < 0 ? model_.size()
                                        : static_cast<std::size_t>(selected_) + 1, s);
        if (selected_ < 0) selected_ = static_cast<int>(model_.size()) - 1;
        else ++selected_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && selected_ >= 0 &&
        selected_ < static_cast<int>(model_.size())) {
        model_.removeStep(static_cast<std::size_t>(selected_));
        selected_ = std::min(selected_, static_cast<int>(model_.size()) - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { model_.clear(); selected_ = -1; }

    if (selected_ < 0 || selected_ >= static_cast<int>(model_.size())) {
        ImGui::TextDisabled("Select a bar to edit its pitch / duration.");
        return;
    }
    Step s = model_.at(static_cast<std::size_t>(selected_));
    int period = s.period, length = s.length;
    bool changed = false;
    ImGui::Text("Step %d", selected_);
    changed |= ImGui::SliderInt("Pitch (period, 0 = rest)", &period, 0, 255);
    changed |= ImGui::SliderInt("Duration (half-cycles)", &length, 1, 255);
    if (changed) {
        s.period = static_cast<uint8_t>(period);
        s.length = static_cast<uint8_t>(std::max(1, length));
        model_.setStep(static_cast<std::size_t>(selected_), s);
    }
}

void SfxEditor::doExport() {
    if (!host_) return;
    std::string path;
    if (!host_->pickFilePath(/*forSave=*/true, "Export beeper SFX (.inc)",
                             "ca65 include", "inc,asm", "", std::string(nameBuf_) + ".inc",
                             path)) {
        status_ = "Export cancelled (or no native picker).";
        return;
    }
    const std::string text = formatSfxAsm(model_);
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
        status_ = "Exported " + path;
    } else {
        status_ = "Could not write " + path;
    }
}

}  // namespace sfxbeep
