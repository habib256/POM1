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
