// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// CassetteDeck_ImGui.cpp — procedural drawing + interaction for the
// realistic cassette deck widget. All geometry is in a fixed "design"
// coordinate system (480 × 660 units); a per-frame uniform scale factor
// maps it into the window's available content region so the deck stays
// pixel-crisp at any window size. Buttons are drawn with ImDrawList and
// captured with InvisibleButton at their screen-space rects.

#include "CassetteDeck_ImGui.h"

#include "EmulationController.h"
#include "EmulationSnapshot.h"
#include "IconsFontAwesome6.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace pom1 {
namespace {

// ---------------------------------------------------------------------------
// Design canvas size — every geometric constant in this file is expressed
// in these units; scaled to screen pixels at render time.
// ---------------------------------------------------------------------------
// Portrait proportions closer to the Panasonic Slim Line reference:
// narrower than the original 480 × 660 to read as "tall, compact deck"
// instead of "wide block" when the window is resized modestly.
constexpr float kDesignW = 378.0f;
constexpr float kDesignH = 404.0f;

// ---------------------------------------------------------------------------
// Palette — matte-black consumer tape deck, late 80s / early 90s look.
// ---------------------------------------------------------------------------
constexpr ImU32 kChassis        = IM_COL32( 24,  24,  28, 255);
constexpr ImU32 kChassisEdgeLo  = IM_COL32(  6,   6,   8, 255);
constexpr ImU32 kChassisEdgeHi  = IM_COL32( 56,  56,  62, 255);
constexpr ImU32 kGlassDark      = IM_COL32(  8,   8,  12, 235);
constexpr ImU32 kGlassReflect   = IM_COL32(255, 255, 255,  18);
constexpr ImU32 kGlassEdgeDark  = IM_COL32(  0,   0,   0, 255);
constexpr ImU32 kBrandStrip     = IM_COL32(232, 232, 234, 255);
constexpr ImU32 kBrandStripEdge = IM_COL32(150, 150, 152, 255);
constexpr ImU32 kBrandText      = IM_COL32( 20,  20,  22, 255);
constexpr ImU32 kLabelText      = IM_COL32(198, 200, 204, 255);
constexpr ImU32 kLabelTextDim   = IM_COL32(128, 130, 134, 255);
constexpr ImU32 kButtonBody     = IM_COL32( 22,  22,  26, 255);
constexpr ImU32 kButtonEdge     = IM_COL32(  4,   4,   6, 255);
constexpr ImU32 kButtonDown     = IM_COL32(  8,   8,  10, 255);
constexpr ImU32 kButtonHi       = IM_COL32( 46,  46,  52, 255);
constexpr ImU32 kGlyph          = IM_COL32(214, 216, 220, 255);
constexpr ImU32 kGlyphDim       = IM_COL32( 90,  92,  96, 255);
constexpr ImU32 kGlyphRec       = IM_COL32(214, 216, 220, 255); // default (white circle)
constexpr ImU32 kGlyphRecActive = IM_COL32(234,  60,  52, 255); // engaged → red
constexpr ImU32 kCounterBg      = IM_COL32( 10,   8,   4, 255);
constexpr ImU32 kCounterRim     = IM_COL32( 50,  50,  54, 255);
constexpr ImU32 kCounterDigit   = IM_COL32(232, 176,  72, 255);
constexpr ImU32 kCounterDigitDim = IM_COL32( 64,  46,  18, 255);
constexpr ImU32 kBadgeBorder    = IM_COL32(200, 202, 206, 255);
constexpr ImU32 kBadgeText      = IM_COL32(210, 212, 216, 255);
constexpr ImU32 kCompartmentLip = IM_COL32(  4,   4,   6, 255);
constexpr ImU32 kHubDark        = IM_COL32(  2,   2,   4, 255);
constexpr ImU32 kHubMid         = IM_COL32( 40,  42,  48, 255);

// ---------------------------------------------------------------------------
// Layout in design coordinates (x0,y0,x1,y1 — inclusive rects).
// ---------------------------------------------------------------------------
struct Rect { float x0, y0, x1, y1; };
// Speaker grille removed — it was decorative-only and ate ~230 px of vertical
// space. Everything below shifts up by 236 px to keep the counter/cassette/keys
// sitting on the same deck.
constexpr Rect kCounterBarR {   9.0f,  14.0f, 369.0f,  70.0f };
constexpr Rect kBrandBadgeR { 306.0f,  30.0f, 360.0f,  56.0f };
constexpr Rect kCounterWinR { 148.0f,  26.0f, 212.0f,  60.0f };
// REC LED — sits in the counter bar where the MIC switch used to live, on
// the LEFT of the COUNTER window. Larger than the previous tiny lamp since
// we have the whole MIC footprint to work with.
constexpr Rect kRecLedR     {  22.0f,  22.0f,  46.0f,  46.0f };
constexpr Rect kCassetteR   {  18.0f,  80.0f, 360.0f, 242.0f };
constexpr Rect kBrandR      {  14.0f, 252.0f, 364.0f, 280.0f };
constexpr Rect kLabelsR     {  18.0f, 284.0f, 360.0f, 302.0f };

constexpr float kKeyW      = 47.0f;
constexpr float kKeyH      = 64.0f;
constexpr float kKeyRadius = 5.0f;
constexpr float kKeysTop   = 308.0f;
constexpr float kKeyCenterXs[6] = { 44.0f, 102.0f, 160.0f, 218.0f, 276.0f, 334.0f };

constexpr float kCounterResetW = 10.0f;  // little silver button next to counter window

// Counter rolls over at 1000. Speed chosen for visual plausibility:
// tape playback ~1 tick per 1.5 seconds (real decks vary 0.5–3 s/count);
// REW/FF visibly faster.
constexpr double kCounterPlaySecPerTick = 1.5;
constexpr double kCounterWindSecPerTick = 0.15;

// REW/FF auto-release after this many wall-clock seconds (real deck:
// mechanical end-of-tape detects the tension and releases; here we just
// animate for a moment and fall back to Stopped).
constexpr double kWindDurationSeconds = 1.4;

// ---------------------------------------------------------------------------
// Tiny drawing helpers.
// ---------------------------------------------------------------------------
inline ImVec2 P(ImVec2 p0, float s, float x, float y) {
    return ImVec2(p0.x + x * s, p0.y + y * s);
}

inline float S(float s, float v) { return v * s; }

// Draw text at a given design-space position with automatic scaled font size.
void drawText(ImDrawList* dl, ImVec2 p0, float s, float x, float y,
              float fontPx, ImU32 col, const char* text)
{
    ImFont* font = ImGui::GetFont();
    if (!font || !text || !text[0]) return;
    const float fs = std::max(7.0f, fontPx * s);
    const ImVec2 pos = P(p0, s, x, y);
    dl->AddText(font, fs, pos, col, text);
}

// Draw centered text in a design-space rect.
void drawCenteredText(ImDrawList* dl, ImVec2 p0, float s, Rect r,
                      float fontPx, ImU32 col, const char* text)
{
    ImFont* font = ImGui::GetFont();
    if (!font || !text || !text[0]) return;
    const float fs = std::max(7.0f, fontPx * s);
    const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    const float rw = (r.x1 - r.x0) * s;
    const float rh = (r.y1 - r.y0) * s;
    const ImVec2 pos(
        p0.x + r.x0 * s + (rw - sz.x) * 0.5f,
        p0.y + r.y0 * s + (rh - sz.y) * 0.5f);
    dl->AddText(font, fs, pos, col, text);
}

// Filled right-pointing isoceles triangle centered at (cx,cy) with half-size h.
void drawPlayGlyph(ImDrawList* dl, ImVec2 p0, float s, float cx, float cy, float h, ImU32 col) {
    const ImVec2 a = P(p0, s, cx - h * 0.65f, cy - h);
    const ImVec2 b = P(p0, s, cx - h * 0.65f, cy + h);
    const ImVec2 c = P(p0, s, cx + h * 0.85f, cy);
    dl->AddTriangleFilled(a, b, c, col);
}

// Two stacked triangles pointing left (REW) or right (FF).
void drawChevronGlyph(ImDrawList* dl, ImVec2 p0, float s, float cx, float cy,
                      float h, ImU32 col, bool pointRight) {
    const float dx = pointRight ? 1.0f : -1.0f;
    const float gap = h * 0.35f;
    for (int i = 0; i < 2; ++i) {
        const float ox = (i == 0 ? -gap : gap) * dx;
        const ImVec2 a = P(p0, s, cx + ox - dx * h * 0.5f, cy - h * 0.75f);
        const ImVec2 b = P(p0, s, cx + ox - dx * h * 0.5f, cy + h * 0.75f);
        const ImVec2 c = P(p0, s, cx + ox + dx * h * 0.6f, cy);
        dl->AddTriangleFilled(a, b, c, col);
    }
}

// Solid square (STOP).
void drawStopGlyph(ImDrawList* dl, ImVec2 p0, float s, float cx, float cy, float h, ImU32 col) {
    const ImVec2 a = P(p0, s, cx - h * 0.75f, cy - h * 0.75f);
    const ImVec2 b = P(p0, s, cx + h * 0.75f, cy + h * 0.75f);
    dl->AddRectFilled(a, b, col);
}

// Two vertical bars (PAUSE).
void drawPauseGlyph(ImDrawList* dl, ImVec2 p0, float s, float cx, float cy, float h, ImU32 col) {
    const float bw = h * 0.28f;
    const float gap = h * 0.35f;
    const ImVec2 a0 = P(p0, s, cx - gap - bw, cy - h * 0.85f);
    const ImVec2 a1 = P(p0, s, cx - gap,       cy + h * 0.85f);
    const ImVec2 b0 = P(p0, s, cx + gap,       cy - h * 0.85f);
    const ImVec2 b1 = P(p0, s, cx + gap + bw,  cy + h * 0.85f);
    dl->AddRectFilled(a0, a1, col);
    dl->AddRectFilled(b0, b1, col);
}

// Filled circle (REC).
void drawRecGlyph(ImDrawList* dl, ImVec2 p0, float s, float cx, float cy, float h, ImU32 col) {
    // Slightly smaller than the other glyphs — a solid circle reads as
    // larger than a triangle/square of the same bounding size, so we
    // pull it in to match visual weight.
    dl->AddCircleFilled(P(p0, s, cx, cy), S(s, h * 0.62f), col, 24);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CassetteDeck_ImGui::reset()
{
    transport_ = Transport::Stopped;
    paused_ = false;
    counter_ = 0;
    counterAccum_ = 0.0;
    hubAngle_ = 0.0f;
    rewEndsAt_ = 0.0;
}

CassetteDeck_ImGui::FrameResult
CassetteDeck_ImGui::render(const char* title,
                           bool& open,
                           EmulationController* emulation,
                           const EmulationSnapshot& snap,
                           float deltaSeconds)
{
    FrameResult out;
    if (!open) return out;

    // Scaled initial window size big enough to show the deck without
    // squeezing. Resize-friendly (user can enlarge; we scale uniformly).
    ImGui::SetNextWindowSize(ImVec2(kDesignW + 28.0f, kDesignH + 40.0f),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(kDesignW * 0.55f + 28.0f, kDesignH * 0.55f + 40.0f),
        ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
    const bool visible = ImGui::Begin(title, &open, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return out;
    }

    wallClock_ += std::max(0.0f, deltaSeconds);
    syncWithSnapshot(snap);
    advanceCounter(deltaSeconds, snap);

    // Auto-release REW/FF after a short interval — but not while the
    // pulse-mode device is still physically rewinding. Progressive REW
    // can take longer than kWindDurationSeconds on long tapes, so we
    // keep the visual latch pinned as long as the device reports
    // rewinding. Once isRewinding() flips false, the normal timeout
    // takes over and drops us back to Stopped on the next frame.
    if ((transport_ == Transport::Rewinding || transport_ == Transport::FastForwarding)
         && wallClock_ >= rewEndsAt_ && !snap.cassetteRewinding) {
        transport_ = Transport::Stopped;
    }

    // Header row — six compact icon buttons above the deck. Font
    // Awesome glyphs scaled up just enough to read clearly inside the
    // 38×38 squares; a narrower header row leaves more vertical room
    // for the chassis / piano keys below.
    constexpr float kActionBtnSize = 38.0f;
    constexpr float kActionIconScale = 1.45f;
    const ImVec2 actionSize(kActionBtnSize, kActionBtnSize);
    ImGui::SetWindowFontScale(kActionIconScale);
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "##DeckLoad", actionSize)) {
        out.requestLoadDialog = true;
    }
    ImGui::SetWindowFontScale(1.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load tape (ACI / WAV / MP3 / OGG / FLAC)");

    // New (blank) cassette — ejects whatever's loaded AND clears any captured
    // output so the user gets a truly fresh tape, ready to record. Sits
    // right after Load since "file" ← → "fresh tape" are the two ways a
    // deck gets a cassette.
    ImGui::SameLine();
    ImGui::SetWindowFontScale(kActionIconScale);
    if (ImGui::Button(ICON_FA_FILE_CIRCLE_PLUS "##DeckNew", actionSize)) {
        if (emulation) {
            emulation->ejectTape();
            emulation->clearTapeCapture();
        }
        transport_ = Transport::Stopped;
        paused_    = false;
        counter_   = 0;
        counterAccum_ = 0.0;
        out.statusMessage = "Nouvelle cassette (vide)";
    }
    ImGui::SetWindowFontScale(1.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Nouvelle cassette vierge\n"
                          "(ejecte la cassette courante + efface l'enregistrement)");

    ImGui::SameLine();
    ImGui::SetWindowFontScale(kActionIconScale);
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "##DeckSave", actionSize)) {
        out.requestSaveDialog = true;
    }
    ImGui::SetWindowFontScale(1.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save captured tape (ACI / WAV)");

    ImGui::SameLine();
    ImGui::BeginDisabled(snap.cassetteRecordedTransitionCount == 0);
    ImGui::SetWindowFontScale(kActionIconScale);
    if (ImGui::Button(ICON_FA_ERASER "##DeckClear", actionSize)) {
        if (emulation) emulation->clearTapeCapture();
        out.statusMessage = "Cassette capture cleared";
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(snap.cassetteRecordedTransitionCount == 0
            ? "Clear capture (nothing recorded yet)"
            : "Clear captured output");
    }

    // VOL- / VOL+ — half-height buttons stacked vertically so they take
    // the same horizontal slot as one transport button. 0.10 step = 10
    // percent per click; range clamped [0, 2] in CassetteDevice::setVolume.
    // volume_ is local state so rapid clicks accumulate deterministically
    // (the snapshot path has round-trip latency: click → setCassetteVolume
    // → audio-thread reads atomic → emulation-thread publishes → UI reads
    // snapshot. If the user double-clicks before a publish, two clicks
    // would both read the stale snapshot value and effectively collapse
    // into one. Reading/writing volume_ bypasses that loop.)
    if (!volumeSynced_) {
        volume_ = snap.cassetteVolume;
        volumeSynced_ = true;
    }
    ImGui::SameLine();
    constexpr float kVolBtnW = kActionBtnSize;
    constexpr float kVolBtnH = (kActionBtnSize - 4.0f) * 0.5f;  // 4 px gap between the two
    const ImVec2 volSize(kVolBtnW, kVolBtnH);
    constexpr float kVolStep = 0.10f;
    constexpr float kVolMax  = 2.0f;
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(kActionIconScale * 0.7f);
    if (ImGui::Button(ICON_FA_VOLUME_HIGH "##DeckVolUp", volSize)) {
        // Touching the volume slider implicitly unmutes — otherwise the
        // mute lamp would lie about the current state.
        muted_  = false;
        volume_ = std::min(kVolMax, volume_ + kVolStep);
        if (emulation) emulation->setCassetteVolume(volume_);
        char msg[64];
        std::snprintf(msg, sizeof(msg), "Cassette volume: %d%%",
                      static_cast<int>(std::round(volume_ * 100.0f)));
        out.statusMessage = msg;
    }
    ImGui::SetWindowFontScale(1.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Volume + 10%%");
    if (ImGui::Button(ICON_FA_VOLUME_LOW "##DeckVolDown", volSize)) {
        muted_  = false;
        volume_ = std::max(0.0f, volume_ - kVolStep);
        if (emulation) emulation->setCassetteVolume(volume_);
        char msg[64];
        std::snprintf(msg, sizeof(msg), "Cassette volume: %d%%",
                      static_cast<int>(std::round(volume_ * 100.0f)));
        out.statusMessage = msg;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Volume - 10%%");
    ImGui::EndGroup();

    // MUTE — toggles the cassette audio source between 0 and the pre-mute
    // volume. CassetteDevice has no dedicated mute flag; we just push 0 and
    // remember the level so unmuting restores it. When engaged, the button
    // is drawn in red to signal "silenced".
    //
    // Snapshot the muted state BEFORE the button call so the pop matches
    // the push: a click inside Button() flips `muted_`, so reading it
    // again after Button() would unbalance Push/Pop and trigger ImGui
    // assertion spam "Calling PopStyleColor() too many times".
    ImGui::SameLine();
    const bool muteStylePushed = muted_;
    if (muteStylePushed) {
        ImGui::PushStyleColor(ImGuiCol_Button,
                              (ImVec4)ImColor(0.75f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              (ImVec4)ImColor(0.88f, 0.25f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              (ImVec4)ImColor(0.62f, 0.14f, 0.14f, 1.0f));
    }
    ImGui::SetWindowFontScale(kActionIconScale);
    if (ImGui::Button(ICON_FA_VOLUME_XMARK "##DeckMute", actionSize)) {
        if (muted_) {
            muted_  = false;
            volume_ = preMuteVolume_;
            if (emulation) emulation->setCassetteVolume(volume_);
            char msg[64];
            std::snprintf(msg, sizeof(msg), "Cassette unmuted (%d%%)",
                          static_cast<int>(std::round(volume_ * 100.0f)));
            out.statusMessage = msg;
        } else {
            preMuteVolume_ = volume_;
            muted_  = true;
            volume_ = 0.0f;
            if (emulation) emulation->setCassetteVolume(0.0f);
            out.statusMessage = "Cassette muted";
        }
    }
    ImGui::SetWindowFontScale(1.0f);
    if (muteStylePushed) ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(muted_ ? "Unmute cassette" : "Mute cassette");

    // Compact live status line under the buttons.
    char headerInfo[256];
    std::snprintf(headerInfo, sizeof(headerInfo),
                  "in %zu tr  |  out %zu tr  |  audio %s  |  vol %d%%",
                  snap.cassetteLoadedTransitionCount,
                  snap.cassetteRecordedTransitionCount,
                  snap.cassetteAudioAvailable ? "active" : "off",
                  static_cast<int>(std::round(volume_ * 100.0f)));
    ImGui::TextDisabled("%s", headerInfo);

    // Big deck-mode readout — same information the small "PROGRAM TAPE"
    // stencil on the cassette label carries, surfaced up here so it's
    // legible at a glance. CassetteDevice fires a mechanical clunk on
    // every transition between these states (including eject / load of a
    // different kind, and ACI plug/unplug while a tape is in).
    const char* modeLabel;
    ImVec4 modeColor;
    if (!snap.cassetteLoadedTape) {
        modeLabel = "NO TAPE";
        modeColor = ImVec4(0.58f, 0.58f, 0.62f, 1.0f);
    } else if (snap.cassetteAudioStreamMode) {
        modeLabel = "AUDIO STREAM";
        modeColor = ImVec4(0.20f, 0.55f, 0.80f, 1.0f);
    } else {
        modeLabel = "PROGRAM TAPE";
        modeColor = ImVec4(0.85f, 0.55f, 0.15f, 1.0f);
    }
    constexpr float kModeScale = 1.6f;
    ImGui::SetWindowFontScale(kModeScale);
    const ImVec2 modeSize = ImGui::CalcTextSize(modeLabel);
    const float availW = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (availW - modeSize.x) * 0.5f));
    ImGui::TextColored(modeColor, "%s", modeLabel);
    ImGui::SetWindowFontScale(1.0f);

    // Armed banner — surfaces the CassetteDevice::playbackArmed flag so the
    // user knows the tape is loaded, the capstan is "locked", and the Apple-1
    // still needs to be told to read (typically by typing `C100R` in the Woz
    // Monitor). As soon as the ACI ROM polls $C081 the flag drops and the
    // banner disappears. Stream-mode tapes start playing immediately, so the
    // armed state never lingers there — skip the banner.
    const bool armedWaiting = snap.cassetteLoadedTape
                              && snap.cassettePlaybackArmed
                              && !snap.cassetteAudioStreamMode;
    if (armedWaiting) {
        // Pulse alpha at ~1.2 Hz so the banner breathes without being
        // distracting. wallClock_ is already frame-accumulated in seconds.
        const float pulse = 0.55f + 0.45f * std::sin(static_cast<float>(wallClock_) * 7.5f);
        const ImVec4 armedColor(0.95f, 0.28f, 0.22f, pulse);
        const char* kArmedText = "ARMED - waiting for C100R";
        const ImVec2 armedSize = ImGui::CalcTextSize(kArmedText);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                             + std::max(0.0f, (availW - armedSize.x) * 0.5f));
        ImGui::TextColored(armedColor, "%s", kArmedText);
    }

    ImGui::Separator();

    // Compute scale to fit the remaining content region while preserving
    // aspect. The header above has advanced the cursor, so the deck gets
    // whatever is left under it.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float sx = avail.x / kDesignW;
    const float sy = avail.y / kDesignH;
    const float s  = std::max(0.25f, std::min(sx, sy));
    const float canvasW = kDesignW * s;
    const float canvasH = kDesignH * s;

    // Centre the deck in the available region.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 p0(
        origin.x + (avail.x - canvasW) * 0.5f,
        origin.y + (avail.y - canvasH) * 0.5f);

    // Reserve canvas so downstream ImGui items (none, here) still flow.
    ImGui::Dummy(ImVec2(avail.x, avail.y));

    ImDrawList* dl = ImGui::GetWindowDrawList();

    drawChassis(dl, p0, s);

    // Counter bar content: slim line badge, counter window, tiny reset button.
    drawSlimLineBadge(dl, p0, s);
    bool counterResetClicked = false;
    // Pick the counter-bar lamp mode. Priority: recording > armed >
    // active reading > off. `armedWaiting` is already pulse-mode-gated
    // above; `playbackActive` means the ACI ROM has polled $C081 at
    // least once and pulses are flowing through the tape head into
    // memory — that's the "DATA" regime the user asked to see.
    LampMode lampMode = LampMode::Off;
    if (transport_ == Transport::Recording && !paused_) {
        lampMode = LampMode::Rec;
    } else if (armedWaiting) {
        lampMode = LampMode::Armed;
    } else if (snap.cassettePlaybackActive
               && snap.cassetteLoadedTape
               && !snap.cassetteAudioStreamMode
               && !paused_) {
        lampMode = LampMode::Data;
    }
    drawCounter(dl, p0, s, "##CounterReset", counterResetClicked, lampMode);
    if (counterResetClicked) {
        counter_ = 0;
        counterAccum_ = 0.0;
        out.statusMessage = "Tape counter reset";
    }

    drawCassetteWindow(dl, p0, s, snap);
    drawBrandStrip(dl, p0, s);
    drawButtonLabels(dl, p0, s);

    // Piano keys — interlock rules: REC/PLAY/PAUSE require a loaded tape
    // for visual engagement to make sense, but the real mechanical keys
    // latch regardless. Keep press action live; device calls themselves
    // no-op when there's nothing loaded.

    // REC
    if (drawPianoKey(dl, p0, s, kKeyCenterXs[0], kKeysTop + kKeyH * 0.5f,
                     "##DeckKeyRec", "rec", recKeyEngaged(), false)) {
        std::string msg = onRecord(emulation, snap);
        if (!msg.empty()) out.statusMessage = std::move(msg);
    }
    // PLAY
    if (drawPianoKey(dl, p0, s, kKeyCenterXs[1], kKeysTop + kKeyH * 0.5f,
                     "##DeckKeyPlay", "play", playKeyEngaged(), false)) {
        std::string msg = onPlay(emulation, snap);
        if (!msg.empty()) out.statusMessage = std::move(msg);
    }
    // REW — click triggers the transport action (full rewind in ACI mode,
    // start-of-scrub in stream mode); holding continues to seek backward
    // at ~30× while the mouse stays pressed.
    bool rewHeld = false;
    if (drawPianoKey(dl, p0, s, kKeyCenterXs[2], kKeysTop + kKeyH * 0.5f,
                     "##DeckKeyRew", "rew", rewKeyEngaged(), false, &rewHeld)) {
        std::string msg = onRewind(emulation, snap);
        if (!msg.empty()) out.statusMessage = std::move(msg);
    }
    if (rewHeld && snap.cassetteAudioStreamMode && snap.cassetteLoadedTape) {
        constexpr double kScrubSpeedX = 30.0;
        if (emulation) {
            emulation->stopTape();  // silence audio while scrubbing
            emulation->seekTapeRelative(-static_cast<double>(deltaSeconds) * kScrubSpeedX);
        }
        transport_ = Transport::Rewinding;
        paused_    = false;
        rewEndsAt_ = wallClock_ + 0.25;  // refreshed each frame while held
    }

    // FF — mirror of REW with forward seek.
    bool ffHeld = false;
    if (drawPianoKey(dl, p0, s, kKeyCenterXs[3], kKeysTop + kKeyH * 0.5f,
                     "##DeckKeyFF", "ff", ffKeyEngaged(), false, &ffHeld)) {
        std::string msg = onFForward(emulation, snap);
        if (!msg.empty()) out.statusMessage = std::move(msg);
    }
    if (ffHeld && snap.cassetteAudioStreamMode && snap.cassetteLoadedTape) {
        constexpr double kScrubSpeedX = 30.0;
        if (emulation) {
            emulation->stopTape();
            emulation->seekTapeRelative(+static_cast<double>(deltaSeconds) * kScrubSpeedX);
        }
        transport_ = Transport::FastForwarding;
        paused_    = false;
        rewEndsAt_ = wallClock_ + 0.25;
    }
    // STOP — always just stops. Eject lives on its own control (header
    // Clear/Cassette-Control window) to avoid the surprising second-click
    // eject users hit when re-tapping STOP to confirm the tape halted.
    const bool stopDisabled = false;
    if (drawPianoKey(dl, p0, s, kKeyCenterXs[4], kKeysTop + kKeyH * 0.5f,
                     "##DeckKeyStop", "stop", stopKeyEngaged(), stopDisabled)) {
        std::string msg = onStop(emulation);
        if (!msg.empty()) out.statusMessage = std::move(msg);
    }
    // PAUSE
    if (drawPianoKey(dl, p0, s, kKeyCenterXs[5], kKeysTop + kKeyH * 0.5f,
                     "##DeckKeyPause", "pause", pauseKeyEngaged(), false)) {
        std::string msg = onPause(emulation);
        if (!msg.empty()) out.statusMessage = std::move(msg);
    }

    ImGui::End();
    return out;
}

// ---------------------------------------------------------------------------
// Chassis + background elements
// ---------------------------------------------------------------------------

void CassetteDeck_ImGui::drawChassis(ImDrawList* dl, ImVec2 p0, float s) const
{
    const ImVec2 a = P(p0, s, 0.0f, 0.0f);
    const ImVec2 b = P(p0, s, kDesignW, kDesignH);
    const float r = S(s, 14.0f);
    // Outer drop shadow (a cheap darker rect outside the body).
    dl->AddRectFilled(ImVec2(a.x - S(s, 1.0f), a.y + S(s, 2.0f)),
                      ImVec2(b.x + S(s, 1.0f), b.y + S(s, 4.0f)),
                      IM_COL32(0, 0, 0, 90), r);
    // Main body.
    dl->AddRectFilled(a, b, kChassis, r);
    // Top highlight (thin lighter edge)
    dl->AddRect(a, b, kChassisEdgeHi, r, 0, S(s, 1.5f));
    // Inner shadow
    dl->AddRect(ImVec2(a.x + S(s, 1.5f), a.y + S(s, 1.5f)),
                ImVec2(b.x - S(s, 1.5f), b.y - S(s, 1.5f)),
                kChassisEdgeLo, r * 0.85f, 0, S(s, 1.0f));
}

void CassetteDeck_ImGui::drawSlimLineBadge(ImDrawList* dl, ImVec2 p0, float s) const
{
    const Rect r = kBrandBadgeR;
    const ImVec2 a = P(p0, s, r.x0, r.y0);
    const ImVec2 b = P(p0, s, r.x1, r.y1);
    dl->AddRect(a, b, kBadgeBorder, S(s, 2.0f), 0, std::max(1.0f, S(s, 0.9f)));
    drawCenteredText(dl, p0, s, r, 12.0f, kBadgeText, "POM1");
}

void CassetteDeck_ImGui::drawCounter(ImDrawList* dl, ImVec2 p0, float s,
                                     const char* resetId, bool& resetClicked,
                                     LampMode lamp)
{
    // "COUNTER" label to the left of the window (aligned on the window's
    // vertical centre).
    drawText(dl, p0, s, 98.0f, 37.0f, 9.0f, kLabelTextDim, "COUNTER");

    // Window housing.
    const Rect r = kCounterWinR;
    const ImVec2 a = P(p0, s, r.x0, r.y0);
    const ImVec2 b = P(p0, s, r.x1, r.y1);
    const float round = S(s, 3.0f);
    dl->AddRectFilled(a, b, kCounterBg, round);
    dl->AddRect(a, b, kCounterRim, round, 0, std::max(1.0f, S(s, 0.9f)));

    // Digits — 3 big numerals centered, no phantom backplate (the 7-seg
    // "888 behind digits" trick only works with a real segment font; with
    // a proportional font it overlays the real digits and muddies them).
    char digits[8];
    std::snprintf(digits, sizeof(digits), "%03u", counter_ % 1000);
    ImFont* font = ImGui::GetFont();
    if (font) {
        const float fs = std::max(10.0f, S(s, 22.0f));
        const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, digits);
        const ImVec2 pos(
            a.x + ((b.x - a.x) - sz.x) * 0.5f,
            a.y + ((b.y - a.y) - sz.y) * 0.5f - S(s, 0.5f));
        // Soft glow underneath (one-pixel offset in each axis, low alpha)
        // evokes an LCD bleed/backlight without touching digit sharpness.
        const ImU32 glow = IM_COL32(226, 158, 56, 40);
        const float off = std::max(1.0f, S(s, 0.8f));
        dl->AddText(font, fs, ImVec2(pos.x + off, pos.y), glow, digits);
        dl->AddText(font, fs, ImVec2(pos.x - off, pos.y), glow, digits);
        dl->AddText(font, fs, pos, kCounterDigit, digits);
    }

