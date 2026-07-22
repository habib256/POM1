// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "sidtrack/SidTrackerEditor.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "sidtrack/SidSongAsmExport.h"
#include "sidtrack/SidSongBank.h"

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
    bank_ = parseSongAsm(kSidSongBank50);   // the built-in 50-tune catalogue
    if (!bank_.empty()) loadFromBank(0);    // start on the Nokia tune
    else loadDemo();
}

// Load one built-in bank tune into the working model. Waveform is taken from the
// tune's first real note so the piano/instrument default matches.
void SidTrackerEditor::loadFromBank(int index) {
    if (index < 0 || index >= static_cast<int>(bank_.size())) return;
    const ParsedSong& b = bank_[index];
    model_.clear();
    model_.setName(b.name);
    bool haveWave = false;
    for (const Row& r : b.rows) {
        model_.addRow(r);
        if (r.ctrl && !haveWave) { waveform_ = r.ctrl; haveWave = true; }  // first real note
    }
    bankSel_ = index;
    playing_ = false;
    playingNote_ = -1;
    selected_ = model_.empty() ? -1 : 0;
    std::strncpy(nameBuf_, b.name.c_str(), sizeof(nameBuf_) - 1);
    nameBuf_[sizeof(nameBuf_) - 1] = '\0';
}

void SidTrackerEditor::loadDemo() {
    // The Nokia Tune (Tárrega's "Gran Vals", bars 13-16) — the most recognisable
    // phone ringtone ever. Sixteenths = 5 frames, eighths = 10, final = 22.
    model_.clear();
    model_.setName("nokia");
    waveform_ = WAVE_PULSE;
    const uint8_t W = WAVE_PULSE;
    model_.addRow({64, W,  5});   // E5
    model_.addRow({62, W,  5});   // D5
    model_.addRow({54, W, 10});   // F#4
    model_.addRow({56, W, 10});   // G#4
    model_.addRow({61, W,  5});   // C#5
    model_.addRow({59, W,  5});   // B4
    model_.addRow({50, W, 10});   // D4
    model_.addRow({52, W, 10});   // E4
    model_.addRow({59, W,  5});   // B4
    model_.addRow({57, W,  5});   // A4
    model_.addRow({49, W, 10});   // C#4
    model_.addRow({52, W, 10});   // E4
    model_.addRow({57, W, 22});   // A4 (long)
    model_.addRow({NOTE_OFF, 0, 6});
    std::strncpy(nameBuf_, "nokia", sizeof(nameBuf_) - 1);
    nameBuf_[sizeof(nameBuf_) - 1] = '\0';
    selected_ = 0;
}

void SidTrackerEditor::render() {
    renderToolbar();
    if (playing_) tickPlayback();
    ImGui::Separator();

    // Top: pattern grid (left) + instrument/ADSR (right).
    ImGui::BeginChild("sid_top", ImVec2(0, 250), false);
    ImGui::Columns(2, "sid_cols", true);
    if (ImGui::GetColumnWidth(0) < 100.0f) ImGui::SetColumnWidth(0, 340.0f);
    renderPatternGrid();
    ImGui::NextColumn();
    renderInstrument();
    ImGui::Columns(1);
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 16.0f));   // breathing room above the piano
    renderKeyboard();     // full-width piano

    if (!status_.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(status_.c_str());
    }
}

