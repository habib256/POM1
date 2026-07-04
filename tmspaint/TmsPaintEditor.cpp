// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TMS9918 paint editor — see TmsPaintEditor.h. Sibling of hgrpaint/HgrPaintEditor.

#include "TmsPaintEditor.h"
#include "TmsConvert.h"          // image → TMS9918 import (ii-pix style)
#include "HgrFont.h"             // shared bbfont CP437 glyph table (hgrpaint/)

#include "imgui.h"
#include "IconsFontAwesome6.h"
// No GL/GLFW include: texture work goes through ITmsPaintHost::uploadTexture/
// destroyTexture so this portable module stays backend-agnostic.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

namespace {

// microSD "SD CARD OS" tagged-filename helpers (see src/MicroSD.cpp::parseTag).
// A file's type + load address live in its NAME as "NAME#TTAAAA". A raw 16 KB
// VRAM dump is not directly displayable (VRAM sits behind $CC00/$CC01), so the
// tag address is the CPU-RAM address the SD OS `@L NAME` drops the 16 KB at for
// the resident TMSLOAD utility to copy into VRAM — a FIXED $0800 (type 06 = BIN).
// DIAPO streams via the MCU and ignores the address; the tag is for TMSLOAD.
constexpr uint16_t kSdLoadAddr = 0x0800;

std::string sdCardDefaultName(std::string base)
{
    // Drop a legacy ".tms" extension (any case) — the tag replaces it.
    auto dot = base.find_last_of('.');
    if (dot != std::string::npos) {
        std::string ext = base.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == "tms") base.erase(dot);
    }
    if (base.empty()) base = "IMAGE";
    if (base.find('#') != std::string::npos) return base;   // already tagged
    char t[8];
    std::snprintf(t, sizeof(t), "#06%04X", kSdLoadAddr);
    return base + t;
}

// Fixed zoom ladder (texture pixels → screen). Mouse-wheel + Fit step the index.
const int kZoomLadder[] = { 1, 2, 3, 4, 6, 8, 12, 16 };
const int kZoomLadderCount = static_cast<int>(sizeof(kZoomLadder) / sizeof(kZoomLadder[0]));

// The 15 TMS9918 colours + transparent, byte-identical to TMS9918::kPalette
// (mirrored here so the portable editor needs no emulator header). Index 0 is
// transparent — drawn as dark grey in the swatch bar so it reads distinct from
// index 1 (black), but its semantic value written to VRAM is 0.
const ImU32 kSwatch[16] = {
    IM_COL32( 40,  40,  40, 255), // 0  Transparent (shown grey)
    IM_COL32(  0,   0,   0, 255), // 1  Black
    IM_COL32( 33, 200,  66, 255), // 2  Medium Green
    IM_COL32( 94, 220, 120, 255), // 3  Light Green
    IM_COL32( 84,  85, 237, 255), // 4  Dark Blue
    IM_COL32(125, 118, 252, 255), // 5  Light Blue
    IM_COL32(212,  82,  77, 255), // 6  Dark Red
    IM_COL32( 66, 235, 245, 255), // 7  Cyan
    IM_COL32(252,  85,  84, 255), // 8  Medium Red
    IM_COL32(255, 121, 120, 255), // 9  Light Red
    IM_COL32(212, 193,  84, 255), // 10 Dark Yellow
    IM_COL32(230, 206, 128, 255), // 11 Light Yellow
    IM_COL32( 33, 176,  59, 255), // 12 Dark Green
    IM_COL32(201,  91, 186, 255), // 13 Magenta
    IM_COL32(204, 204, 204, 255), // 14 Grey
    IM_COL32(255, 255, 255, 255), // 15 White
};

const char* kColorName[16] = {
    "Transparent", "Black", "Med Green", "Lt Green", "Dk Blue", "Lt Blue",
    "Dk Red", "Cyan", "Med Red", "Lt Red", "Dk Yellow", "Lt Yellow",
    "Dk Green", "Magenta", "Grey", "White",
};

const char* const kToolIcons[9] = {
    ICON_FA_PENCIL,        // Pencil
    ICON_FA_ERASER,        // Eraser
    ICON_FA_SLASH,         // Line
    ICON_FA_SQUARE,        // Rectangle
    ICON_FA_CIRCLE,        // Ellipse
    ICON_FA_FILL_DRIP,     // Fill
    ICON_FA_EYE_DROPPER,   // Eyedropper
    ICON_FA_VECTOR_SQUARE, // Select
    ICON_FA_FONT,          // Text
};

} // namespace

using tmspaint::Mode;

tmspaint::TmsPaintEditor::TmsPaintEditor(ITmsPaintHost* host_)
    : host(host_),
      canvasRgba(static_cast<size_t>(kGfx2Width) * kGfx2Height, 0),
      shadow(kVramSize, 0)
{
    tmspaint::canonicalRegisters(mode_, regs_);
    tmspaint::writeCanonicalNameTable(shadow.data(), mode_);
}

tmspaint::TmsPaintEditor::~TmsPaintEditor()
{
    releaseGL();
}

void tmspaint::TmsPaintEditor::releaseGL()
{
    if (host) {
        if (texture)          host->destroyTexture(texture);
        if (importPreviewTex) host->destroyTexture(importPreviewTex);
        if (importSrcTex)     host->destroyTexture(importSrcTex);
    }
    texture = importPreviewTex = importSrcTex = nullptr;
}

// ─────────────────────────────────────────────────────────────
// Mode programming
// ─────────────────────────────────────────────────────────────

void tmspaint::TmsPaintEditor::applyModeToChip()
{
    if (!host) return;
    host->applyRegisters(regs_);
    // Push the canonical name table so the live chip interprets the pattern /
    // colour tables as the editor does. Pattern + colour bytes are left as the
    // chip already has them (the drawing surface).
    host->beginBatch();
    for (int i = 0; i < 768; ++i)
        host->pokeVram(static_cast<uint16_t>(kNameBase + i), shadow[kNameBase + i]);
    host->endBatch();
}

void tmspaint::TmsPaintEditor::setMode(Mode m)
{
    if (dragging) { commitStroke(); dragging = false; }
    undo.clear();
    redo.clear();
    hasSel = false; pasting = false; textPlaced = false;
    mode_ = m;
    tmspaint::canonicalRegisters(mode_, regs_);
    tmspaint::writeCanonicalNameTable(shadow.data(), mode_);
    applyModeToChip();
    modeApplied = true;
    wantFit = true;
    status = (m == Mode::Multicolor) ? "Multicolor (64x48)" : "Graphics II (256x192)";
}

void tmspaint::TmsPaintEditor::ensureModeApplied()
{
    // Entering the editor is non-destructive — the canvas just shows the live
    // card. The canonical paint layout (registers + name table) is only pushed to
    // the chip when the user actually starts editing, so viewing never disturbs a
    // running program's display.
    if (modeApplied) return;
    applyModeToChip();
    modeApplied = true;
}

// ─────────────────────────────────────────────────────────────
// Painting primitives
// ─────────────────────────────────────────────────────────────

void tmspaint::TmsPaintEditor::beginStroke(bool batch)
{
    ensureModeApplied();   // first edit programs the chip into the paint layout
    stroke.clear();
    strokeBatching = batch && host;
    if (strokeBatching) host->beginBatch();
}

void tmspaint::TmsPaintEditor::commitStroke()
{
    if (strokeBatching) { host->endBatch(); strokeBatching = false; }
    if (stroke.empty()) return;
    undo.push_back(std::move(stroke));
    stroke.clear();
    if (undo.size() > 64) undo.erase(undo.begin());
    redo.clear();
}

void tmspaint::TmsPaintEditor::recordByte(uint16_t addr, uint8_t old)
{
    if (shadow[addr] == old) return;
    stroke.push_back({addr, old, shadow[addr]});
    if (host) host->pokeVram(addr, shadow[addr]);
}

