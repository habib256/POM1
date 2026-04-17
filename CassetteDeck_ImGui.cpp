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
constexpr float kDesignH = 640.0f;

// ---------------------------------------------------------------------------
// Palette — matte-black consumer tape deck, late 80s / early 90s look.
// ---------------------------------------------------------------------------
constexpr ImU32 kChassis        = IM_COL32( 24,  24,  28, 255);
constexpr ImU32 kChassisEdgeLo  = IM_COL32(  6,   6,   8, 255);
constexpr ImU32 kChassisEdgeHi  = IM_COL32( 56,  56,  62, 255);
constexpr ImU32 kGrilleBg       = IM_COL32( 18,  18,  22, 255);
constexpr ImU32 kSlatLight      = IM_COL32(186, 188, 192, 255);
constexpr ImU32 kSlatShadow     = IM_COL32( 96,  98, 104, 255);
constexpr ImU32 kSlatGap        = IM_COL32( 10,  10,  12, 255);
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
constexpr Rect kGrilleR     {   9.0f,  14.0f, 369.0f, 240.0f };
constexpr Rect kCounterBarR {   9.0f, 250.0f, 369.0f, 306.0f };
constexpr Rect kBrandBadgeR { 306.0f, 266.0f, 360.0f, 292.0f };
constexpr Rect kCounterWinR { 148.0f, 262.0f, 212.0f, 296.0f };
constexpr Rect kRecLedR     { 288.0f, 270.0f, 300.0f, 288.0f };
constexpr Rect kCassetteR   {  18.0f, 316.0f, 360.0f, 478.0f };
constexpr Rect kBrandR      {  14.0f, 488.0f, 364.0f, 516.0f };
constexpr Rect kLabelsR     {  18.0f, 520.0f, 360.0f, 538.0f };

constexpr float kKeyW      = 47.0f;
constexpr float kKeyH      = 64.0f;
constexpr float kKeyRadius = 5.0f;
constexpr float kKeysTop   = 544.0f;
constexpr float kKeyCenterXs[6] = { 44.0f, 102.0f, 160.0f, 218.0f, 276.0f, 334.0f };

