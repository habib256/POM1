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

// Fixed zoom ladder (HGR-09), 1x .. 16x. Mouse-wheel + Fit step the index.
const int kZoomLadder[] = { 1, 2, 3, 4, 6, 8, 12, 16 };
const int kZoomLadderCount = static_cast<int>(sizeof(kZoomLadder) / sizeof(kZoomLadder[0]));

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
    // A fresh edit invalidates any redo history.
    redo.clear();
}

void HGRPaintEditor_ImGui::applyPlot(int x, int y, HgrColor c)
{
    const int off = hgrpaint::targetOffset(x, y, c);
    if (off < 0) return;
    const uint8_t old = shadow[off];
    const int changed = hgrpaint::plotPage(shadow.data(), x, y, c);
    if (changed < 0) return;
    const uint16_t addr = static_cast<uint16_t>(baseAddr() + changed);
    stroke.push_back({addr, old, shadow[changed]});
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

void HGRPaintEditor_ImGui::paintEllipse(int x0, int y0, int x1, int y1, HgrColor c, bool filled)
{
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    const int a = (x1 - x0) / 2;       // semi-axes
    const int b = (y1 - y0) / 2;
    const int cx = x0 + a;             // centre
    const int cy = y0 + b;

    // Degenerate boxes: fall back to a point / line so 1px drags still draw.
    if (a <= 0 && b <= 0) { applyPlot(cx, cy, c); return; }
    if (a <= 0) { paintLine(cx, y0, cx, y1, c); return; }
    if (b <= 0) { paintLine(x0, cy, x1, cy, c); return; }

    // Plot the four symmetric points (outline) or two horizontal spans (fill)
    // for ellipse coordinates (ex, ey) relative to the centre.
    auto emit = [&](int ex, int ey) {
        if (filled) {
            for (int x = cx - ex; x <= cx + ex; ++x) {
                applyPlot(x, cy + ey, c);
                applyPlot(x, cy - ey, c);
            }
        } else {
            applyPlot(cx + ex, cy + ey, c);
            applyPlot(cx - ex, cy + ey, c);
            applyPlot(cx + ex, cy - ey, c);
            applyPlot(cx - ex, cy - ey, c);
        }
    };

    // Midpoint ellipse algorithm.
    const long a2 = static_cast<long>(a) * a;
    const long b2 = static_cast<long>(b) * b;
    long x = 0, y = b;
    long dx = 0, dy = 2 * a2 * y;
    long d1 = b2 - a2 * b + a2 / 4;
    emit(static_cast<int>(x), static_cast<int>(y));
    // Region 1.
    while (dx < dy) {
        x++;
        dx += 2 * b2;
        if (d1 < 0) {
            d1 += b2 + dx;
        } else {
            y--;
            dy -= 2 * a2;
            d1 += b2 + dx - dy;
        }
        emit(static_cast<int>(x), static_cast<int>(y));
    }
    // Region 2.
    long d2 = b2 * (x * 2 + 1) * (x * 2 + 1) / 4 + a2 * (y - 1) * (y - 1) - a2 * b2;
    while (y > 0) {
        y--;
        dy -= 2 * a2;
        if (d2 > 0) {
            d2 += a2 - dy;
        } else {
            x++;
            dx += 2 * b2;
            d2 += a2 - dy + dx;
        }
        emit(static_cast<int>(x), static_cast<int>(y));
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

void HGRPaintEditor_ImGui::applyOps(const std::vector<ByteEdit>& ops, bool forward)
{
    // forward = redo (write newVal, in recorded order); reverse = undo (write
    // oldVal, in reverse order so repeated touches of the same byte unwind to
    // the correct earliest value — see commitStroke ordering).
    auto write = [&](const ByteEdit& e, uint8_t val) {
        const int off = e.addr - baseAddr();
        if (off >= 0 && off < static_cast<int>(shadow.size())) shadow[off] = val;
        if (writeCallback) writeCallback(e.addr, val);
    };
    if (forward) {
        for (const auto& e : ops) write(e, e.newVal);
    } else {
        for (auto it = ops.rbegin(); it != ops.rend(); ++it) write(*it, it->oldVal);
    }
}

void HGRPaintEditor_ImGui::doUndo()
{
    if (undo.empty()) return;
    auto ops = std::move(undo.back());
    undo.pop_back();
    applyOps(ops, false);
    redo.push_back(std::move(ops));
    if (redo.size() > 64) redo.erase(redo.begin());
}

void HGRPaintEditor_ImGui::doRedo()
{
    if (redo.empty()) return;
    auto ops = std::move(redo.back());
    redo.pop_back();
    applyOps(ops, true);
    undo.push_back(std::move(ops));
    if (undo.size() > 64) undo.erase(undo.begin());
}

void HGRPaintEditor_ImGui::clearPage()
{
    beginStroke();
    for (int off = 0; off < static_cast<int>(shadow.size()); ++off) {
        if (shadow[off] != 0) {
            const uint16_t addr = static_cast<uint16_t>(baseAddr() + off);
            stroke.push_back({addr, shadow[off], 0});
            shadow[off] = 0;
            if (writeCallback) writeCallback(addr, 0);
        }
    }
    commitStroke();
}

// ─────────────────────────────────────────────────────────────
// Selection / clipboard (HGR-06) and palette-shift (HGR-11)
// ─────────────────────────────────────────────────────────────

void HGRPaintEditor_ImGui::copySelection(bool cut)
{
    if (!hasSel) return;
    const int x0 = std::min(selX0, selX1), x1 = std::max(selX0, selX1);
    const int y0 = std::min(selY0, selY1), y1 = std::max(selY0, selY1);
    clip.w = x1 - x0 + 1;
    clip.h = y1 - y0 + 1;
    clip.px.assign(static_cast<size_t>(clip.w) * clip.h, HgrColor::Black);
    // Store LOGICAL colours so paste re-snaps parity at any destination column.
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            clip.px[static_cast<size_t>(y - y0) * clip.w + (x - x0)] =
                hgrpaint::colorAt(shadow.data(), x, y);
    if (cut) {
        beginStroke();
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                applyPlot(x, y, HgrColor::Black);
        commitStroke();
    }
}

void HGRPaintEditor_ImGui::pasteFloatingAt(int destX, int destY)
{
    if (clip.w <= 0) return;
    beginStroke();
    for (int y = 0; y < clip.h; ++y)
        for (int x = 0; x < clip.w; ++x) {
            const HgrColor c = clip.px[static_cast<size_t>(y) * clip.w + x];
            if (c == HgrColor::Black) continue;   // black = transparent (overlay paste)
            applyPlot(destX + x, destY + y, c);
        }
    commitStroke();
}

void HGRPaintEditor_ImGui::paintPaletteByte(int lx, int ly)
{
    if (lx < 0 || lx > 279 || ly < 0 || ly > 191) return;
    const int byteCol = lx / 7;
    const int off = hgrpaint::hgrByteOffset(0, ly) + byteCol;
    const uint8_t old = shadow[off];
    const int ch = hgrpaint::setBytePalette(shadow.data(), byteCol, ly, paletteMsbMode);
    if (ch < 0) return;
    const uint16_t addr = static_cast<uint16_t>(baseAddr() + ch);
    stroke.push_back({addr, old, shadow[ch]});
    if (writeCallback) writeCallback(addr, shadow[ch]);
}

// ─────────────────────────────────────────────────────────────
// UI
// ─────────────────────────────────────────────────────────────

void HGRPaintEditor_ImGui::renderMinimap(float canvasOriginX, float canvasOriginY, float scale)
{
    // Navigator overlay (HGR-10): a 0.5x-of-logical thumbnail pinned to the
    // canvas child's top-right, with a rectangle marking the visible viewport.
    // Click/drag recentres the view (applied next frame via pendingScroll).
    (void)canvasOriginX; (void)canvasOriginY;
    const float mmW = 140.0f, mmH = 96.0f;   // 280x192 / 2
    const ImVec2 wpos = ImGui::GetWindowPos();
    const ImVec2 wsize = ImGui::GetWindowSize();
    // Only useful (and only drawn) when the image overflows the viewport.
    const float imgW = GraphicsCard::kHiresWidth * scale;
    const float imgH = GraphicsCard::kHiresHeight * scale;
    if (!showMinimap || (imgW <= wsize.x + 1.0f && imgH <= wsize.y + 1.0f)) return;

    const ImVec2 mmMin(wpos.x + wsize.x - mmW - 10.0f, wpos.y + 10.0f);
    const ImVec2 mmMax(mmMin.x + mmW, mmMin.y + mmH);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(mmMin.x - 2, mmMin.y - 2), ImVec2(mmMax.x + 2, mmMax.y + 2),
                      IM_COL32(0, 0, 0, 200));
    dl->AddImage((ImTextureID)(uintptr_t)texture, mmMin, mmMax);
    dl->AddRect(ImVec2(mmMin.x - 2, mmMin.y - 2), ImVec2(mmMax.x + 2, mmMax.y + 2),
                IM_COL32(160, 160, 160, 255));

    // Viewport rectangle (logical → minimap).
    const float sx = ImGui::GetScrollX(), sy = ImGui::GetScrollY();
    const float lx0 = (sx / scale), ly0 = (sy / scale);
    const float lx1 = ((sx + wsize.x) / scale), ly1 = ((sy + wsize.y) / scale);
    auto mmx = [&](float lx){ return mmMin.x + (lx / GraphicsCard::kHiresWidth)  * mmW; };
    auto mmy = [&](float ly){ return mmMin.y + (ly / GraphicsCard::kHiresHeight) * mmH; };
    dl->AddRect(ImVec2(mmx(lx0), mmy(ly0)), ImVec2(mmx(lx1), mmy(ly1)),
                IM_COL32(255, 255, 0, 230), 0, 0, 1.5f);

    // Click/drag inside the minimap recentres the viewport.
    const ImVec2 m = ImGui::GetIO().MousePos;
    const bool inside = m.x >= mmMin.x && m.x <= mmMax.x && m.y >= mmMin.y && m.y <= mmMax.y;
    if (inside && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float tlx = (m.x - mmMin.x) / mmW * GraphicsCard::kHiresWidth;
        const float tly = (m.y - mmMin.y) / mmH * GraphicsCard::kHiresHeight;
        pendingScrollX = std::clamp(tlx * scale - wsize.x * 0.5f, 0.0f, std::max(0.0f, imgW - wsize.x));
        pendingScrollY = std::clamp(tly * scale - wsize.y * 0.5f, 0.0f, std::max(0.0f, imgH - wsize.y));
    }
}

void HGRPaintEditor_ImGui::renderToolbar()
{
    // Page select.
    int pageIdx = page2 ? 1 : 0;
    ImGui::TextUnformatted("Page:"); ImGui::SameLine();
    if (ImGui::RadioButton("1 ($2000)", pageIdx == 0)) page2 = false; ImGui::SameLine();
    if (ImGui::RadioButton("2 ($4000)", pageIdx == 1)) page2 = true;

    // Tools.
    const char* toolNames[] = { "Pencil", "Eraser", "Line", "Rect", "Ellipse",
                                "Fill", "Eyedropper", "Select", "Palette" };
    const int kToolCount = 9;
    int t = static_cast<int>(tool);
    ImGui::TextUnformatted("Tool:");
    for (int i = 0; i < kToolCount; ++i) {
        ImGui::SameLine();
        if (ImGui::RadioButton(toolNames[i], t == i)) { prevTool = tool; tool = static_cast<Tool>(i); }
    }
    ImGui::SameLine();
    ImGui::Checkbox("Filled", &rectFilled);
    // Palette-shift sub-mode (HGR-11): flips a whole byte's high bit, recolouring
    // its 7 pixels without touching pixel bits.
    if (tool == Tool::PaletteShift) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        ImGui::Combo("MSB", &paletteMsbMode, "Clear (->green/violet)\0Set (->orange/blue)\0Toggle\0");
    }
    // Selection clipboard actions.
    if (tool == Tool::Select) {
        ImGui::SameLine();
        if (ImGui::Button("Copy"))  copySelection(false);
        ImGui::SameLine();
        if (ImGui::Button("Cut"))   copySelection(true);
        ImGui::SameLine();
        if (ImGui::Button("Paste") && clip.w > 0) { pasting = true; pasteX = std::min(selX0, selX1); pasteY = std::min(selY0, selY1); }
    }

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
    // Zoom is a fixed ladder (1..16x); the slider drives the index.
    ImGui::SetNextItemWidth(120);
    int zi = zoomIdx;
    if (ImGui::SliderInt("Zoom", &zi, 0, kZoomLadderCount - 1, ""))
        zoomIdx = zi;
    ImGui::SameLine();
    ImGui::Text("%dx", kZoomLadder[zoomIdx]);
    ImGui::SameLine();
    if (ImGui::Button("Fit")) wantFit = true;
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &showGrid);
    ImGui::SameLine();
    ImGui::Checkbox("Seams", &showConflicts);
    ImGui::SameLine();
    ImGui::Checkbox("Map", &showMinimap);
    ImGui::SameLine();
    if (ImGui::Checkbox("NTSC color", &ntscColor)) { /* applied in renderCanvas */ }

    ImGui::SameLine();
    if (ImGui::Button("Undo")) doUndo();
    ImGui::SameLine();
    if (ImGui::Button("Redo")) doRedo();
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

    float scale = static_cast<float>(kZoomLadder[zoomIdx]);
    ImVec2 imgSize(GraphicsCard::kHiresWidth * scale, GraphicsCard::kHiresHeight * scale);

    ImGui::BeginChild("hgrcanvas", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    // Apply a scroll requested by the minimap last frame (HGR-10).
    if (pendingScrollX >= 0) { ImGui::SetScrollX(pendingScrollX); pendingScrollX = -1; }
    if (pendingScrollY >= 0) { ImGui::SetScrollY(pendingScrollY); pendingScrollY = -1; }

    // ── Zoom-to-fit (HGR-09): pick the largest ladder step that fits ─────────
    if (wantFit) {
        wantFit = false;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        int best = 0;
        for (int i = 0; i < kZoomLadderCount; ++i) {
            if (GraphicsCard::kHiresWidth  * kZoomLadder[i] <= avail.x &&
                GraphicsCard::kHiresHeight * kZoomLadder[i] <= avail.y)
                best = i;
        }
        zoomIdx = best;
        scale = static_cast<float>(kZoomLadder[zoomIdx]);
        imgSize = ImVec2(GraphicsCard::kHiresWidth * scale, GraphicsCard::kHiresHeight * scale);
    }

    // ── Mouse-wheel zoom (HGR-09): step the ladder, recentre on the cursor ──
    {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f) {
            const int oldZoom = kZoomLadder[zoomIdx];
            const int ni = std::clamp(zoomIdx + (io.MouseWheel > 0 ? 1 : -1),
                                      0, kZoomLadderCount - 1);
            if (ni != zoomIdx) {
                // Logical pixel currently under the cursor, in the OLD scale.
                const ImVec2 cur = ImGui::GetCursorScreenPos();
                const float anchorLX = (io.MousePos.x - cur.x) / oldZoom;
                const float anchorLY = (io.MousePos.y - cur.y) / oldZoom;
                zoomIdx = ni;
                const int newZoom = kZoomLadder[zoomIdx];
                scale = static_cast<float>(newZoom);
                imgSize = ImVec2(GraphicsCard::kHiresWidth * scale, GraphicsCard::kHiresHeight * scale);
                // Keep that logical pixel under the mouse after rescaling.
                const float mouseInChildX = io.MousePos.x - cur.x + ImGui::GetScrollX();
                const float mouseInChildY = io.MousePos.y - cur.y + ImGui::GetScrollY();
                ImGui::SetScrollX(anchorLX * newZoom - (mouseInChildX - ImGui::GetScrollX()));
                ImGui::SetScrollY(anchorLY * newZoom - (mouseInChildY - ImGui::GetScrollY()));
            }
        }
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(uintptr_t)texture, imgSize);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Optional pixel grid (only at high zoom so it stays readable).
    if (showGrid && kZoomLadder[zoomIdx] >= 3) {
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

    // ── Palette-seam overlay (HGR-07): mark adjacent lit bytes that disagree
    // on the shared high bit — where NTSC artifact-colour bleed happens. Scan
    // only the visible/scrolled region for perf.
    if (showConflicts) {
        const float sx = ImGui::GetScrollX(), sy = ImGui::GetScrollY();
        const ImVec2 vis = ImGui::GetContentRegionAvail();
        const int y0 = std::clamp(static_cast<int>(sy / scale), 0, GraphicsCard::kHiresHeight - 1);
        const int y1 = std::clamp(static_cast<int>((sy + vis.y) / scale) + 1, 0, GraphicsCard::kHiresHeight - 1);
        const int bc0 = std::clamp(static_cast<int>((sx / scale) / 7) - 1, 0, 38);
        const int bc1 = std::clamp(static_cast<int>(((sx + vis.x) / scale) / 7) + 1, 0, 38);
        const ImU32 seamCol = IM_COL32(255, 0, 0, 110);
        for (int y = y0; y <= y1; ++y)
            for (int bc = bc0; bc <= bc1; ++bc)
                if (hgrpaint::byteHasPaletteSeam(shadow.data(), bc, y)) {
                    const float fx = origin.x + (bc + 1) * 7 * scale;  // seam at byte boundary
                    const float fy = origin.y + y * scale;
                    dl->AddRect(ImVec2(fx - scale, fy), ImVec2(fx + scale, fy + scale), seamCol);
                }
    }

    // Map mouse → logical pixel.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int lx = static_cast<int>((mouse.x - origin.x) / scale);
    int ly = static_cast<int>((mouse.y - origin.y) / scale);
    lx = std::clamp(lx, 0, GraphicsCard::kHiresWidth - 1);
    ly = std::clamp(ly, 0, GraphicsCard::kHiresHeight - 1);
    if (hovered) { lastHoverX = lx; lastHoverY = ly; }

    const bool altDown = ImGui::GetIO().KeyAlt;
    const bool eyedrop = (tool == Tool::Eyedropper) || altDown;
    const HgrColor activeColor = (tool == Tool::Eraser) ? HgrColor::Black : color;

    // ── Brush-footprint + colour-snapped cursor preview (HGR-08) ────────────
    if (hovered && !dragging && !eyedrop &&
        (tool == Tool::Pencil || tool == Tool::Eraser)) {
        const int snapped = hgrpaint::snapColumn(lx, activeColor);
        const ImU32 ghost = (swatchColor(activeColor) & 0x00FFFFFF) | 0x80000000;
        const int r = brushSize - 1;
        const float bx = origin.x + (snapped - r) * scale;
        const float by = origin.y + (ly - r) * scale;
        const float bw = (2 * r + 1) * scale;
        dl->AddRect(ImVec2(bx, by), ImVec2(bx + bw, by + bw), ghost);
        // Marker showing parity nudge: actual click column vs snapped column.
        if (snapped != lx) {
            const float ax = origin.x + lx * scale;
            dl->AddLine(ImVec2(ax, by), ImVec2(ax, by + bw), IM_COL32(255, 255, 0, 200));
        }
    }

    // ── Selection marching-ants (HGR-06) ────────────────────────────────────
    if (hasSel && !pasting) {
        const int sx0 = std::min(selX0, selX1), sx1 = std::max(selX0, selX1);
        const int sy0 = std::min(selY0, selY1), sy1 = std::max(selY0, selY1);
        const ImVec2 a(origin.x + sx0 * scale, origin.y + sy0 * scale);
        const ImVec2 b(origin.x + (sx1 + 1) * scale, origin.y + (sy1 + 1) * scale);
        const bool phase = (static_cast<int>(ImGui::GetTime() * 4.0) & 1) != 0;
        dl->AddRect(a, b, phase ? IM_COL32(255,255,255,255) : IM_COL32(0,0,0,255));
        dl->AddRect(ImVec2(a.x-1,a.y-1), ImVec2(b.x+1,b.y+1),
                    phase ? IM_COL32(0,0,0,255) : IM_COL32(255,255,255,255));
    }

    if (pasting) {
        // ── Floating paste (HGR-06): clip follows the cursor; click commits ──
        if (hovered) { pasteX = lx; pasteY = ly; }
        for (int cy = 0; cy < clip.h; ++cy)
            for (int cx = 0; cx < clip.w; ++cx) {
                const HgrColor c = clip.px[static_cast<size_t>(cy) * clip.w + cx];
                if (c == HgrColor::Black) continue;
                const float px = origin.x + (pasteX + cx) * scale;
                const float py = origin.y + (pasteY + cy) * scale;
                dl->AddRectFilled(ImVec2(px, py), ImVec2(px + scale, py + scale),
                                  (swatchColor(c) & 0x00FFFFFF) | 0xC0000000);
            }
        dl->AddRect(ImVec2(origin.x + pasteX * scale, origin.y + pasteY * scale),
                    ImVec2(origin.x + (pasteX + clip.w) * scale, origin.y + (pasteY + clip.h) * scale),
                    IM_COL32(255, 255, 0, 230));
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            pasteFloatingAt(pasteX, pasteY);
            pasting = false;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) pasting = false;   // cancel
    } else {
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (eyedrop) {
                color = hgrpaint::colorAt(shadow.data(), lx, ly);
                if (tool == Tool::Eyedropper) { tool = prevTool; }  // one-shot revert
            } else if (tool == Tool::Select) {
                dragging = true; hasSel = true;
                dragStartX = lx; dragStartY = ly;
                selX0 = selX1 = lx; selY0 = selY1 = ly;
            } else {
                dragging = true;
                dragStartX = lx; dragStartY = ly;
                lastX = lx; lastY = ly;
                beginStroke();
                if (tool == Tool::Pencil || tool == Tool::Eraser) paintBrush(lx, ly, activeColor);
                else if (tool == Tool::PaletteShift) paintPaletteByte(lx, ly);
                else if (tool == Tool::Fill) { floodFill(lx, ly, activeColor); commitStroke(); dragging = false; }
            }
        }

        if (dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (tool == Tool::Pencil || tool == Tool::Eraser) {
                if (lx != lastX || ly != lastY) {
                    paintLine(lastX, lastY, lx, ly, activeColor);   // interpolate fast drags
                    lastX = lx; lastY = ly;
                }
            } else if (tool == Tool::PaletteShift) {
                if (lx / 7 != lastX / 7 || ly != lastY) {
                    paintPaletteByte(lx, ly);
                    lastX = lx; lastY = ly;
                }
            } else if (tool == Tool::Select) {
                selX1 = lx; selY1 = ly;
            } else if (tool == Tool::Line) {
                const ImVec2 a(origin.x + (dragStartX + 0.5f) * scale, origin.y + (dragStartY + 0.5f) * scale);
                const ImVec2 b(origin.x + (lx + 0.5f) * scale, origin.y + (ly + 0.5f) * scale);
                dl->AddLine(a, b, swatchColor(activeColor), 1.5f);
            } else if (tool == Tool::Rectangle) {
                const ImVec2 a(origin.x + dragStartX * scale, origin.y + dragStartY * scale);
                const ImVec2 b(origin.x + (lx + 1) * scale, origin.y + (ly + 1) * scale);
                if (rectFilled) dl->AddRectFilled(a, b, (swatchColor(activeColor) & 0x00FFFFFF) | 0x80000000);
                else            dl->AddRect(a, b, swatchColor(activeColor), 0, 0, 1.5f);
            } else if (tool == Tool::Ellipse) {
                const ImVec2 center(origin.x + (dragStartX + lx + 1) * 0.5f * scale,
                                    origin.y + (dragStartY + ly + 1) * 0.5f * scale);
                const ImVec2 radius(std::abs(lx - dragStartX) * 0.5f * scale + 0.5f,
                                    std::abs(ly - dragStartY) * 0.5f * scale + 0.5f);
                if (rectFilled) dl->AddEllipseFilled(center, radius, (swatchColor(activeColor) & 0x00FFFFFF) | 0x80000000);
                else            dl->AddEllipse(center, radius, swatchColor(activeColor), 0.0f, 0, 1.5f);
            }
        }

        if (dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (tool == Tool::Line) paintLine(dragStartX, dragStartY, lx, ly, activeColor);
            else if (tool == Tool::Rectangle) paintRect(dragStartX, dragStartY, lx, ly, activeColor, rectFilled);
            else if (tool == Tool::Ellipse) paintEllipse(dragStartX, dragStartY, lx, ly, activeColor, rectFilled);
            else if (tool == Tool::Select) { selX1 = lx; selY1 = ly; }
            // Commit the open stroke (no-op for Select, whose edits aren't a stroke,
            // and for Fill, which already committed on click).
            commitStroke();
            dragging = false;
        }
    }

    // Minimap navigator overlay (HGR-10) + scroll metrics for next frame.
    renderMinimap(origin.x, origin.y, scale);

    ImGui::EndChild();
}