void tmspaint::TmsPaintEditor::applyPlot(int x, int y, int c)
{
    // The model may touch up to two bytes (pattern + colour in Graphics II, or a
    // single block byte in Multicolor). Snapshot those byte addresses, plot, then
    // record whichever changed so undo + the live pokes stay exact.
    int a0, a1;
    if (mode_ == Mode::Multicolor) { a0 = tmspaint::mcBlockAddr(x, y); a1 = -1; }
    else { a0 = tmspaint::gfx2PatternAddr(x, y); a1 = tmspaint::gfx2ColorAddr(x, y); }
    if (a0 < 0) return;
    const uint8_t o0 = shadow[a0];
    const uint8_t o1 = (a1 >= 0) ? shadow[a1] : 0;
    if (!tmspaint::plotPixel(shadow.data(), mode_, x, y, c)) return;
    recordByte(static_cast<uint16_t>(a0), o0);
    if (a1 >= 0) recordByte(static_cast<uint16_t>(a1), o1);
}

void tmspaint::TmsPaintEditor::paintBrush(int cx, int cy, int c)
{
    const int r = brushSize - 1;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            applyPlot(cx + dx, cy + dy, c);
}

void tmspaint::TmsPaintEditor::paintLine(int x0, int y0, int x1, int y1, int c)
{
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

void tmspaint::TmsPaintEditor::paintRect(int x0, int y0, int x1, int y1, int c, bool filled)
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

void tmspaint::TmsPaintEditor::paintEllipse(int x0, int y0, int x1, int y1, int c, bool filled)
{
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    const int a = (x1 - x0) / 2, b = (y1 - y0) / 2;
    const int cx = x0 + a, cy = y0 + b;
    if (a <= 0 && b <= 0) { paintBrush(cx, cy, c); return; }
    if (a <= 0) { paintLine(cx, y0, cx, y1, c); return; }
    if (b <= 0) { paintLine(x0, cy, x1, cy, c); return; }

    auto emit = [&](int ex, int ey) {
        if (filled) {
            for (int x = cx - ex; x <= cx + ex; ++x) {
                applyPlot(x, cy + ey, c);
                applyPlot(x, cy - ey, c);
            }
        } else {
            paintBrush(cx + ex, cy + ey, c);
            paintBrush(cx - ex, cy + ey, c);
            paintBrush(cx + ex, cy - ey, c);
            paintBrush(cx - ex, cy - ey, c);
        }
    };

    const long a2 = static_cast<long>(a) * a, b2 = static_cast<long>(b) * b;
    long x = 0, y = b, dx = 0, dy = 2 * a2 * y;
    long d1 = b2 - a2 * b + a2 / 4;
    emit(static_cast<int>(x), static_cast<int>(y));
    while (dx < dy) {
        x++; dx += 2 * b2;
        if (d1 < 0) d1 += b2 + dx;
        else { y--; dy -= 2 * a2; d1 += b2 + dx - dy; }
        emit(static_cast<int>(x), static_cast<int>(y));
    }
    long d2 = b2 * (x * 2 + 1) * (x * 2 + 1) / 4 + a2 * (y - 1) * (y - 1) - a2 * b2;
    while (y > 0) {
        y--; dy -= 2 * a2;
        if (d2 > 0) d2 += a2 - dy;
        else { x++; dx += 2 * b2; d2 += a2 - dy + dx; }
        emit(static_cast<int>(x), static_cast<int>(y));
    }
}

void tmspaint::TmsPaintEditor::floodFill(int x, int y, int c)
{
    if (x < 0 || x >= cw() || y < 0 || y >= ch()) return;
    // The model floods on a buffer; snapshot, run, then diff into per-byte edits
    // so undo + the live pokes work.
    std::vector<uint8_t> before = shadow;
    tmspaint::fillRegion(shadow.data(), mode_, x, y, c);
    for (int off = 0; off < kVramSize; ++off) {
        if (shadow[off] == before[off]) continue;
        stroke.push_back({static_cast<uint16_t>(off), before[off], shadow[off]});
        if (host) host->pokeVram(static_cast<uint16_t>(off), shadow[off]);
    }
}

void tmspaint::TmsPaintEditor::applyOps(const std::vector<ByteEdit>& ops, bool forward)
{
    auto write = [&](const ByteEdit& e, uint8_t val) {
        shadow[e.addr] = val;
        if (host) host->pokeVram(e.addr, val);
    };
    if (host) host->beginBatch();
    if (forward) for (const auto& e : ops) write(e, e.newVal);
    else for (auto it = ops.rbegin(); it != ops.rend(); ++it) write(*it, it->oldVal);
    if (host) host->endBatch();
}

void tmspaint::TmsPaintEditor::doUndo()
{
    if (undo.empty()) return;
    auto ops = std::move(undo.back()); undo.pop_back();
    applyOps(ops, false);
    redo.push_back(std::move(ops));
    if (redo.size() > 64) redo.erase(redo.begin());
}

void tmspaint::TmsPaintEditor::doRedo()
{
    if (redo.empty()) return;
    auto ops = std::move(redo.back()); redo.pop_back();
    applyOps(ops, true);
    undo.push_back(std::move(ops));
    if (undo.size() > 64) undo.erase(undo.begin());
}

void tmspaint::TmsPaintEditor::clearPage()
{
    // Zero the pattern table (and, in Graphics II, the colour table) so the canvas
    // shows the backdrop everywhere; keep the canonical name table intact.
    std::vector<uint8_t> before = shadow;
    for (int i = 0; i < tmspaint::kNameBase; ++i) shadow[i] = 0;
    if (mode_ == Mode::GraphicsII)
        for (int i = tmspaint::kGfx2ColorBase; i < kVramSize; ++i) shadow[i] = 0;
    tmspaint::writeCanonicalNameTable(shadow.data(), mode_);
    beginStroke(true);
    for (int off = 0; off < kVramSize; ++off) {
        if (shadow[off] == before[off]) continue;
        stroke.push_back({static_cast<uint16_t>(off), before[off], shadow[off]});
        if (host) host->pokeVram(static_cast<uint16_t>(off), shadow[off]);
    }
    commitStroke();
}

// ─────────────────────────────────────────────────────────────
// Selection / clipboard
// ─────────────────────────────────────────────────────────────

void tmspaint::TmsPaintEditor::copySelection(bool cut)
{
    if (!hasSel) return;
    const int x0 = std::min(selX0, selX1), x1 = std::max(selX0, selX1);
    const int y0 = std::min(selY0, selY1), y1 = std::max(selY0, selY1);
    clip.w = x1 - x0 + 1;
    clip.h = y1 - y0 + 1;
    clip.px.assign(static_cast<size_t>(clip.w) * clip.h, 0);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const int c = tmspaint::colorAt(shadow.data(), mode_, x, y);
            clip.px[static_cast<size_t>(y - y0) * clip.w + (x - x0)] =
                static_cast<uint8_t>(c < 0 ? 0 : c);
        }
    if (cut) {
        beginStroke(true);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                applyPlot(x, y, 0);
        commitStroke();
    }
}

void tmspaint::TmsPaintEditor::pasteFloatingAt(int destX, int destY)
{
    if (clip.w <= 0) return;
    beginStroke(true);
    for (int y = 0; y < clip.h; ++y)
        for (int x = 0; x < clip.w; ++x) {
            const int c = clip.px[static_cast<size_t>(y) * clip.w + x];
            if (c == 0) continue;   // transparent overlay paste
            applyPlot(destX + x, destY + y, c);
        }
    commitStroke();
}

