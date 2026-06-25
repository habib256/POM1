// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HGR paint editor — see HGRPaintEditor_ImGui.h. Independent reimplementation
// inspired by fadden's HGRTool (concept only).

#include "HGRPaintEditor_ImGui.h"

#include "imgui.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>

using hgrpaint::HgrColor;

namespace {

// Approximate sRGB for each HGR colour, for the palette swatches + tool
// previews (the canvas itself is rendered by GraphicsCard, the source of truth).
ImU32 swatchColor(HgrColor c)
{
    switch (c) {
    case HgrColor::Black:  return IM_COL32(0, 0, 0, 255);
    case HgrColor::White:  return IM_COL32(255, 255, 255, 255);
    case HgrColor::Violet: return IM_COL32(221, 34, 221, 255);
    case HgrColor::Green:  return IM_COL32(20, 245, 60, 255);
    case HgrColor::Blue:   return IM_COL32(34, 102, 255, 255);
    case HgrColor::Orange: return IM_COL32(255, 120, 20, 255);
    }
    return IM_COL32(255, 255, 255, 255);
}

const char* colorName(HgrColor c)
{
    switch (c) {
    case HgrColor::Black:  return "Black";
    case HgrColor::White:  return "White";
    case HgrColor::Violet: return "Violet";
    case HgrColor::Green:  return "Green";
    case HgrColor::Blue:   return "Blue";
    case HgrColor::Orange: return "Orange";
    }
    return "?";
}

} // namespace

HGRPaintEditor_ImGui::HGRPaintEditor_ImGui()
    : shadow(GraphicsCard::kHiresSize, 0)
{
}

HGRPaintEditor_ImGui::~HGRPaintEditor_ImGui()
{
    if (texture != 0) glDeleteTextures(1, &texture);
}

// ─────────────────────────────────────────────────────────────
// Painting primitives (operate on the shadow, emit writes, record undo)
// ─────────────────────────────────────────────────────────────

void HGRPaintEditor_ImGui::beginStroke() { stroke.clear(); }

void HGRPaintEditor_ImGui::commitStroke()
{
    if (stroke.empty()) return;
    undo.push_back(std::move(stroke));
    stroke.clear();
    if (undo.size() > 64) undo.erase(undo.begin());
}

void HGRPaintEditor_ImGui::applyPlot(int x, int y, HgrColor c)
{
    const int off = hgrpaint::targetOffset(x, y, c);
    if (off < 0) return;
    const uint8_t old = shadow[off];
    const int changed = hgrpaint::plotPage(shadow.data(), x, y, c);
    if (changed < 0) return;
    const uint16_t addr = static_cast<uint16_t>(baseAddr() + changed);
    stroke.emplace_back(addr, old);
    if (writeCallback) writeCallback(addr, shadow[changed]);
}

void HGRPaintEditor_ImGui::paintBrush(int cx, int cy, HgrColor c)
{
    const int r = brushSize - 1;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            applyPlot(cx + dx, cy + dy, c);
}

void HGRPaintEditor_ImGui::paintLine(int x0, int y0, int x1, int y1, HgrColor c)
{
    // Bresenham; stamp the brush at each step.
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        paintBrush(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void HGRPaintEditor_ImGui::paintRect(int x0, int y0, int x1, int y1, HgrColor c, bool filled)
{
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    if (filled) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                applyPlot(x, y, c);
    } else {
        paintLine(x0, y0, x1, y0, c);
        paintLine(x0, y1, x1, y1, c);
        paintLine(x0, y0, x0, y1, c);
        paintLine(x1, y0, x1, y1, c);
    }
}

void HGRPaintEditor_ImGui::floodFill(int x, int y, HgrColor c)
{
    if (x < 0 || x > 279 || y < 0 || y > 191) return;
    const bool target = hgrpaint::pixelOn(shadow.data(), x, y);
    // Collect the connected same-state region first (painting flips per-byte
    // high bits, which can recolour neighbours but never changes their on/off
    // state, so the region is stable). 4-connected flood.
    std::vector<uint8_t> seen(static_cast<size_t>(280) * 192, 0);
    std::vector<std::pair<int,int>> stack, region;
    stack.emplace_back(x, y);
    seen[static_cast<size_t>(y) * 280 + x] = 1;
    while (!stack.empty()) {
        auto [px, py] = stack.back();
        stack.pop_back();
        region.emplace_back(px, py);
        const int nb[4][2] = {{px - 1, py}, {px + 1, py}, {px, py - 1}, {px, py + 1}};
        for (auto& n : nb) {
            const int nx = n[0], ny = n[1];
            if (nx < 0 || nx > 279 || ny < 0 || ny > 191) continue;
            const size_t idx = static_cast<size_t>(ny) * 280 + nx;
            if (seen[idx]) continue;
            if (hgrpaint::pixelOn(shadow.data(), nx, ny) != target) continue;
            seen[idx] = 1;
            stack.emplace_back(nx, ny);
        }
    }
    for (auto& p : region) applyPlot(p.first, p.second, c);
}

void HGRPaintEditor_ImGui::doUndo()
{
    if (undo.empty()) return;
    auto ops = std::move(undo.back());
    undo.pop_back();
    // Replay in reverse so multiple touches of the same byte restore correctly.
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        const uint16_t addr = it->first;
        const uint8_t val = it->second;
        const int off = addr - baseAddr();
        if (off >= 0 && off < static_cast<int>(shadow.size())) shadow[off] = val;
        if (writeCallback) writeCallback(addr, val);
    }
}