    // Small silver reset button, just right of the window.
    const float bx0 = r.x1 + 3.0f;
    const float bx1 = bx0 + kCounterResetW;
    const float by0 = r.y0 + 6.0f;
    const float by1 = r.y1 - 6.0f;
    const ImVec2 bp0 = P(p0, s, bx0, by0);
    const ImVec2 bp1 = P(p0, s, bx1, by1);
    ImGui::SetCursorScreenPos(bp0);
    ImGui::InvisibleButton(resetId, ImVec2(bp1.x - bp0.x, bp1.y - bp0.y));
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    resetClicked   = ImGui::IsItemClicked();
    const ImU32 col = act ? IM_COL32(120, 120, 124, 255)
                          : hov ? IM_COL32(200, 200, 204, 255)
                                : IM_COL32(160, 162, 166, 255);
    dl->AddRectFilled(bp0, bp1, col, S(s, 1.5f));
    dl->AddRect(bp0, bp1, IM_COL32(20,20,22,255), S(s, 1.5f), 0, 1.0f);
    if (hov) ImGui::SetTooltip("Reset tape counter");

    // Transport lamp — four states:
    //   Rec    → solid red  (recording to tape)
    //   Armed  → amber pulse (PLAY pressed, waiting for the ACI ROM to
    //            poll $C081 — the "ARMED — waiting for C100R" banner)
    //   Data   → solid green (ACI is actively consuming pulses; bytes
    //            are being clocked into RAM — the indicator the user
    //            wants to watch during a load)
    //   Off    → dim burgundy (stopped / EOF / paused)
    // Label flips REC/CUE/DATA/REC to match the mode.
    const Rect lr = kRecLedR;
    const ImVec2 lc = P(p0, s, (lr.x0 + lr.x1) * 0.5f, (lr.y0 + lr.y1) * 0.5f);
    const float lrad = S(s, (lr.y1 - lr.y0) * 0.45f);
    const char* lampLabel = "REC";
    ImU32 lampOutline = IM_COL32(8, 8, 10, 255);
    switch (lamp) {
        case LampMode::Rec: {
            dl->AddCircleFilled(lc, lrad * 1.6f, IM_COL32(232, 56, 44, 40), 22);
            dl->AddCircleFilled(lc, lrad,        IM_COL32(232, 56, 44, 255), 22);
            lampLabel = "REC";
            break;
        }
        case LampMode::Armed: {
            const float pulse = 0.55f + 0.45f * std::sin(static_cast<float>(wallClock_) * 7.5f);
            const ImU32 coreA  = (ImU32)std::clamp((int)(255.0f * pulse), 60, 255);
            const ImU32 bloomA = (ImU32)std::clamp((int)(60.0f * pulse), 12, 60);
            dl->AddCircleFilled(lc, lrad * 1.6f, IM_COL32(240, 178, 32, bloomA), 22);
            dl->AddCircleFilled(lc, lrad,        IM_COL32(240, 178, 32, coreA), 22);
            lampLabel = "CUE";
            break;
        }
        case LampMode::Data: {
            // Gentle breathing so the green reads as "data flowing"
            // rather than a frozen indicator. Alpha stays high enough to
            // be obvious even at a glance.
            const float pulse = 0.70f + 0.30f * std::sin(static_cast<float>(wallClock_) * 14.0f);
            const ImU32 coreA  = (ImU32)std::clamp((int)(255.0f * pulse), 150, 255);
            dl->AddCircleFilled(lc, lrad * 1.6f, IM_COL32(48, 210, 96, 55), 22);
            dl->AddCircleFilled(lc, lrad,        IM_COL32(48, 210, 96, coreA), 22);
            lampLabel = "DATA";
            break;
        }
        case LampMode::Off:
        default:
            dl->AddCircleFilled(lc, lrad, IM_COL32(48, 14, 12, 255), 18);
            lampLabel = "REC";
            break;
    }
    dl->AddCircle(lc, lrad, lampOutline, 22, std::max(1.0f, S(s, 0.8f)));
    drawCenteredText(dl, p0, s,
                     Rect{ lr.x0 - 4.0f, lr.y1 + 0.5f, lr.x1 + 4.0f, lr.y1 + 10.5f },
                     8.5f, kLabelTextDim, lampLabel);
}