void tmspaint::TmsPaintEditor::stampText(const char* text, int c)
{
    if (!textPlaced || !text || !*text) return;
    using namespace hgrpaint;   // bbFontPixel + glyph constants
    beginStroke(true);
    int cx = textX, cy = textY;
    for (const char* p = text; *p; ++p) {
        const unsigned char chc = static_cast<unsigned char>(*p);
        if (chc == '\n') { cx = textHomeX; cy += kBBFontGlyphH; continue; }
        if (cx + kBBFontGlyphW > cw()) { cx = textHomeX; cy += kBBFontGlyphH; }
        if (cy >= ch()) break;
        for (int gy = 0; gy < kBBFontGlyphH; ++gy)
            for (int gx = 0; gx < kBBFontGlyphW; ++gx)
                if (bbFontPixel(chc, gx, gy))
                    applyPlot(cx + gx, cy + gy, c);
        cx += kBBFontAdvance;
    }
    commitStroke();
    textY = std::min(cy + kBBFontGlyphH, ch() - 1);
    textX = textHomeX;
}

// ─────────────────────────────────────────────────────────────
// UI
// ─────────────────────────────────────────────────────────────

void tmspaint::TmsPaintEditor::renderMinimap()
{
    if (canvasScale <= 0.0f) return;
    const bool overflow = (canvasScrollMaxX > 1.0f) || (canvasScrollMaxY > 1.0f);
    if (!overflow) return;

    const float mmW = std::min(ImGui::GetContentRegionAvail().x, 130.0f);
    const float mmH = mmW * static_cast<float>(kGfx2Height) / static_cast<float>(kGfx2Width);
    ImGui::TextDisabled("Navigator");
    const ImVec2 mmMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##tmsminimap", ImVec2(mmW, mmH));
    const bool active = ImGui::IsItemActive();
    const ImVec2 mmMax(mmMin.x + mmW, mmMin.y + mmH);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(mmMin, mmMax, IM_COL32(0, 0, 0, 255));
    dl->AddImage(host->textureToImTexture(texture), mmMin, mmMax);
    dl->AddRect(mmMin, mmMax, IM_COL32(160, 160, 160, 255));

    const float lx0 = canvasScrollX / canvasScale;
    const float ly0 = canvasScrollY / canvasScale;
    const float lw  = canvasViewW  / canvasScale;
    const float lh  = canvasViewH  / canvasScale;
    auto mmx = [&](float lx){ return mmMin.x + (lx / kGfx2Width)  * mmW; };
    auto mmy = [&](float ly){ return mmMin.y + (ly / kGfx2Height) * mmH; };
    dl->AddRect(ImVec2(mmx(lx0), mmy(ly0)), ImVec2(mmx(lx0 + lw), mmy(ly0 + lh)),
                IM_COL32(255, 255, 0, 230), 0, 0, 1.5f);

    if (active) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float fx = std::clamp((m.x - mmMin.x) / mmW, 0.0f, 1.0f);
        const float fy = std::clamp((m.y - mmMin.y) / mmH, 0.0f, 1.0f);
        pendingScrollX = std::clamp(fx * kGfx2Width  * canvasScale - canvasViewW * 0.5f, 0.0f, canvasScrollMaxX);
        pendingScrollY = std::clamp(fy * kGfx2Height * canvasScale - canvasViewH * 0.5f, 0.0f, canvasScrollMaxY);
    }
}

void tmspaint::TmsPaintEditor::renderTopBar()
{
    // Mode selector: programs the canonical register set + name table onto the
    // live chip and refits the canvas.
    int m = static_cast<int>(mode_);
    ImGui::TextUnformatted("Mode:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(190.0f);
    if (ImGui::Combo("##tmsmode", &m, "Graphics II (256x192)\0Multicolor (64x48)\0"))
        setMode(static_cast<Mode>(m));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Switch canvas mode (reprograms the card, clears undo history)");
    ImGui::SameLine();
    if (ImGui::Button("Apply mode to card")) { applyModeToChip(); modeApplied = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Re-program the TMS9918 registers + name table so the\n"
                          "live Graphic Card window shows what you paint");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Mouse\n"
            "  Left drag    draw with the current tool / colour\n"
            "  Right drag   quick-erase (paint transparent) — no tool switch\n"
            "  Middle drag  pan the canvas\n"
            "  Wheel        zoom, anchored on the cursor\n"
            "  Alt + Left   eyedropper (pick colour)\n"
            "  Shift        constrain Line to 0/45/90 deg, Rect/Ellipse to a square\n"
            "\n"
            "Keys\n"
            "  P E L R O F I S T   pencil eraser line rect ellipse fill eyedrop select text\n"
            "  0-9 colours   [ ] thickness   +/- zoom   G grid   X toggle filled\n"
            "  Ctrl+Z / Ctrl+Y undo/redo   Ctrl+C/X/V copy/cut/paste");

    renderFileRow();
}

void tmspaint::TmsPaintEditor::renderToolPanel()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 kSelTint   = IM_COL32(58, 96, 150, 255);
    const ImU32 kSelHover  = IM_COL32(78, 116, 170, 255);
    const ImU32 kSelBorder = IM_COL32(255, 220, 60, 255);

    const char* toolTips[] = {
        "Pencil (P)", "Eraser (E)", "Line (L)", "Rectangle (R)", "Ellipse (O)",
        "Fill (F)", "Eyedropper (I)", "Select (S)", "Text (T)" };
    const ImVec2 btnSz(34, 34);
    for (int i = 0; i < kToolCount; ++i) {
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

    const bool usesThickness = (tool == Tool::Pencil || tool == Tool::Eraser ||
                                tool == Tool::Line || tool == Tool::Rectangle ||
                                tool == Tool::Ellipse);
    if (usesThickness) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##thickness", &brushSize, 1, 7, "Thickness %d px");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stroke thickness / brush size  ([ and ])");
    }
    if (tool == Tool::Rectangle || tool == Tool::Ellipse)
        ImGui::Checkbox("Filled", &rectFilled);
    if (tool == Tool::Select) {
        if (ImGui::Button("Copy")) copySelection(false);
        ImGui::SameLine();
        if (ImGui::Button("Cut"))  copySelection(true);
        if (ImGui::Button("Paste") && clip.w > 0) {
            if (dragging) { commitStroke(); dragging = false; }
            pasting = true; pasteX = std::min(selX0, selX1); pasteY = std::min(selY0, selY1);
        }
    }
    if (tool == Tool::Text) {
        ImGui::TextWrapped("%s", textPlaced
            ? "Type, Enter to stamp in the current colour. Click to move the caret."
            : "Click the canvas to place the text caret.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool enter = ImGui::InputText("##textbuf", textBuf, sizeof(textBuf),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        const bool stamp = ImGui::Button("Stamp") || enter;
        if (stamp && textPlaced && textBuf[0]) { stampText(textBuf, color); textBuf[0] = '\0'; }
        ImGui::SameLine();
        ImGui::TextDisabled(textPlaced ? "@ %d,%d" : "(no caret)", textX, textY);
    }

    ImGui::Separator();

    if (ImGui::Button("-##zoom")) zoomIdx = std::max(zoomIdx - 1, 0);
    ImGui::SameLine();
    ImGui::Text("%dx", kZoomLadder[zoomIdx]);
    ImGui::SameLine();
    if (ImGui::Button("+##zoom")) zoomIdx = std::min(zoomIdx + 1, kZoomLadderCount - 1);
    ImGui::SameLine();
    if (ImGui::Button("Fit")) wantFit = true;

    ImGui::Separator();
    ImGui::Checkbox("Grid", &showGrid);

    ImGui::Separator();
    if (ImGui::Button("Undo")) doUndo();
    ImGui::SameLine();
    if (ImGui::Button("Redo")) doRedo();
    if (ImGui::Button("Clear page")) clearPage();

    ImGui::Separator();
    renderMinimap();
}