void HGRPaintEditor_ImGui::clearPage()
{
    beginStroke();
    for (int off = 0; off < static_cast<int>(shadow.size()); ++off) {
        if (shadow[off] != 0) {
            stroke.emplace_back(static_cast<uint16_t>(baseAddr() + off), shadow[off]);
            shadow[off] = 0;
            if (writeCallback) writeCallback(static_cast<uint16_t>(baseAddr() + off), 0);
        }
    }
    commitStroke();
}

// ─────────────────────────────────────────────────────────────
// UI
// ─────────────────────────────────────────────────────────────

void HGRPaintEditor_ImGui::renderToolbar()
{
    // Page select.
    int pageIdx = page2 ? 1 : 0;
    ImGui::TextUnformatted("Page:"); ImGui::SameLine();
    if (ImGui::RadioButton("1 ($2000)", pageIdx == 0)) page2 = false; ImGui::SameLine();
    if (ImGui::RadioButton("2 ($4000)", pageIdx == 1)) page2 = true;

    // Tools.
    const char* toolNames[] = { "Pencil", "Eraser", "Line", "Rect", "Fill" };
    int t = static_cast<int>(tool);
    ImGui::TextUnformatted("Tool:");
    for (int i = 0; i < 5; ++i) {
        ImGui::SameLine();
        if (ImGui::RadioButton(toolNames[i], t == i)) tool = static_cast<Tool>(i);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Filled", &rectFilled);

    // Colour palette.
    ImGui::TextUnformatted("Color:");
    const HgrColor palette[] = { HgrColor::Black, HgrColor::White, HgrColor::Violet,
                                 HgrColor::Green, HgrColor::Blue, HgrColor::Orange };
    for (HgrColor c : palette) {
        ImGui::SameLine();
        const bool selected = (c == color);
        ImGui::PushStyleColor(ImGuiCol_Button, swatchColor(c));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, swatchColor(c));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, swatchColor(c));
        ImVec2 sz(22, 22);
        if (ImGui::Button(selected ? "##selcol" : (std::string("##col") + colorName(c)).c_str(), sz))
            color = c;
        ImGui::PopStyleColor(3);
        if (selected) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                        IM_COL32(255, 255, 0, 255), 0, 0, 2.0f);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", colorName(c));
    }
    ImGui::SameLine();
    ImGui::Text("(%s)", colorName(color));

    // Brush / zoom / display.
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("Brush", &brushSize, 1, 7);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("Zoom", &zoom, 1, 6);
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &showGrid);
    ImGui::SameLine();
    if (ImGui::Checkbox("NTSC color", &ntscColor)) { /* applied in renderCanvas */ }

    ImGui::SameLine();
    if (ImGui::Button("Undo")) doUndo();
    ImGui::SameLine();
    if (ImGui::Button("Clear page")) clearPage();
}

