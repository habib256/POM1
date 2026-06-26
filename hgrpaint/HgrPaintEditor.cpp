// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HGR paint editor — see HgrPaintEditor.h. Independent reimplementation
// inspired by fadden's HGRTool (concept only).

#include "HgrPaintEditor.h"
#include "HgrConvert.h"          // image → HGR import (ii-pix-style, all in hgrpaint/)

#include "imgui.h"
#include "IconsFontAwesome6.h"   // FA-solid glyphs (merged into the UI font, like bench/)
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

using hgrpaint::HgrColor;

namespace {

// Fixed zoom ladder (HGR-09), 1x .. 16x. Mouse-wheel + Fit step the index.
const int kZoomLadder[] = { 1, 2, 3, 4, 6, 8, 12, 16 };
const int kZoomLadderCount = static_cast<int>(sizeof(kZoomLadder) / sizeof(kZoomLadder[0]));

// Approximate sRGB for each HGR colour, for the palette swatches + tool
// previews (the canvas itself is rendered by the host NTSC pipeline, the
// source of truth).
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

// FontAwesome-solid glyph for each tool, in Tool-enum order.
const char* const kToolIcons[9] = {
    ICON_FA_PENCIL,        // Pencil
    ICON_FA_ERASER,        // Eraser
    ICON_FA_SLASH,         // Line
    ICON_FA_SQUARE,        // Rectangle
    ICON_FA_CIRCLE,        // Ellipse
    ICON_FA_FILL_DRIP,     // Fill
    ICON_FA_EYE_DROPPER,   // Eyedropper
    ICON_FA_VECTOR_SQUARE, // Select (marquee)
    ICON_FA_PALETTE,       // Palette shift
};

} // namespace

hgrpaint::HgrPaintEditor::HgrPaintEditor(IHgrPaintHost* host_)
    : host(host_),
      canvasRgba(static_cast<size_t>(kHiresWidth) * kHiresHeight, 0),
      shadow(kHiresSize, 0)
{
}

hgrpaint::HgrPaintEditor::~HgrPaintEditor()
{
    if (texture != 0) glDeleteTextures(1, &texture);
}

void hgrpaint::HgrPaintEditor::renderShadow(uint32_t* out, bool mono)
{
    if (host) host->renderHgrPage(shadow.data(), out, mono);
}

// ─────────────────────────────────────────────────────────────
// Painting primitives (operate on the shadow, emit writes, record undo)
// ─────────────────────────────────────────────────────────────

void hgrpaint::HgrPaintEditor::beginStroke() { stroke.clear(); }

void hgrpaint::HgrPaintEditor::commitStroke()
{
    if (stroke.empty()) return;
    undo.push_back(std::move(stroke));
    stroke.clear();
    if (undo.size() > 64) undo.erase(undo.begin());
    // A fresh edit invalidates any redo history.
    redo.clear();
}

void hgrpaint::HgrPaintEditor::applyPlot(int x, int y, HgrColor c)
{
    const int off = hgrpaint::targetOffset(x, y, c);
    if (off < 0) return;
    const uint8_t old = shadow[off];
    const int changed = hgrpaint::plotPage(shadow.data(), x, y, c);
    if (changed < 0) return;
    const uint16_t addr = static_cast<uint16_t>(baseAddr() + changed);
    stroke.push_back({addr, old, shadow[changed]});
    if (host) host->pokeByte(addr, shadow[changed]);
}

void hgrpaint::HgrPaintEditor::paintBrush(int cx, int cy, HgrColor c)
{
    const int r = brushSize - 1;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            applyPlot(cx + dx, cy + dy, c);
}

void hgrpaint::HgrPaintEditor::paintLine(int x0, int y0, int x1, int y1, HgrColor c)
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

void hgrpaint::HgrPaintEditor::paintRect(int x0, int y0, int x1, int y1, HgrColor c, bool filled)
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

void hgrpaint::HgrPaintEditor::paintEllipse(int x0, int y0, int x1, int y1, HgrColor c, bool filled)
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

void hgrpaint::HgrPaintEditor::floodFill(int x, int y, HgrColor c)
{
    if (x < 0 || x > 279 || y < 0 || y > 191) return;
    // Flood by *perceived* artifact colour (hgrpaint::fillRegion renders the page
    // through the host NTSC pipeline), which is what the eye sees — a raw-bit
    // flood leaks through the off sub-pixels that dither every chromatic region.
    // fillRegion mutates a page buffer directly, so snapshot the shadow, run it,
    // then diff back into per-byte edits to keep undo + the live RAM writes working.
    std::vector<uint8_t> before = shadow;
    hgrpaint::fillRegion(shadow.data(), x, y, c,
                         [this](const uint8_t* page8k, uint32_t* out) {
                             if (host) host->renderHgrPage(page8k, out, /*mono=*/false);
                         });
    for (int off = 0; off < static_cast<int>(shadow.size()); ++off) {
        if (shadow[off] == before[off]) continue;
        const uint16_t addr = static_cast<uint16_t>(baseAddr() + off);
        stroke.push_back({addr, before[off], shadow[off]});
        if (host) host->pokeByte(addr, shadow[off]);
    }
}