void CassetteDeck_ImGui::drawCassetteWindow(ImDrawList* dl, ImVec2 p0, float s,
                                            const EmulationSnapshot& snap) const
{
    const Rect r = kCassetteR;
    const ImVec2 a = P(p0, s, r.x0, r.y0);
    const ImVec2 b = P(p0, s, r.x1, r.y1);
    const float round = S(s, 6.0f);

    // Compartment lip (recessed frame).
    const ImVec2 la = P(p0, s, r.x0 - 2.0f, r.y0 - 2.0f);
    const ImVec2 lb = P(p0, s, r.x1 + 2.0f, r.y1 + 2.0f);
    dl->AddRectFilled(la, lb, kCompartmentLip, round + S(s, 1.5f));

    // Glass door.
    dl->AddRectFilled(a, b, kGlassDark, round);
    dl->AddRect(a, b, kGlassEdgeDark, round, 0, std::max(1.0f, S(s, 0.9f)));

    // Inside: either an empty dark slot or a simple cassette body rectangle.
    if (snap.cassetteLoadedTape) {
        // Cassette body centered within the door, with small inset.
        const float pad = 10.0f;
        const ImVec2 ca = P(p0, s, r.x0 + pad,       r.y0 + pad);
        const ImVec2 cb = P(p0, s, r.x1 - pad,       r.y1 - pad);
        dl->AddRectFilled(ca, cb, IM_COL32(212, 210, 202, 255), S(s, 3.0f));
        dl->AddRect(ca, cb, IM_COL32(70, 68, 60, 255), S(s, 3.0f), 0, std::max(1.0f, S(s, 0.9f)));

        // Label strip (top two thirds of the cassette face).
        const float lpad = 18.0f;
        const Rect labelR {
            r.x0 + lpad, r.y0 + lpad,
            r.x1 - lpad, r.y0 + (r.y1 - r.y0) * 0.55f
        };
        const ImVec2 la2 = P(p0, s, labelR.x0, labelR.y0);
        const ImVec2 lb2 = P(p0, s, labelR.x1, labelR.y1);
        dl->AddRectFilled(la2, lb2, IM_COL32(244, 242, 232, 255), S(s, 2.0f));
        dl->AddRect(la2, lb2, IM_COL32(150, 148, 136, 255), S(s, 2.0f), 0, 1.0f);

        // Optional artwork on the right (Apple 50th Anniversary logo).
        // Painted first so text below draws above if regions overlap, and
        // so we can carve out a text-safe rectangle by shrinking labelR.x1.
        float textRightLimit = labelR.x1 - 4.0f;
        if (labelLogoTex_ && labelLogoW_ > 0 && labelLogoH_ > 0) {
            const float logoPad = 4.0f;
            const float logoH = (labelR.y1 - labelR.y0) - 2.0f * logoPad;
            const float logoW = logoH *
                (static_cast<float>(labelLogoW_) / static_cast<float>(labelLogoH_));
            const Rect logoR {
                labelR.x1 - logoPad - logoW, labelR.y0 + logoPad,
                labelR.x1 - logoPad,         labelR.y1 - logoPad
            };
            dl->AddImage(labelLogoTex_,
                         P(p0, s, logoR.x0, logoR.y0),
                         P(p0, s, logoR.x1, logoR.y1));
            textRightLimit = logoR.x0 - 6.0f;
        }

        // Filename (tape title) — extract basename from the full path.
        // Truncate to fit in the text-safe width (accounts for logo space).
        std::string name = snap.cassetteLoadedTapePath;
        const size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        const float textAvail = textRightLimit - (labelR.x0 + 4.0f);
        // Rough glyph width at 22 px ≈ 11 px per char with the default font.
        const size_t maxChars = std::max<size_t>(6, static_cast<size_t>(textAvail / 11.0f));
        // ASCII-only truncation marker — ImGui's default font has no glyph
        // for U+2026 (…) and renders it as "?", so we use three ASCII dots.
        if (name.size() > maxChars) name = name.substr(0, maxChars - 3) + "...";
        drawText(dl, p0, s, labelR.x0 + 4.0f, labelR.y0 + 4.0f, 22.0f,
                 IM_COL32(40, 40, 44, 255), name.c_str());

        // Mode line + mode-specific detail (transitions for pulse-program
        // tapes, duration for audio-stream tapes). Makes it obvious which
        // of the deck's two playback paths is driving the inserted tape.
        char detail[96];
        if (snap.cassetteAudioStreamMode) {
            drawText(dl, p0, s, labelR.x0 + 4.0f, labelR.y0 + 32.0f, 13.0f,
                     IM_COL32(50, 120, 160, 255), "AUDIO STREAM");
            const double total = snap.cassettePlaybackTotalSeconds;
            if (total > 0.0) {
                std::snprintf(detail, sizeof(detail), "%d:%02d",
                              static_cast<int>(total) / 60,
                              static_cast<int>(total) % 60);
            } else {
                std::snprintf(detail, sizeof(detail), "streaming");
            }
            drawText(dl, p0, s, labelR.x0 + 4.0f, labelR.y0 + 50.0f, 15.0f,
                     IM_COL32(96, 96, 100, 255), detail);
        } else {
            drawText(dl, p0, s, labelR.x0 + 4.0f, labelR.y0 + 32.0f, 13.0f,
                     IM_COL32(170, 110, 30, 255), "PROGRAM TAPE");
            if (!snap.cassetteLoadInfo.empty()) {
                std::snprintf(detail, sizeof(detail), "%s",
                              snap.cassetteLoadInfo.c_str());
            } else {
                std::snprintf(detail, sizeof(detail), "%zu transitions",
                              snap.cassetteLoadedTransitionCount);
            }
            drawText(dl, p0, s, labelR.x0 + 4.0f, labelR.y0 + 50.0f, 15.0f,
                     IM_COL32(96, 96, 100, 255), detail);
        }

        // Two hubs (left and right) — static for Phase 1. Phase 2 rotates them.
        const float hubY = r.y0 + (r.y1 - r.y0) * 0.68f;
        const float hubR = 19.0f;
        for (int i = 0; i < 2; ++i) {
            const float hx = (i == 0 ? r.x0 + (r.x1 - r.x0) * 0.28f
                                     : r.x0 + (r.x1 - r.x0) * 0.72f);
            dl->AddCircleFilled(P(p0, s, hx, hubY), S(s, hubR), kHubMid, 28);
            dl->AddCircleFilled(P(p0, s, hx, hubY), S(s, hubR * 0.55f), kHubDark, 20);
        }
    } else {
        // Empty: soft "NO TAPE" hint. ASCII-only — ImGui default font has
        // no em-dash glyph and renders it as a replacement char.
        drawCenteredText(dl, p0, s, r, 12.0f, IM_COL32(100, 100, 108, 200),
                         "NO TAPE - Load tape below");
    }

    // Glass pane — semi-transparent dark overlay drawn AFTER the cassette
    // so the compartment reads as "cassette seen through tinted plastic
    // door" instead of "cassette sitting in front of a dark wall".
    // Opacity kept low (≈16 %) so the label and hubs stay readable; the
    // overlay is skipped when the compartment is empty — nothing to tint
    // behind the glass there, the void should read as a true hollow.
    if (snap.cassetteLoadedTape) {
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 42), round);
        // Subtle inner highlight along the top edge to suggest curved
        // plastic + a tiny bottom shadow for the same reason.
        dl->AddRectFilledMultiColor(
            a, ImVec2(b.x, a.y + S(s, 14.0f)),
            IM_COL32(255, 255, 255, 14), IM_COL32(255, 255, 255, 14),
            IM_COL32(255, 255, 255, 0),  IM_COL32(255, 255, 255, 0));
    }

    // AC/BATTERY FULL AUTO STOP label — silkscreened on the glass surface.
    // Drawn AFTER the tint so it sits ON the outer face, not behind it.
    drawCenteredText(dl, p0, s,
                     Rect{ r.x0, r.y1 - 12.0f, r.x1, r.y1 + 2.0f },
                     9.0f, IM_COL32(220, 220, 224, 220),
                     "AC/BATTERY  FULL AUTO STOP");

    // A faint reflection stripe across the top of the glass.
    const ImVec2 ra = P(p0, s, r.x0 + 4.0f, r.y0 + 2.0f);
    const ImVec2 rb = P(p0, s, r.x1 - 4.0f, r.y0 + 14.0f);
    dl->AddRectFilledMultiColor(ra, rb,
                                kGlassReflect, kGlassReflect,
                                IM_COL32(255,255,255,0), IM_COL32(255,255,255,0));
}

