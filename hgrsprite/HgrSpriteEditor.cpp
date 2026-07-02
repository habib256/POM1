// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// GEN2 HGR sprite editor — see HgrSpriteEditor.h. Sibling of tmssprite/TmsSpriteEditor.

#include "HgrSpriteEditor.h"

#include "imgui.h"
#include "IconsFontAwesome6.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using hgrpaint::HgrColor;

namespace {

// Representative RGB for the six HGR artifact colours (crisp per-pixel grid +
// preview overlay). Order matches hgrpaint::HgrColor.
const ImU32 kSwatch[6] = {
    IM_COL32(  0,   0,   0, 255),   // Black
    IM_COL32(255, 255, 255, 255),   // White
    IM_COL32(212,  40, 255, 255),   // Violet
    IM_COL32( 40, 240,  70, 255),   // Green
    IM_COL32( 40, 130, 255, 255),   // Blue
    IM_COL32(255, 120,  40, 255),   // Orange
};
const char* kColorName[6] = { "Black", "White", "Violet", "Green", "Blue", "Orange" };

inline ImU32 swatchU32(HgrColor c) { return kSwatch[static_cast<int>(c)]; }

} // namespace

namespace hgrsprite {

HgrSpriteEditor::HgrSpriteEditor(hgrpaint::IHgrPaintHost* host_)
    : host(host_),
      scratch(hgrpaint::kHiresSize, 0),
      previewRgba(static_cast<size_t>(hgrpaint::kHiresWidth) * hgrpaint::kHiresHeight, 0)
{
}

HgrSpriteEditor::~HgrSpriteEditor() { releaseGL(); }

void HgrSpriteEditor::releaseGL()
{
    if (host && previewTex) host->destroyTexture(previewTex);
    previewTex = nullptr;
}

void HgrSpriteEditor::clampGeom()
{
    wBytes_ = std::clamp(wBytes_, 1, kByteCols);
    hRows_  = std::clamp(hRows_, 1, kRows);
    // Placement clamps to the ×2-magnified footprint so a doubled sprite still
    // fits the page (stamp() also clips, but keep the sliders honest).
    destByteCol_ = std::clamp(destByteCol_, 0, std::max(0, kByteCols - magF() * wBytes_));
    destRow_     = std::clamp(destRow_, 0, std::max(0, kRows - magF() * hRows_));
}

// Build the ×2-magnified sprite (2*wBytes × 2*hRows, row-major) from the base
// sprite bytes: each source pixel becomes a 2×2 block. A source byte's 7 pixels
// double into 14 columns = two destination bytes; the byte's palette high bit is
// copied to both so the colour group is preserved.
static void magnify2x(const std::vector<uint8_t>& src, int wBytes, int hRows,
                      std::vector<uint8_t>& out)
{
    const int dW = wBytes * 2;
    out.assign(static_cast<size_t>(dW) * (hRows * 2), 0);
    for (int sr = 0; sr < hRows; ++sr)
        for (int sb = 0; sb < wBytes; ++sb) {
            const uint8_t sByte = src[static_cast<size_t>(sr) * wBytes + sb];
            const uint8_t pal   = sByte & 0x80u;
            uint8_t d0 = pal, d1 = pal;                 // two doubled bytes (cols 0..6 / 7..13)
            for (int p = 0; p < 7; ++p) {
                if (!((sByte >> p) & 1)) continue;
                for (int k = 0; k < 2; ++k) {           // source col p → dest cols 2p, 2p+1
                    const int dp = 2 * p + k;
                    if (dp < 7) d0 |= static_cast<uint8_t>(1u << dp);
                    else        d1 |= static_cast<uint8_t>(1u << (dp - 7));
                }
            }
            for (int k = 0; k < 2; ++k) {               // row doubling
                const int dr = 2 * sr + k;
                out[static_cast<size_t>(dr) * dW + 2 * sb + 0] = d0;
                out[static_cast<size_t>(dr) * dW + 2 * sb + 1] = d1;
            }
        }
}

// ── Undo primitives (scratch-only; no live poke — editor is non-destructive) ─

void HgrSpriteEditor::beginStroke() { stroke.clear(); }

void HgrSpriteEditor::commitStroke()
{
    if (stroke.empty()) return;
    undo.push_back(stroke);
    redo.clear();
    stroke.clear();
}

bool HgrSpriteEditor::recordPlot(int x, int y, HgrColor c)
{
    const int off = hgrpaint::targetOffset(x, y, c);
    if (off < 0) return false;
    const uint8_t old = scratch[off];
    if (hgrpaint::plotPage(scratch.data(), x, y, c) < 0) return false;   // unchanged
    stroke.push_back({static_cast<uint16_t>(off), old, scratch[off]});
    return true;
}

void HgrSpriteEditor::applyOps(const std::vector<ByteEdit>& ops, bool forward)
{
    if (forward) for (const auto& e : ops) scratch[e.addr] = e.newVal;
    else         for (const auto& e : ops) scratch[e.addr] = e.oldVal;
}

void HgrSpriteEditor::doUndo()
{
    if (undo.empty()) return;
    auto ops = undo.back(); undo.pop_back();
    applyOps(ops, false);
    redo.push_back(ops);
    status = "Undo";
}

void HgrSpriteEditor::doRedo()
{
    if (redo.empty()) return;
    auto ops = redo.back(); redo.pop_back();
    applyOps(ops, true);
    undo.push_back(ops);
    status = "Redo";
}

// ── Region-diff bulk-edit helpers ───────────────────────────────────────────

std::vector<std::pair<uint16_t, uint8_t>> HgrSpriteEditor::snapshotRegion() const
{
    std::vector<std::pair<uint16_t, uint8_t>> out;
    out.reserve(static_cast<size_t>(wBytes_) * hRows_);
    for (int r = 0; r < hRows_; ++r)
        for (int b = 0; b < wBytes_; ++b) {
            const int off = hgrpaint::hgrByteOffset(b * 7, r);
            if (off >= 0) out.emplace_back(static_cast<uint16_t>(off), scratch[off]);
        }
    return out;
}

void HgrSpriteEditor::commitRegionDiff(const std::vector<std::pair<uint16_t, uint8_t>>& before)
{
    stroke.clear();
    for (const auto& p : before)
        if (scratch[p.first] != p.second)
            stroke.push_back({p.first, p.second, scratch[p.first]});
    if (!stroke.empty()) { undo.push_back(stroke); redo.clear(); }
    stroke.clear();
}

// ── Fill (bounded 4-connected flood over equal artifact colour) ─────────────

void HgrSpriteEditor::floodFill(int x, int y, HgrColor c)
{
    const int W = wpx(), H = hRows_;
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    const HgrColor seed = hgrpaint::colorAt(scratch.data(), x, y);
    if (seed == c) return;
    auto before = snapshotRegion();
    std::vector<uint8_t> seen(static_cast<size_t>(W) * H, 0);
    std::vector<std::pair<int,int>> st, region;
    st.emplace_back(x, y);
    seen[static_cast<size_t>(y) * W + x] = 1;
    while (!st.empty()) {
        const auto p = st.back(); st.pop_back();
        region.push_back(p);
        const int nb[4][2] = {{p.first-1,p.second},{p.first+1,p.second},
                              {p.first,p.second-1},{p.first,p.second+1}};
        for (auto& n : nb) {
            const int nx = n[0], ny = n[1];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            const size_t idx = static_cast<size_t>(ny) * W + nx;
            if (seen[idx]) continue;
            if (hgrpaint::colorAt(scratch.data(), nx, ny) != seed) continue;
            seen[idx] = 1;
            st.emplace_back(nx, ny);
        }
    }
    for (auto& p : region) hgrpaint::plotPage(scratch.data(), p.first, p.second, c);
    commitRegionDiff(before);
    status = "Filled";
}

// ── Whole-sprite transforms (pixel-level; chromatic snaps to HGR parity) ────

void HgrSpriteEditor::transformClear()
{
    auto before = snapshotRegion();
    for (const auto& p : before) scratch[p.first] = 0;
    commitRegionDiff(before);
    status = "Cleared";
}

// Rebuild the sprite region from a transformed colour grid.
static void rebuildFromGrid(std::vector<uint8_t>& scratch, int W, int H,
                            const std::vector<HgrColor>& g,
                            const std::vector<std::pair<uint16_t, uint8_t>>& region)
{
    for (const auto& p : region) scratch[p.first] = 0;   // clear region
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const HgrColor c = g[static_cast<size_t>(y) * W + x];
            if (c != HgrColor::Black) hgrpaint::plotPage(scratch.data(), x, y, c);
        }
}