void HGRPaintEditor_ImGui::renderFileRow()
{
    ImGui::SetNextItemWidth(-360);
    ImGui::InputTextWithHint("##hgrpath", "path to .HGR / .bin (8 KB) — or .png for export",
                             filePath, sizeof(filePath));
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string err;
        if (loadCallback && loadCallback(filePath, baseAddr(), err))
            status = std::string("Loaded into $") + (page2 ? "4000" : "2000");
        else
            status = "Load failed: " + (err.empty() ? std::string("(no loader / bad path)") : err);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        // Opt-in metadata stamp (HGR-12): a "POM1HGR" tag in the page's trailing
        // screen-hole bytes ($1FF8-$1FFF), which lie past the last displayed byte
        // ($3FF7) so they never disturb visible pixels.
        if (stampMeta) {
            static const char kTag[8] = { 'P','O','M','1','H','G','R','\0' };
            for (int i = 0; i < 8; ++i) {
                const int off = 0x1FF8 + i;
                shadow[off] = static_cast<uint8_t>(kTag[i]);
                if (writeCallback) writeCallback(static_cast<uint16_t>(baseAddr() + off),
                                                 static_cast<uint8_t>(kTag[i]));
            }
        }
        std::string err;
        if (saveCallback && saveCallback(filePath, baseAddr(), err))
            status = stampMeta ? "Saved 8 KB HGR (+POM1HGR tag)" : "Saved 8 KB HGR image";
        else
            status = "Save failed: " + (err.empty() ? std::string("(no saver / bad path)") : err);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save PNG")) {
        // Export the exact NTSC render currently on the canvas.
        std::string path = filePath;
        const auto dot = path.find_last_of('.');
        if (dot == std::string::npos) path += ".png";
        else if (path.substr(dot) != ".png" && path.substr(dot) != ".PNG") path = path.substr(0, dot) + ".png";
        std::string err;
        if (savePngCallback && !path.empty() &&
            savePngCallback(path, gfx.pixels(), GraphicsCard::kHiresWidth, GraphicsCard::kHiresHeight, err))
            status = "Exported PNG: " + path;
        else
            status = "PNG export failed: " + (err.empty() ? std::string("(no exporter / bad path)") : err);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Stamp tag", &stampMeta);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On Save, write a 'POM1HGR' marker into the unused\n"
                          "screen-hole bytes ($1FF8-$1FFF) — invisible, opt-in.");
    if (!status.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", status.c_str()); }
}