void CassetteDeck_ImGui::drawBrandStrip(ImDrawList* dl, ImVec2 p0, float s) const
{
    const Rect r = kBrandR;
    const ImVec2 a = P(p0, s, r.x0, r.y0);
    const ImVec2 b = P(p0, s, r.x1, r.y1);
    dl->AddRectFilled(a, b, kBrandStrip, S(s, 2.0f));
    dl->AddRect(a, b, kBrandStripEdge, S(s, 2.0f), 0, 1.0f);
    // Centred wordmark.
    drawCenteredText(dl, p0, s, r, 14.0f, kBrandText, "POM1 - CASSETTE DECK");
}

void CassetteDeck_ImGui::drawButtonLabels(ImDrawList* dl, ImVec2 p0, float s) const
{
    static const char* labels[6] = {
        "RECORD", "PLAY", "REW/REV", "FF/CUE", "STOP/EJECT", "PAUSE"
    };
    // Draw a dark underline strip behind the labels to evoke the printed
    // sticker on the real deck.
    const ImVec2 la = P(p0, s, kLabelsR.x0, kLabelsR.y0);
    const ImVec2 lb = P(p0, s, kLabelsR.x1, kLabelsR.y1);
    dl->AddRectFilled(la, lb, IM_COL32(14, 14, 16, 255));

    ImFont* font = ImGui::GetFont();
    if (!font) return;
    const float fs = std::max(8.0f, S(s, 9.5f));
    for (int i = 0; i < 6; ++i) {
        const float cx = kKeyCenterXs[i];
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, labels[i]);
        const ImVec2 pos(
            p0.x + cx * s - ts.x * 0.5f,
            p0.y + (kLabelsR.y0 + 2.0f) * s);
        dl->AddText(font, fs, pos, kLabelText, labels[i]);
    }
}

