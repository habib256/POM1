// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TMS9918 sprite editor — see TmsSpriteEditor.h. Sibling of tmspaint/TmsPaintEditor.

#include "TmsSpriteEditor.h"

#include "imgui.h"
#include "IconsFontAwesome6.h"
// No GL/GLFW include: texture work goes through ITmsPaintHost::uploadTexture /
// destroyTexture so this portable module stays backend-agnostic.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace {

// The 15 TMS9918 colours + transparent, byte-identical to TMS9918::kPalette
// (mirrored so the portable editor needs no emulator header). Index 0 is drawn
// grey so it reads distinct from index 1 (black).
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

} // namespace

namespace tmssprite {

TmsSpriteEditor::TmsSpriteEditor(tmspaint::ITmsPaintHost* host_)
    : host(host_),
      shadow(kVramSize, 0),
      previewRgba(static_cast<size_t>(256) * 192, 0),
      sheetRgba(static_cast<size_t>(kSheet) * kSheet, 0)
{
}

TmsSpriteEditor::~TmsSpriteEditor()
{
    releaseGL();
}

void TmsSpriteEditor::releaseGL()
{
    if (host) {
        if (sheetTex)   host->destroyTexture(sheetTex);
    }
    sheetTex = nullptr;
}

// ── Shadow / mode / SAT ─────────────────────────────────────────────────────

void TmsSpriteEditor::refreshShadow()
{
    if (host) host->readVram(shadow.data());
}

void TmsSpriteEditor::ensureModeApplied()
{
    if (modeApplied) return;
    applyModeToChip();
    modeApplied = true;
}

void TmsSpriteEditor::applyModeToChip()
{
    if (!host) return;
    uint8_t regs[8];
    canonicalRegisters(regs, size_, magnified_, backdrop_);
    host->applyRegisters(regs);

    // Push the blank Graphics-I backdrop (name / pattern-slot-0 / colour table)
    // so the playfield renders flat and the sprite stands out. The sprite pattern
    // table is left untouched — that's the drawing surface.
    writeCanonicalBackdrop(shadow.data(), backdrop_);
    host->beginBatch();
    for (int i = 0; i < 768; ++i)
        host->pokeVram(static_cast<uint16_t>(kNameBase + i), shadow[kNameBase + i]);
    for (int i = 0; i < 8; ++i)
        host->pokeVram(static_cast<uint16_t>(kGfx1PatternBase + i), shadow[kGfx1PatternBase + i]);
    for (int i = 0; i < 32; ++i)
        host->pokeVram(static_cast<uint16_t>(kGfx1ColorBase + i), shadow[kGfx1ColorBase + i]);
    host->endBatch();
    satValid = false;
    updateSat();
}

void TmsSpriteEditor::updateSat()
{
    if (!host) return;
    const int pat = patNum_ & (size_ == Size::S16 ? 0xFC : 0xFF);
    uint8_t d[8];
    d[0] = static_cast<uint8_t>((previewY_ - 1) & 0xFF);   // SAT Y = display line - 1
    d[1] = static_cast<uint8_t>(previewX_ & 0xFF);
    d[2] = static_cast<uint8_t>(pat);
    d[3] = static_cast<uint8_t>((color_ & 0x0F) | (earlyClock_ ? 0x80 : 0));
    d[4] = static_cast<uint8_t>(kSatTerminator);           // hide sprite 1..31
    d[5] = d[6] = d[7] = 0;

    host->beginBatch();
    for (int i = 0; i < 8; ++i) {
        if (satValid && lastSat[i] == d[i]) continue;
        const uint16_t a = static_cast<uint16_t>(kSpriteAttrBase + i);
        shadow[a] = d[i];
        host->pokeVram(a, d[i]);
    }
    host->endBatch();
    std::memcpy(lastSat, d, 8);
    satValid = true;
}

// ── Editing primitives ──────────────────────────────────────────────────────

void TmsSpriteEditor::beginStroke() { stroke.clear(); }

void TmsSpriteEditor::commitStroke()
{
    if (stroke.empty()) return;
    undo.push_back(stroke);
    if (undo.size() > 64) undo.erase(undo.begin());   // cap history (matches paint editor)
    redo.clear();
    stroke.clear();
}

void TmsSpriteEditor::recordSet(int x, int y, bool on)
{
    const int addr = patternByteAddr(patNum_, size_, x, y);
    if (addr < 0) return;
    const uint8_t old = shadow[addr];
    if (!setPixel(shadow.data(), patNum_, size_, x, y, on)) return;
    stroke.push_back({static_cast<uint16_t>(addr), old, shadow[addr]});
    if (host) host->pokeVram(static_cast<uint16_t>(addr), shadow[addr]);
}

void TmsSpriteEditor::floodFill(int x, int y, bool on)
{
    const int N = nDim();
    if (x < 0 || x >= N || y < 0 || y >= N) return;
    const bool seed = getPixel(shadow.data(), patNum_, size_, x, y);
    if (seed == on) return;
    ensureModeApplied();
    if (host) host->beginBatch();
    beginStroke();
    std::vector<uint8_t> seen(static_cast<size_t>(N) * N, 0);
    std::vector<std::pair<int,int>> st;
    st.emplace_back(x, y);
    seen[static_cast<size_t>(y) * N + x] = 1;
    while (!st.empty()) {
        const auto p = st.back(); st.pop_back();
        recordSet(p.first, p.second, on);
        const int nb[4][2] = {{p.first-1,p.second},{p.first+1,p.second},
                              {p.first,p.second-1},{p.first,p.second+1}};
        for (auto& n : nb) {
            const int nx = n[0], ny = n[1];
            if (nx < 0 || nx >= N || ny < 0 || ny >= N) continue;
            const size_t idx = static_cast<size_t>(ny) * N + nx;
            if (seen[idx]) continue;
            if (getPixel(shadow.data(), patNum_, size_, nx, ny) != seed) continue;
            seen[idx] = 1;
            st.emplace_back(nx, ny);
        }
    }
    commitStroke();
    if (host) host->endBatch();
}

void TmsSpriteEditor::applyOps(const std::vector<ByteEdit>& ops, bool forward)
{
    if (!host) return;
    host->beginBatch();
    if (forward)
        for (const auto& e : ops) { shadow[e.addr] = e.newVal; host->pokeVram(e.addr, e.newVal); }
    else
        for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
            shadow[it->addr] = it->oldVal; host->pokeVram(it->addr, it->oldVal);
        }
    host->endBatch();
}