void tmspaint::TmsPaintEditor::renderColorBar()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 swSz(30, 24);
    for (int i = 0; i < 16; ++i) {
        if (i % 8 != 0) ImGui::SameLine();
        const bool sel = (i == color);
        ImGui::PushID(200 + i);
        ImGui::PushStyleColor(ImGuiCol_Button, kSwatch[i]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kSwatch[i]);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kSwatch[i]);
        if (ImGui::Button("##sw", swSz)) color = i;
        ImGui::PopStyleColor(3);
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        if (sel) dl->AddRect(a, b, IM_COL32(255, 220, 60, 255), 0, 0, 2.5f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%d)", kColorName[i], i);
        ImGui::PopID();
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("  Colour: %s", kColorName[color]);
}

void tmspaint::TmsPaintEditor::renderCanvas()
{
    // Mirror the live chip: refresh the shadow from the chip's actual VRAM (the
    // read source for the drawing model — colour/flood/eyedropper), then keep the
    // name table canonical (the editor owns the layout for its addressing).
    if (host) host->readVram(shadow.data());
    tmspaint::writeCanonicalNameTable(shadow.data(), mode_);

    // Display the chip's OWN framebuffer — exactly what the Graphic Card window
    // shows — so the canvas never diverges from the real card. Fall back to a
    // VRAM re-render only if the host has no live framebuffer (headless).
    if (!host || !host->liveFramebuffer(canvasRgba.data())) {
        if (host) host->renderVram(shadow.data(), regs_, canvasRgba.data());
    }
    if (host)
        texture = host->uploadTexture(texture, canvasRgba.data(),
                                      kGfx2Width, kGfx2Height, /*linear=*/false);

    const int lp = lpx();
    float scale = static_cast<float>(kZoomLadder[zoomIdx]);
    ImVec2 imgSize(kGfx2Width * scale, kGfx2Height * scale);

    ImGui::BeginChild("tmscanvas", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    if (pendingScrollX >= 0) { ImGui::SetScrollX(pendingScrollX); pendingScrollX = -1; }
    if (pendingScrollY >= 0) { ImGui::SetScrollY(pendingScrollY); pendingScrollY = -1; }

    if (firstFit) { firstFit = false; wantFit = true; }
    if (wantFit) {
        wantFit = false;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        int best = 0;
        for (int i = 0; i < kZoomLadderCount; ++i)
            if (kGfx2Width * kZoomLadder[i] <= avail.x && kGfx2Height * kZoomLadder[i] <= avail.y)
                best = i;
        zoomIdx = best;
        scale = static_cast<float>(kZoomLadder[zoomIdx]);
        imgSize = ImVec2(kGfx2Width * scale, kGfx2Height * scale);
    }

    {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f) {
            const int oldZoom = kZoomLadder[zoomIdx];
            const int ni = std::clamp(zoomIdx + (io.MouseWheel > 0 ? 1 : -1), 0, kZoomLadderCount - 1);
            if (ni != zoomIdx) {
                const ImVec2 cur = ImGui::GetCursorScreenPos();
                const float anchorLX = (io.MousePos.x - cur.x) / oldZoom;
                const float anchorLY = (io.MousePos.y - cur.y) / oldZoom;
                zoomIdx = ni;
                const int newZoom = kZoomLadder[zoomIdx];
                scale = static_cast<float>(newZoom);
                imgSize = ImVec2(kGfx2Width * scale, kGfx2Height * scale);
                const float mouseInChildX = io.MousePos.x - cur.x + ImGui::GetScrollX();
                const float mouseInChildY = io.MousePos.y - cur.y + ImGui::GetScrollY();
                ImGui::SetScrollX(anchorLX * newZoom - (mouseInChildX - ImGui::GetScrollX()));
                ImGui::SetScrollY(anchorLY * newZoom - (mouseInChildY - ImGui::GetScrollY()));
            }
        }
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##tmscanvasimg", imgSize,
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddImage(host->textureToImTexture(texture), origin,
                 ImVec2(origin.x + imgSize.x, origin.y + imgSize.y));

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning = true;
    if (panning && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        ImGui::SetScrollX(ImGui::GetScrollX() - d.x);
        ImGui::SetScrollY(ImGui::GetScrollY() - d.y);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) panning = false;

    // Logical-cell grid (a logical pixel = lp texture px; show cell boundaries).
    if (showGrid && kZoomLadder[zoomIdx] * lp >= 3) {
        const ImU32 gcol = IM_COL32(80, 80, 80, 90);
        const int stepX = (mode_ == Mode::GraphicsII) ? 8 : 1;   // 8×1 clash cell / block
        const int stepY = (mode_ == Mode::GraphicsII) ? 8 : 1;
        for (int x = 0; x <= cw(); x += stepX) {
            const float fx = origin.x + x * lp * scale;
            dl->AddLine(ImVec2(fx, origin.y), ImVec2(fx, origin.y + imgSize.y), gcol);
        }
        for (int y = 0; y <= ch(); y += stepY) {
            const float fy = origin.y + y * lp * scale;
            dl->AddLine(ImVec2(origin.x, fy), ImVec2(origin.x + imgSize.x, fy), gcol);
        }
    }

    // Mouse → logical pixel (texture px / lp).
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int lx = std::clamp(static_cast<int>((mouse.x - origin.x) / scale) / lp, 0, cw() - 1);
    int ly = std::clamp(static_cast<int>((mouse.y - origin.y) / scale) / lp, 0, ch() - 1);
    if (hovered) { lastHoverX = lx; lastHoverY = ly; }

    const bool altDown = ImGui::GetIO().KeyAlt;
    const bool eyedrop = (tool == Tool::Eyedropper) || altDown;
    const int activeColor = (tool == Tool::Eraser) ? 0 : color;

    // Logical pixel (lx,ly) → screen rect helper (spans lp texture px).
    auto cellMin = [&](int gx, int gy) {
        return ImVec2(origin.x + gx * lp * scale, origin.y + gy * lp * scale);
    };
    auto cellMax = [&](int gx, int gy) {
        return ImVec2(origin.x + (gx + 1) * lp * scale, origin.y + (gy + 1) * lp * scale);
    };

    auto constrainEnd = [&]() -> std::pair<int,int> {
        int ex = lx, ey = ly;
        if (ImGui::GetIO().KeyShift) {
            const int ddx = ex - dragStartX, ddy = ey - dragStartY;
            if (tool == Tool::Line) {
                const int adx = std::abs(ddx), ady = std::abs(ddy);
                if (adx > 2 * ady)      ey = dragStartY;
                else if (ady > 2 * adx) ex = dragStartX;
                else { const int m = (adx + ady) / 2;
                       ex = dragStartX + (ddx < 0 ? -m : m);
                       ey = dragStartY + (ddy < 0 ? -m : m); }
            } else if (tool == Tool::Rectangle || tool == Tool::Ellipse) {
                const int m = std::max(std::abs(ddx), std::abs(ddy));
                ex = dragStartX + (ddx < 0 ? -m : m);
                ey = dragStartY + (ddy < 0 ? -m : m);
            }
            ex = std::clamp(ex, 0, cw() - 1);
            ey = std::clamp(ey, 0, ch() - 1);
        }
        return {ex, ey};
    };

    // Brush-footprint cursor preview.
    if (hovered && !dragging && !eyedrop && (tool == Tool::Pencil || tool == Tool::Eraser)) {
        const ImU32 ghost = (kSwatch[activeColor] & 0x00FFFFFF) | 0x80000000;
        const int r = brushSize - 1;
        dl->AddRect(cellMin(lx - r, ly - r), cellMax(lx + r, ly + r), ghost);
    }

    if (tool == Tool::Text && textPlaced) {
        const bool phase = (static_cast<int>(ImGui::GetTime() * 2.0) & 1) != 0;
        dl->AddRect(cellMin(textX, textY),
                    cellMax(textX + hgrpaint::kBBFontGlyphW - 1, textY + hgrpaint::kBBFontGlyphH - 1),
                    phase ? IM_COL32(255, 220, 60, 235) : IM_COL32(255, 220, 60, 110));
    }

    if (hasSel && !pasting) {
        const int sx0 = std::min(selX0, selX1), sx1 = std::max(selX0, selX1);
        const int sy0 = std::min(selY0, selY1), sy1 = std::max(selY0, selY1);
        const ImVec2 a = cellMin(sx0, sy0), b = cellMax(sx1, sy1);
        const bool phase = (static_cast<int>(ImGui::GetTime() * 4.0) & 1) != 0;
        dl->AddRect(a, b, phase ? IM_COL32(255,255,255,255) : IM_COL32(0,0,0,255));
        dl->AddRect(ImVec2(a.x-1,a.y-1), ImVec2(b.x+1,b.y+1),
                    phase ? IM_COL32(0,0,0,255) : IM_COL32(255,255,255,255));
    }

    if (pasting) {
        if (hovered) { pasteX = lx; pasteY = ly; }
        for (int cy = 0; cy < clip.h; ++cy)
            for (int cxx = 0; cxx < clip.w; ++cxx) {
                const int c = clip.px[static_cast<size_t>(cy) * clip.w + cxx];
                if (c == 0) continue;
                dl->AddRectFilled(cellMin(pasteX + cxx, pasteY + cy), cellMax(pasteX + cxx, pasteY + cy),
                                  (kSwatch[c] & 0x00FFFFFF) | 0xC0000000);
            }
        dl->AddRect(cellMin(pasteX, pasteY), cellMax(pasteX + clip.w - 1, pasteY + clip.h - 1),
                    IM_COL32(255, 255, 0, 230));
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { pasteFloatingAt(pasteX, pasteY); pasting = false; }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) pasting = false;
    } else {
        if (!dragging && tool != Tool::Select && !eyedrop) {
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                rmbErase = true; beginStroke();
                lastX = lx; lastY = ly;
                paintBrush(lx, ly, 0);
            } else if (rmbErase && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                if (lx != lastX || ly != lastY) { paintLine(lastX, lastY, lx, ly, 0); lastX = lx; lastY = ly; }
            }
        }
        if (rmbErase && (ImGui::IsMouseReleased(ImGuiMouseButton_Right) ||
                         !ImGui::IsMouseDown(ImGuiMouseButton_Right))) {
            commitStroke(); rmbErase = false;
        }

        if (!rmbErase && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (eyedrop) {
                const int c = tmspaint::colorAt(shadow.data(), mode_, lx, ly);
                if (c >= 0) color = c;
                if (tool == Tool::Eyedropper) tool = prevTool;
            } else if (tool == Tool::Select) {
                dragging = true; hasSel = true;
                dragStartX = lx; dragStartY = ly;
                selX0 = selX1 = lx; selY0 = selY1 = ly;
            } else if (tool == Tool::Text) {
                textPlaced = true; textX = lx; textY = ly; textHomeX = lx;
            } else {
                dragging = true;
                dragStartX = lx; dragStartY = ly; lastX = lx; lastY = ly;
                const bool bulk = (tool == Tool::Line || tool == Tool::Rectangle ||
                                   tool == Tool::Ellipse || tool == Tool::Fill);
                beginStroke(bulk);
                if (tool == Tool::Pencil || tool == Tool::Eraser) paintBrush(lx, ly, activeColor);
                else if (tool == Tool::Fill) { floodFill(lx, ly, activeColor); commitStroke(); dragging = false; }
            }
        }

        if (dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (tool == Tool::Pencil || tool == Tool::Eraser) {
                if (lx != lastX || ly != lastY) { paintLine(lastX, lastY, lx, ly, activeColor); lastX = lx; lastY = ly; }
            } else if (tool == Tool::Select) {
                selX1 = lx; selY1 = ly;
            } else if (tool == Tool::Line) {
                const auto [ex, ey] = constrainEnd();
                const ImVec2 a = cellMin(dragStartX, dragStartY), b = cellMin(ex, ey);
                dl->AddLine(ImVec2(a.x + lp*scale*0.5f, a.y + lp*scale*0.5f),
                            ImVec2(b.x + lp*scale*0.5f, b.y + lp*scale*0.5f), kSwatch[activeColor], 1.5f);
            } else if (tool == Tool::Rectangle) {
                const auto [ex, ey] = constrainEnd();
                const ImVec2 a = cellMin(std::min(dragStartX,ex), std::min(dragStartY,ey));
                const ImVec2 b = cellMax(std::max(dragStartX,ex), std::max(dragStartY,ey));
                if (rectFilled) dl->AddRectFilled(a, b, (kSwatch[activeColor] & 0x00FFFFFF) | 0x80000000);
                else            dl->AddRect(a, b, kSwatch[activeColor], 0, 0, 1.5f);
            } else if (tool == Tool::Ellipse) {
                const auto [ex, ey] = constrainEnd();
                const ImVec2 center((cellMin(dragStartX,dragStartY).x + cellMax(ex,ey).x) * 0.5f,
                                    (cellMin(dragStartX,dragStartY).y + cellMax(ex,ey).y) * 0.5f);
                const ImVec2 radius(std::abs(ex - dragStartX) * 0.5f * lp * scale + 0.5f,
                                    std::abs(ey - dragStartY) * 0.5f * lp * scale + 0.5f);
                if (rectFilled) dl->AddEllipseFilled(center, radius, (kSwatch[activeColor] & 0x00FFFFFF) | 0x80000000);
                else            dl->AddEllipse(center, radius, kSwatch[activeColor], 0.0f, 0, 1.5f);
            }
        }

        if (dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const auto [ex, ey] = constrainEnd();
            if (tool == Tool::Line) paintLine(dragStartX, dragStartY, ex, ey, activeColor);
            else if (tool == Tool::Rectangle) paintRect(dragStartX, dragStartY, ex, ey, activeColor, rectFilled);
            else if (tool == Tool::Ellipse) paintEllipse(dragStartX, dragStartY, ex, ey, activeColor, rectFilled);
            else if (tool == Tool::Select) { selX1 = lx; selY1 = ly; }
            commitStroke();
            dragging = false;
        }
    }

    canvasScrollX    = ImGui::GetScrollX();
    canvasScrollY    = ImGui::GetScrollY();
    canvasScrollMaxX = ImGui::GetScrollMaxX();
    canvasScrollMaxY = ImGui::GetScrollMaxY();
    canvasViewW      = ImGui::GetWindowSize().x;
    canvasViewH      = ImGui::GetWindowSize().y;
    canvasScale      = scale;

    ImGui::EndChild();
}