bool CassetteDeck_ImGui::drawPianoKey(ImDrawList* dl, ImVec2 p0, float s,
                                      float cx, float cy, const char* id,
                                      const char* glyph, bool engaged, bool disabled,
                                      bool* heldOut)
{
    const float hw = kKeyW * 0.5f;
    const float hh = kKeyH * 0.5f;
    const ImVec2 a = P(p0, s, cx - hw, cy - hh);
    const ImVec2 b = P(p0, s, cx + hw, cy + hh);
    const float round = S(s, kKeyRadius);

    // Hitbox BEFORE drawing — so IsItemActive can influence visuals below.
    ImGui::SetCursorScreenPos(a);
    const ImVec2 size(b.x - a.x, b.y - a.y);
    ImGui::InvisibleButton(id, size);
    if (disabled) {
        // Visual-only dim — no hit suppression since our keys never
        // actually disable at Phase 1. Reserved for future interlocks.
    }
    const bool hov     = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();
    const bool clicked = ImGui::IsItemClicked();

    // Engaged (latched) OR held down → shift the cap down a hair and
    // darken it; otherwise draw normal matte-black cap with a top bevel.
    const float latchOffset = (engaged || active) ? 2.0f : 0.0f;
    const ImVec2 capA(a.x, a.y + S(s, latchOffset));
    const ImVec2 capB(b.x, b.y + S(s, latchOffset));

    // Recessed socket behind the cap.
    dl->AddRectFilled(a, ImVec2(b.x, b.y + S(s, 3.0f)), kButtonEdge, round);

    // Cap body.
    dl->AddRectFilled(capA, capB,
                      (engaged || active) ? kButtonDown : kButtonBody, round);
    // Top bevel highlight (only when not pressed). Uses a thin filled rect
    // rather than AddLine: AddLine's AA rasteriser halves the effective
    // alpha for sub-2 px thickness on macOS GL 3.2 / WebGL2 (same reason
    // Screen_ImGui draws CRT scanlines with AddRectFilled).
    if (!engaged && !active) {
        const float by = std::floor(capA.y + S(s, 1.0f)) + 0.5f;
        const float bh = std::max(1.0f, S(s, 0.9f));
        dl->AddRectFilled(ImVec2(capA.x + round, by),
                          ImVec2(capB.x - round, by + bh),
                          kButtonHi);
    }
    // Outline.
    dl->AddRect(capA, capB, kButtonEdge, round, 0, std::max(1.0f, S(s, 0.8f)));
    // Hover tint overlay.
    if (hov && !disabled) {
        dl->AddRect(capA, capB, IM_COL32(255, 255, 255, 28), round, 0,
                    std::max(1.0f, S(s, 1.2f)));
    }

    // Glyph.
    const float gx = cx;
    const float gy = cy + latchOffset;
    const float gh = 9.0f; // design units half-size
    ImU32 col = disabled ? kGlyphDim : kGlyph;
    if (std::strcmp(glyph, "rec") == 0) {
        col = engaged ? kGlyphRecActive : kGlyphRec;
        drawRecGlyph(dl, p0, s, gx, gy, gh, col);
    } else if (std::strcmp(glyph, "play") == 0) {
        drawPlayGlyph(dl, p0, s, gx, gy, gh, col);
    } else if (std::strcmp(glyph, "rew") == 0) {
        drawChevronGlyph(dl, p0, s, gx, gy, gh, col, false);
    } else if (std::strcmp(glyph, "ff") == 0) {
        drawChevronGlyph(dl, p0, s, gx, gy, gh, col, true);
    } else if (std::strcmp(glyph, "stop") == 0) {
        drawStopGlyph(dl, p0, s, gx, gy, gh, col);
    } else if (std::strcmp(glyph, "pause") == 0) {
        drawPauseGlyph(dl, p0, s, gx, gy, gh, col);
    }

    if (heldOut) *heldOut = active && !disabled;
    return clicked && !disabled;
}