void SidTrackerEditor::renderToolbar() {
    if (!playing_) {
        if (ImGui::Button("Play song") && !model_.empty()) {
            demoMode_ = false;
            playing_ = true; playRow_ = 0; framesLeft_ = 0;
        }
    } else {
        // Same button stops whatever is playing (single tune or the demo).
        if (ImGui::Button(demoMode_ ? "Stop demo" : "Stop")) {
            playing_ = false;
            demoMode_ = false;
            playingNote_ = -1;
            if (host_) host_->previewSilence();
        }
    }
    ImGui::SameLine();
    // Panic must actually stop the sequencer, not just mute one frame — otherwise
    // the next row re-triggers the note. Drops every held key too.
    if (ImGui::Button("Panic")) panic();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Bank browser: pick one of the 50 built-in tunes (loads + can auto-play).
    ImGui::SetNextItemWidth(170.0f);
    const char* cur = (bankSel_ >= 0 && bankSel_ < static_cast<int>(bank_.size()))
                          ? bank_[bankSel_].name.c_str() : "(bank)";
    if (ImGui::BeginCombo("##bank", cur)) {
        for (int i = 0; i < static_cast<int>(bank_.size()); ++i) {
            if (ImGui::Selectable(bank_[i].name.c_str(), i == bankSel_)) {
                loadFromBank(i);
                demoMode_ = false;
                playing_ = true; playRow_ = 0; framesLeft_ = 0;   // audition on pick
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    // Demo: jukebox the whole bank end to end (auto-advance + wrap). Highlighted
    // while running; click again to stop.
    if (demoMode_) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 110, 60, 255));
        if (ImGui::Button("Demo \xE2\x96\xA0")) {   // "Demo ■"
            playing_ = false;
            demoMode_ = false;
            playingNote_ = -1;
            if (host_) host_->previewSilence();
        }
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("Demo") && !bank_.empty()) startDemo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Bank (%zu)", bank_.size());
    ImGui::SameLine();
    if (ImGui::Button("Export ASM...")) doExport();

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("Name", nameBuf_, sizeof(nameBuf_))) model_.setName(nameBuf_);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu rows", model_.size());
}

// Start jukebox demo mode: play the current bank tune, then auto-advance through
// the rest of the catalogue (wrapping) until the user stops.
void SidTrackerEditor::startDemo() {
    if (bank_.empty()) return;
    int start = (bankSel_ >= 0 && bankSel_ < static_cast<int>(bank_.size())) ? bankSel_ : 0;
    loadFromBank(start);
    demoMode_ = true;
    demoGapLeft_ = 0;
    playing_ = true; playRow_ = 0; framesLeft_ = 0;
}

// Silent pause (video frames ≈ 1/60 s) between demo tunes so they don't run
// together as one blur.
static const int kDemoGapFrames = 48;

// Hard stop everything: silence the chip, halt the sequencer + demo, and drop
// every note held by the piano / PC keyboard so nothing lingers or re-triggers.
void SidTrackerEditor::panic() {
    playing_ = false;
    demoMode_ = false;
    demoGapLeft_ = 0;
    playingNote_ = -1;
    heldNote_ = -1;
    kbdNote_ = -1;
    pianoDown_ = false;
    if (host_) host_->previewSilence();
}