void tmspaint::TmsPaintEditor::openFileBrowser(bool forSave, int saveKind, bool importMode)
{
    browserForSave = forSave;
    browserImport = importMode;
    browserSaveKind = saveKind;
    if (browserDir.empty()) {
        // Prefer the host's context folder (POM1 → sdcard/TMS/), else the CWD.
        if (host) browserDir = host->browseDir();
        if (browserDir.empty()) {
            std::error_code ec;
            browserDir = std::filesystem::current_path(ec).string();
            if (ec || browserDir.empty()) browserDir = ".";
        }
    }
    std::string defName;
    if (forSave) {
        std::string base = std::filesystem::path(filePath).filename().string();
        if (saveKind == 1) {                          // PNG export
            if (base.empty()) base = "image.png";
        } else {                                      // raw VRAM → SD-card tag
            base = sdCardDefaultName(base);
        }
        std::snprintf(browserSaveName, sizeof(browserSaveName), "%s", base.c_str());
        defName = base;
    }

    // Prefer the host's OS-native file picker; fall back to the in-process ImGui
    // browser below when the host has none (WASM, Linux without zenity/kdialog,
    // or a headless/test host). Matches the MainWindow dialogs' native+fallback.
    if (host) {
        std::string title, desc, ext;
        if (importMode)        { title = "Import picture"; desc = "Images (PNG, JPG, BMP)"; ext = "png,jpg,jpeg,bmp,gif,tga"; }
        else if (saveKind == 1){ title = "Export PNG";     desc = "PNG image";             ext = "png"; }
        else                   { title = forSave ? "Save VRAM image" : "Load VRAM image";
                                 // No extension filter: SD-card exports are named
                                 // NAME#060800 (no extension), so *.tms would hide
                                 // them. Empty ext = all files, and on save it also
                                 // disables the single-extension auto-append so the
                                 // #06 tag isn't mangled into "...#060800.tms".
                                 desc = forSave ? "TMS VRAM (saved as NAME#060800)"
                                                : "TMS VRAM / SD-card image (NAME#060800)";
                                 ext  = ""; }
        std::string picked;
        if (host->pickFilePath(forSave, title, desc, ext, browserDir, defName, picked)) {
            std::filesystem::path pp(picked);
            std::string dir = pp.parent_path().string();
            if (!dir.empty()) browserDir = dir;
            performFileAction(forSave, saveKind, importMode, picked);
            return;
        }
        // A native picker that returned false means the user CANCELLED (or it
        // errored) — stay put. Only fall back to the ImGui browser when the host
        // has no native picker at all (WASM, Linux without zenity/kdialog).
        if (host->nativeFilePickerAvailable()) return;
    }
    browserOpen = true;
}