// ---------------------------------------------------------------------------
// Transport state machine
// ---------------------------------------------------------------------------

std::string CassetteDeck_ImGui::onRecord(EmulationController* emu,
                                         const EmulationSnapshot& /*snap*/)
{
    // Real mechanical deck: REC alone interlocks PLAY as well. Here we
    // also clear any prior capture so the user gets a fresh recording.
    if (emu) emu->clearTapeCapture();
    transport_ = Transport::Recording;
    paused_    = false;
    rewEndsAt_ = 0.0;
    return "Cassette: REC+PLAY engaged (output capture armed)";
}

std::string CassetteDeck_ImGui::onPlay(EmulationController* emu,
                                       const EmulationSnapshot& snap)
{
    // If we were in REC mode, pressing PLAY keeps REC engaged (mechanical
    // latch-on-both behaviour). If we were winding, this promotes to Play.
    if (transport_ == Transport::Recording) {
        paused_ = false;
        return "Cassette: REC+PLAY";
    }
    if (!snap.cassetteLoadedTape) {
        // Still latch visually so the key stays pressed — real deck does too.
        transport_ = Transport::Playing;
        paused_    = false;
        return "Cassette: PLAY engaged (no tape loaded)";
    }
    if (emu) emu->playTape();
    transport_ = Transport::Playing;
    paused_    = false;
    rewEndsAt_ = 0.0;
    return "Cassette: PLAY - tape rolling";
}