void HGRPaintEditor_ImGui::renderStatusBar(int lx, int ly, bool hovered)
{
    // Persistent info line (HGR-04): coords, byte, parity-snapped column, tool,
    // colour swatch, page, zoom, undo/redo depth. Survives the mouse leaving the
    // canvas via the cached lastHoverX/Y.
    const char* toolNames[] = { "Pencil", "Eraser", "Line", "Rect", "Ellipse",
                                "Fill", "Eyedropper", "Select", "Palette" };
    const HgrColor activeColor = (tool == Tool::Eraser) ? HgrColor::Black : color;

    if (hovered && lx >= 0 && ly >= 0) {
        const int snapped = hgrpaint::snapColumn(lx, activeColor);
        ImGui::Text("x=%3d y=%3d  byte=$%04X  col->%d", lx, ly,
                    baseAddr() + hgrpaint::hgrByteOffset(snapped, ly), snapped);
    } else {
        ImGui::TextUnformatted("x=--- y=---  byte=$----  col->-");
    }
    ImGui::SameLine();
    ImGui::Text(" | %s ", toolNames[static_cast<int>(tool)]);
    ImGui::SameLine();
    // Active-colour swatch.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        dl->AddRectFilled(p, ImVec2(p.x + h, p.y + h), swatchColor(activeColor));
        dl->AddRect(p, ImVec2(p.x + h, p.y + h), IM_COL32(160, 160, 160, 255));
        ImGui::Dummy(ImVec2(h, h));
    }
    ImGui::SameLine();
    ImGui::Text("%s | Page %d ($%04X) | Zoom %dx | Undo:%zu Redo:%zu",
                colorName(activeColor), page2 ? 2 : 1, baseAddr(),
                kZoomLadder[zoomIdx], undo.size(), redo.size());
}