void HgrSpriteEditor::transformInvert()
{
    const int W = wpx(), H = hRows_;
    auto before = snapshotRegion();
    std::vector<HgrColor> g(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            g[static_cast<size_t>(y) * W + x] =
                (hgrpaint::colorAt(scratch.data(), x, y) == HgrColor::Black)
                    ? HgrColor::White : HgrColor::Black;
    rebuildFromGrid(scratch, W, H, g, before);
    commitRegionDiff(before);
    status = "Inverted";
}

void HgrSpriteEditor::transformFlipH()
{
    const int W = wpx(), H = hRows_;
    auto before = snapshotRegion();
    std::vector<HgrColor> g(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            g[static_cast<size_t>(y) * W + x] =
                hgrpaint::colorAt(scratch.data(), W - 1 - x, y);
    rebuildFromGrid(scratch, W, H, g, before);
    commitRegionDiff(before);
    status = "Flipped horizontally";
}

void HgrSpriteEditor::transformFlipV()
{
    const int W = wpx(), H = hRows_;
    auto before = snapshotRegion();
    std::vector<HgrColor> g(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            g[static_cast<size_t>(y) * W + x] =
                hgrpaint::colorAt(scratch.data(), x, H - 1 - y);
    rebuildFromGrid(scratch, W, H, g, before);
    commitRegionDiff(before);
    status = "Flipped vertically";
}

void HgrSpriteEditor::transformShift(int dx, int dy)
{
    const int W = wpx(), H = hRows_;
    auto before = snapshotRegion();
    std::vector<HgrColor> g(static_cast<size_t>(W) * H, HgrColor::Black);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const int sx = x - dx, sy = y - dy;
            if (sx < 0 || sx >= W || sy < 0 || sy >= H) continue;
            g[static_cast<size_t>(y) * W + x] = hgrpaint::colorAt(scratch.data(), sx, sy);
        }
    rebuildFromGrid(scratch, W, H, g, before);
    commitRegionDiff(before);
    status = "Shifted";
}

// ── Live-card actions + files ───────────────────────────────────────────────

void HgrSpriteEditor::stampToPage()
{
    if (!host) return;
    std::vector<uint8_t> spr(static_cast<size_t>(wBytes_) * hRows_, 0);
    extract(scratch.data(), 0, 0, wBytes_, hRows_, spr.data());
    const uint16_t base = pageBase();
    host->beginBatch();
    if (mag2_) {
        std::vector<uint8_t> dbl;
        magnify2x(spr, wBytes_, hRows_, dbl);
        stamp(dbl.data(), wBytes_ * 2, hRows_ * 2, destByteCol_, destRow_,
              [&](int off, uint8_t v) { host->pokeByte(static_cast<uint16_t>(base + off), v); });
    } else {
        stamp(spr.data(), wBytes_, hRows_, destByteCol_, destRow_,
              [&](int off, uint8_t v) { host->pokeByte(static_cast<uint16_t>(base + off), v); });
    }
    host->endBatch();
    status = mag2_ ? "Stamped to HGR page (\xC3\x97""2)" : "Stamped to HGR page";
}

void HgrSpriteEditor::doSaveSprite()
{
    if (!host) return;
    if (!host->nativeFilePickerAvailable()) { status = "No native file picker"; return; }
    std::string path;
    if (!host->pickFilePath(true, "Save HGR sprite", "HGR sprite bytes",
                            "hgrspr,bin", "", "sprite.hgrspr", path))
        return;
    std::vector<uint8_t> spr(static_cast<size_t>(wBytes_) * hRows_, 0);
    extract(scratch.data(), 0, 0, wBytes_, hRows_, spr.data());
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { status = "Cannot write file"; return; }
    const size_t n = std::fwrite(spr.data(), 1, spr.size(), f);
    std::fclose(f);
    status = (n == spr.size()) ? "Saved sprite" : "Short write";
}

void HgrSpriteEditor::doLoadSprite()
{
    if (!host) return;
    if (!host->nativeFilePickerAvailable()) { status = "No native file picker"; return; }
    std::string path;
    if (!host->pickFilePath(false, "Load HGR sprite", "HGR sprite bytes",
                            "hgrspr,bin", "", "", path))
        return;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { status = "Cannot open file"; return; }
    std::vector<uint8_t> spr(static_cast<size_t>(wBytes_) * hRows_, 0);
    const size_t got = std::fread(spr.data(), 1, spr.size(), f);   // short files zero-pad
    std::fclose(f);
    (void)got;
    auto before = snapshotRegion();
    for (const auto& p : before) scratch[p.first] = 0;
    stamp(spr.data(), wBytes_, hRows_, 0, 0,
          [&](int off, uint8_t v) { scratch[off] = v; });
    commitRegionDiff(before);
    status = "Loaded sprite (current W\xC3\x97H)";
}

void HgrSpriteEditor::doSavePng()
{
    if (!host) return;
    if (!host->nativeFilePickerAvailable()) { status = "No native file picker"; return; }
    std::string path;
    if (!host->pickFilePath(true, "Export sprite PNG", "PNG image", "png", "", "sprite.png", path))
        return;
    const int W = wpx(), H = hRows_;
    std::vector<uint32_t> rgba(static_cast<size_t>(W) * H, 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            rgba[static_cast<size_t>(y) * W + x] =
                swatchU32(hgrpaint::colorAt(scratch.data(), x, y));
    std::string err;
    status = host->savePng(path, rgba.data(), W, H, err) ? "Saved PNG"
                                                         : (std::string("PNG failed: ") + err);
}

// ── Dev sprite library (dev/lib/gen2/sprites via the host) ──────────────────

void HgrSpriteEditor::loadDevSprite(const hgrpaint::DevSprite& s)
{
    wBytes_ = s.wBytes;
    hRows_  = s.hRows;
    clampGeom();
    auto before = snapshotRegion();
    for (const auto& p : before) scratch[p.first] = 0;
    stamp(s.bytes.data(), wBytes_, hRows_, 0, 0,
          [&](int off, uint8_t v) { scratch[off] = v; });
    commitRegionDiff(before);
    status = "Loaded dev sprite: " + s.name;
}

void HgrSpriteEditor::renderDevSprites()
{
    if (!host) return;
    if (!devLoaded_) { devCats_ = host->devSprites(); devLoaded_ = true; }
    if (devCats_.empty()) {
        ImGui::TextDisabled("No sprite library found (dev/lib/gen2/sprites).");
        return;
    }
    devCat_ = std::clamp(devCat_, 0, static_cast<int>(devCats_.size()) - 1);

    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo("Category", devCats_[devCat_].name.c_str())) {
        for (int i = 0; i < static_cast<int>(devCats_.size()); ++i)
            if (ImGui::Selectable(devCats_[i].name.c_str(), devCat_ == i))
                devCat_ = i;
        ImGui::EndCombo();
    }

    const auto& cat = devCats_[devCat_];
    if (cat.sprites.empty()) return;

    // Uniform thumbnail size (all GEN2 HGR sprites are 3 bytes × 16 rows).
    const int    W  = cat.sprites[0].wBytes * 7;
    const int    H  = cat.sprites[0].hRows;
    const float  th = 3.0f;                              // px per sprite pixel
    const ImVec2 sz(W * th, H * th);
    const float  cellW = sz.x + ImGui::GetStyle().ItemSpacing.x + 6.0f;
    const float  avail = ImGui::GetContentRegionAvail().x;
    const int    perRow = std::max(1, static_cast<int>(avail / cellW));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < static_cast<int>(cat.sprites.size()); ++i) {
        const auto& s = cat.sprites[i];
        ImGui::PushID(i);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##thumb", sz);
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), IM_COL32(20,20,20,255));
        for (int y = 0; y < s.hRows; ++y)
            for (int x = 0; x < s.wBytes * 7; ++x) {
                const int b = x / 7, bit = x % 7;
                if ((s.bytes[static_cast<size_t>(y) * s.wBytes + b] >> bit) & 1) {
                    const ImVec2 a(p0.x + x * th, p0.y + y * th);
                    dl->AddRectFilled(a, ImVec2(a.x + th, a.y + th), IM_COL32(230,230,230,255));
                }
            }
        dl->AddRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y),
                    hov ? IM_COL32(255,255,0,255) : IM_COL32(90,90,90,255));
        if (hov) {
            ImGui::SetTooltip("%s", s.name.c_str());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) loadDevSprite(s);
        }
        ImGui::PopID();
        if ((i % perRow) != perRow - 1 && i != static_cast<int>(cat.sprites.size()) - 1)
            ImGui::SameLine();
    }
}