std::string CassetteDeck_ImGui::onRewind(EmulationController* emu,
                                         const EmulationSnapshot& snap)
{
    // Stream mode: ma_decoder_seek is instant, same as before. Pulse
    // mode: rewindTape() now enters a progressive REW state that walks
    // playbackIndex backward at ~20× play speed. The Rewinding latch
    // stays pinned until the device clears isRewinding() — handled in
    // the auto-release block in render().
    if (emu) {
        emu->pauseTape(false);
        emu->rewindTape();
    }
    transport_ = Transport::Rewinding;
    paused_    = false;
    rewEndsAt_ = wallClock_ + kWindDurationSeconds;
    if (!snap.cassetteAudioStreamMode) {
        return "Cassette: REW - tape rewinding...";
    }
    return "Cassette: REW - tape rewound to start";
}

std::string CassetteDeck_ImGui::onFForward(EmulationController* emu,
                                           const EmulationSnapshot& snap)
{
    if (emu) emu->pauseTape(false);
    if (snap.cassetteAudioStreamMode && snap.cassetteLoadedTape) {
        if (emu) emu->seekTapeRelative(+5.0);
        transport_ = Transport::FastForwarding;
        paused_    = false;
        rewEndsAt_ = wallClock_ + kWindDurationSeconds;
        return "Cassette: FF +5s";
    }
    // No real FF target for pulse data — latch visually only.
    transport_ = Transport::FastForwarding;
    paused_    = false;
    rewEndsAt_ = wallClock_ + kWindDurationSeconds;
    return "Cassette: FF (virtual tape has no seek - decorative)";
}