// Horizontal MIC switch in the counter bar, to the LEFT of the COUNTER
// window. Slot runs left→right; the knob slides along X (off = left,
// on = right). Centred on the bar's vertical midline (y=278) with the
// "MIC" label sitting just below in the gap before the cassette window.
constexpr float kMicSwitchX0 = 14.0f;
constexpr float kMicSwitchY0 = 272.0f;
constexpr float kMicSwitchW  = 46.0f;
constexpr float kMicSwitchH  = 13.0f;

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
    advanceCounter(deltaSeconds);

    // Auto-release REW/FF after a short interval.
    if ((transport_ == Transport::Rewinding || transport_ == Transport::FastForwarding)
         && wallClock_ >= rewEndsAt_) {
        transport_ = Transport::Stopped;
    }

    // Header row — three big square icon buttons above the deck. Font
    // Awesome glyphs pumped up via a temporary window font scale so the
    // icons look chunky and centred inside the 56×56 squares.
    constexpr float kActionBtnSize = 56.0f;
    constexpr float kActionIconScale = 2.0f;
    const ImVec2 actionSize(kActionBtnSize, kActionBtnSize);
    ImGui::SetWindowFontScale(kActionIconScale);
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "##DeckLoad", actionSize)) {
        out.requestLoadDialog = true;
    }
    ImGui::SetWindowFontScale(1.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load tape (ACI / WAV / MP3 / OGG / FLAC)");

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

    // Compact live status line under the buttons.
    char headerInfo[256];
    std::snprintf(headerInfo, sizeof(headerInfo),
                  "in %zu tr  |  out %zu tr  |  audio %s",
                  snap.cassetteLoadedTransitionCount,
                  snap.cassetteRecordedTransitionCount,
                  snap.cassetteAudioAvailable ? "active" : "off");
    ImGui::TextDisabled("%s", headerInfo);
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
    drawSpeakerGrille(dl, p0, s);

    // Counter bar content: slim line badge, counter window, tiny reset button.
    drawSlimLineBadge(dl, p0, s);
    bool counterResetClicked = false;
    drawCounter(dl, p0, s, "##CounterReset", counterResetClicked);
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
    const bool hasTape = snap.cassetteLoadedTape;

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
    // REW
    if (drawPianoKey(dl, p0, s, kKeyCenterXs[2], kKeysTop + kKeyH * 0.5f,
                     "##DeckKeyRew", "rew", rewKeyEngaged(), false)) {
        std::string msg = onRewind(emulation, snap);
        if (!msg.empty()) out.statusMessage = std::move(msg);
    }
    // FF
    if (drawPianoKey(dl, p0, s, kKeyCenterXs[3], kKeysTop + kKeyH * 0.5f,
                     "##DeckKeyFF", "ff", ffKeyEngaged(), false)) {
        std::string msg = onFForward(emulation, snap);
        if (!msg.empty()) out.statusMessage = std::move(msg);
    }
    // STOP / EJECT — single-press STOP; double-click (or click while Stopped) ejects.
    const bool stopDisabled = false;
    if (drawPianoKey(dl, p0, s, kKeyCenterXs[4], kKeysTop + kKeyH * 0.5f,
                     "##DeckKeyStop", "stop", stopKeyEngaged(), stopDisabled)) {
        // If already Stopped and a tape is loaded, this press means EJECT.
        std::string msg;
        if (transport_ == Transport::Stopped && !paused_ && hasTape) {
            msg = onEject(emulation, snap);
        } else {
            msg = onStop(emulation);
        }
        if (!msg.empty()) out.statusMessage = std::move(msg);
    }
    // PAUSE
    if (drawPianoKey(dl, p0, s, kKeyCenterXs[5], kKeysTop + kKeyH * 0.5f,
                     "##DeckKeyPause", "pause", pauseKeyEngaged(), false)) {
        std::string msg = onPause();
        if (!msg.empty()) out.statusMessage = std::move(msg);
    }

    // MIC switch (decorative: sets hardware-accurate live audio mode).
    drawMicSwitch(dl, p0, s, "##DeckMicSwitch");

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

void CassetteDeck_ImGui::drawSpeakerGrille(ImDrawList* dl, ImVec2 p0, float s) const
{
    const Rect r = kGrilleR;
    const ImVec2 a = P(p0, s, r.x0, r.y0);
    const ImVec2 b = P(p0, s, r.x1, r.y1);
    const float round = S(s, 6.0f);

    // Recessed bed behind the slats.
    dl->AddRectFilled(a, b, kGrilleBg, round);
    // Inner shadow ring.
    dl->AddRect(ImVec2(a.x + S(s, 1.0f), a.y + S(s, 1.0f)),
                ImVec2(b.x - S(s, 1.0f), b.y - S(s, 1.0f)),
                kSlatGap, round, 0, S(s, 1.0f));

    // Horizontal slats. Slat height in design units ≈ 2.4; gap ≈ 3.2.
    // Keeping gap > slat thickness gives the "brushed metal between dark
    // gaps" look rather than the opposite.
    const float slatStep   = 5.6f;
    const float slatHeight = 2.4f;
    const float innerPadX  = 8.0f;
    const float innerPadY  = 10.0f;
    const float y0 = r.y0 + innerPadY;
    const float y1 = r.y1 - innerPadY;
    for (float y = y0; y + slatHeight <= y1; y += slatStep) {
        const ImVec2 p = P(p0, s, r.x0 + innerPadX, y);
        const ImVec2 q = P(p0, s, r.x1 - innerPadX, y + slatHeight);
        // Main slat — brushed highlight then thin shadow underneath for
        // depth. Integer pixel snapping on Y keeps rows consistent at any
        // scale (same trick as Screen_ImGui's CRT scanlines).
        ImVec2 ps(p.x, std::floor(p.y) + 0.5f);
        ImVec2 qs(q.x, std::floor(q.y) + 0.5f);
        dl->AddRectFilled(ps, qs, kSlatLight);
        const float shY = std::floor(q.y) + 0.5f;
        dl->AddRectFilled(ImVec2(p.x, shY),
                          ImVec2(q.x, shY + std::max(1.0f, S(s, 0.9f))),
                          kSlatShadow);
    }

    // Subtle inner vignette — darker stripe mid-grille for depth.
    const ImVec2 va = P(p0, s, r.x0 + 10.0f, r.y0 + (r.y1 - r.y0) * 0.5f - 8.0f);
    const ImVec2 vb = P(p0, s, r.x1 - 10.0f, r.y0 + (r.y1 - r.y0) * 0.5f + 8.0f);
    dl->AddRectFilledMultiColor(va, vb,
                                IM_COL32(0,0,0,32), IM_COL32(0,0,0,32),
                                IM_COL32(0,0,0, 0), IM_COL32(0,0,0, 0));
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
                                     const char* resetId, bool& resetClicked)
{
    // "COUNTER" label to the left of the window (aligned on the window's
    // vertical centre).
    drawText(dl, p0, s, 98.0f, 273.0f, 9.0f, kLabelTextDim, "COUNTER");

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

    // REC LED — small round lamp just right of the reset button. Lit red
    // while transport is Recording, otherwise a dim burgundy (off state).
    const Rect lr = kRecLedR;
    const ImVec2 lc = P(p0, s, (lr.x0 + lr.x1) * 0.5f, (lr.y0 + lr.y1) * 0.5f);
    const float lrad = S(s, (lr.y1 - lr.y0) * 0.45f);
    const bool recActive = (transport_ == Transport::Recording && !paused_);
    if (recActive) {
        // Soft outer bloom before the core dot for a glow feel.
        dl->AddCircleFilled(lc, lrad * 1.6f, IM_COL32(232, 56, 44, 40), 22);
        dl->AddCircleFilled(lc, lrad,        IM_COL32(232, 56, 44, 255), 22);
    } else {
        dl->AddCircleFilled(lc, lrad, IM_COL32(48, 14, 12, 255), 18);
    }
    dl->AddCircle(lc, lrad, IM_COL32(8, 8, 10, 255), 22, std::max(1.0f, S(s, 0.8f)));
    // Tiny "REC" label under the LED.
    drawText(dl, p0, s, lr.x0 - 3.0f, lr.y1 + 1.0f, 7.0f, kLabelTextDim, "REC");
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

        // Filename (tape title) — extract basename from the full path.
        std::string name = snap.cassetteLoadedTapePath;
        const size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (name.size() > 28) name = name.substr(0, 27) + "…";
        drawText(dl, p0, s, labelR.x0 + 4.0f, labelR.y0 + 4.0f, 10.0f,
                 IM_COL32(40, 40, 44, 255), name.c_str());

        // Transition count — a stand-in for Phase 2 metadata (LOAD/RUN).
        char detail[96];
        std::snprintf(detail, sizeof(detail), "transitions: %zu",
                      snap.cassetteLoadedTransitionCount);
        drawText(dl, p0, s, labelR.x0 + 4.0f, labelR.y0 + 22.0f, 9.0f,
                 IM_COL32(96, 96, 100, 255), detail);

        // Two hubs (left and right) — static for Phase 1. Phase 2 rotates them.
        const float hubY = r.y0 + (r.y1 - r.y0) * 0.78f;
        const float hubR = 14.0f;
        for (int i = 0; i < 2; ++i) {
            const float hx = (i == 0 ? r.x0 + (r.x1 - r.x0) * 0.28f
                                     : r.x0 + (r.x1 - r.x0) * 0.72f);
            dl->AddCircleFilled(P(p0, s, hx, hubY), S(s, hubR), kHubMid, 24);
            dl->AddCircleFilled(P(p0, s, hx, hubY), S(s, hubR * 0.55f), kHubDark, 18);
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
                     Rect{ r.x0, r.y1 - 18.0f, r.x1, r.y1 - 4.0f },
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
    drawCenteredText(dl, p0, s, r, 14.0f, kBrandText, "POM1 · CASSETTE DECK");
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
                                      const char* glyph, bool engaged, bool disabled)
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

    return clicked && !disabled;
}

void CassetteDeck_ImGui::drawMicSwitch(ImDrawList* dl, ImVec2 p0, float s,
                                       const char* id)
{
    const float x0 = kMicSwitchX0;
    const float y0 = kMicSwitchY0;
    const float x1 = x0 + kMicSwitchW;
    const float y1 = y0 + kMicSwitchH;
    const ImVec2 a = P(p0, s, x0, y0);
    const ImVec2 b = P(p0, s, x1, y1);
    const float round = S(s, 2.0f);

    ImGui::SetCursorScreenPos(a);
    ImGui::InvisibleButton(id, ImVec2(b.x - a.x, b.y - a.y));
    const bool hov     = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    if (clicked) micSwitch_ = !micSwitch_;

    // Body — recessed track.
    dl->AddRectFilled(a, b, IM_COL32(10, 10, 12, 255), round);
    dl->AddRect(a, b, kButtonEdge, round, 0, 1.0f);

    // Horizontal switch: knob is a vertical bar sliding along X. Flip
    // left = off, right = on, matching the "MIC / LINE" reading order.
    const float knobW = 14.0f;
    const float knobX = micSwitch_ ? x1 - 4.0f - knobW : x0 + 4.0f;
    const ImVec2 ka = P(p0, s, knobX,           y0 + 2.0f);
    const ImVec2 kb = P(p0, s, knobX + knobW,   y1 - 2.0f);
    dl->AddRectFilled(ka, kb,
                      micSwitch_ ? IM_COL32(198, 200, 204, 255)
                                 : IM_COL32(150, 152, 156, 255),
                      S(s, 1.5f));
    dl->AddRect(ka, kb, IM_COL32(60, 60, 64, 255), S(s, 1.5f), 0, 1.0f);

    // "MIC" label beneath the switch, in the thin chassis band between
    // the counter bar (ends at y=306) and the cassette compartment
    // (starts at y=316).
    drawCenteredText(dl, p0, s,
                     Rect{ x0 - 2.0f, y1 + 2.0f, x1 + 2.0f, y1 + 14.0f },
                     7.5f, kLabelTextDim, "MIC");

    if (hov) {
        ImGui::SetTooltip("MIC / LINE selector (decorative)");
    }
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
    return "Cassette: PLAY — tape rolling";
}

std::string CassetteDeck_ImGui::onRewind(EmulationController* emu,
                                         const EmulationSnapshot& /*snap*/)
{
    if (emu) emu->rewindTape();
    transport_ = Transport::Rewinding;
    paused_    = false;
    rewEndsAt_ = wallClock_ + kWindDurationSeconds;
    return "Cassette: REW — tape rewound to start";
}

std::string CassetteDeck_ImGui::onFForward(EmulationController* /*emu*/,
                                           const EmulationSnapshot& /*snap*/)
{
    // No real FF target in virtual tape — latch visually for feedback.
    transport_ = Transport::FastForwarding;
    paused_    = false;
    rewEndsAt_ = wallClock_ + kWindDurationSeconds;
    return "Cassette: FF (virtual tape has no seek — decorative)";
}

std::string CassetteDeck_ImGui::onStop(EmulationController* /*emu*/)
{
    transport_ = Transport::Stopped;
    paused_    = false;
    rewEndsAt_ = 0.0;
    return "Cassette: STOP";
}

std::string CassetteDeck_ImGui::onPause()
{
    // PAUSE only latches when Playing or Recording (real mechanical rule).
    if (transport_ != Transport::Playing && transport_ != Transport::Recording) {
        return {};
    }
    paused_ = !paused_;
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
    return "Cassette: EJECT — tape removed";
}

// ---------------------------------------------------------------------------
// Counter / sync
// ---------------------------------------------------------------------------

void CassetteDeck_ImGui::advanceCounter(float deltaSeconds)
{
    if (deltaSeconds <= 0.0f) return;
    double secPerTick = 0.0;
    switch (transport_) {
        case Transport::Playing:
        case Transport::Recording:
            if (paused_) return;
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
    // Hub rotation (reserved for Phase 2 — advance a phase angle so we
    // are ready to draw it later without another timing pipeline).
    hubAngle_ = std::fmod(hubAngle_ + deltaSeconds * 4.0f, 6.2831853f);
}

void CassetteDeck_ImGui::syncWithSnapshot(const EmulationSnapshot& snap)
{
    // If the device reports playback ended (tape read through), drop the
    // latch back to Stopped. The device auto-stops at the end of the
    // loaded pulse train.
    if (transport_ == Transport::Playing && !snap.cassettePlaybackActive
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