// ── Shortcuts ───────────────────────────────────────────────────────────────

void HgrSpriteEditor::handleShortcuts()
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    const bool ctrl = io.KeyCtrl || io.KeySuper;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { io.KeyShift ? doRedo() : doUndo(); }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) doRedo();
    if (!ctrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_B, false)) tool = Tool::Pencil;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) tool = Tool::Eraser;
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) tool = Tool::Fill;
    }
}

// ── UI ──────────────────────────────────────────────────────────────────────

void HgrSpriteEditor::renderTopBar()
{
    ImGui::TextUnformatted("W:");
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("bytes", &wBytes_)) clampGeom();
    ImGui::SameLine(); ImGui::TextDisabled("(%d px)", wpx());
    ImGui::SameLine(0, 14); ImGui::TextUnformatted("H:");
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("rows", &hRows_)) clampGeom();

    ImGui::SameLine(0, 20);
    if (ImGui::Button(ICON_FA_ROTATE_LEFT "##undo")) doUndo();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Undo (Ctrl+Z)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE_RIGHT "##redo")) doRedo();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Redo (Ctrl+Y)");

    // Transform row.
    if (ImGui::Button("Clear")) transformClear();
    ImGui::SameLine(); if (ImGui::Button("Invert")) transformInvert();
    ImGui::SameLine(); if (ImGui::Button(ICON_FA_ARROWS_LEFT_RIGHT "##fh")) transformFlipH();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Flip horizontally");
    ImGui::SameLine(); if (ImGui::Button(ICON_FA_ARROWS_UP_DOWN "##fv")) transformFlipV();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Flip vertically");
    ImGui::SameLine(0, 12);
    if (ImGui::ArrowButton("##sl", ImGuiDir_Left))  transformShift(-1, 0);
    ImGui::SameLine(); if (ImGui::ArrowButton("##sr", ImGuiDir_Right)) transformShift(1, 0);
    ImGui::SameLine(); if (ImGui::ArrowButton("##su", ImGuiDir_Up))    transformShift(0, -1);
    ImGui::SameLine(); if (ImGui::ArrowButton("##sd", ImGuiDir_Down))  transformShift(0, 1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shift sprite pixels");

    ImGui::SameLine(0, 16);
    ImGui::Checkbox("Grid", &showGrid_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("Zoom", &zoom_, 6, 32);
}

void HgrSpriteEditor::renderToolPanel()
{
    const char* toolIcon[3] = { ICON_FA_PENCIL, ICON_FA_ERASER, ICON_FA_FILL_DRIP };
    const char* toolName[3] = { "Pencil (B)", "Eraser (E)", "Fill (G)" };
    for (int i = 0; i < 3; ++i) {
        const bool sel = (static_cast<int>(tool) == i);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(toolIcon[i])) tool = static_cast<Tool>(i);
        if (sel) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", toolName[i]);
        ImGui::SameLine();
    }
    ImGui::NewLine();

    // HGR colours (skip Black — that's the eraser). Lone pixels artifact-colour by
    // parity, so a single "White" dot may show violet/green — faithful HGR.
    ImGui::TextUnformatted("Colour");
    for (int i = 1; i < 6; ++i) {
        ImGui::PushID(2000 + i);
        const bool sel = (static_cast<int>(color_) == i);
        if (ImGui::ColorButton(kColorName[i], ImGui::ColorConvertU32ToFloat4(kSwatch[i]),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(22, 22)))
            color_ = static_cast<HgrColor>(i);
        if (sel) {
            ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(mn, mx, IM_COL32(255,255,0,255), 0, 0, 2.0f);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kColorName[i]);
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Separator();

    // Placement on the live page.
    ImGui::TextUnformatted("Stamp position");
    int pageIdx = page2_ ? 1 : 0;
    ImGui::SetNextItemWidth(150);
    if (ImGui::Combo("Page", &pageIdx, "HGR1 ($2000)\0HGR2 ($4000)\0")) page2_ = (pageIdx == 1);
    ImGui::SetNextItemWidth(150);
    if (ImGui::SliderInt("Byte X", &destByteCol_, 0, std::max(0, kByteCols - magF() * wBytes_))) clampGeom();
    ImGui::SetNextItemWidth(150);
    if (ImGui::SliderInt("Row Y", &destRow_, 0, std::max(0, kRows - magF() * hRows_))) clampGeom();
    if (ImGui::Checkbox("\xC3\x97""2 (magnify)", &mag2_)) clampGeom();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Double the sprite (each pixel \xE2\x86\x92 2\xC3\x97""2) when stamping / previewing");
    if (ImGui::Button(ICON_FA_STAMP " Stamp to page")) stampToPage();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Write the sprite bytes onto the live HGR page (destructive)");

    ImGui::Separator();
    if (ImGui::Button("Load")) doLoadSprite();
    ImGui::SameLine(); if (ImGui::Button("Save")) doSaveSprite();
    ImGui::SameLine(); if (ImGui::Button("PNG"))  doSavePng();

    // Built-in GEN2 HGR sprite library (dev/lib/gen2/sprites).
    if (ImGui::CollapsingHeader("Dev sprite library", ImGuiTreeNodeFlags_DefaultOpen))
        renderDevSprites();
}

void HgrSpriteEditor::renderCanvas()
{
    const int W = wpx(), H = hRows_;
    const float cell = static_cast<float>(zoom_);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 sz(W * cell, H * cell);
    ImGui::InvisibleButton("##hgrspritecanvas", sz,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hov = ImGui::IsItemHovered();

    // Pixels (crisp per-pixel artifact colour).
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const HgrColor c = hgrpaint::colorAt(scratch.data(), x, y);
            const ImVec2 a(p0.x + x * cell, p0.y + y * cell);
            dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), swatchU32(c));
        }

    // Grid: light every pixel, heavier on 7-pixel byte boundaries.
    if (showGrid_) {
        for (int x = 0; x <= W; ++x) {
            const bool major = (x % 7) == 0;
            const float gx = p0.x + x * cell;
            dl->AddLine(ImVec2(gx, p0.y), ImVec2(gx, p0.y + sz.y),
                        major ? IM_COL32(120,120,120,255) : IM_COL32(70,70,70,150),
                        major ? 2.0f : 1.0f);
        }
        for (int y = 0; y <= H; ++y) {
            const float gy = p0.y + y * cell;
            dl->AddLine(ImVec2(p0.x, gy), ImVec2(p0.x + sz.x, gy),
                        IM_COL32(70,70,70,150), 1.0f);
        }
    }
    dl->AddRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), IM_COL32(200,200,200,255));

    // Mouse.
    ImGuiIO& io = ImGui::GetIO();
    int cx = -1, cy = -1;
    if (hov) {
        cx = static_cast<int>((io.MousePos.x - p0.x) / cell);
        cy = static_cast<int>((io.MousePos.y - p0.y) / cell);
        if (cx >= 0 && cx < W && cy >= 0 && cy < H) {
            const ImVec2 a(p0.x + cx * cell, p0.y + cy * cell);
            dl->AddRect(a, ImVec2(a.x + cell, a.y + cell), IM_COL32(255,255,0,255), 0, 0, 2.0f);
        } else { cx = cy = -1; }
    }

    const bool L = io.MouseDown[0], R = io.MouseDown[1];
    if (tool == Tool::Fill) {
        if (hov && cx >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            floodFill(cx, cy, color_);
        if (hov && cx >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            floodFill(cx, cy, HgrColor::Black);
    } else {
        if (!dragging && hov && cx >= 0 && (L || R)) {
            beginStroke();
            dragging = true;
            eraseDrag = R || (tool == Tool::Eraser);
            lastPx = lastPy = -1;
        }
        if (dragging && cx >= 0) {
            const HgrColor c = eraseDrag ? HgrColor::Black : color_;
            if (lastPx < 0) { recordPlot(cx, cy, c); }
            else {
                int x0 = lastPx, y0 = lastPy, x1 = cx, y1 = cy;
                const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
                const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
                int err = dx + dy;
                for (;;) {
                    recordPlot(x0, y0, c);
                    if (x0 == x1 && y0 == y1) break;
                    const int e2 = 2 * err;
                    if (e2 >= dy) { err += dy; x0 += sx; }
                    if (e2 <= dx) { err += dx; y0 += sy; }
                }
            }
            lastPx = cx; lastPy = cy;
        }
    }
    if (dragging && !L && !R) {
        commitStroke();
        dragging = false;
        lastPx = lastPy = -1;
    }
}

void HgrSpriteEditor::renderPreview(const std::vector<uint8_t>& memory)
{
    if (!host) return;
    ImGui::Separator();
    ImGui::TextUnformatted("Live page preview");

    const uint16_t base = pageBase();
    const size_t need = static_cast<size_t>(base) + hgrpaint::kHiresSize;
    if (memory.size() < need) { ImGui::TextDisabled("(page out of RAM)"); return; }

    // Render the live page, then composite the sprite on top (non-destructive).
    // With ×2 each source pixel paints an m×m block so the preview matches the stamp.
    std::vector<uint8_t> live(memory.begin() + base, memory.begin() + base + hgrpaint::kHiresSize);
    host->renderHgrPage(live.data(), previewRgba.data(), false, false);
    const int m = magF();
    for (int y = 0; y < hRows_; ++y)
        for (int x = 0; x < wpx(); ++x) {
            const HgrColor c = hgrpaint::colorAt(scratch.data(), x, y);
            if (c == HgrColor::Black) continue;
            for (int ky = 0; ky < m; ++ky)
                for (int kx = 0; kx < m; ++kx) {
                    const int px = destByteCol_ * 7 + x * m + kx, py = destRow_ + y * m + ky;
                    if (px < hgrpaint::kHiresWidth && py < hgrpaint::kHiresHeight)
                        previewRgba[static_cast<size_t>(py) * hgrpaint::kHiresWidth + px] = swatchU32(c);
                }
        }
    previewTex = host->uploadTexture(previewTex, previewRgba.data(),
                                     hgrpaint::kHiresWidth, hgrpaint::kHiresHeight, false);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Image(host->textureToImTexture(previewTex),
                 ImVec2(hgrpaint::kHiresWidth, hgrpaint::kHiresHeight));
    // Outline the sprite bounds (magnified footprint).
    const ImVec2 a(pos.x + destByteCol_ * 7, pos.y + destRow_);
    ImGui::GetWindowDrawList()->AddRect(a, ImVec2(a.x + wpx() * m, a.y + hRows_ * m),
                                        IM_COL32(255,255,0,200));
}

void HgrSpriteEditor::render(const std::vector<uint8_t>& memory)
{
    clampGeom();
    handleShortcuts();

    renderTopBar();
    ImGui::Separator();

    const float canvasW = wpx() * static_cast<float>(zoom_) + 24.0f;
    ImGui::BeginChild("##hgrcanvaspane", ImVec2(canvasW, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    renderCanvas();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##hgrsidepane", ImVec2(0, 0));
    renderToolPanel();
    renderPreview(memory);
    ImGui::EndChild();

    if (!status.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(status.c_str());
    }
}

} // namespace hgrsprite
