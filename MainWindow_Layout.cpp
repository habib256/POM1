// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Layout.cpp — definitions of the layout / drawing helpers
// declared in MainWindow_Internal.h. These are pure UI utilities (no
// MainWindow_ImGui state); kept in their own TU so other MainWindow_*.cpp
// files can call them without dragging the full window class along.

#include "MainWindow_Internal.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>

namespace pom1::mainwindow::detail {

float apple1LayoutVerticalChrome()
{
    return ImGui::GetFrameHeight() + kMainMenuBarHeightExtra + kToolbarBandHeight +
           kGapBelowToolbarBeforeApple1 + kStatusBarBandHeight + kApple1WindowDecorationSlop;
}

ImVec2 layoutFitVideoViewport(ImVec2 avail, float nativeW, float nativeH, float& pixelScaleOut)
{
    const float aw = std::max(avail.x, 1.0f);
    const float ah = std::max(avail.y, 1.0f);
    float ps = std::min(aw / nativeW, ah / nativeH);
    ps = std::max(ps, kVideoCardMinPixelScale);
    pixelScaleOut = ps;
    return ImVec2(nativeW * ps, nativeH * ps);
}

void drawToolbarCassetteIcon(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax)
{
    const float pad = 2.5f;
    const ImVec2 a(rmin.x + pad, rmin.y + pad);
    const ImVec2 b(rmax.x - pad, rmax.y - pad);
    const float iw = b.x - a.x;
    const float ih = b.y - a.y;

    const ImU32 outline = IM_COL32(26, 26, 34, 255);
    const ImU32 body    = IM_COL32(228, 229, 236, 255);
    const ImU32 hole    = IM_COL32(72, 74, 86, 255);
    const float round   = 2.5f;

    dl->AddRectFilled(a, b, body, round);
    dl->AddRect(a, b, outline, round, 0, 1.15f);

    const float cy  = a.y + ih * 0.5f;
    const float cxL = a.x + iw * 0.32f;
    const float cxR = a.x + iw * 0.68f;
    const float rad = std::clamp(std::min(iw, ih) * 0.15f, 2.0f, 4.5f);
    dl->AddCircleFilled(ImVec2(cxL, cy), rad, hole);
    dl->AddCircleFilled(ImVec2(cxR, cy), rad, hole);
    dl->AddCircle(ImVec2(cxL, cy), rad, outline, 0, 0.9f);
    dl->AddCircle(ImVec2(cxR, cy), rad, outline, 0, 0.9f);
}

void drawToolbarDipChipIcon(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax)
{
    // Minimal DIP silhouette: a plain black rectangle body with white pin
    // stubs along the top and bottom edges. Vertical orientation — the
    // body runs tall, pins go up and down (like a 28c256 seen from above
    // on a rotated board).
    const float pad = 3.0f;
    const ImVec2 a(rmin.x + pad, rmin.y + pad);
    const ImVec2 b(rmax.x - pad, rmax.y - pad);

    const ImU32 body = IM_COL32(0, 0, 0, 255);
    const ImU32 pin  = IM_COL32(255, 255, 255, 255);

    // Body occupies the central 70 % vertically so pins are visible above
    // and below.
    const float bodyTop    = a.y + (b.y - a.y) * 0.18f;
    const float bodyBottom = b.y - (b.y - a.y) * 0.18f;
    dl->AddRectFilled(ImVec2(a.x, bodyTop), ImVec2(b.x, bodyBottom), body);

    // 5 pins along each horizontal edge. Short vertical stubs in white.
    const int kPins = 5;
    const float pinThick = 1.6f;
    const float pinLen   = (bodyTop - a.y) * 0.5f;
    const float span = b.x - a.x;
    for (int i = 0; i < kPins; ++i) {
        const float t  = (i + 0.5f) / static_cast<float>(kPins);
        const float px = a.x + span * t;
        // top pin (goes from body-top up to icon-top)
        dl->AddLine(ImVec2(px, bodyTop), ImVec2(px, bodyTop - pinLen), pin, pinThick);
        // bottom pin (goes from body-bottom down to icon-bottom)
        dl->AddLine(ImVec2(px, bodyBottom), ImVec2(px, bodyBottom + pinLen), pin, pinThick);
    }
}

void drawToolbarTankIcon(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax)
{
    // Procedural tank silhouette for the P-LAB CodeTank toolbar button.
    // FontAwesome 6 Free has no military-tank glyph, so we paint one from
    // primitives — same approach as drawToolbarCassetteIcon. Components:
    // tread band (bottom 18 %) with road wheels, hull (mid 30 %), turret
    // (top centre 35 %), and a horizontal cannon barrel pointing right.
    // Colours are chosen to read on both the "plugged" blue background
    // and the "unplugged" grey background — body in white, accents in
    // dark hull-shadow.
    const float pad = 2.0f;
    const ImVec2 a(rmin.x + pad, rmin.y + pad);
    const ImVec2 b(rmax.x - pad, rmax.y - pad);
    const float w = b.x - a.x;
    const float h = b.y - a.y;

    const ImU32 hull   = IM_COL32(232, 234, 240, 255);
    const ImU32 shadow = IM_COL32(40, 44, 56, 255);

    // Tread band (bottom). Solid dark rectangle with light road wheels.
    const float treadTop = b.y - h * 0.22f;
    const float treadBot = b.y - h * 0.04f;
    dl->AddRectFilled(ImVec2(a.x + w * 0.02f, treadTop),
                      ImVec2(b.x - w * 0.02f, treadBot),
                      shadow, 1.5f);
    // 4 road wheels evenly spaced.
    const float wheelY = (treadTop + treadBot) * 0.5f;
    const float wheelR = std::clamp(std::min(w, h) * 0.07f, 1.4f, 2.6f);
    const float wxLeft  = a.x + w * 0.14f;
    const float wxRight = b.x - w * 0.14f;
    for (int i = 0; i < 4; ++i) {
        const float t = static_cast<float>(i) / 3.0f;
        const float wx = wxLeft + (wxRight - wxLeft) * t;
        dl->AddCircleFilled(ImVec2(wx, wheelY), wheelR, hull);
    }

    // Hull — trapezoidal-ish silhouette: wide bottom edge along the tread
    // top, slightly inset top edge above. ImDrawList's quad path keeps it
    // crisp at any DPI.
    const float hullTop = a.y + h * 0.42f;
    {
        ImVec2 p0(a.x + w * 0.05f, treadTop);
        ImVec2 p1(b.x - w * 0.05f, treadTop);
        ImVec2 p2(b.x - w * 0.10f, hullTop);
        ImVec2 p3(a.x + w * 0.10f, hullTop);
        dl->AddQuadFilled(p0, p1, p2, p3, hull);
    }

    // Turret — rounded "dome" sitting on top of the hull.
    const float turretLeft  = a.x + w * 0.30f;
    const float turretRight = a.x + w * 0.70f;
    const float turretTop   = a.y + h * 0.18f;
    dl->AddRectFilled(ImVec2(turretLeft, turretTop),
                      ImVec2(turretRight, hullTop),
                      hull, std::min(h * 0.10f, 2.5f));

    // Cannon barrel — points right (hull-side). Horizontal rectangle from
    // the turret's right edge to near the icon's right edge, at turret
    // mid-height. Thickness scales with the icon size so it stays
    // readable at small sizes.
    const float barrelY      = (turretTop + hullTop) * 0.5f;
    const float barrelHalf   = std::clamp(h * 0.05f, 0.9f, 1.6f);
    dl->AddRectFilled(ImVec2(turretRight, barrelY - barrelHalf),
                      ImVec2(b.x - w * 0.04f, barrelY + barrelHalf),
                      hull);
}

void drawToolbarTextLabel(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax, const char* t)
{
    ImFont* font = ImGui::GetFont();
    if (!font || !t || !t[0])
        return;
    const float bh = rmax.y - rmin.y;
    const float bw = rmax.x - rmin.x;
    float fs = std::clamp(bh * 0.56f, 9.5f, 13.5f);
    while (fs > 7.5f) {
        const ImVec2 tsTry = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, t);
        if (tsTry.x <= bw - 2.0f && tsTry.y <= bh - 2.0f)
            break;
        fs -= 0.5f;
    }
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, t);
    const ImVec2 pos(
        rmin.x + (rmax.x - rmin.x - ts.x) * 0.5f,
        rmin.y + (rmax.y - rmin.y - ts.y) * 0.5f - 0.5f);
    dl->AddText(font, fs, pos, ImGui::GetColorU32(ImGuiCol_Text), t);
}