void hgrpaint::HgrPaintEditor::applyOps(const std::vector<ByteEdit>& ops, bool forward)
{
    // forward = redo (write newVal, in recorded order); reverse = undo (write
    // oldVal, in reverse order so repeated touches of the same byte unwind to
    // the correct earliest value — see commitStroke ordering).
    auto write = [&](const ByteEdit& e, uint8_t val) {
        const int off = e.addr - baseAddr();
        if (off >= 0 && off < static_cast<int>(shadow.size())) shadow[off] = val;
        if (host) host->pokeByte(e.addr, val);
    };
    if (forward) {
        for (const auto& e : ops) write(e, e.newVal);
    } else {
        for (auto it = ops.rbegin(); it != ops.rend(); ++it) write(*it, it->oldVal);
    }
}

void hgrpaint::HgrPaintEditor::doUndo()
{
    if (undo.empty()) return;
    auto ops = std::move(undo.back());
    undo.pop_back();
    applyOps(ops, false);
    redo.push_back(std::move(ops));
    if (redo.size() > 64) redo.erase(redo.begin());
}

void hgrpaint::HgrPaintEditor::doRedo()
{
    if (redo.empty()) return;
    auto ops = std::move(redo.back());
    redo.pop_back();
    applyOps(ops, true);
    undo.push_back(std::move(ops));
    if (undo.size() > 64) undo.erase(undo.begin());
}

void hgrpaint::HgrPaintEditor::clearPage()
{
    beginStroke();
    for (int off = 0; off < static_cast<int>(shadow.size()); ++off) {
        if (shadow[off] != 0) {
            const uint16_t addr = static_cast<uint16_t>(baseAddr() + off);
            stroke.push_back({addr, shadow[off], 0});
            shadow[off] = 0;
            if (host) host->pokeByte(addr, 0);
        }
    }
    commitStroke();
}

// ─────────────────────────────────────────────────────────────
// Selection / clipboard (HGR-06) and palette-shift (HGR-11)
// ─────────────────────────────────────────────────────────────

void hgrpaint::HgrPaintEditor::copySelection(bool cut)
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

void hgrpaint::HgrPaintEditor::pasteFloatingAt(int destX, int destY)
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

void hgrpaint::HgrPaintEditor::paintPaletteByte(int lx, int ly)
{
    if (lx < 0 || lx > 279 || ly < 0 || ly > 191) return;
    const int byteCol = lx / 7;
    const int off = hgrpaint::hgrByteOffset(0, ly) + byteCol;
    const uint8_t old = shadow[off];
    const int ch = hgrpaint::setBytePalette(shadow.data(), byteCol, ly, paletteMsbMode);
    if (ch < 0) return;
    const uint16_t addr = static_cast<uint16_t>(baseAddr() + ch);
    stroke.push_back({addr, old, shadow[ch]});
    if (host) host->pokeByte(addr, shadow[ch]);
}

// ─────────────────────────────────────────────────────────────
// UI
// ─────────────────────────────────────────────────────────────