// Advance the live sequencer one video frame (render() calls us per frame).
void SidTrackerEditor::tickPlayback() {
    if (model_.empty()) { playing_ = false; demoMode_ = false; return; }
    if (demoGapLeft_ > 0) {                              // silent pause between demo tunes
        if (--demoGapLeft_ == 0) {                       // gap elapsed -> load & play next
            int next = (bankSel_ + 1) % static_cast<int>(bank_.size());
            loadFromBank(next);
            demoMode_ = true;                            // loadFromBank cleared playing_
            playing_ = true; playRow_ = 0; framesLeft_ = 0;
        }
        return;
    }
    if (framesLeft_ > 0) { --framesLeft_; return; }
    if (playRow_ >= static_cast<int>(model_.size())) {   // reached the end of the tune
        if (host_) host_->previewSilence();
        playingNote_ = -1;
        if (demoMode_ && !bank_.empty()) {               // jukebox: pause, then next tune
            demoGapLeft_ = kDemoGapFrames;
            return;
        }
        playing_ = false;                                // single tune -> stop
        return;
    }
    const Row& r = model_.at(static_cast<std::size_t>(playRow_));
    if (host_) {
        if (r.note == NOTE_OFF)      { host_->previewNoteOff(playingCtrl_); playingNote_ = -1; }
        else if (r.note == NOTE_TIE) { /* hold */ }
        else                          { host_->previewNoteOn(r.note, r.ctrl, inst_); playingNote_ = r.note; playingCtrl_ = r.ctrl; }
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
            if (ImGui::SmallButton("X"))   {
                model_.removeRow(i);
                if (selected_ == static_cast<int>(i))      selected_ = -1;   // deleted the selected row
                else if (selected_ > static_cast<int>(i))  --selected_;      // rows shifted up
                ImGui::PopID(); break;
            }

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
        if (at > model_.size()) at = model_.size();   // clamp a stale selection (matches insertRow)
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
    renderAdsrGraph();
}

// A little ADSR envelope preview: attack ramp up, decay to sustain, sustain
// hold, release ramp down — proportioned by the current nibbles.
void SidTrackerEditor::renderAdsrGraph() {
    const float a = (inst_.ad >> 4) & 0xF, d = inst_.ad & 0xF;
    const float s = (inst_.sr >> 4) & 0xF, r = inst_.sr & 0xF;
    ImVec2 org = ImGui::GetCursorScreenPos();
    float w = std::max(140.0f, ImGui::GetContentRegionAvail().x);
    float h = 60.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(org, ImVec2(org.x + w, org.y + h), IM_COL32(18, 18, 24, 255));

    // widths proportional to A/D/(fixed sustain)/R; +1 so a zero stage still shows.
    float wa = a + 1, wd = d + 1, wr = r + 1, wsus = 6.0f;
    float sum = wa + wd + wsus + wr;
    float xa = w * wa / sum, xd = w * wd / sum, xs = w * wsus / sum, xr = w * wr / sum;
    float sy = org.y + h - (s / 15.0f) * (h - 4.0f) - 2.0f;   // sustain level
    ImVec2 p0(org.x, org.y + h - 2.0f);                       // start (silent)
    ImVec2 pPeak(p0.x + xa, org.y + 2.0f);                    // attack peak
    ImVec2 pSus(pPeak.x + xd, sy);                            // decay -> sustain
    ImVec2 pHold(pSus.x + xs, sy);                            // sustain hold
    ImVec2 pEnd(pHold.x + xr, org.y + h - 2.0f);              // release -> silent
    const ImU32 col = IM_COL32(120, 220, 140, 255);
    dl->AddLine(p0, pPeak, col, 2.0f);
    dl->AddLine(pPeak, pSus, col, 2.0f);
    dl->AddLine(pSus, pHold, col, 2.0f);
    dl->AddLine(pHold, pEnd, col, 2.0f);
    ImGui::Dummy(ImVec2(w, h + 2.0f));
    ImGui::TextDisabled("ADSR envelope");
}

// Note-on for a piano press. Handles glissando (note-off the previous key first)
// and the optional "write to row" mode (enter the note into the selected row).
void SidTrackerEditor::pressPiano(int note) {
    if (note < 0 || note == heldNote_) return;
    if (heldNote_ >= 0 && host_) host_->previewNoteOff(waveform_);
    heldNote_ = note;
    if (host_) host_->previewNoteOn(static_cast<uint8_t>(note), waveform_, inst_);
    if (writeToRow_ && selected_ >= 0 && selected_ < static_cast<int>(model_.size())) {
        Row r = model_.at(static_cast<std::size_t>(selected_));
        r.note = static_cast<uint8_t>(note);
        r.ctrl = waveform_;
        model_.setRow(static_cast<std::size_t>(selected_), r);
        if (selected_ + 1 < static_cast<int>(model_.size())) ++selected_;   // auto-advance
    }
}

// The window stopped rendering (closed/collapsed). Clear the keyboard grab so
// the host regains its keys, and silence any note still held on the piano/PC
// keyboard. Idle-guarded so it doesn't spam register writes every hidden frame.
void SidTrackerEditor::onWindowHidden() {
    if (!kbdActive_ && kbdNote_ < 0 && heldNote_ < 0 && !pianoDown_) return;
    kbdActive_ = false;
    kbdNote_   = -1;
    heldNote_  = -1;
    pianoDown_ = false;
    if (host_) host_->previewSilence();
}

void SidTrackerEditor::releasePiano() {
    if (heldNote_ >= 0 && host_) host_->previewNoteOff(waveform_);
    heldNote_ = -1;
}

// A real piano: white keys with black keys overlaid in the correct positions,
// spanning 3 octaves from the base octave. Click/drag to audition (glissando);
// keys light up while held or while the song plays them.
void SidTrackerEditor::renderKeyboard() {
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("Base octave", &octave_, 0, 5);
    ImGui::SameLine();
    ImGui::Checkbox("Write to row", &writeToRow_);
    ImGui::SameLine();
    // Toggle the PC-keyboard note layout. QWERTY reads the physical US positions;
    // AZERTY swaps the keys whose label/character differs (Z<->W, Q<->A, the
    // punctuation ends) so a French keyboard's labels line up. Pick whichever
    // actually plays right on your machine.
    if (ImGui::Button(azerty_ ? "AZERTY" : "QWERTY")) azerty_ = !azerty_;
    ImGui::SameLine();
    ImGui::TextDisabled(azerty_
        ? "mouse or PC keys (W S X D C V G B H N J , = lower, A Z E R T Y = upper)"
        : "mouse or PC keys (Z S X D C V G B H N J M , = lower, Q W E R T Y = upper)");

    // --- PC-keyboard play (tracker layout) ----------------------------------
    // Only when the window is focused and no text field is active, so typing a
    // name doesn't play notes. kbdActive_ is read by the host to suppress its own
    // keyboard forwarding (Apple-1) while the tracker owns the keys.
    {
        ImGuiIO& io = ImGui::GetIO();
        kbdActive_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                     !io.WantTextInput;
        struct KM { ImGuiKey key; int off; };
        static const KM kQwerty[] = {
            {ImGuiKey_Z, 0}, {ImGuiKey_S, 1}, {ImGuiKey_X, 2}, {ImGuiKey_D, 3},
            {ImGuiKey_C, 4}, {ImGuiKey_V, 5}, {ImGuiKey_G, 6}, {ImGuiKey_B, 7},
            {ImGuiKey_H, 8}, {ImGuiKey_N, 9}, {ImGuiKey_J, 10}, {ImGuiKey_M, 11},
            {ImGuiKey_Comma, 12},
            {ImGuiKey_Q, 12}, {ImGuiKey_2, 13}, {ImGuiKey_W, 14}, {ImGuiKey_3, 15},
            {ImGuiKey_E, 16}, {ImGuiKey_R, 17}, {ImGuiKey_5, 18}, {ImGuiKey_T, 19},
            {ImGuiKey_6, 20}, {ImGuiKey_Y, 21}, {ImGuiKey_7, 22}, {ImGuiKey_U, 23},
            {ImGuiKey_I, 24},
        };
        // AZERTY: bottom-left is 'W' (physical Z), 'Q' becomes 'A', 'W' becomes
        // 'Z'; the two lower-octave punctuation keys shift one to the right.
        static const KM kAzerty[] = {
            {ImGuiKey_W, 0}, {ImGuiKey_S, 1}, {ImGuiKey_X, 2}, {ImGuiKey_D, 3},
            {ImGuiKey_C, 4}, {ImGuiKey_V, 5}, {ImGuiKey_G, 6}, {ImGuiKey_B, 7},
            {ImGuiKey_H, 8}, {ImGuiKey_N, 9}, {ImGuiKey_J, 10}, {ImGuiKey_Comma, 11},
            {ImGuiKey_Semicolon, 12},
            {ImGuiKey_A, 12}, {ImGuiKey_2, 13}, {ImGuiKey_Z, 14}, {ImGuiKey_3, 15},
            {ImGuiKey_E, 16}, {ImGuiKey_R, 17}, {ImGuiKey_5, 18}, {ImGuiKey_T, 19},
            {ImGuiKey_6, 20}, {ImGuiKey_Y, 21}, {ImGuiKey_7, 22}, {ImGuiKey_U, 23},
            {ImGuiKey_I, 24},
        };
        const KM* kMap = azerty_ ? kAzerty : kQwerty;
        const int kMapN = azerty_ ? IM_ARRAYSIZE(kAzerty) : IM_ARRAYSIZE(kQwerty);
        if (kbdActive_) {
            for (int i = 0; i < kMapN; ++i) {
                const KM& m = kMap[i];
                if (ImGui::IsKeyPressed(m.key, /*repeat=*/false)) {
                    int note = std::clamp(octave_ * 12 + m.off, 0, 95);
                    pressPiano(note);
                    kbdNote_ = note;
                }
            }
            // Release when no key producing the held note is down anymore.
            if (kbdNote_ >= 0) {
                bool stillDown = false;
                for (int i = 0; i < kMapN; ++i)
                    if (std::clamp(octave_ * 12 + kMap[i].off, 0, 95) == kbdNote_ &&
                        ImGui::IsKeyDown(kMap[i].key)) { stillDown = true; break; }
                if (!stillDown) {
                    if (heldNote_ == kbdNote_) releasePiano();
                    kbdNote_ = -1;
                }
            }
        } else if (kbdNote_ >= 0) {          // lost focus while a key was held
            if (heldNote_ == kbdNote_) releasePiano();
            kbdNote_ = -1;
        }
    }

    const int   NOCT   = 3;
    const float whiteW = 26.0f, whiteH = 100.0f, blackW = 16.0f, blackH = 62.0f;
    static const int whiteSemis[7]     = {0, 2, 4, 5, 7, 9, 11};       // C D E F G A B
    static const int blackAfter[5]     = {0, 1, 3, 4, 5};             // black sits right of these white keys
    static const int blackSemis[5]     = {1, 3, 6, 8, 10};           // C# D# F# G# A#
    const int totalWhite = NOCT * 7;

    ImVec2 org = ImGui::GetCursorScreenPos();
    ImVec2 size(totalWhite * whiteW, whiteH);
    ImGui::InvisibleButton("piano", size);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Hit-test mouse -> note (black keys are on top, so test them first).
    auto noteAt = [&](ImVec2 p) -> int {
        float rx = p.x - org.x, ry = p.y - org.y;
        if (rx < 0 || rx >= size.x || ry < 0 || ry >= whiteH) return -1;
        if (ry < blackH) {
            for (int o = 0; o < NOCT; ++o)
                for (int b = 0; b < 5; ++b) {
                    float bx = (o * 7 + blackAfter[b] + 1) * whiteW - blackW * 0.5f;
                    if (rx >= bx && rx < bx + blackW)
                        return std::clamp((octave_ + o) * 12 + blackSemis[b], 0, 95);
                }
        }
        int wi = static_cast<int>(rx / whiteW);
        return std::clamp((octave_ + wi / 7) * 12 + whiteSemis[wi % 7], 0, 95);
    };

    int hovered = (ImGui::IsItemHovered() || ImGui::IsItemActive())
                      ? noteAt(ImGui::GetIO().MousePos) : -1;
    if (ImGui::IsItemActivated())        { pianoDown_ = true; pressPiano(hovered); }
    else if (ImGui::IsItemActive() && pianoDown_ && hovered >= 0 && hovered != heldNote_)
                                          pressPiano(hovered);                  // glissando
    if (ImGui::IsItemDeactivated())      { releasePiano(); pianoDown_ = false; }

    auto lit = [&](int note) { return note == heldNote_ || note == playingNote_; };

    // white keys
    for (int wi = 0; wi < totalWhite; ++wi) {
        int note = std::clamp((octave_ + wi / 7) * 12 + whiteSemis[wi % 7], 0, 95);
        ImVec2 p0(org.x + wi * whiteW, org.y), p1(p0.x + whiteW - 1.0f, org.y + whiteH);
        dl->AddRectFilled(p0, p1, lit(note) ? IM_COL32(140, 200, 255, 255)
                                            : IM_COL32(238, 238, 240, 255));
        dl->AddRect(p0, p1, IM_COL32(40, 40, 40, 255));
        if (wi % 7 == 0) {                                   // label each C
            char b[8]; std::snprintf(b, sizeof(b), "C%d", octave_ + wi / 7);
            dl->AddText(ImVec2(p0.x + 4.0f, org.y + whiteH - 15.0f),
                        IM_COL32(90, 90, 90, 255), b);
        }
    }
    // black keys (on top)
    for (int o = 0; o < NOCT; ++o)
        for (int b = 0; b < 5; ++b) {
            int note = std::clamp((octave_ + o) * 12 + blackSemis[b], 0, 95);
            float bx = (o * 7 + blackAfter[b] + 1) * whiteW - blackW * 0.5f;
            ImVec2 p0(org.x + bx, org.y), p1(p0.x + blackW, org.y + blackH);
            dl->AddRectFilled(p0, p1, lit(note) ? IM_COL32(90, 150, 220, 255)
                                                : IM_COL32(24, 24, 28, 255));
            dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 255));
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
        const bool wrote = std::fwrite(text.data(), 1, text.size(), f) == text.size();
        const bool closed = std::fclose(f) == 0;
        status_ = (wrote && closed) ? "Exported " + path
                                    : "Write error (disk full?) — " + path;
    } else {
        status_ = "Could not write " + path;
    }
}

}  // namespace sidtrack