std::string CassetteDeck_ImGui::onStop(EmulationController* emu)
{
    if (emu) emu->stopTape();
    transport_ = Transport::Stopped;
    paused_    = false;
    rewEndsAt_ = 0.0;
    return "Cassette: STOP";
}

std::string CassetteDeck_ImGui::onPause(EmulationController* emu)
{
    // PAUSE only latches when Playing or Recording (real mechanical rule).
    if (transport_ != Transport::Playing && transport_ != Transport::Recording) {
        return {};
    }
    paused_ = !paused_;
    if (emu) emu->pauseTape(paused_);
    return paused_ ? "Cassette: PAUSE" : "Cassette: resume";
}

std::string CassetteDeck_ImGui::onEject(EmulationController* emu,
                                        const EmulationSnapshot& /*snap*/)
{
    // EJECT allowed only from Stopped (guarded by the caller). Ask the
    // device to drop the loaded tape; the snapshot will reflect it next
    // frame.
    if (emu) emu->ejectTape();
    transport_ = Transport::Stopped;
    paused_    = false;
    return "Cassette: EJECT - tape removed";
}

// ---------------------------------------------------------------------------
// Counter / sync
// ---------------------------------------------------------------------------

void CassetteDeck_ImGui::advanceCounter(float deltaSeconds,
                                        const EmulationSnapshot& snap)
{
    if (deltaSeconds <= 0.0f) return;

    // Stream mode: counter mirrors the actual audio cursor. Freezes in
    // PAUSE (position stops advancing), jumps on FF/REW (snapshot reflects
    // the seek on the next publish), resets naturally when PLAY restarts
    // from the beginning. Wallclock path below only runs in ACI pulse mode
    // where there is no measurable position to derive from.
    if (snap.cassetteAudioStreamMode && snap.cassetteLoadedTape) {
        const double ticks = snap.cassettePlaybackPositionSeconds / kCounterPlaySecPerTick;
        counter_ = static_cast<uint32_t>(ticks) % 1000;
        counterAccum_ = 0.0;
        hubAngle_ = std::fmod(hubAngle_ + deltaSeconds * 4.0f, 6.2831853f);
        return;
    }

    double secPerTick = 0.0;
    switch (transport_) {
        case Transport::Playing:
        case Transport::Recording:
            if (paused_) return;
            // Armed = PLAY pressed but the ACI ROM hasn't polled $C081
            // yet. In that window the deck is silent and the counter
            // stays frozen to match the "ARMED — waiting for C100R"
            // banner. As soon as the first poll arrives, armed → active
            // and the counter starts ticking on the next frame.
            if (snap.cassettePlaybackArmed && !snap.cassetteAudioStreamMode) return;
            secPerTick = kCounterPlaySecPerTick;
            break;
        case Transport::Rewinding:
        case Transport::FastForwarding:
            secPerTick = kCounterWindSecPerTick;
            break;
        default:
            return;
    }
    counterAccum_ += static_cast<double>(deltaSeconds) / secPerTick;
    if (counterAccum_ >= 1.0) {
        const uint32_t ticks = static_cast<uint32_t>(counterAccum_);
        // REW walks the counter backward visually; PLAY/REC/FF forward.
        if (transport_ == Transport::Rewinding) {
            // Decrement with underflow → wrap to 999.
            const uint32_t dec = ticks % 1000;
            counter_ = (counter_ + 1000 - dec) % 1000;
        } else {
            counter_ = (counter_ + ticks) % 1000;
        }
        counterAccum_ -= ticks;
    }
    hubAngle_ = std::fmod(hubAngle_ + deltaSeconds * 4.0f, 6.2831853f);
}

void CassetteDeck_ImGui::syncWithSnapshot(const EmulationSnapshot& snap)
{
    // If the device reports playback ended (tape read through), drop the
    // latch back to Stopped. The device auto-stops at the end of the
    // loaded pulse train. Under B6 (play-on-first-read) the deck sits
    // armed-but-inactive between PLAY and the first $C081 poll; that
    // state looks identical to "finished" from a raw playbackActive
    // flag, so we have to explicitly exclude it — otherwise the very
    // next frame after the user clicks PLAY flips the transport back to
    // Stopped and the cassette appears broken.
    if (transport_ == Transport::Playing && !snap.cassettePlaybackActive
        && !snap.cassettePlaybackArmed && !snap.cassetteRewinding
        && snap.cassetteLoadedTape) {
        // Was playing, device says idle: tape finished or stopped externally.
        // Only auto-return to Stopped if we actually had been rolling for
        // a frame — avoids racing on the very first frame after PLAY,
        // where the snapshot may not yet reflect the call.
        if (counterAccum_ > 0.0 || counter_ > 0) {
            transport_ = Transport::Stopped;
            paused_    = false;
        }
    }
    // Ejecting externally (via "Eject" elsewhere) should also reset our
    // latch — no tape to play.
    if (!snap.cassetteLoadedTape
        && (transport_ == Transport::Playing || transport_ == Transport::Recording)) {
        // Keep REC latched even without a loaded tape (real deck does),
        // but drop PLAY to Stopped so the UI reflects reality.
        if (transport_ == Transport::Playing) {
            transport_ = Transport::Stopped;
            paused_    = false;
        }
    }
}

} // namespace pom1
