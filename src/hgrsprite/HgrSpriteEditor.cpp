// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// GEN2 HGR sprite editor — see HgrSpriteEditor.h. Sibling of tmssprite/TmsSpriteEditor.

#include "HgrSpriteEditor.h"

#include "imgui.h"
#include "IconsFontAwesome6.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
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

} // namespace

namespace hgrsprite {

HgrSpriteEditor::HgrSpriteEditor(hgrpaint::IHgrPaintHost* host_)
    : host(host_),
      scratch(hgrpaint::kHiresSize, 0),
      colorRgba_(static_cast<size_t>(hgrpaint::kHiresWidth) * hgrpaint::kHiresHeight, 0)
{
}

HgrSpriteEditor::~HgrSpriteEditor() { releaseGL(); }

void HgrSpriteEditor::releaseGL()
{
    if (host && colorTex_) host->destroyTexture(colorTex_);
    colorTex_ = nullptr;
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

// ── Undo primitives (scratch-only; no live poke — editor is non-destructive) ─

void HgrSpriteEditor::beginStroke() { stroke.clear(); }

void HgrSpriteEditor::commitStroke()
{
    if (stroke.empty()) return;
    undo.push_back(stroke);
    if (undo.size() > 64) undo.erase(undo.begin());   // cap history (matches paint editor)
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
    if (undo.size() > 64) undo.erase(undo.begin());
    status = "Redo";
}

// ── Region-diff bulk-edit helpers ───────────────────────────────────────────

std::vector<std::pair<uint16_t, uint8_t>> HgrSpriteEditor::snapshotRegion() const
{
    return snapshotRegion(wBytes_, hRows_);
}

std::vector<std::pair<uint16_t, uint8_t>> HgrSpriteEditor::snapshotRegion(int wB, int hR) const
{
    std::vector<std::pair<uint16_t, uint8_t>> out;
    out.reserve(static_cast<size_t>(wB) * hR);
    for (int r = 0; r < hR; ++r)
        for (int b = 0; b < wB; ++b) {
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
    if (!stroke.empty()) {
        undo.push_back(stroke);
        if (undo.size() > 64) undo.erase(undo.begin());   // cap history (matches paint editor)
        redo.clear();
    }
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
    // Sprites are a monochrome bit shape (pencil/fill draw White, which
    // plotPage writes without parity-snapping). Transform the raw bits, NOT
    // the artifact colour: colorAt() classifies isolated/edge pixels as a
    // chromatic Violet/Green, and re-plotting that through plotPage would snap
    // the pixel back onto the colour's required column parity — shifting it
    // ±1 whenever the flip changes its parity.
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            g[static_cast<size_t>(y) * W + x] =
                hgrpaint::pixelOn(scratch.data(), W - 1 - x, y)
                    ? HgrColor::White : HgrColor::Black;
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
                hgrpaint::pixelOn(scratch.data(), x, H - 1 - y)
                    ? HgrColor::White : HgrColor::Black;   // raw bits, not artifact colour
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
            g[static_cast<size_t>(y) * W + x] =
                hgrpaint::pixelOn(scratch.data(), sx, sy)   // raw bits, not artifact colour
                    ? HgrColor::White : HgrColor::Black;
        }
    rebuildFromGrid(scratch, W, H, g, before);
    commitRegionDiff(before);
    status = "Shifted";
}

// Rotate 90° clockwise. Unlike TMS sprites the geometry is not square, so W and
// H swap: new width = ceil(H px / 7) bytes, new height = old width in px. Only
// allowed while the rotated height fits the page (button disabled otherwise).
// Like loadDevSprite, the geometry change itself is not undoable — undo restores
// the bytes of the union of the old + new regions.
void HgrSpriteEditor::transformRotateCW()
{
    const int W = wpx(), H = hRows_;
    const int nW = (H + 6) / 7;          // new byte width  = ceil(H px / 7)
    const int nH = W;                    // new height (rows) = old width (px)
    if (nW > kByteCols || nH > kRows) return;
    std::vector<HgrColor> src(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            src[static_cast<size_t>(y) * W + x] =
                hgrpaint::pixelOn(scratch.data(), x, y)   // raw bits, not artifact colour
                    ? HgrColor::White : HgrColor::Black;
    auto before = snapshotRegion(std::max(wBytes_, nW), std::max(hRows_, nH));
    for (const auto& p : before) scratch[p.first] = 0;
    wBytes_ = nW;
    hRows_  = nH;
    clampGeom();
    // dest(x,y) = src(y, H-1-x); columns past H-1 are the rounded-up byte padding.
    for (int y = 0; y < nH; ++y)
        for (int x = 0; x < nW * 7; ++x) {
            const int sx = y, sy = H - 1 - x;
            if (sy < 0) continue;
            const HgrColor c = src[static_cast<size_t>(sy) * W + sx];
            if (c != HgrColor::Black) hgrpaint::plotPage(scratch.data(), x, y, c);
        }
    commitRegionDiff(before);
    status = "Rotated 90\xC2\xB0";
}

// ── Live-card actions + files ───────────────────────────────────────────────

// ── Sprite bytes: mono shape (×1) or single-colour doubled clock pattern (×2) ─
//
// The sprite is authored as a MONOCHROME shape in `scratch` (white pixels). At
// ×2 the whole sprite takes a single chosen colour `color_`: each lit pixel is
// doubled into a 2-aligned NTSC colour clock (light one column of the pair + the
// palette high bit), so the doubled sprite reads as that one artifact colour
// instead of the solid white two-adjacent-dots would otherwise give.
void HgrSpriteEditor::buildSpriteBytes(std::vector<uint8_t>& out, int& wB, int& hR) const
{
    if (mag2_) {
        const int W = wpx();
        std::vector<HgrColor> cells(static_cast<size_t>(W) * hRows_, HgrColor::Black);
        for (int y = 0; y < hRows_; ++y)
            for (int x = 0; x < W; ++x)
                if (hgrpaint::pixelOn(scratch.data(), x, y))
                    cells[static_cast<size_t>(y) * W + x] = color_;
        wB = wBytes_ * 2;
        hR = hRows_ * 2;
        out.assign(static_cast<size_t>(wB) * hR, 0);
        magnifyColor2x(cells.data(), wBytes_, hRows_, out.data());
    } else {
        wB = wBytes_;
        hR = hRows_;
        out.assign(static_cast<size_t>(wB) * hR, 0);
        extract(scratch.data(), 0, 0, wBytes_, hRows_, out.data());
    }
}

void HgrSpriteEditor::stampToPage()
{
    if (!host) return;
    std::vector<uint8_t> bytes; int wB = 0, hR = 0;
    buildSpriteBytes(bytes, wB, hR);
    const uint16_t base = pageBase();
    host->beginBatch();
    stamp(bytes.data(), wB, hR, destByteCol_, destRow_,
          [&](int off, uint8_t v) { host->pokeByte(static_cast<uint16_t>(base + off), v); });
    host->endBatch();
    status = mag2_ ? "Stamped to HGR page (\xC3\x97""2 " + std::string(kColorName[static_cast<int>(color_)]) + ")"
                   : "Stamped to HGR page";
}

// Inverse of Stamp: extract the wBytes×hRows rectangle at (Byte X, Row Y) from
// the live HGR page into the scratch canvas — grab from screen, edit as sprite.
void HgrSpriteEditor::grabFromPage(const std::vector<uint8_t>& memory)
{
    const uint16_t base = pageBase();
    const size_t need = static_cast<size_t>(base) + hgrpaint::kHiresSize;
    if (memory.size() < need) { status = "Page out of RAM"; return; }
    std::vector<uint8_t> spr(static_cast<size_t>(wBytes_) * hRows_, 0);
    extract(memory.data() + base, destByteCol_, destRow_, wBytes_, hRows_, spr.data());
    auto before = snapshotRegion();
    for (const auto& p : before) scratch[p.first] = 0;
    stamp(spr.data(), wBytes_, hRows_, 0, 0,
          [&](int off, uint8_t v) { scratch[off] = v; });
    commitRegionDiff(before);
    status = "Grabbed from HGR page";
}

// ── Files (native picker first; ImGui browser fallback — mirrors the paint
//    editor's openFileBrowser / performFileAction / renderFileBrowser split) ──

void HgrSpriteEditor::openFileBrowser(FileAction a)
{
    browserAction = a;
    if (browserDir.empty()) {
        std::error_code ec;
        browserDir = std::filesystem::current_path(ec).string();
        if (ec || browserDir.empty()) browserDir = ".";
    }
    const bool forSave = (a != FileAction::LoadSprite);
    std::string title, desc, defName;
    switch (a) {
    case FileAction::LoadSprite:
        title = "Load HGR sprite";   desc = "HGR sprite bytes"; browserExts = "hgrspr,bin"; break;
    case FileAction::SaveSprite:
        title = "Save HGR sprite";   desc = "HGR sprite bytes"; browserExts = "hgrspr,bin";
        defName = "sprite.hgrspr"; break;
    case FileAction::SavePng:
        title = "Export sprite PNG"; desc = "PNG image";        browserExts = "png";
        defName = "sprite.png"; break;
    case FileAction::ExportAsm:
        title = "Export ca65 sprite"; desc = "ca65 assembly";   browserExts = "asm";
        defName = sanitizeAsmName(asmName_) + ".asm"; break;
    }
    if (host) {
        std::string picked;
        if (host->pickFilePath(forSave, title, desc, browserExts, browserDir, defName, picked)) {
            // Track where the user went so the next pick starts there too.
            const std::string dir = std::filesystem::path(picked).parent_path().string();
            if (!dir.empty()) browserDir = dir;
            performFileAction(a, picked);
            return;
        }
        // A native picker that returned false means the user CANCELLED — stay
        // put. Only fall back to the ImGui browser when the host has no native
        // picker at all (WASM, Linux without zenity/kdialog).
        if (host->nativeFilePickerAvailable()) return;
    }
    std::snprintf(browserSaveName, sizeof(browserSaveName), "%s", defName.c_str());
    browserOpen = true;
}

// Shared by the native picker and the ImGui browser. Returns false only on a
// failed save (so the ImGui browser keeps its popup open); true otherwise.
bool HgrSpriteEditor::performFileAction(FileAction a, const std::string& fullPath)
{
    switch (a) {
    case FileAction::LoadSprite: {
        std::FILE* f = std::fopen(fullPath.c_str(), "rb");
        if (!f) { status = "Cannot open file"; return true; }
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
        return true;
    }
    case FileAction::SaveSprite: {
        std::vector<uint8_t> spr(static_cast<size_t>(wBytes_) * hRows_, 0);
        extract(scratch.data(), 0, 0, wBytes_, hRows_, spr.data());
        std::FILE* f = std::fopen(fullPath.c_str(), "wb");
        if (!f) { status = "Cannot write file"; return false; }
        const size_t n = std::fwrite(spr.data(), 1, spr.size(), f);
        std::fclose(f);
        const bool ok = (n == spr.size());
        status = ok ? "Saved sprite" : "Short write";
        return ok;
    }
    case FileAction::SavePng: {
        // WYSIWYG image: decode the sprite's real bytes (mono ×1 / single-colour
        // ×2) through the NTSC pipeline and crop to the sprite region — matches
        // the colour view, unlike the raw .hgrspr which stays the mono source.
        if (!host) { status = "PNG failed: no host"; return false; }
        std::vector<uint8_t> bytes; int wB = 0, hR = 0;
        buildSpriteBytes(bytes, wB, hR);
        std::vector<uint8_t> pg(hgrpaint::kHiresSize, 0);
        stamp(bytes.data(), wB, hR, 0, 0,
              [&](int off, uint8_t v) { pg[off] = v; });
        std::vector<uint32_t> full(static_cast<size_t>(hgrpaint::kHiresWidth) *
                                   hgrpaint::kHiresHeight, 0);
        host->renderHgrPage(pg.data(), full.data(), false, false);
        const int W = wB * 7, H = hR;
        std::vector<uint32_t> rgba(static_cast<size_t>(W) * H, 0);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                rgba[static_cast<size_t>(y) * W + x] =
                    full[static_cast<size_t>(y) * hgrpaint::kHiresWidth + x];
        std::string err;
        const bool ok = host->savePng(fullPath, rgba.data(), W, H, err);
        status = ok ? "Saved PNG" : (std::string("PNG failed: ") + err);
        return ok;
    }
    case FileAction::ExportAsm: {
        // Write the dev-catalogue ca65 format — parseable back by the host's
        // dev-sprite library loader (round-trip pinned by sprite_asm_export_smoke).
        std::vector<uint8_t> spr(static_cast<size_t>(wBytes_) * hRows_, 0);
        extract(scratch.data(), 0, 0, wBytes_, hRows_, spr.data());
        const std::string text =
            formatSpriteAsm(sanitizeAsmName(asmName_), wBytes_, hRows_, spr.data());
        std::FILE* f = std::fopen(fullPath.c_str(), "wb");
        if (!f) { status = "Cannot write file"; return false; }
        const size_t n = std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
        const bool ok = (n == text.size());
        status = ok ? "Exported ca65 sprite" : "Short write";
        return ok;
    }
    }
    return true;
}

void HgrSpriteEditor::renderFileBrowser()
{
    namespace fs = std::filesystem;
    if (browserOpen) { ImGui::OpenPopup("HGR Sprite File##browser"); browserOpen = false; }

    const ImVec2 vpCenter = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(vpCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 460), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("HGR Sprite File##browser", nullptr, ImGuiWindowFlags_NoCollapse))
        return;

    const bool forSave = (browserAction != FileAction::LoadSprite);
    switch (browserAction) {
    case FileAction::LoadSprite: ImGui::TextUnformatted("Load raw HGR sprite bytes (current W\xC3\x97H)"); break;
    case FileAction::SaveSprite: ImGui::TextUnformatted("Save raw HGR sprite bytes"); break;
    case FileAction::SavePng:    ImGui::TextUnformatted("Export sprite PNG"); break;
    case FileAction::ExportAsm:  ImGui::TextUnformatted("Export ca65 sprite (.byte block, dev-library format)"); break;
    }
    ImGui::TextDisabled("%s", browserDir.c_str());
    ImGui::Separator();

    // ── Directory + file listing ─────────────────────────────────────────────
    ImGui::BeginChild("##fblist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.0f), true);
    if (ImGui::Selectable("../", false)) {
        fs::path up = fs::path(browserDir).parent_path();
        if (!up.empty()) browserDir = up.string();
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
    // Highlight files whose extension matches the pending action's filter.
    auto extRelevant = [this](std::string ext) {
        if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        size_t pos = 0;
        while (pos <= browserExts.size()) {
            const size_t comma = browserExts.find(',', pos);
            const std::string tok = browserExts.substr(pos, comma - pos);
            if (tok == ext) return true;
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        return false;
    };
    for (const auto& f : files) {
        std::error_code ec;
        const std::uintmax_t sz = f.file_size(ec);
        const std::string name = f.path().filename().string();
        const bool relevant = extRelevant(f.path().extension().string());
        char label[320];
        std::snprintf(label, sizeof(label), "%-28s %8llu B", name.c_str(),
                      static_cast<unsigned long long>(sz));
        if (!relevant) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
        if (ImGui::Selectable(label, false)) {
            if (forSave) {
                std::snprintf(browserSaveName, sizeof(browserSaveName), "%s", name.c_str());
            } else {                       // Load: act immediately
                performFileAction(browserAction, f.path().string());
                ImGui::CloseCurrentPopup();
            }
        }
        if (!relevant) ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    // ── Action row ───────────────────────────────────────────────────────────
    if (forSave) {
        ImGui::SetNextItemWidth(-160);
        ImGui::InputText("##fbname", browserSaveName, sizeof(browserSaveName));
        ImGui::SameLine();
        if (ImGui::Button("Save", ImVec2(70, 0)) && browserSaveName[0]) {
            const std::string full = (fs::path(browserDir) / browserSaveName).string();
            if (performFileAction(browserAction, full))
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Cancel", ImVec2(70, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
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
    // Rotate swaps W and H, so the rotated height (old width in px) must fit.
    const bool rotOk = (wpx() <= kRows);
    ImGui::SameLine();
    ImGui::BeginDisabled(!rotOk);
    if (ImGui::Button(ICON_FA_ROTATE "##rot")) transformRotateCW();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(rotOk ? "Rotate 90\xC2\xB0 clockwise (W and H swap)"
                                : "Too wide to rotate (rotated height would exceed 192 rows)");
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

void HgrSpriteEditor::renderToolPanel(const std::vector<uint8_t>& memory)
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

    // The sprite is MONOCHROME: a single chosen colour for the whole shape, and
    // it only reads as colour at ×2 (two adjacent ×1 dots make white — see the
    // colour view). So the palette picks the sprite's one colour and is active
    // only in ×2 mode; the shape itself is drawn in black & white.
    ImGui::TextUnformatted("Sprite colour (\xC3\x97""2)");
    ImGui::BeginDisabled(!mag2_);
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
    ImGui::EndDisabled();
    if (!mag2_)
        ImGui::TextDisabled("(enable \xC3\x97""2 to colour the sprite)");
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
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_EYE_DROPPER " Grab")) grabFromPage(memory);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copy the W\xC3\x97H rectangle at Byte X / Row Y from the live page\n"
                          "into the canvas (inverse of Stamp \xE2\x80\x94 grab from screen, edit as sprite)");

    ImGui::Separator();
    if (ImGui::Button("Load")) openFileBrowser(FileAction::LoadSprite);
    ImGui::SameLine(); if (ImGui::Button("Save")) openFileBrowser(FileAction::SaveSprite);
    ImGui::SameLine(); if (ImGui::Button("PNG"))  openFileBrowser(FileAction::SavePng);
    // ca65 export: label + Export ASM (round-trips through the dev-library parser).
    ImGui::SetNextItemWidth(110);
    ImGui::InputText("##asmname", asmName_, sizeof(asmName_));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ca65 label for the ASM export (sanitized to [a-z0-9_])");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FILE_EXPORT " Export ASM")) openFileBrowser(FileAction::ExportAsm);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Write the sprite as a ca65 .byte block\n"
                          "(the dev sprite library format \xE2\x80\x94 loadable back by POM1)");

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

    // Pixels — strictly black & white (the shape). Colour is a whole-sprite
    // attribute shown in the colour view, not per pixel here.
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const bool on = hgrpaint::pixelOn(scratch.data(), x, y);
            const ImVec2 a(p0.x + x * cell, p0.y + y * cell);
            dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell),
                              on ? IM_COL32(255,255,255,255) : IM_COL32(0,0,0,255));
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
        // Shape is authored monochrome; the sprite's single colour is chosen
        // separately and applied at ×2 (see buildSpriteBytes / the colour view).
        if (hov && cx >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            floodFill(cx, cy, HgrColor::White);
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
            const HgrColor c = eraseDrag ? HgrColor::Black : HgrColor::White;
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

// Read-only colour view: the sprite decoded through the real GEN2 NTSC pipeline
// (mono ×1 or single-colour ×2), shown at the same on-screen size as the B&W
// shape canvas so the two sit side by side and the colour bascules are legible.
void HgrSpriteEditor::renderColorCanvas()
{
    const int W = wpx(), H = hRows_;
    const float cell = static_cast<float>(zoom_);
    ImGui::BeginGroup();
    ImGui::TextUnformatted(mag2_ ? "Colour (\xC3\x97""2 NTSC)" : "Colour (NTSC)");
    if (host) {
        // Build the sprite's real bytes and stamp them into a blank page, then
        // decode through the host NTSC pipeline (identical to the live card).
        std::vector<uint8_t> bytes; int wB = 0, hR = 0;
        buildSpriteBytes(bytes, wB, hR);
        std::vector<uint8_t> pg(hgrpaint::kHiresSize, 0);
        stamp(bytes.data(), wB, hR, 0, 0,
              [&](int off, uint8_t v) { pg[off] = v; });
        host->renderHgrPage(pg.data(), colorRgba_.data(), false, false);
        colorTex_ = host->uploadTexture(colorTex_, colorRgba_.data(),
                                        hgrpaint::kHiresWidth, hgrpaint::kHiresHeight, false);
        // Crop the full-page texture to the sprite's decoded region (wB*7 × hR px)
        // and scale it to the B&W canvas footprint so both align row-for-row.
        const int pw = wB * 7, ph = hR;
        const ImVec2 uv0(0.0f, 0.0f);
        const ImVec2 uv1(static_cast<float>(pw) / hgrpaint::kHiresWidth,
                         static_cast<float>(ph) / hgrpaint::kHiresHeight);
        ImGui::Image(host->textureToImTexture(colorTex_),
                     ImVec2(W * cell, H * cell), uv0, uv1);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                mag2_ ? "The ×2 sprite in its single colour (%s). Each source pixel\n"
                        "becomes a 2-dot NTSC colour clock: one dot of the pair lit +\n"
                        "the palette high bit pick the hue instead of solid white."
                      : "×1 decodes monochrome. Enable ×2 to colour the sprite —\n"
                        "two adjacent dots read white, so colour needs the doubled clock.",
                kColorName[static_cast<int>(color_)]);
    } else {
        ImGui::Dummy(ImVec2(W * cell, H * cell));
    }
    ImGui::EndGroup();
}

void HgrSpriteEditor::render(const std::vector<uint8_t>& memory)
{
    clampGeom();
    handleShortcuts();

    renderTopBar();
    ImGui::Separator();

    // Dual display: the editable black-&-white shape canvas on the left, a
    // read-only NTSC colour view of the same sprite on the right, so the colour
    // bascules are legible while editing.
    const float oneCanvas = wpx() * static_cast<float>(zoom_);
    const float canvasW = oneCanvas * 2.0f + 40.0f;
    ImGui::BeginChild("##hgrcanvaspane", ImVec2(canvasW, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Black & white (shape)");
    renderCanvas();
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 16.0f);
    renderColorCanvas();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##hgrsidepane", ImVec2(0, 0));
    renderToolPanel(memory);
    ImGui::EndChild();

    renderFileBrowser();

    if (!status.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(status.c_str());
    }
}

} // namespace hgrsprite