void hgrpaint::HgrPaintEditor::renderMinimap()
{
    // Navigator thumbnail, drawn in the LEFT tool panel (below "Clear page").
    // Reads the canvas scroll metrics captured by renderCanvas last frame and
    // recentres the view via pendingScroll (applied at the next canvas draw).
    if (canvasScale <= 0.0f) return;
    // Only useful when the image overflows the viewport (something to navigate).
    const bool overflow = (canvasScrollMaxX > 1.0f) || (canvasScrollMaxY > 1.0f);
    if (!overflow) return;

    const float mmW = std::min(ImGui::GetContentRegionAvail().x, 130.0f);
    const float mmH = mmW * static_cast<float>(kHiresHeight) / static_cast<float>(kHiresWidth);
    ImGui::TextDisabled("Navigator");
    const ImVec2 mmMin = ImGui::GetCursorScreenPos();
    // InvisibleButton (not Dummy): it captures the mouse so a click/drag pans the
    // view instead of moving the editor window (same fix as the canvas).
    ImGui::InvisibleButton("##hgrminimap", ImVec2(mmW, mmH));
    const bool active = ImGui::IsItemActive();
    const ImVec2 mmMax(mmMin.x + mmW, mmMin.y + mmH);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(mmMin, mmMax, IM_COL32(0, 0, 0, 255));
    dl->AddImage((ImTextureID)(uintptr_t)texture, mmMin, mmMax);
    dl->AddRect(mmMin, mmMax, IM_COL32(160, 160, 160, 255));

    // Visible viewport box (logical coords) → thumbnail.
    const float lx0 = canvasScrollX / canvasScale;
    const float ly0 = canvasScrollY / canvasScale;
    const float lw  = canvasViewW  / canvasScale;
    const float lh  = canvasViewH  / canvasScale;
    auto mmx = [&](float lx){ return mmMin.x + (lx / kHiresWidth)  * mmW; };
    auto mmy = [&](float ly){ return mmMin.y + (ly / kHiresHeight) * mmH; };
    dl->AddRect(ImVec2(mmx(lx0), mmy(ly0)), ImVec2(mmx(lx0 + lw), mmy(ly0 + lh)),
                IM_COL32(255, 255, 0, 230), 0, 0, 1.5f);

    // Click/drag inside the thumbnail recentres the view. Driven by the button's
    // active (held) state, and the mouse fraction is clamped so dragging past the
    // thumbnail edge still pans to the image border.
    if (active) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float fx = std::clamp((m.x - mmMin.x) / mmW, 0.0f, 1.0f);
        const float fy = std::clamp((m.y - mmMin.y) / mmH, 0.0f, 1.0f);
        pendingScrollX = std::clamp(fx * kHiresWidth  * canvasScale - canvasViewW * 0.5f, 0.0f, canvasScrollMaxX);
        pendingScrollY = std::clamp(fy * kHiresHeight * canvasScale - canvasViewH * 0.5f, 0.0f, canvasScrollMaxY);
    }
}

void hgrpaint::HgrPaintEditor::renderTopBar()
{
    // Slim top strip: page select + help on line 1, file ops on line 2. Lives
    // above the tool palette + canvas, MacPaint-style.
    if (ImGui::Button(page2 ? "Page 2 ($4000)" : "Page 1 ($2000)")) page2 = !page2;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle the edited HGR page");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Mouse\n"
            "  Left drag    draw with the current tool / colour\n"
            "  Right drag   quick-erase (paint black) — no tool switch\n"
            "  Middle drag  pan the canvas\n"
            "  Wheel        zoom, anchored on the cursor\n"
            "  Alt + Left   eyedropper (pick colour)\n"
            "  Shift        constrain Line to 0/45/90 deg, Rect/Ellipse to a square\n"
            "\n"
            "Keys\n"
            "  P E L R O F I S M   pencil eraser line rect ellipse fill eyedrop select palette\n"
            "  1-6 colours   [ ] brush size   +/- zoom   G grid\n"
            "  Ctrl+Z / Ctrl+Y undo/redo   Ctrl+C/X/V copy/cut/paste");

    renderFileRow();   // line 2: path + Load / Save / Save PNG / stamp / status
}