Screen_ImGui::MonitorMode monitorTintAdvance(Screen_ImGui::MonitorMode m)
{
    int n = (static_cast<int>(m) + 1) % kMonitorTintCount;
    return static_cast<Screen_ImGui::MonitorMode>(n);
}

ImVec4 monitorTintSwatchColor(Screen_ImGui::MonitorMode m)
{
    switch (m) {
    case Screen_ImGui::MonitorMode::Green:
        return ImVec4(0.12f, 0.78f, 0.28f, 1.0f);
    case Screen_ImGui::MonitorMode::Amber:
        return ImVec4(0.98f, 0.58f, 0.12f, 1.0f);
    case Screen_ImGui::MonitorMode::Monochrome:
    default:
        return ImVec4(0.9f, 0.9f, 0.92f, 1.0f);
    }
}

const char* monitorTintLabel(Screen_ImGui::MonitorMode m)
{
    switch (m) {
    case Screen_ImGui::MonitorMode::Green:
        return "Green";
    case Screen_ImGui::MonitorMode::Amber:
        return "Brown";
    case Screen_ImGui::MonitorMode::Monochrome:
    default:
        return "Monochrome";
    }
}

bool monitorTintCycleButton(const char* id, const ImVec2& size, Screen_ImGui* screen)
{
    const Screen_ImGui::MonitorMode mode = screen->monitorMode;
    const ImVec4 base = monitorTintSwatchColor(mode);
    const ImVec4 hov(std::min(1.0f, base.x * 1.14f + 0.03f),
                     std::min(1.0f, base.y * 1.14f + 0.03f),
                     std::min(1.0f, base.z * 1.14f + 0.03f), 1.0f);
    const ImVec4 act(base.x * 0.82f, base.y * 0.82f, base.z * 0.82f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, act);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    const bool clicked = ImGui::Button(id, size);
    ImGui::PopStyleColor(4);
    if (clicked) {
        screen->monitorMode = monitorTintAdvance(mode);
    }
    return clicked;
}

} // namespace pom1::mainwindow::detail