bool tmspaint::TmsPaintEditor::performFileAction(bool forSave, int saveKind,
                                                 bool importMode,
                                                 const std::string& fullPath)
{
    namespace fs = std::filesystem;
    const std::string name = fs::path(fullPath).filename().string();

    if (!forSave && importMode) {
        openImportPreview(fullPath);          // leaves filePath alone
        return true;
    }
    if (!forSave) {                            // Load raw 16 KB VRAM
        std::snprintf(filePath, sizeof(filePath), "%s", fullPath.c_str());
        std::string err;
        if (host && host->loadVram(filePath, err)) status = "Loaded " + name;
        else status = "Load failed: " + (err.empty() ? std::string("(bad file)") : err);
        return true;
    }

    // Save (raw VRAM dump or PNG export).
    std::string outPath = fullPath;
    if (saveKind != 1) {
        // Guarantee the SD-CARD-OS tag on a raw VRAM save: if the name has no
        // '#', append "#060800" so `@L NAME` drops the 16 KB at $0800 for the
        // resident TMSLOAD utility. Basename only; directory preserved.
        fs::path outP(fullPath);
        if (outP.filename().string().find('#') == std::string::npos)
            outP = outP.parent_path() / sdCardDefaultName(outP.filename().string());
        outPath = outP.string();
    }
    std::snprintf(filePath, sizeof(filePath), "%s", outPath.c_str());
    std::string err;
    bool ok = false;
    if (saveKind == 1)
        ok = host && host->savePng(outPath, canvasRgba.data(), kGfx2Width, kGfx2Height, err);
    else
        ok = host && host->saveVram(outPath, err);
    status = ok ? ("Saved: " + fs::path(outPath).filename().string())
                : ((saveKind == 1 ? "PNG export failed: " : "Save failed: ")
                   + (err.empty() ? std::string("(error)") : err));
    return ok;
}