void hgrpaint::HgrPaintEditor::renderToolPanel()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 kSelTint   = IM_COL32(58, 96, 150, 255);
    const ImU32 kSelHover  = IM_COL32(78, 116, 170, 255);
    const ImU32 kSelBorder = IM_COL32(255, 220, 60, 255);

    // ── Tool palette: 3-column grid of icon buttons ──────────────────────────
    const char* toolTips[] = {
        "Pencil (P)", "Eraser (E)", "Line (L)", "Rectangle (R)", "Ellipse (O)",
        "Fill (F)", "Eyedropper (I)", "Select (S)", "Palette shift (M)" };
    const ImVec2 btnSz(34, 34);
    for (int i = 0; i < 9; ++i) {
        if (i % 3 != 0) ImGui::SameLine();
        const bool sel = (static_cast<int>(tool) == i);
        ImGui::PushID(i);
        if (sel) {
            ImGui::PushStyleColor(ImGuiCol_Button, kSelTint);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kSelHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, kSelHover);
        }
        if (ImGui::Button(kToolIcons[i], btnSz)) { prevTool = tool; tool = static_cast<Tool>(i); }
        if (sel) ImGui::PopStyleColor(3);
        if (sel) dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), kSelBorder, 0, 0, 2.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", toolTips[i]);
        ImGui::PopID();
    }

    ImGui::Separator();

    // ── Tool options (contextual) ────────────────────────────────────────────
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##brush", &brushSize, 1, 7, "Brush %d");
    if (tool == Tool::Rectangle || tool == Tool::Ellipse)
        ImGui::Checkbox("Filled", &rectFilled);
    if (tool == Tool::PaletteShift) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::Combo("##msb", &paletteMsbMode,
                     "MSB: clear\0MSB: set\0MSB: toggle\0");
    }
    if (tool == Tool::Select) {
        if (ImGui::Button("Copy")) copySelection(false);
        ImGui::SameLine();
        if (ImGui::Button("Cut"))  copySelection(true);
        if (ImGui::Button("Paste") && clip.w > 0) {
            pasting = true; pasteX = std::min(selX0, selX1); pasteY = std::min(selY0, selY1);
        }
    }

    ImGui::Separator();

    // ── Zoom ─────────────────────────────────────────────────────────────────
    if (ImGui::Button("-##zoom")) zoomIdx = std::max(zoomIdx - 1, 0);
    ImGui::SameLine();
    ImGui::Text("%dx", kZoomLadder[zoomIdx]);
    ImGui::SameLine();
    if (ImGui::Button("+##zoom")) zoomIdx = std::min(zoomIdx + 1, kZoomLadderCount - 1);
    ImGui::SameLine();
    if (ImGui::Button("Fit")) wantFit = true;

    ImGui::Separator();

    // ── Display toggles ──────────────────────────────────────────────────────
    ImGui::Checkbox("Grid", &showGrid);
    ImGui::Checkbox("Seams", &showConflicts);
    ImGui::Checkbox("NTSC", &ntscColor);

    ImGui::Separator();

    // ── Edit ─────────────────────────────────────────────────────────────────
    if (ImGui::Button("Undo")) doUndo();
    ImGui::SameLine();
    if (ImGui::Button("Redo")) doRedo();
    if (ImGui::Button("Clear page")) clearPage();

    // Navigator thumbnail, below the edit buttons (only shown when the zoomed
    // image overflows the canvas viewport).
    ImGui::Separator();
    renderMinimap();
}

void hgrpaint::HgrPaintEditor::renderColorBar()
{
    // Horizontal colour palette along the bottom (MacPaint pattern strip).
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const HgrColor palette[] = { HgrColor::Black, HgrColor::White, HgrColor::Violet,
                                 HgrColor::Green, HgrColor::Blue, HgrColor::Orange };
    const ImVec2 swSz(34, 26);
    for (int i = 0; i < 6; ++i) {
        if (i != 0) ImGui::SameLine();
        const HgrColor c = palette[i];
        const bool sel = (c == color);
        ImGui::PushID(200 + i);
        ImGui::PushStyleColor(ImGuiCol_Button, swatchColor(c));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, swatchColor(c));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, swatchColor(c));
        if (ImGui::Button("##sw", swSz)) color = c;
        ImGui::PopStyleColor(3);
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        if (sel) dl->AddRect(a, b, IM_COL32(255, 220, 60, 255), 0, 0, 2.5f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%d)", colorName(c), i + 1);
        ImGui::PopID();
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("  Colour: %s", colorName(color));
}

