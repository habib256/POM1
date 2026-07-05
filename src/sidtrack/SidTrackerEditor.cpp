// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "sidtrack/SidTrackerEditor.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "sidtrack/SidSongAsmExport.h"

namespace sidtrack {

static const char* kWaveNames[4] = {"Triangle", "Sawtooth", "Pulse", "Noise"};
static const uint8_t kWaveMasks[4] = {WAVE_TRI, WAVE_SAW, WAVE_PULSE, WAVE_NOISE};

static int waveToIndex(uint8_t w) {
    for (int i = 0; i < 4; ++i) if (kWaveMasks[i] == w) return i;
    return 0;
}

SidTrackerEditor::SidTrackerEditor(ISidHost* host) : host_(host) {
    std::strncpy(nameBuf_, "song", sizeof(nameBuf_) - 1);
    nameBuf_[sizeof(nameBuf_) - 1] = '\0';
    loadDemo();
}

void SidTrackerEditor::loadDemo() {
    model_.clear();
    model_.setName("song");
    model_.addRow({57, WAVE_TRI, 8});     // A4
    model_.addRow({60, WAVE_TRI, 8});     // C5
    model_.addRow({64, WAVE_PULSE, 8});   // E5
    model_.addRow({NOTE_OFF, 0, 4});
    std::strncpy(nameBuf_, "song", sizeof(nameBuf_) - 1);
    selected_ = 0;
}

void SidTrackerEditor::render() {
    renderToolbar();
    if (playing_) tickPlayback();
    ImGui::Separator();
    renderPatternGrid();
    ImGui::Separator();
    renderInstrument();
    ImGui::Separator();
    renderKeyboard();
    if (!status_.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(status_.c_str());
    }
}

void SidTrackerEditor::renderToolbar() {
    if (!playing_) {
        if (ImGui::Button("Play song") && !model_.empty()) {
            playing_ = true; playRow_ = 0; framesLeft_ = 0;
        }
    } else {
        if (ImGui::Button("Stop")) {
            playing_ = false;
            if (host_) host_->previewSilence();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Panic") && host_) host_->previewSilence();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button("Demo")) loadDemo();
    ImGui::SameLine();
    if (ImGui::Button("Export ASM...")) doExport();

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("Name", nameBuf_, sizeof(nameBuf_))) model_.setName(nameBuf_);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu rows", model_.size());
}

// Advance the live sequencer one video frame (render() calls us per frame).
void SidTrackerEditor::tickPlayback() {
    if (model_.empty()) { playing_ = false; return; }
    if (framesLeft_ > 0) { --framesLeft_; return; }
    if (playRow_ >= static_cast<int>(model_.size())) {   // reached the end -> stop
        playing_ = false;
        if (host_) host_->previewSilence();
        return;
    }
    const Row& r = model_.at(static_cast<std::size_t>(playRow_));
    if (host_) {
        if (r.note == NOTE_OFF)      host_->previewNoteOff(waveform_);
        else if (r.note == NOTE_TIE) { /* hold */ }
        else                          host_->previewNoteOn(r.note, r.ctrl, inst_);
    }
    framesLeft_ = r.frames;
    ++playRow_;
}

void SidTrackerEditor::renderPatternGrid() {
    ImGui::TextDisabled("Pattern");
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY;
    ImVec2 size(0.0f, 190.0f);
    if (ImGui::BeginTable("pattern", 5, flags, size)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Note");
        ImGui::TableSetupColumn("Wave");
        ImGui::TableSetupColumn("Frames");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < model_.size(); ++i) {
            Row r = model_.at(i);
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            if (playing_ && static_cast<int>(i) == playRow_ - 1)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(50, 80, 40, 255));

            ImGui::TableSetColumnIndex(0);
            char lbl[8]; std::snprintf(lbl, sizeof(lbl), "%zu", i);
            if (ImGui::Selectable(lbl, selected_ == static_cast<int>(i),
                                  ImGuiSelectableFlags_SpanAllColumns))
                selected_ = static_cast<int>(i);

            // Note
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            int note = r.note;
            bool special = (r.note == NOTE_OFF || r.note == NOTE_TIE);
            if (!special) {
                if (ImGui::SliderInt("##n", &note, 0, 95, noteName(static_cast<uint8_t>(note)).c_str())) {
                    r.note = static_cast<uint8_t>(note); model_.setRow(i, r);
                }
            } else {
                if (ImGui::SmallButton(r.note == NOTE_OFF ? "--- (off)" : "=== (tie)")) {
                    r.note = 57; model_.setRow(i, r);   // click a special back to a note
                }
            }

            // Wave
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1);
            int wi = waveToIndex(r.ctrl);
            if (ImGui::Combo("##w", &wi, kWaveNames, 4)) { r.ctrl = kWaveMasks[wi]; model_.setRow(i, r); }

            // Frames
            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-1);
            int fr = r.frames;
            if (ImGui::SliderInt("##f", &fr, 1, 64)) {
                r.frames = static_cast<uint8_t>(std::max(1, fr)); model_.setRow(i, r);
            }