void tmspaint::TmsPaintEditor::renderFileBrowser()
{
    namespace fs = std::filesystem;
    if (browserOpen) { ImGui::OpenPopup("TMS File##browser"); browserOpen = false; }

    const ImVec2 vpCenter = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(vpCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 460), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("TMS File##browser", nullptr, ImGuiWindowFlags_NoCollapse))
        return;

    ImGui::TextUnformatted(browserForSave
        ? (browserSaveKind == 1 ? "Save PNG export" : "Save VRAM image (16 KB)")
        : (browserImport ? "Import picture (PNG / JPG / BMP) — converted to TMS9918"
                         : "Load VRAM image (pick a file — 16 KB ones are highlighted)"));
    ImGui::TextDisabled("%s", browserDir.c_str());
    ImGui::Separator();

    ImGui::BeginChild("##fblist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.0f), true);
    if (ImGui::Selectable("../", false)) {
        fs::path up = fs::path(browserDir).parent_path();
        if (!up.empty()) browserDir = up.string();
    }
    std::vector<fs::directory_entry> dirs, files;
    try {
        for (const auto& e : fs::directory_iterator(browserDir, fs::directory_options::skip_permission_denied)) {
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
        if (ImGui::Selectable((name + "/").c_str(), false)) browserDir = d.path().string();
    }
    for (const auto& f : files) {
        std::error_code ec;
        const std::uintmax_t sz = f.file_size(ec);
        const std::string name = f.path().filename().string();
        std::string ext = f.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(c));
        const bool isImg = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                            ext == ".bmp" || ext == ".gif" || ext == ".tga");
        const bool relevant = browserImport ? isImg : (!ec && sz == 16384);
        char label[320];
        std::snprintf(label, sizeof(label), "%-28s %8llu B", name.c_str(),
                      static_cast<unsigned long long>(sz));
        if (!relevant) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
        if (ImGui::Selectable(label, false)) {
            if (browserForSave) {
                std::snprintf(filePath, sizeof(filePath), "%s", f.path().string().c_str());
                std::snprintf(browserSaveName, sizeof(browserSaveName), "%s", name.c_str());
            } else {                       // Load or Import: act immediately
                performFileAction(false, browserSaveKind, browserImport, f.path().string());
                ImGui::CloseCurrentPopup();
            }
        }
        if (!relevant) ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    if (browserForSave) {
        ImGui::SetNextItemWidth(-160);
        ImGui::InputText("##fbname", browserSaveName, sizeof(browserSaveName));
        ImGui::SameLine();
        if (ImGui::Button("Save", ImVec2(70, 0)) && browserSaveName[0]) {
            const std::string full = (fs::path(browserDir) / browserSaveName).string();
            if (performFileAction(true, browserSaveKind, false, full))
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Cancel", ImVec2(70, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void tmspaint::TmsPaintEditor::openImportPreview(const std::string& path)
{
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    std::string err;
    if (!tmspaint::decodeImageFile(path, w, h, rgba, err)) {
        status = "Import failed: " + err;
        return;
    }
    importSrcRgba = std::move(rgba);
    importSrcW = w; importSrcH = h;
    importSrcName = std::filesystem::path(path).filename().string();
    importCropActive = false; importCropDragging = false;
    importDirty = true; importSrcTexDirty = true;
    importPreviewOpen = true;
}

void tmspaint::TmsPaintEditor::renderImportPreview()
{
    // Free the decoded source + its texture once the dialog has closed.
    if (!importPreviewOpen && !importSrcRgba.empty() &&
        !ImGui::IsPopupOpen("Import preview##tms")) {
        std::vector<uint8_t>().swap(importSrcRgba);
        std::vector<uint8_t>().swap(importVram);
        std::vector<uint32_t>().swap(importPreview);
        if (importSrcTex && host) { host->destroyTexture(importSrcTex); importSrcTex = nullptr; }
        // The 256×192 preview texture follows the same lifetime — drop it
        // here so it doesn't stick around for the whole session after Apply
        // / Cancel / Esc closes the popup.
        if (importPreviewTex && host) { host->destroyTexture(importPreviewTex); importPreviewTex = nullptr; }
        importSrcW = importSrcH = 0;
        importSrcTexDirty = true;
    }

    if (importPreviewOpen) { ImGui::OpenPopup("Import preview##tms"); importPreviewOpen = false; importDirty = true; }

    const ImVec2 vpCenter = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(vpCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(980.0f, 620.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Import preview##tms", nullptr, ImGuiWindowFlags_NoCollapse))
        return;

    ImGui::Text("Image \xE2\x86\x92 %s  (ii-pix: CAM16-UCS perceptual dithering)",
                mode_ == Mode::Multicolor ? "Multicolor" : "Graphics II");
    ImGui::TextDisabled("%s  \xE2\x80\x94  %d x %d source", importSrcName.c_str(), importSrcW, importSrcH);
    ImGui::Separator();

    const float ph = kGfx2Height * 2.0f;        // result preview height (384)
    float sw = ph * (importSrcH ? static_cast<float>(importSrcW) / importSrcH : 1.0f), sh = ph;
    if (sw > 360.0f) { sw = 360.0f; sh = importSrcW ? sw * importSrcH / importSrcW : ph; }
    sw = std::max(sw, 48.0f); sh = std::max(sh, 48.0f);

    auto cropUsable = [&]() {
        if (importSrcW <= 0 || importSrcH <= 0) return false;
        return (importCropX1 - importCropX0) * sw / importSrcW >= 5.0f &&
               (importCropY1 - importCropY0) * sh / importSrcH >= 5.0f;
    };

    // ── Reconvert + re-render when anything changed ──────────────────────────
    if (importDirty && !importSrcRgba.empty()) {
        importDirty = false;
        tmspaint::ImportOptions opt;
        opt.stretch    = importStretch;
        opt.dither     = importDither;
        opt.serpentine = importSerpentine;
        opt.diffusion  = importDiffusion;
        opt.brightness = importBrightness;
        opt.contrast   = importContrast;
        opt.gamma      = importGamma;
        opt.chromaWeight = 6.0f - importColourNoise * 5.2f;   // 0→6 clean, 1→0.8 vivid
        opt.kernel = importKernel ? hgrpaint::DitherKernel::JarvisMod
                                  : hgrpaint::DitherKernel::FloydSteinberg;
        if ((importCropActive || importCropDragging) && cropUsable()) {
            opt.cropX0 = importCropX0; opt.cropY0 = importCropY0;
            opt.cropX1 = importCropX1; opt.cropY1 = importCropY1;
        }
        importVram.assign(kVramSize, 0);
        tmspaint::imageToTmsVram(importSrcRgba.data(), importSrcW, importSrcH, mode_, opt, importVram.data());
        importPreview.assign(static_cast<size_t>(kGfx2Width) * kGfx2Height, 0);
        if (host) {
            host->renderVram(importVram.data(), regs_, importPreview.data());
            importPreviewTex = host->uploadTexture(importPreviewTex, importPreview.data(),
                                                   kGfx2Width, kGfx2Height, /*linear=*/false);
        }
    }

    if (importSrcTexDirty && !importSrcRgba.empty() && host) {
        importSrcTexDirty = false;
        importSrcTex = host->uploadTexture(importSrcTex, importSrcRgba.data(),
                                           importSrcW, importSrcH, /*linear=*/true);
    }

    // ── Side-by-side: source (left, with crop) | result (right) ──────────────
    ImGui::BeginGroup();
    ImGui::TextDisabled("Source  \xE2\x80\x94  drag to select a crop region");
    if (importSrcTex && importSrcW > 0 && importSrcH > 0) {
        const ImVec2 imgPos = ImGui::GetCursorScreenPos();
        ImGui::Image(host->textureToImTexture(importSrcTex), ImVec2(sw, sh));
        ImGui::SetCursorScreenPos(imgPos);
        ImGui::InvisibleButton("##cropsrc", ImVec2(sw, sh));
        const bool hov = ImGui::IsItemHovered();
        auto toSrc = [&](const ImVec2& p, int& sx, int& sy) {
            float fx = (p.x - imgPos.x) / sw, fy = (p.y - imgPos.y) / sh;
            fx = std::clamp(fx, 0.0f, 1.0f); fy = std::clamp(fy, 0.0f, 1.0f);
            sx = static_cast<int>(fx * importSrcW + 0.5f);
            sy = static_cast<int>(fy * importSrcH + 0.5f);
        };
        auto toScreen = [&](int sx, int sy) {
            return ImVec2(imgPos.x + static_cast<float>(sx) / importSrcW * sw,
                          imgPos.y + static_cast<float>(sy) / importSrcH * sh);
        };
        if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            importCropDragging = true;
            toSrc(ImGui::GetIO().MousePos, importCropAnchorX, importCropAnchorY);
        }
        if (importCropDragging) {
            int mx, my; toSrc(ImGui::GetIO().MousePos, mx, my);
            const int nx0 = std::min(importCropAnchorX, mx), ny0 = std::min(importCropAnchorY, my);
            const int nx1 = std::max(importCropAnchorX, mx), ny1 = std::max(importCropAnchorY, my);
            if (nx0 != importCropX0 || ny0 != importCropY0 || nx1 != importCropX1 || ny1 != importCropY1) {
                importCropX0 = nx0; importCropY0 = ny0; importCropX1 = nx1; importCropY1 = ny1;
                importDirty = true;
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                importCropDragging = false;
                importCropActive = cropUsable();
                importDirty = true;
            }
        }
        if (importCropActive || importCropDragging) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 a = toScreen(importCropX0, importCropY0);
            const ImVec2 b = toScreen(importCropX1, importCropY1);
            if (cropUsable()) {
                const ImVec2 tl(imgPos.x, imgPos.y), br(imgPos.x + sw, imgPos.y + sh);
                const ImU32 dim = IM_COL32(0, 0, 0, 110);
                dl->AddRectFilled(tl, ImVec2(br.x, a.y), dim);
                dl->AddRectFilled(ImVec2(tl.x, b.y), br, dim);
                dl->AddRectFilled(ImVec2(tl.x, a.y), ImVec2(a.x, b.y), dim);
                dl->AddRectFilled(ImVec2(b.x, a.y), ImVec2(br.x, b.y), dim);
            }
            dl->AddRect(a, b, IM_COL32(255, 215, 0, 255), 0.0f, 0, 2.0f);
        }
    }
    if (importCropActive) {
        ImGui::Text("Crop: %d,%d  %dx%d", importCropX0, importCropY0,
                    importCropX1 - importCropX0, importCropY1 - importCropY0);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear crop")) { importCropActive = false; importCropDragging = false; importDirty = true; }
    } else {
        ImGui::TextDisabled("Crop: whole image");
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("TMS9918 result");
    if (importPreviewTex && host)
        ImGui::Image(host->textureToImTexture(importPreviewTex),
                     ImVec2(kGfx2Width * 2.0f, ph));
    ImGui::EndGroup();
    ImGui::Separator();

    // ── Live controls ────────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(-180);
    importDirty |= ImGui::SliderFloat("Colour noise", &importColourNoise, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Left: flat greys dither clean black/white.\nRight: more vivid colour.");
    ImGui::SetNextItemWidth(-180);
    importDirty |= ImGui::SliderFloat("Brightness", &importBrightness, 0.3f, 2.0f, "%.2f");
    ImGui::SetNextItemWidth(-180);
    importDirty |= ImGui::SliderFloat("Contrast", &importContrast, 0.4f, 2.5f, "%.2f");
    ImGui::SetNextItemWidth(-180);
    importDirty |= ImGui::SliderFloat("Gamma", &importGamma, 0.4f, 2.5f, "%.2f");
    ImGui::SetNextItemWidth(-180);
    importDirty |= ImGui::SliderFloat("Diffusion (grain)", &importDiffusion, 0.0f, 1.0f, "%.2f");
    ImGui::SetNextItemWidth(-180);
    {
        const char* kKernelNames[] = { "Floyd-Steinberg", "Jarvis-mod" };
        importDirty |= ImGui::Combo("Diffusion kernel", &importKernel, kKernelNames, 2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Floyd-Steinberg: classic grain (default).\n"
                              "Jarvis-mod: error pushed 4 pixels forward - smoother.");
    }
    importDirty |= ImGui::Checkbox("Dither", &importDither);
    ImGui::SameLine();
    importDirty |= ImGui::Checkbox("Serpentine", &importSerpentine);
    ImGui::SameLine();
    importDirty |= ImGui::Checkbox("Stretch", &importStretch);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
        importColourNoise = 0.30f; importBrightness = 1.0f; importContrast = 1.0f;
        importGamma = 1.0f; importDiffusion = 1.0f; importDither = true;
        importSerpentine = true; importStretch = false; importKernel = 0;
        importDirty = true;
        importCropActive = false; importCropDragging = false;
    }
    ImGui::Separator();

    if (ImGui::Button("Apply to page", ImVec2(130, 0)) && !importVram.empty()) {
        beginStroke(true);
        for (int off = 0; off < kVramSize; ++off) {
            if (importVram[off] == shadow[off]) continue;
            stroke.push_back({static_cast<uint16_t>(off), shadow[off], importVram[off]});
            shadow[off] = importVram[off];
            if (host) host->pokeVram(static_cast<uint16_t>(off), importVram[off]);
        }
        commitStroke();
        status = "Imported " + importSrcName;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void tmspaint::TmsPaintEditor::renderFileRow()
{
    if (ImGui::Button("Load")) openFileBrowser(false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open a file picker and load a raw 16 KB VRAM image");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_IMAGE " Import")) openFileBrowser(false, 0, /*importMode=*/true);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Import a PNG/JPG picture and convert it to TMS9918\n"
                          "(ii-pix style: CAM16-UCS perceptual dithering vs the 15 palette colours)");
    ImGui::SameLine();
    if (ImGui::Button("Save")) openFileBrowser(true, 0);
    ImGui::SameLine();
    if (ImGui::Button("Save PNG")) openFileBrowser(true, 1);
    if (!status.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", status.c_str()); }
}

void tmspaint::TmsPaintEditor::renderStatusBar(int lx, int ly, bool hovered)
{
    const char* toolNames[] = { "Pencil", "Eraser", "Line", "Rect", "Ellipse",
                                "Fill", "Eyedropper", "Select", "Text" };
    const int activeColor = (tool == Tool::Eraser) ? 0 : color;

    if (hovered && lx >= 0 && ly >= 0) {
        const int a = (mode_ == Mode::Multicolor) ? tmspaint::mcBlockAddr(lx, ly)
                                                   : tmspaint::gfx2PatternAddr(lx, ly);
        ImGui::Text("x=%3d y=%3d  vram=$%04X", lx, ly, a < 0 ? 0 : a);
    } else {
        ImGui::TextUnformatted("x=--- y=---  vram=$----");
    }
    ImGui::SameLine();
    ImGui::Text(" | %s ", toolNames[static_cast<int>(tool)]);
    ImGui::SameLine();
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        dl->AddRectFilled(p, ImVec2(p.x + h, p.y + h), kSwatch[activeColor]);
        dl->AddRect(p, ImVec2(p.x + h, p.y + h), IM_COL32(160, 160, 160, 255));
        ImGui::Dummy(ImVec2(h, h));
    }
    ImGui::SameLine();
    ImGui::Text("%s | %s | Zoom %dx | Undo:%zu Redo:%zu",
                kColorName[activeColor],
                mode_ == Mode::Multicolor ? "Multicolor" : "Graphics II",
                kZoomLadder[zoomIdx], undo.size(), redo.size());
}

void tmspaint::TmsPaintEditor::handleShortcuts()
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;

    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    auto pick = [&](Tool t) { if (t != tool) prevTool = tool; tool = t; };

    if (io.KeyCtrl) {
        if (pressed(ImGuiKey_Z)) { if (io.KeyShift) doRedo(); else doUndo(); }
        if (pressed(ImGuiKey_Y)) doRedo();
        if (pressed(ImGuiKey_C)) copySelection(false);
        if (pressed(ImGuiKey_X)) copySelection(true);
        if (pressed(ImGuiKey_V) && clip.w > 0) {
            if (dragging) { commitStroke(); dragging = false; }
            pasting = true;
            pasteX = hasSel ? std::min(selX0, selX1) : 0;
            pasteY = hasSel ? std::min(selY0, selY1) : 0;
        }
        return;
    }

    if (pressed(ImGuiKey_Escape)) { pasting = false; hasSel = false; }
    if (pasting) {
        if (pressed(ImGuiKey_LeftArrow))  pasteX = std::max(pasteX - 1, 0);
        if (pressed(ImGuiKey_RightArrow)) pasteX = std::min(pasteX + 1, cw() - 1);
        if (pressed(ImGuiKey_UpArrow))    pasteY = std::max(pasteY - 1, 0);
        if (pressed(ImGuiKey_DownArrow))  pasteY = std::min(pasteY + 1, ch() - 1);
        if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) { pasteFloatingAt(pasteX, pasteY); pasting = false; }
    }

    if (pressed(ImGuiKey_P)) pick(Tool::Pencil);
    if (pressed(ImGuiKey_E)) pick(Tool::Eraser);
    if (pressed(ImGuiKey_L)) pick(Tool::Line);
    if (pressed(ImGuiKey_R)) pick(Tool::Rectangle);
    if (pressed(ImGuiKey_F)) pick(Tool::Fill);
    if (pressed(ImGuiKey_O)) pick(Tool::Ellipse);
    if (pressed(ImGuiKey_I)) pick(Tool::Eyedropper);
    if (pressed(ImGuiKey_S)) pick(Tool::Select);
    if (pressed(ImGuiKey_T)) pick(Tool::Text);

    // Digit keys pick palette indices 0..9 (10..15 via the colour bar / eyedropper).
    const ImGuiKey numKeys[] = { ImGuiKey_0, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
                                 ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9 };
    for (int i = 0; i < 10; ++i) if (pressed(numKeys[i])) color = i;

    if (pressed(ImGuiKey_X) && (tool == Tool::Rectangle || tool == Tool::Ellipse))
        rectFilled = !rectFilled;
    if (pressed(ImGuiKey_G)) showGrid = !showGrid;

    if (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd))
        zoomIdx = std::min(zoomIdx + 1, kZoomLadderCount - 1);
    if (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract))
        zoomIdx = std::max(zoomIdx - 1, 0);
    if (pressed(ImGuiKey_LeftBracket))  brushSize = std::max(brushSize - 1, 1);
    if (pressed(ImGuiKey_RightBracket)) brushSize = std::min(brushSize + 1, 7);
}

void tmspaint::TmsPaintEditor::render()
{
    handleShortcuts();

    renderTopBar();
    ImGui::Separator();

    const float bottomH = 30.0f
                        + ImGui::GetTextLineHeightWithSpacing() * 2.0f
                        + ImGui::GetStyle().ItemSpacing.y * 3.0f;
    const float panelW = 146.0f;

    ImGui::BeginChild("tmsbody", ImVec2(0.0f, -bottomH), false);
    {
        ImGui::BeginChild("tmstools", ImVec2(panelW, 0.0f), true);
        renderToolPanel();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("tmscanvasside", ImVec2(0.0f, 0.0f), false);
        renderCanvas();
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::Separator();
    renderColorBar();
    renderStatusBar(lastHoverX, lastHoverY, lastHoverX >= 0);

    renderFileBrowser();
    renderImportPreview();   // modal image-import preview with live sliders
}