void hgrpaint::HgrPaintEditor::renderCanvas(const std::vector<uint8_t>& memory)
{
    // Refresh the shadow from live RAM so external program writes show, and so
    // read-modify-write paints start from the current bytes.
    if (memory.size() >= static_cast<size_t>(baseAddr()) + kHiresSize)
        std::copy(memory.begin() + baseAddr(),
                  memory.begin() + baseAddr() + kHiresSize,
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
                     kHiresWidth, kHiresHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    // Render the shadow page through the host NTSC pipeline (the same renderer the
    // emulator screen uses) so the canvas is pixel-identical. The shadow was just
    // refreshed from live RAM and reflects any pokes we made this frame.
    renderShadow(canvasRgba.data(), !ntscColor);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    kHiresWidth, kHiresHeight,
                    GL_RGBA, GL_UNSIGNED_BYTE, canvasRgba.data());

    float scale = static_cast<float>(kZoomLadder[zoomIdx]);
    ImVec2 imgSize(kHiresWidth * scale, kHiresHeight * scale);

    ImGui::BeginChild("hgrcanvas", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    // Apply a scroll requested by the minimap last frame (HGR-10).
    if (pendingScrollX >= 0) { ImGui::SetScrollX(pendingScrollX); pendingScrollX = -1; }
    if (pendingScrollY >= 0) { ImGui::SetScrollY(pendingScrollY); pendingScrollY = -1; }

    // First open: fit the image to the viewport rather than leaving a tiny 3x
    // canvas adrift in a big window (a poor first impression).
    if (firstFit) { firstFit = false; wantFit = true; }

    // ── Zoom-to-fit (HGR-09): pick the largest ladder step that fits ─────────
    if (wantFit) {
        wantFit = false;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        int best = 0;
        for (int i = 0; i < kZoomLadderCount; ++i) {
            if (kHiresWidth  * kZoomLadder[i] <= avail.x &&
                kHiresHeight * kZoomLadder[i] <= avail.y)
                best = i;
        }
        zoomIdx = best;
        scale = static_cast<float>(kZoomLadder[zoomIdx]);
        imgSize = ImVec2(kHiresWidth * scale, kHiresHeight * scale);
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
                imgSize = ImVec2(kHiresWidth * scale, kHiresHeight * scale);
                // Keep that logical pixel under the mouse after rescaling.
                const float mouseInChildX = io.MousePos.x - cur.x + ImGui::GetScrollX();
                const float mouseInChildY = io.MousePos.y - cur.y + ImGui::GetScrollY();
                ImGui::SetScrollX(anchorLX * newZoom - (mouseInChildX - ImGui::GetScrollX()));
                ImGui::SetScrollY(anchorLY * newZoom - (mouseInChildY - ImGui::GetScrollY()));
            }
        }
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    // The canvas is an InvisibleButton, NOT an Image: it captures the mouse so a
    // drag PAINTS instead of moving the editor window. ImGui moves a window when
    // you drag its background — an Image never consumes the drag, a button does.
    // It grabs left/right/middle so erase (RMB) and pan (MMB) also keep the window
    // put; the window now only moves from its title bar (HGR draw bug fix).
    ImGui::InvisibleButton("##hgrcanvasimg", imgSize,
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddImage((ImTextureID)(uintptr_t)texture, origin,
                 ImVec2(origin.x + imgSize.x, origin.y + imgSize.y));

    // ── Middle-button drag pans the canvas at any zoom. Start when pressed over
    // the canvas, continue via per-frame delta until release — robust while the
    // InvisibleButton owns the mouse. ───────────────────────────────────────────
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning = true;
    if (panning && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        ImGui::SetScrollX(ImGui::GetScrollX() - d.x);
        ImGui::SetScrollY(ImGui::GetScrollY() - d.y);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) panning = false;

    // Optional pixel grid (only at high zoom so it stays readable).
    if (showGrid && kZoomLadder[zoomIdx] >= 3) {
        const ImU32 gcol = IM_COL32(80, 80, 80, 90);
        for (int x = 0; x <= kHiresWidth; x += 7) {  // byte columns
            const float fx = origin.x + x * scale;
            dl->AddLine(ImVec2(fx, origin.y), ImVec2(fx, origin.y + imgSize.y), gcol);
        }
        for (int y = 0; y <= kHiresHeight; y += 8) {
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
        const int y0 = std::clamp(static_cast<int>(sy / scale), 0, kHiresHeight - 1);
        const int y1 = std::clamp(static_cast<int>((sy + vis.y) / scale) + 1, 0, kHiresHeight - 1);
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
    lx = std::clamp(lx, 0, kHiresWidth - 1);
    ly = std::clamp(ly, 0, kHiresHeight - 1);
    if (hovered) { lastHoverX = lx; lastHoverY = ly; }

    const bool altDown = ImGui::GetIO().KeyAlt;
    const bool eyedrop = (tool == Tool::Eyedropper) || altDown;
    const HgrColor activeColor = (tool == Tool::Eraser) ? HgrColor::Black : color;

    // Shift constrains shapes: Line to 0/45/90°, Rect/Ellipse to a square. Used
    // for the live preview AND the committed geometry so they always agree.
    auto constrainEnd = [&]() -> std::pair<int,int> {
        int ex = lx, ey = ly;
        if (ImGui::GetIO().KeyShift) {
            const int ddx = ex - dragStartX, ddy = ey - dragStartY;
            if (tool == Tool::Line) {
                const int adx = std::abs(ddx), ady = std::abs(ddy);
                if (adx > 2 * ady)      ey = dragStartY;            // horizontal
                else if (ady > 2 * adx) ex = dragStartX;           // vertical
                else { const int m = (adx + ady) / 2;              // 45°
                       ex = dragStartX + (ddx < 0 ? -m : m);
                       ey = dragStartY + (ddy < 0 ? -m : m); }
            } else if (tool == Tool::Rectangle || tool == Tool::Ellipse) {
                const int m = std::max(std::abs(ddx), std::abs(ddy));
                ex = dragStartX + (ddx < 0 ? -m : m);
                ey = dragStartY + (ddy < 0 ? -m : m);
            }
            ex = std::clamp(ex, 0, kHiresWidth - 1);
            ey = std::clamp(ey, 0, kHiresHeight - 1);
        }
        return {ex, ey};
    };

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
        // Right-button = quick freehand erase (paint black) for any drawing tool —
        // no need to switch to the Eraser. Runs only when no left-button op is open.
        if (!dragging && tool != Tool::Select && !eyedrop) {
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                rmbErase = true; beginStroke();
                lastX = lx; lastY = ly;
                paintBrush(lx, ly, HgrColor::Black);
            } else if (rmbErase && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                if (lx != lastX || ly != lastY) {
                    paintLine(lastX, lastY, lx, ly, HgrColor::Black);   // interpolate
                    lastX = lx; lastY = ly;
                }
            }
            if (rmbErase && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                commitStroke(); rmbErase = false;
            }
        }

        if (!rmbErase && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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
                const auto [ex, ey] = constrainEnd();
                const ImVec2 a(origin.x + (dragStartX + 0.5f) * scale, origin.y + (dragStartY + 0.5f) * scale);
                const ImVec2 b(origin.x + (ex + 0.5f) * scale, origin.y + (ey + 0.5f) * scale);
                dl->AddLine(a, b, swatchColor(activeColor), 1.5f);
            } else if (tool == Tool::Rectangle) {
                const auto [ex, ey] = constrainEnd();
                const ImVec2 a(origin.x + dragStartX * scale, origin.y + dragStartY * scale);
                const ImVec2 b(origin.x + (ex + 1) * scale, origin.y + (ey + 1) * scale);
                if (rectFilled) dl->AddRectFilled(a, b, (swatchColor(activeColor) & 0x00FFFFFF) | 0x80000000);
                else            dl->AddRect(a, b, swatchColor(activeColor), 0, 0, 1.5f);
            } else if (tool == Tool::Ellipse) {
                const auto [ex, ey] = constrainEnd();
                const ImVec2 center(origin.x + (dragStartX + ex + 1) * 0.5f * scale,
                                    origin.y + (dragStartY + ey + 1) * 0.5f * scale);
                const ImVec2 radius(std::abs(ex - dragStartX) * 0.5f * scale + 0.5f,
                                    std::abs(ey - dragStartY) * 0.5f * scale + 0.5f);
                if (rectFilled) dl->AddEllipseFilled(center, radius, (swatchColor(activeColor) & 0x00FFFFFF) | 0x80000000);
                else            dl->AddEllipse(center, radius, swatchColor(activeColor), 0.0f, 0, 1.5f);
            }
        }

        if (dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const auto [ex, ey] = constrainEnd();
            if (tool == Tool::Line) paintLine(dragStartX, dragStartY, ex, ey, activeColor);
            else if (tool == Tool::Rectangle) paintRect(dragStartX, dragStartY, ex, ey, activeColor, rectFilled);
            else if (tool == Tool::Ellipse) paintEllipse(dragStartX, dragStartY, ex, ey, activeColor, rectFilled);
            else if (tool == Tool::Select) { selX1 = lx; selY1 = ly; }
            // Commit the open stroke (no-op for Select, whose edits aren't a stroke,
            // and for Fill, which already committed on click).
            commitStroke();
            dragging = false;
        }
    }

    // Capture canvas scroll/view metrics so the navigator thumbnail in the left
    // tool panel (drawn earlier this frame, from last frame's values) can map the
    // visible viewport and recentre on click.
    canvasScrollX    = ImGui::GetScrollX();
    canvasScrollY    = ImGui::GetScrollY();
    canvasScrollMaxX = ImGui::GetScrollMaxX();
    canvasScrollMaxY = ImGui::GetScrollMaxY();
    canvasViewW      = ImGui::GetWindowSize().x;
    canvasViewH      = ImGui::GetWindowSize().y;
    canvasScale      = scale;

    ImGui::EndChild();
}