void TmsSpriteEditor::doUndo()
{
    if (undo.empty()) return;
    ensureModeApplied();
    auto ops = undo.back(); undo.pop_back();
    applyOps(ops, false);
    redo.push_back(ops);
    status = "Undo";
}

void TmsSpriteEditor::doRedo()
{
    if (redo.empty()) return;
    ensureModeApplied();
    auto ops = redo.back(); redo.pop_back();
    applyOps(ops, true);
    undo.push_back(ops);
    if (undo.size() > 64) undo.erase(undo.begin());
    status = "Redo";
}

// ── Whole-sprite transforms ─────────────────────────────────────────────────

void TmsSpriteEditor::writeGrid(const std::vector<uint8_t>& grid)
{
    const int N = nDim();
    ensureModeApplied();
    if (host) host->beginBatch();
    beginStroke();
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            recordSet(x, y, grid[static_cast<size_t>(y) * N + x] != 0);
    commitStroke();
    if (host) host->endBatch();
}

void TmsSpriteEditor::transformClear()
{
    const int N = nDim();
    writeGrid(std::vector<uint8_t>(static_cast<size_t>(N) * N, 0));
    status = "Cleared";
}

void TmsSpriteEditor::transformInvert()
{
    const int N = nDim();
    std::vector<uint8_t> g(static_cast<size_t>(N) * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            g[static_cast<size_t>(y) * N + x] = getPixel(shadow.data(), patNum_, size_, x, y) ? 0 : 1;
    writeGrid(g);
    status = "Inverted";
}

void TmsSpriteEditor::transformFlipH()
{
    const int N = nDim();
    std::vector<uint8_t> g(static_cast<size_t>(N) * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            g[static_cast<size_t>(y) * N + x] = getPixel(shadow.data(), patNum_, size_, N - 1 - x, y);
    writeGrid(g);
    status = "Flipped horizontally";
}

void TmsSpriteEditor::transformFlipV()
{
    const int N = nDim();
    std::vector<uint8_t> g(static_cast<size_t>(N) * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            g[static_cast<size_t>(y) * N + x] = getPixel(shadow.data(), patNum_, size_, x, N - 1 - y);
    writeGrid(g);
    status = "Flipped vertically";
}

void TmsSpriteEditor::transformShift(int dx, int dy)
{
    const int N = nDim();
    std::vector<uint8_t> g(static_cast<size_t>(N) * N, 0);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const int sx = x - dx, sy = y - dy;   // source of destination (x,y)
            if (sx < 0 || sx >= N || sy < 0 || sy >= N) continue;
            g[static_cast<size_t>(y) * N + x] = getPixel(shadow.data(), patNum_, size_, sx, sy);
        }
    writeGrid(g);
    status = "Shifted";
}

void TmsSpriteEditor::transformRotateCW()
{
    const int N = nDim();
    std::vector<uint8_t> g(static_cast<size_t>(N) * N);
    // dest(x,y) = src(y, N-1-x)  (90° clockwise)
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            g[static_cast<size_t>(y) * N + x] = getPixel(shadow.data(), patNum_, size_, y, N - 1 - x);
    writeGrid(g);
    status = "Rotated 90\xC2\xB0";
}

// ── Files (native picker first; ImGui browser fallback — mirrors the paint
//    editor's openFileBrowser / performFileAction / renderFileBrowser split) ──

void TmsSpriteEditor::openFileBrowser(FileAction a)
{
    browserAction = a;
    if (browserDir.empty()) {
        std::error_code ec;
        browserDir = std::filesystem::current_path(ec).string();
        if (ec || browserDir.empty()) browserDir = ".";
    }
    const bool forSave = (a != FileAction::LoadVram);
    std::string title, desc, defName;
    switch (a) {
    case FileAction::LoadVram:
        title = "Load TMS9918 VRAM"; desc = "TMS9918 VRAM"; browserExts = "tms,vram,bin"; break;
    case FileAction::SaveVram:
        title = "Save TMS9918 VRAM"; desc = "TMS9918 VRAM"; browserExts = "tms";
        defName = "sprites.tms"; break;
    case FileAction::SavePng:
        title = "Export screen PNG"; desc = "PNG image";    browserExts = "png";
        defName = "sprites.png"; break;
    case FileAction::ExportAsm:
        title = "Export ca65 sprite"; desc = "ca65 assembly"; browserExts = "asm";
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
bool TmsSpriteEditor::performFileAction(FileAction a, const std::string& fullPath)
{
    if (!host) return true;
    switch (a) {
    case FileAction::LoadVram: {
        std::string err;
        if (host->loadVram(fullPath, err)) { status = "Loaded VRAM"; satValid = false; }
        else status = std::string("Load failed: ") + err;
        return true;
    }
    case FileAction::SaveVram: {
        std::string err;
        const bool ok = host->saveVram(fullPath, err);
        status = ok ? "Saved VRAM" : (std::string("Save failed: ") + err);
        return ok;
    }
    case FileAction::SavePng: {
        if (!host->liveFramebuffer(previewRgba.data())) { status = "No live framebuffer"; return false; }
        std::string err;
        const bool ok = host->savePng(fullPath, previewRgba.data(), 256, 192, err);
        status = ok ? "Saved PNG" : (std::string("PNG failed: ") + err);
        return ok;
    }
    case FileAction::ExportAsm: {
        // Write the current sprite's pattern bytes (8 = 8×8, 32 = 16×16 native
        // quadrant stream) in the dev-catalogue ca65 format — parseable back by
        // the host's library loader (round-trip pinned by sprite_asm_export_smoke).
        const int pat = patNum_ & (size_ == Size::S16 ? 0xFC : 0xFF);
        const int nBytes = slotsPerSprite(size_) * 8;
        const std::string text = formatSpriteAsm(sanitizeAsmName(asmName_), nBytes,
                                                 &shadow[kSpritePatternBase + pat * 8]);
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

void TmsSpriteEditor::renderFileBrowser()
{
    namespace fs = std::filesystem;
    if (browserOpen) { ImGui::OpenPopup("TMS Sprite File##browser"); browserOpen = false; }

    const ImVec2 vpCenter = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(vpCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 460), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("TMS Sprite File##browser", nullptr, ImGuiWindowFlags_NoCollapse))
        return;

    const bool forSave = (browserAction != FileAction::LoadVram);
    switch (browserAction) {
    case FileAction::LoadVram:  ImGui::TextUnformatted("Load TMS9918 VRAM (16 KB)"); break;
    case FileAction::SaveVram:  ImGui::TextUnformatted("Save TMS9918 VRAM (16 KB)"); break;
    case FileAction::SavePng:   ImGui::TextUnformatted("Export screen PNG"); break;
    case FileAction::ExportAsm: ImGui::TextUnformatted("Export ca65 sprite (.byte block, dev-library format)"); break;
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

// ── Small helpers ───────────────────────────────────────────────────────────

void TmsSpriteEditor::setSize(Size s)
{
    if (s == size_) return;
    size_ = s;
    clampPattern();
    satValid = false;
    if (modeApplied) applyModeToChip();   // sprite-size bit lives in R1
    status = (s == Size::S16) ? "16\xC3\x97""16 sprite" : "8\xC3\x97""8 sprite";
}

void TmsSpriteEditor::clampPattern()
{
    const int maxPat = kPatternSlots - slotsPerSprite(size_);
    if (patNum_ < 0) patNum_ = 0;
    if (patNum_ > maxPat) patNum_ = maxPat;
    if (size_ == Size::S16) patNum_ &= 0xFC;
}

void TmsSpriteEditor::handleShortcuts()
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

void TmsSpriteEditor::renderTopBar()
{
    // Geometry.
    int sizeIdx = (size_ == Size::S16) ? 1 : 0;
    ImGui::SetNextItemWidth(90);
    if (ImGui::Combo("##size", &sizeIdx, "8\xC3\x97""8\0" "16\xC3\x97""16\0"))
        setSize(sizeIdx == 1 ? Size::S16 : Size::S8);
    ImGui::SameLine();
    if (ImGui::Checkbox("Mag", &magnified_)) { if (modeApplied) applyModeToChip(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Magnify sprite \xC3\x97""2 on screen");

    ImGui::SameLine();
    ImGui::TextUnformatted("Back:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    if (ImGui::BeginCombo("##backdrop", kColorName[backdrop_ & 0x0F])) {
        for (int i = 0; i < 16; ++i) {
            ImGui::PushID(i);
            ImGui::ColorButton("##bd", ImGui::ColorConvertU32ToFloat4(kSwatch[i]),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
            ImGui::SameLine();
            if (ImGui::Selectable(kColorName[i], backdrop_ == i)) {
                backdrop_ = i;
                if (modeApplied) applyModeToChip();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

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
    ImGui::SameLine(); if (ImGui::Button(ICON_FA_ROTATE "##rot")) transformRotateCW();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate 90\xC2\xB0 clockwise");
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
    ImGui::SliderInt("Zoom", &zoom_, 8, 40);
}

void TmsSpriteEditor::renderToolPanel()
{
    // Tools.
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

    // Colour palette (1..15; 0 = transparent sprite = invisible, so skipped).
    ImGui::TextUnformatted("Sprite colour");
    for (int i = 1; i < 16; ++i) {
        ImGui::PushID(1000 + i);
        const bool sel = (color_ == i);
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(kSwatch[i]);
        ImGuiColorEditFlags fl = ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop;
        if (ImGui::ColorButton(kColorName[i], c, fl, ImVec2(20, 20))) {
            color_ = i;
            if (modeApplied) updateSat();
        }
        if (sel) {
            ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(mn, mx, IM_COL32(255,255,255,255), 0, 0, 2.0f);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kColorName[i]);
        ImGui::PopID();
        if ((i % 5) != 0) ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Separator();

    // Pattern selector.
    ImGui::TextUnformatted("Pattern #");
    const int step = slotsPerSprite(size_);
    if (ImGui::Button(ICON_FA_CARET_LEFT "##pprev")) { patNum_ -= step; clampPattern(); if (modeApplied) updateSat(); }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("##pat", &patNum_, step, step * 4)) { clampPattern(); if (modeApplied) updateSat(); }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CARET_RIGHT "##pnext")) { patNum_ += step; clampPattern(); if (modeApplied) updateSat(); }
    if (size_ == Size::S16)
        ImGui::TextDisabled("slots %d-%d", patNum_ & 0xFC, (patNum_ & 0xFC) + 3);

    // Sprite-bank browser: direct view of the chip's sprite pattern table ($3800)
    // — click any cell to edit that sprite.
    if (ImGui::CollapsingHeader("Sprite bank (VRAM $3800)", ImGuiTreeNodeFlags_DefaultOpen))
        renderSpriteBank();

    ImGui::Separator();

    // On-screen sprite placement (SAT X/Y — visible on the Graphic Card window).
    ImGui::TextUnformatted("Screen position");
    ImGui::SetNextItemWidth(150);
    if (ImGui::SliderInt("X", &previewX_, 0, 255)) { ensureModeApplied(); }
    ImGui::SetNextItemWidth(150);
    if (ImGui::SliderInt("Y", &previewY_, 0, 191)) { ensureModeApplied(); }
    if (ImGui::Checkbox("Early Clock (-32px)", &earlyClock_)) { if (modeApplied) updateSat(); }

    ImGui::Separator();
    if (ImGui::Button("Load VRAM")) openFileBrowser(FileAction::LoadVram);
    ImGui::SameLine();
    if (ImGui::Button("Save VRAM")) openFileBrowser(FileAction::SaveVram);
    ImGui::SameLine();
    if (ImGui::Button("Save PNG")) openFileBrowser(FileAction::SavePng);
    // ca65 export: label + Export ASM (round-trips through the dev-library parser).
    ImGui::SetNextItemWidth(110);
    ImGui::InputText("##asmname", asmName_, sizeof(asmName_));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ca65 label for the ASM export (sanitized to [a-z0-9_])");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FILE_EXPORT " Export ASM")) openFileBrowser(FileAction::ExportAsm);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Write the current sprite's pattern bytes as a ca65 .byte block\n"
                          "(the dev sprite library format \xE2\x80\x94 loadable back by POM1)");

    // Built-in TMS9918 sprite library (dev/lib/tms9918) — loads into VRAM at the
    // current pattern slot group (16×16).
    if (ImGui::CollapsingHeader("Dev sprite library", ImGuiTreeNodeFlags_DefaultOpen))
        renderDevSprites();
}

void TmsSpriteEditor::loadDevSprite(const tmspaint::DevSprite& s)
{
    if (!host || s.bytes.size() < 32) return;
    setSize(Size::S16);               // the dev catalogue is 16×16 sprites
    ensureModeApplied();
    const int pat = patNum_ & 0xFC;
    host->beginBatch();
    beginStroke();
    for (int i = 0; i < 32; ++i) {
        const uint16_t addr = static_cast<uint16_t>(kSpritePatternBase + pat * 8 + i);
        const uint8_t old = shadow[addr];
        if (old == s.bytes[i]) continue;
        shadow[addr] = s.bytes[i];
        stroke.push_back({addr, old, s.bytes[i]});
        host->pokeVram(addr, s.bytes[i]);
    }
    commitStroke();
    host->endBatch();
    updateSat();
    status = "Loaded dev sprite: " + s.name;
}

void TmsSpriteEditor::renderDevSprites()
{
    if (!host) return;
    if (!devLoaded_) { devCats_ = host->devSprites(); devLoaded_ = true; }
    if (devCats_.empty()) {
        ImGui::TextDisabled("No sprite library found (dev/lib/tms9918).");
        return;
    }
    devCat_ = std::clamp(devCat_, 0, static_cast<int>(devCats_.size()) - 1);

    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo("Category##dev", devCats_[devCat_].name.c_str())) {
        for (int i = 0; i < static_cast<int>(devCats_.size()); ++i)
            if (ImGui::Selectable(devCats_[i].name.c_str(), devCat_ == i))
                devCat_ = i;
        ImGui::EndCombo();
    }

    const auto& cat = devCats_[devCat_];
    if (cat.sprites.empty()) return;

    const float  th = 3.0f;                             // px per sprite pixel (16×16 → 48)
    const ImVec2 sz(16 * th, 16 * th);
    const float  cellW = sz.x + ImGui::GetStyle().ItemSpacing.x + 6.0f;
    const float  avail = ImGui::GetContentRegionAvail().x;
    const int    perRow = std::max(1, static_cast<int>(avail / cellW));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < static_cast<int>(cat.sprites.size()); ++i) {
        const auto& s = cat.sprites[i];
        ImGui::PushID(i);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##devthumb", sz);
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), IM_COL32(20,20,20,255));
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const int quad = (x >= 8 ? 2 : 0) + (y >= 8 ? 1 : 0);   // TMS quadrant order
                const int bi   = quad * 8 + (y & 7);
                if (bi < static_cast<int>(s.bytes.size()) && (s.bytes[bi] & (0x80 >> (x & 7)))) {
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

void TmsSpriteEditor::renderCanvas()
{
    const int N = nDim();
    const float cell = static_cast<float>(zoom_);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 sz(N * cell, N * cell);
    ImGui::InvisibleButton("##spritecanvas", sz,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hov = ImGui::IsItemHovered();

    // Pixels.
    const ImU32 offCol = kSwatch[backdrop_ & 0x0F];
    const ImU32 onCol  = kSwatch[color_ & 0x0F];
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const bool on = getPixel(shadow.data(), patNum_, size_, x, y);
            const ImVec2 a(p0.x + x * cell, p0.y + y * cell);
            const ImVec2 b(a.x + cell, a.y + cell);
            dl->AddRectFilled(a, b, on ? onCol : offCol);
        }

    // Grid lines (every pixel, plus a heavier line on the 8-pixel byte boundary).
    if (showGrid_) {
        for (int i = 0; i <= N; ++i) {
            const float gx = p0.x + i * cell, gy = p0.y + i * cell;
            const bool major = (i % 8) == 0;
            const ImU32 gc = major ? IM_COL32(120,120,120,255) : IM_COL32(70,70,70,180);
            dl->AddLine(ImVec2(gx, p0.y), ImVec2(gx, p0.y + sz.y), gc, major ? 2.0f : 1.0f);
            dl->AddLine(ImVec2(p0.x, gy), ImVec2(p0.x + sz.x, gy), gc, major ? 2.0f : 1.0f);
        }
    }
    dl->AddRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), IM_COL32(200,200,200,255));

    // Mouse.
    ImGuiIO& io = ImGui::GetIO();
    int cx = -1, cy = -1;
    if (hov) {
        cx = static_cast<int>((io.MousePos.x - p0.x) / cell);
        cy = static_cast<int>((io.MousePos.y - p0.y) / cell);
        if (cx >= 0 && cx < N && cy >= 0 && cy < N) {
            const ImVec2 a(p0.x + cx * cell, p0.y + cy * cell);
            dl->AddRect(a, ImVec2(a.x + cell, a.y + cell), IM_COL32(255,255,0,255), 0, 0, 2.0f);
        } else { cx = cy = -1; }
    }

    const bool L = io.MouseDown[0], R = io.MouseDown[1];
    if (tool == Tool::Fill) {
        if (hov && cx >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            floodFill(cx, cy, true);
        if (hov && cx >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            floodFill(cx, cy, false);
    } else {
        if (!dragging && hov && cx >= 0 && (L || R)) {
            ensureModeApplied();
            beginStroke();
            dragging = true;
            eraseDrag = R || (tool == Tool::Eraser);
            lastPx = lastPy = -1;
        }
        if (dragging && cx >= 0) {
            const bool on = !eraseDrag;
            if (lastPx < 0) { recordSet(cx, cy, on); }
            else {
                // Bresenham between the last and current cell so fast drags fill.
                int x0 = lastPx, y0 = lastPy, x1 = cx, y1 = cy;
                const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
                const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
                int err = dx + dy;
                for (;;) {
                    recordSet(x0, y0, on);
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

void TmsSpriteEditor::renderSpriteBank()
{
    if (!host) return;
    const int N     = nDim();              // 8 or 16
    const int cols  = kSheet / N;          // 16 (8×8) or 8 (16×16)
    const int rows  = kSheet / N;
    const int total = cols * rows;         // 256 or 64
    const ImU32 onCol = kSwatch[color_ & 0x0F];

    // Rasterise the whole sprite pattern table straight from live VRAM so every
    // sprite in the chip is visible — a checkerboard backs the empty pixels.
    for (int slot = 0; slot < total; ++slot) {
        const int scol = slot % cols, srow = slot / cols;
        const int pat  = (N == 16) ? slot * 4 : slot;
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const bool on  = getPixel(shadow.data(), pat, size_, x, y);
                const bool alt = (((scol * N + x) >> 2) + ((srow * N + y) >> 2)) & 1;
                const ImU32 bg = alt ? IM_COL32(40,40,40,255) : IM_COL32(24,24,24,255);
                sheetRgba[static_cast<size_t>(srow * N + y) * kSheet + (scol * N + x)] =
                    on ? onCol : bg;
            }
    }
    sheetTex = host->uploadTexture(sheetTex, sheetRgba.data(), kSheet, kSheet, false);

    const float scale = 2.0f;              // 128 → 256 px on screen
    const float disp  = kSheet * scale;
    const float cell  = N * scale;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##spritebank", ImVec2(disp, disp));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddImage(host->textureToImTexture(sheetTex), p0, ImVec2(p0.x + disp, p0.y + disp));

    for (int i = 0; i <= cols; ++i) {
        const float g = p0.x + i * cell, h = p0.y + i * cell;
        dl->AddLine(ImVec2(g, p0.y), ImVec2(g, p0.y + disp), IM_COL32(60,60,60,160));
        dl->AddLine(ImVec2(p0.x, h), ImVec2(p0.x + disp, h), IM_COL32(60,60,60,160));
    }
    // Highlight the slot being edited.
    const int selSlot = (N == 16) ? (patNum_ & 0xFC) / 4 : (patNum_ & 0xFF);
    if (selSlot >= 0 && selSlot < total) {
        const ImVec2 a(p0.x + (selSlot % cols) * cell, p0.y + (selSlot / cols) * cell);
        dl->AddRect(a, ImVec2(a.x + cell, a.y + cell), IM_COL32(255,255,0,255), 0, 0, 2.0f);
    }
    // Hover highlight + click to jump to that sprite.
    if (hov) {
        ImGuiIO& io = ImGui::GetIO();
        const int hc = static_cast<int>((io.MousePos.x - p0.x) / cell);
        const int hr = static_cast<int>((io.MousePos.y - p0.y) / cell);
        if (hc >= 0 && hc < cols && hr >= 0 && hr < rows) {
            const int slot = hr * cols + hc;
            const int pat  = (N == 16) ? slot * 4 : slot;
            const ImVec2 a(p0.x + hc * cell, p0.y + hr * cell);
            dl->AddRect(a, ImVec2(a.x + cell, a.y + cell), IM_COL32(255,255,255,190));
            ImGui::SetTooltip("Sprite %d  (pattern $%02X, VRAM $%04X)",
                              slot, pat, kSpritePatternBase + pat * 8);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                patNum_ = pat;
                clampPattern();
                if (modeApplied) updateSat();
            }
        }
    }
}

void TmsSpriteEditor::render()
{
    if (!dragging) refreshShadow();   // don't revert an in-progress stroke
    handleShortcuts();

    renderTopBar();
    ImGui::Separator();

    const float canvasW = nDim() * static_cast<float>(zoom_) + 24.0f;
    ImGui::BeginChild("##canvaspane", ImVec2(canvasW, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    renderCanvas();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##sidepane", ImVec2(0, 0));
    renderToolPanel();
    ImGui::EndChild();

    renderFileBrowser();

    if (modeApplied) updateSat();

    if (!status.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(status.c_str());
    }
}

} // namespace tmssprite