void HGRPaintEditor_ImGui::handleShortcuts()
{
    // Keyboard shortcuts (HGR-02). Only act when the editor window (and its
    // children) is focused and no text widget wants input, so we never steal
    // keys from the file-path InputText or the main Apple 1 keyboard path.
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;

    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    auto pick = [&](Tool t) { prevTool = tool; tool = t; };

    if (io.KeyCtrl) {
        // Ctrl+Z = undo, Ctrl+Y or Ctrl+Shift+Z = redo, Ctrl+C/X/V = clipboard.
        if (pressed(ImGuiKey_Z)) { if (io.KeyShift) doRedo(); else doUndo(); }
        if (pressed(ImGuiKey_Y)) doRedo();
        if (pressed(ImGuiKey_C)) copySelection(false);
        if (pressed(ImGuiKey_X)) copySelection(true);
        if (pressed(ImGuiKey_V) && clip.w > 0) {
            pasting = true;
            pasteX = hasSel ? std::min(selX0, selX1) : 0;
            pasteY = hasSel ? std::min(selY0, selY1) : 0;
        }
        return;   // don't let Ctrl combos fall through to plain-key tools
    }

    // Esc cancels a floating paste / clears the selection.
    if (pressed(ImGuiKey_Escape)) { pasting = false; hasSel = false; }
    // Arrow keys nudge a floating paste by 1px.
    if (pasting) {
        if (pressed(ImGuiKey_LeftArrow))  pasteX = std::max(pasteX - 1, 0);
        if (pressed(ImGuiKey_RightArrow)) pasteX = std::min(pasteX + 1, GraphicsCard::kHiresWidth - 1);
        if (pressed(ImGuiKey_UpArrow))    pasteY = std::max(pasteY - 1, 0);
        if (pressed(ImGuiKey_DownArrow))  pasteY = std::min(pasteY + 1, GraphicsCard::kHiresHeight - 1);
        if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) {
            pasteFloatingAt(pasteX, pasteY);
            pasting = false;
        }
    }

    if (pressed(ImGuiKey_P)) pick(Tool::Pencil);
    if (pressed(ImGuiKey_E)) pick(Tool::Eraser);
    if (pressed(ImGuiKey_L)) pick(Tool::Line);
    if (pressed(ImGuiKey_R)) pick(Tool::Rectangle);
    if (pressed(ImGuiKey_F)) pick(Tool::Fill);
    // (Ellipse on O, Eyedropper on I, Select on S, Palette-shift on M.)
    if (pressed(ImGuiKey_O)) pick(Tool::Ellipse);
    if (pressed(ImGuiKey_I)) pick(Tool::Eyedropper);
    if (pressed(ImGuiKey_S)) pick(Tool::Select);
    if (pressed(ImGuiKey_M)) pick(Tool::PaletteShift);

    // Palette 1-6.
    const HgrColor palette[] = { HgrColor::Black, HgrColor::White, HgrColor::Violet,
                                 HgrColor::Green, HgrColor::Blue, HgrColor::Orange };
    const ImGuiKey numKeys[] = { ImGuiKey_1, ImGuiKey_2, ImGuiKey_3,
                                 ImGuiKey_4, ImGuiKey_5, ImGuiKey_6 };
    for (int i = 0; i < 6; ++i)
        if (pressed(numKeys[i])) color = palette[i];

    if (pressed(ImGuiKey_X)) rectFilled = !rectFilled;
    if (pressed(ImGuiKey_G)) showGrid = !showGrid;

    // Zoom +/- (main row and keypad).
    if (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd))
        zoomIdx = std::min(zoomIdx + 1, kZoomLadderCount - 1);
    if (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract))
        zoomIdx = std::max(zoomIdx - 1, 0);

    // Brush size [ ].
    if (pressed(ImGuiKey_LeftBracket))  brushSize = std::max(brushSize - 1, 1);
    if (pressed(ImGuiKey_RightBracket)) brushSize = std::min(brushSize + 1, 7);
}

void HGRPaintEditor_ImGui::render(const std::vector<uint8_t>& memory)
{
    handleShortcuts();
    renderToolbar();
    ImGui::Separator();
    renderFileRow();
    ImGui::Separator();
    renderStatusBar(lastHoverX, lastHoverY, lastHoverX >= 0);
    renderCanvas(memory);
}