void hgrpaint::HgrPaintEditor::importImageFile(const std::string& path)
{
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    std::string err;
    if (!hgrpaint::decodeImageFile(path, w, h, rgba, err)) {
        status = "Import failed: " + err;
        return;
    }
    hgrpaint::ImportOptions opt;
    opt.stretch = importStretch;
    opt.dither  = importDither;
    std::vector<uint8_t> page(kHiresSize, 0);
    hgrpaint::imageToHgrPage(rgba.data(), w, h, opt, page.data());

    // Load the converted page as one undoable operation (diff vs current shadow).
    beginStroke();
    for (int off = 0; off < static_cast<int>(shadow.size()); ++off) {
        if (page[off] == shadow[off]) continue;
        const uint16_t addr = static_cast<uint16_t>(baseAddr() + off);
        stroke.push_back({addr, shadow[off], page[off]});
        shadow[off] = page[off];
        if (host) host->pokeByte(addr, page[off]);
    }
    commitStroke();
    const std::string name = std::filesystem::path(path).filename().string();
    status = "Imported " + name + " (" + std::to_string(w) + "x" + std::to_string(h) + ")";
}

void hgrpaint::HgrPaintEditor::openFileBrowser(bool forSave, int saveKind)
{
    browserForSave = forSave;
    browserImport = false;
    browserSaveKind = saveKind;
    if (browserDir.empty()) {
        std::error_code ec;
        browserDir = std::filesystem::current_path(ec).string();
        if (ec || browserDir.empty()) browserDir = ".";
    }
    // Seed a default filename for Save from the current path's basename.
    if (forSave) {
        std::string base = std::filesystem::path(filePath).filename().string();
        if (base.empty()) base = (saveKind == 1) ? "image.png" : "image.hgr";
        std::snprintf(browserSaveName, sizeof(browserSaveName), "%s", base.c_str());
    }
    browserOpen = true;
}