            // row ops
            ImGui::TableSetColumnIndex(4);
            if (ImGui::SmallButton("Off")) { r.note = NOTE_OFF; model_.setRow(i, r); }
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))   { model_.removeRow(i); ImGui::PopID(); break; }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("+ Add row")) {
        Row r = (selected_ >= 0 && selected_ < static_cast<int>(model_.size()))
                    ? model_.at(static_cast<std::size_t>(selected_))
                    : Row{57, waveform_, 8};
        std::size_t at = (selected_ < 0) ? model_.size()
                                         : static_cast<std::size_t>(selected_) + 1;
        model_.insertRow(at, r);
        selected_ = static_cast<int>(at);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add OFF")) { model_.addRow({NOTE_OFF, 0, 4}); }
    ImGui::SameLine();
    if (ImGui::Button("Clear rows")) { model_.clear(); selected_ = -1; }
}

void SidTrackerEditor::renderInstrument() {
    ImGui::TextDisabled("Instrument");
    int wi = waveToIndex(waveform_);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::Combo("Waveform", &wi, kWaveNames, 4)) waveform_ = kWaveMasks[wi];

    int a = (inst_.ad >> 4) & 0xF, d = inst_.ad & 0xF;
    int s = (inst_.sr >> 4) & 0xF, r = inst_.sr & 0xF;
    bool ch = false;
    ch |= ImGui::SliderInt("Attack",  &a, 0, 15);
    ch |= ImGui::SliderInt("Decay",   &d, 0, 15);
    ch |= ImGui::SliderInt("Sustain", &s, 0, 15);
    ch |= ImGui::SliderInt("Release", &r, 0, 15);
    if (ch) {
        inst_.ad = static_cast<uint8_t>((a << 4) | d);
        inst_.sr = static_cast<uint8_t>((s << 4) | r);
    }
    int vol = inst_.volume & 0xF;
    if (ImGui::SliderInt("Volume", &vol, 0, 15)) inst_.volume = static_cast<uint8_t>(vol);
    int pw = inst_.pw & 0x0FFF;
    if (ImGui::SliderInt("Pulse width", &pw, 0, 4095)) inst_.pw = static_cast<uint16_t>(pw);
}

// One-octave preview keyboard. Hold a key -> note on; release -> note off.
void SidTrackerEditor::renderKeyboard() {
    ImGui::TextDisabled("Preview keyboard");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("Octave", &octave_, 0, 7);
    static const char* kSemis[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    for (int i = 0; i < 12; ++i) {
        if (i) ImGui::SameLine();
        int note = std::clamp(octave_ * 12 + i, 0, 95);
        ImGui::PushID(1000 + i);
        ImGui::Button(kSemis[i], ImVec2(34, 34));
        if (ImGui::IsItemActivated() && host_) {
            heldNote_ = note; host_->previewNoteOn(static_cast<uint8_t>(note), waveform_, inst_);
        }
        if (ImGui::IsItemDeactivated() && host_ && heldNote_ == note) {
            heldNote_ = -1; host_->previewNoteOff(waveform_);
        }
        ImGui::PopID();
    }
}

void SidTrackerEditor::doExport() {
    if (!host_) return;
    std::string path;
    if (!host_->pickFilePath(/*forSave=*/true, "Export SID song (.inc)",
                             "ca65 include", "inc,asm", "", std::string(nameBuf_) + ".inc",
                             path)) {
        status_ = "Export cancelled (or no native picker).";
        return;
    }
    const std::string text = formatSongAsm(model_);
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
        status_ = "Exported " + path;
    } else {
        status_ = "Could not write " + path;
    }
}

}  // namespace sidtrack