void HGRPaintEditor_ImGui::renderCanvas(const std::vector<uint8_t>& memory)
{
    // Refresh the shadow from live RAM so external program writes show, and so
    // read-modify-write paints start from the current bytes.
    if (memory.size() >= static_cast<size_t>(baseAddr()) + GraphicsCard::kHiresSize)
        std::copy(memory.begin() + baseAddr(),
                  memory.begin() + baseAddr() + GraphicsCard::kHiresSize,
                  shadow.begin());

    // Lazy GL texture (crisp nearest-neighbour, like the GEN2 window).
    if (texture == 0) {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     GraphicsCard::kHiresWidth, GraphicsCard::kHiresHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    // Render the selected page through the exact NTSC pipeline. Synthesize a
    // display state (graphics + hires + page), empty event journal → render()
    // rasterizes that page; page 1 takes the fast path, page 2 the full path.
    gfx.setMonitorMode(static_cast<GraphicsCard::MonitorMode>(
        ntscColor ? 0 /*Colour*/ : 3 /*Monochrome*/));
    GraphicsCard::DisplayState st;
    st.textMode = false; st.mixedMode = false; st.hiRes = true; st.page2 = page2;
    gfx.invalidate();   // always repaint: we may have just poked bytes
    gfx.render(memory.data(), st, st, {});
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    GraphicsCard::kHiresWidth, GraphicsCard::kHiresHeight,
                    GL_RGBA, GL_UNSIGNED_BYTE, gfx.pixels());

    const float scale = static_cast<float>(zoom);
    const ImVec2 imgSize(GraphicsCard::kHiresWidth * scale, GraphicsCard::kHiresHeight * scale);

    ImGui::BeginChild("hgrcanvas", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(uintptr_t)texture, imgSize);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Optional pixel grid (only at high zoom so it stays readable).
    if (showGrid && zoom >= 3) {
        const ImU32 gcol = IM_COL32(80, 80, 80, 90);
        for (int x = 0; x <= GraphicsCard::kHiresWidth; x += 7) {  // byte columns
            const float fx = origin.x + x * scale;
            dl->AddLine(ImVec2(fx, origin.y), ImVec2(fx, origin.y + imgSize.y), gcol);
        }
        for (int y = 0; y <= GraphicsCard::kHiresHeight; y += 8) {
            const float fy = origin.y + y * scale;
            dl->AddLine(ImVec2(origin.x, fy), ImVec2(origin.x + imgSize.x, fy), gcol);
        }
    }

    // Map mouse → logical pixel.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int lx = static_cast<int>((mouse.x - origin.x) / scale);
    int ly = static_cast<int>((mouse.y - origin.y) / scale);
    lx = std::clamp(lx, 0, GraphicsCard::kHiresWidth - 1);
    ly = std::clamp(ly, 0, GraphicsCard::kHiresHeight - 1);

    const HgrColor activeColor = (tool == Tool::Eraser) ? HgrColor::Black : color;

    if (hovered) {
        ImGui::SetTooltip("x=%d y=%d  byte=$%04X", lx, ly, baseAddr() + hgrpaint::hgrByteOffset(lx, ly));

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            dragging = true;
            dragStartX = lx; dragStartY = ly;
            lastX = lx; lastY = ly;
            beginStroke();
            if (tool == Tool::Pencil || tool == Tool::Eraser) paintBrush(lx, ly, activeColor);
            else if (tool == Tool::Fill) { floodFill(lx, ly, activeColor); commitStroke(); dragging = false; }
        }
    }

    if (dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (tool == Tool::Pencil || tool == Tool::Eraser) {
            if (lx != lastX || ly != lastY) {
                paintLine(lastX, lastY, lx, ly, activeColor);   // interpolate fast drags
                lastX = lx; lastY = ly;
            }
        } else if (tool == Tool::Line) {
            const ImVec2 a(origin.x + (dragStartX + 0.5f) * scale, origin.y + (dragStartY + 0.5f) * scale);
            const ImVec2 b(origin.x + (lx + 0.5f) * scale, origin.y + (ly + 0.5f) * scale);
            dl->AddLine(a, b, swatchColor(activeColor), 1.5f);
        } else if (tool == Tool::Rectangle) {
            const ImVec2 a(origin.x + dragStartX * scale, origin.y + dragStartY * scale);
            const ImVec2 b(origin.x + (lx + 1) * scale, origin.y + (ly + 1) * scale);
            if (rectFilled) dl->AddRectFilled(a, b, (swatchColor(activeColor) & 0x00FFFFFF) | 0x80000000);
            else            dl->AddRect(a, b, swatchColor(activeColor), 0, 0, 1.5f);
        }
    }

    if (dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (tool == Tool::Line) paintLine(dragStartX, dragStartY, lx, ly, activeColor);
        else if (tool == Tool::Rectangle) paintRect(dragStartX, dragStartY, lx, ly, activeColor, rectFilled);
        commitStroke();
        dragging = false;
    }

    ImGui::EndChild();
}

void HGRPaintEditor_ImGui::renderFileRow()
{
    ImGui::SetNextItemWidth(-220);
    ImGui::InputTextWithHint("##hgrpath", "path to .HGR / .bin (8 KB)", filePath, sizeof(filePath));
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string err;
        if (loadCallback && loadCallback(filePath, baseAddr(), err))
            status = std::string("Loaded into $") + (page2 ? "4000" : "2000");
        else
            status = "Load failed: " + err;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string err;
        if (saveCallback && saveCallback(filePath, baseAddr(), err))
            status = "Saved 8 KB HGR image";
        else
            status = "Save failed: " + err;
    }
    if (!status.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", status.c_str()); }
}

void HGRPaintEditor_ImGui::render(const std::vector<uint8_t>& memory)
{
    renderToolbar();
    ImGui::Separator();
    renderFileRow();
    ImGui::Separator();
    renderCanvas(memory);
}