void hgrpaint::HgrPaintEditor::renderFileBrowser()
{
    namespace fs = std::filesystem;
    if (browserOpen) { ImGui::OpenPopup("HGR File##browser"); browserOpen = false; }

    const ImVec2 vpCenter = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(vpCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 460), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("HGR File##browser", nullptr, ImGuiWindowFlags_NoCollapse))
        return;

    ImGui::TextUnformatted(browserForSave
        ? (browserSaveKind == 1 ? "Save PNG export" : "Save HGR image (8 KB)")
        : (browserImport ? "Import picture (PNG / JPG / BMP) — converted to HGR"
                         : "Load HGR image (pick a file — 8 KB ones are highlighted)"));
    ImGui::TextDisabled("%s", browserDir.c_str());
    ImGui::Separator();

    // ── Directory + file listing ─────────────────────────────────────────────
    ImGui::BeginChild("##fblist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.0f), true);
    if (ImGui::Selectable("../", false)) {
        std::error_code ec;
        fs::path up = fs::path(browserDir).parent_path();
        if (!up.empty()) browserDir = up.string();
        (void)ec;
    }
    std::vector<fs::directory_entry> dirs, files;
    try {
        for (const auto& e : fs::directory_iterator(browserDir,
                 fs::directory_options::skip_permission_denied)) {
            std::error_code ec;
            if (e.is_directory(ec)) dirs.push_back(e);
            else if (e.is_regular_file(ec)) files.push_back(e);
        }
    } catch (...) {
        ImGui::TextDisabled("(cannot read this directory)");
    }
    auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b) {
        return a.path().filename().string() < b.path().filename().string();
    };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);

    for (const auto& d : dirs) {
        const std::string name = d.path().filename().string();
        if (ImGui::Selectable((name + "/").c_str(), false))
            browserDir = d.path().string();
    }
    for (const auto& f : files) {
        std::error_code ec;
        const std::uintmax_t sz = f.file_size(ec);
        const std::string name = f.path().filename().string();
        // Highlight the files that suit the current mode: 8 KB raw pages for
        // Load/Save, image files for Import.
        std::string ext = f.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(c));
        const bool isImg = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                            ext == ".bmp" || ext == ".gif" || ext == ".tga");
        const bool relevant = browserImport ? isImg : (!ec && sz >= 8000 && sz <= 8192);
        char label[320];
        std::snprintf(label, sizeof(label), "%-28s %8llu B", name.c_str(),
                      static_cast<unsigned long long>(sz));
        if (!relevant) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
        if (ImGui::Selectable(label, false)) {
            std::snprintf(filePath, sizeof(filePath), "%s", f.path().string().c_str());
            if (browserForSave) {
                std::snprintf(browserSaveName, sizeof(browserSaveName), "%s", name.c_str());
            } else if (browserImport) {
                importImageFile(f.path().string());
                ImGui::CloseCurrentPopup();
            } else {
                std::string err;
                if (host && host->loadImage(filePath, baseAddr(), err))
                    status = "Loaded " + name + " into $" + (page2 ? "4000" : "2000");
                else
                    status = "Load failed: " + (err.empty() ? std::string("(bad file)") : err);
                ImGui::CloseCurrentPopup();
            }
        }
        if (!relevant) ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    // Import options (fit/letterbox vs stretch, dithering).
    if (!browserForSave && browserImport) {
        ImGui::Checkbox("Stretch (else fit + letterbox)", &importStretch);
        ImGui::SameLine();
        ImGui::Checkbox("Dither", &importDither);
    }

    // ── Action row ───────────────────────────────────────────────────────────
    if (browserForSave) {
        ImGui::SetNextItemWidth(-160);
        ImGui::InputText("##fbname", browserSaveName, sizeof(browserSaveName));
        ImGui::SameLine();
        if (ImGui::Button("Save", ImVec2(70, 0)) && browserSaveName[0]) {
            const std::string full = (fs::path(browserDir) / browserSaveName).string();
            std::snprintf(filePath, sizeof(filePath), "%s", full.c_str());
            std::string err;
            bool ok = false;
            if (browserSaveKind == 1) {                       // PNG export
                ok = host && host->savePng(full, canvasRgba.data(),
                                           kHiresWidth, kHiresHeight, err);
                status = ok ? ("Exported PNG: " + full)
                            : ("PNG export failed: " + (err.empty() ? std::string("(error)") : err));
            } else {                                          // raw 8 KB HGR
                // Always bake the POM1HGR tag into the unused screen-hole bytes
                // ($1FF8-$1FFF) — past the last displayed byte, so invisible.
                static const char kTag[8] = { 'P','O','M','1','H','G','R','\0' };
                for (int i = 0; i < 8; ++i) {
                    const int off = 0x1FF8 + i;
                    shadow[off] = static_cast<uint8_t>(kTag[i]);
                    if (host) host->pokeByte(static_cast<uint16_t>(baseAddr() + off),
                                             static_cast<uint8_t>(kTag[i]));
                }
                ok = host && host->saveImage(full, baseAddr(), err);
                status = ok ? "Saved 8 KB HGR (+POM1HGR tag)"
                            : ("Save failed: " + (err.empty() ? std::string("(error)") : err));
            }
            if (ok) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Cancel", ImVec2(70, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void hgrpaint::HgrPaintEditor::renderFileRow()
{
    if (ImGui::Button("Load")) openFileBrowser(false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open a file picker and load a raw 8 KB HGR image");
    ImGui::SameLine();
    if (ImGui::Button("Import" ICON_FA_IMAGE)) { openFileBrowser(false); browserImport = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Import a PNG/JPG picture and convert it to HGR\n"
                          "(ii-pix-style: CAM16-UCS perceptual dithering vs the true NTSC colours)");
    ImGui::SameLine();
    if (ImGui::Button("Save")) openFileBrowser(true, /*kind=*/0);
    ImGui::SameLine();
    if (ImGui::Button("Save PNG")) openFileBrowser(true, /*kind=*/1);
    if (!status.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", status.c_str()); }
}

void hgrpaint::HgrPaintEditor::renderStatusBar(int lx, int ly, bool hovered)
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

void hgrpaint::HgrPaintEditor::handleShortcuts()
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
        if (pressed(ImGuiKey_RightArrow)) pasteX = std::min(pasteX + 1, kHiresWidth - 1);
        if (pressed(ImGuiKey_UpArrow))    pasteY = std::max(pasteY - 1, 0);
        if (pressed(ImGuiKey_DownArrow))  pasteY = std::min(pasteY + 1, kHiresHeight - 1);
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

void hgrpaint::HgrPaintEditor::render(const std::vector<uint8_t>& memory)
{
    handleShortcuts();

    // MacPaint / MousePaint layout:
    //   ┌──────────────── top bar: page · file · help ─────────────────┐
    //   │ tools │            drawing canvas                            │
    //   │ (left)│                                                      │
    //   ├───────┴──────────────────────────────────────────────────────
    //   │ colour palette (bottom) · status line                        │
    renderTopBar();
    ImGui::Separator();

    // Reserve the bottom strip for the colour palette + status line, then split
    // the rest into the left tool palette and the drawing canvas.
    const float bottomH = 30.0f
                        + ImGui::GetTextLineHeightWithSpacing()
                        + ImGui::GetStyle().ItemSpacing.y * 3.0f;
    const float panelW = 146.0f;

    ImGui::BeginChild("hgrbody", ImVec2(0.0f, -bottomH), false);
    {
        ImGui::BeginChild("hgrtools", ImVec2(panelW, 0.0f), true);
        renderToolPanel();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("hgrcanvasside", ImVec2(0.0f, 0.0f), false);
        renderCanvas(memory);
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::Separator();
    renderColorBar();
    renderStatusBar(lastHoverX, lastHoverY, lastHoverX >= 0);

    renderFileBrowser();   // modal Load / Save / Save PNG picker
}
