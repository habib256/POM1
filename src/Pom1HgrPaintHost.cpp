// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Pom1HgrPaintHost — see Pom1HgrPaintHost.h.

#include "Pom1HgrPaintHost.h"

#include "EmulationController.h"
#include "HgrPaintModel.h"        // hgrpaint:: geometry constants
#include "NativeFileDialog.h"     // OS-native file picker for Load/Save/Import
#include "PomRenderer.h"          // shared graphics backend (GL or Metal)
#include "third_party/stb/stb_image_write.h"   // decl only; impl lives in main_imgui.cpp

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

Pom1HgrPaintHost::Pom1HgrPaintHost(EmulationController* emu,
                                   GLFWwindow* const* windowSlot)
    : emu_(emu), windowSlot_(windowSlot), scratch_(0x10000, 0)
{
}

bool Pom1HgrPaintHost::pickFilePath(bool forSave, const std::string& title,
                                    const std::string& filterDesc,
                                    const std::string& extCsv,
                                    const std::string& defaultDir,
                                    const std::string& defaultName,
                                    std::string& outPath)
{
    GLFWwindow* parent = windowSlot_ ? *windowSlot_ : nullptr;
    return pom1::NativeFileDialog::pickFiltered(parent, forSave, title, filterDesc,
                                                extCsv, defaultDir, defaultName,
                                                outPath);
}

bool Pom1HgrPaintHost::nativeFilePickerAvailable() const
{
    return pom1::NativeFileDialog::isAvailable();
}

// ── Built-in HGR sprite library (dev/lib/gen2/sprites/*.asm) ─────────────────
// The GEN2 HGR SCROLL-O-SPRITES catalogue is shipped as ca65 sources: each
// sprite is 16 rows × 3 bytes emitted as `.byte $xx, ...` under a per-sprite
// `; slot NN/MM ... -- <name>` comment. We parse those into raw byte blobs the
// sprite editor can drop onto its canvas. Parsed once and cached (the tree is
// bundled next to POM1 in release packages, and lives at the repo root in a
// source build).
namespace {

// Append every "$xx" hex byte on a `.byte` line to `out`.
void parseByteLine(const std::string& line, std::vector<uint8_t>& out)
{
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        if (line[i] != '$') continue;
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        const int hi = hex(line[i + 1]);
        if (hi < 0) continue;
        const int lo = (i + 2 < line.size()) ? hex(line[i + 2]) : -1;
        out.push_back(static_cast<uint8_t>(lo < 0 ? hi : (hi * 16 + lo)));
    }
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

hgrpaint::DevSpriteCategory parseSpriteFile(const std::filesystem::path& file)
{
    hgrpaint::DevSpriteCategory cat;
    // "sprites_creatures_hgr.asm" → "creatures".
    std::string stem = file.stem().string();               // sprites_creatures_hgr
    if (stem.rfind("sprites_", 0) == 0) stem = stem.substr(8);
    const size_t hgrPos = stem.rfind("_hgr");
    if (hgrPos != std::string::npos) stem = stem.substr(0, hgrPos);
    cat.name = stem;

    std::ifstream in(file);
    if (!in) return cat;
    hgrpaint::DevSprite cur;
    bool haveCur = false;
    std::string pendingName;        // from the most recent "; slot ... -- name" comment
    auto flush = [&]() {
        if (haveCur && cur.bytes.size() >= 48) {
            cur.bytes.resize(48);                           // 16 rows × 3 bytes
            cur.wBytes = 3; cur.hRows = 16;
            cat.sprites.push_back(std::move(cur));
        }
        cur = hgrpaint::DevSprite{};
        haveCur = false;
    };
    // True + fills `label` when the trimmed, comment-stripped line is a bare ca65
    // label "ident:". A per-sprite pattern label (e.g. `creat_wolf_pat:`,
    // `normal_pat:`) starts a new sprite; the `<cat>_hgr_data:` base label starts
    // an empty one that the next label flushes away.
    auto isLabel = [](const std::string& code, std::string& label) {
        if (code.size() < 2 || code.back() != ':') return false;
        for (size_t i = 0; i + 1 < code.size(); ++i) {
            const char c = code[i];
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
        }
        const char c0 = code[0];
        if (!(std::isalpha(static_cast<unsigned char>(c0)) || c0 == '_')) return false;
        label = code.substr(0, code.size() - 1);
        return true;
    };
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty()) continue;
        if (t[0] == ';') {                                  // comment: catch slot name
            const size_t dash = t.find("--");
            if (t.find("slot") != std::string::npos && dash != std::string::npos) {
                pendingName = trim(t.substr(dash + 2));
                const size_t paren = pendingName.find('(');   // drop trailing "(renamed …)"
                if (paren != std::string::npos) pendingName = trim(pendingName.substr(0, paren));
            }
            continue;
        }
        // Strip any inline comment before inspecting the code.
        const size_t sc = t.find(';');
        std::string code = trim(sc == std::string::npos ? t : t.substr(0, sc));
        std::string label;
        if (isLabel(code, label)) {
            flush();
            if (!pendingName.empty()) { cur.name = pendingName; pendingName.clear(); }
            else {
                if (label.size() > 4 && label.compare(label.size() - 4, 4, "_pat") == 0)
                    label.resize(label.size() - 4);
                cur.name = label;
            }
            haveCur = true;
            continue;
        }
        if (haveCur && code.find(".byte") != std::string::npos)
            parseByteLine(code, cur.bytes);
    }
    flush();
    return cat;
}

} // namespace

std::vector<hgrpaint::DevSpriteCategory> Pom1HgrPaintHost::devSprites()
{
    if (devSpritesLoaded_) return devSpritesCache_;
    devSpritesLoaded_ = true;

    namespace fs = std::filesystem;
    std::error_code ec;
    // Candidate roots: repo-root run (run_emulator.sh), a build/ subdir, and the
    // packaged layout where dev/ sits next to the binary.
    const char* candidates[] = {
        "dev/lib/gen2/sprites",
        "../dev/lib/gen2/sprites",
        "../../dev/lib/gen2/sprites",
    };
    fs::path dir;
    for (const char* c : candidates)
        if (fs::is_directory(c, ec)) { dir = c; break; }
    if (dir.empty()) return devSpritesCache_;

    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const auto p = e.path();
        const std::string fn = p.filename().string();
        if (p.extension() == ".asm" && fn.rfind("sprites_", 0) == 0 &&
            fn.find("_hgr") != std::string::npos)
            files.push_back(p);
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        auto cat = parseSpriteFile(f);
        if (!cat.sprites.empty()) devSpritesCache_.push_back(std::move(cat));
    }
    return devSpritesCache_;
}

void Pom1HgrPaintHost::pokeByte(uint16_t addr, uint8_t value)
{
    // While batching, defer to one writeMemoryBatch() so a bulk edit costs one
    // lock + one snapshot publish instead of thousands.
    if (batchDepth_ > 0) { batch_.emplace_back(addr, value); return; }
    if (emu_) emu_->writeMemory(addr, value);
}

void Pom1HgrPaintHost::beginBatch()
{
    // Reentrant: only the OUTERMOST begin starts a fresh batch, so a nested
    // begin/end pair (e.g. doUndo()'s applyOps fired while a shape stroke is still
    // open) can't clear the queue or flush it early. Pairs with endBatch's depth
    // count. Previously a plain bool, so a nested endBatch flipped batching off
    // and the outer stroke's remaining pokes escaped the batch (round-2 D1).
    if (batchDepth_++ == 0) batch_.clear();
}

void Pom1HgrPaintHost::endBatch()
{
    if (batchDepth_ > 0 && --batchDepth_ == 0) {
        if (emu_) emu_->writeMemoryBatch(batch_);
        batch_.clear();
    }
}

void Pom1HgrPaintHost::renderHgrPage(const uint8_t* page8k, uint32_t* outRgba, bool mono,
                                     bool grMode)
{
    // Render through the GEN2 pipeline. The Apple II interleaves are
    // page-independent, so we always stage at the PAGE 1 base and render page 1 —
    // the resulting colours are identical to page 2.
    GraphicsCard::DisplayState st;
    st.textMode = false; st.mixedMode = false; st.page2 = false;
    if (grMode) {
        // Lo-res: the first 1 KB of `page8k` is the text/lo-res page; stage it at
        // $0400 and render LORES (hiRes=false, textMode=false → 40×48 blocks).
        std::copy(page8k, page8k + 0x400, scratch_.begin() + 0x0400);
        st.hiRes = false;
    } else {
        std::copy(page8k, page8k + hgrpaint::kHiresSize, scratch_.begin() + 0x2000);
        st.hiRes = true;
    }
    gfx_.setMonitorMode(mono ? GraphicsCard::MonitorMode::Monochrome
                             : GraphicsCard::MonitorMode::Colour);
    gfx_.invalidate();   // always repaint: the editor may have just poked bytes
    gfx_.render(scratch_.data(), st, st, {});
    std::copy(gfx_.pixels(),
              gfx_.pixels() + static_cast<size_t>(hgrpaint::kHiresWidth) * hgrpaint::kHiresHeight,
              outRgba);
}

bool Pom1HgrPaintHost::loadImage(const std::string& path, uint16_t baseAddr, std::string& err)
{
    return emu_ && emu_->loadBinaryToRam(path, baseAddr, err);
}

bool Pom1HgrPaintHost::saveImage(const std::string& path, uint16_t baseAddr, int sizeBytes,
                                 std::string& err)
{
    if (sizeBytes <= 0) sizeBytes = hgrpaint::kHiresSize;
    return emu_ && emu_->saveMemoryRange(
        path, baseAddr, static_cast<uint16_t>(baseAddr + sizeBytes - 1),
        /*binaryFormat=*/true, err);
}

// Phase 3 / Metal: routes through PomRenderer so the editor lights up on
// whichever backend the binary was built for. Same-size repeat uploads
// (the steady state at ~60 Hz while painting) sub-update the existing
// texture; only a dimension change destroys-and-recreates. This restores
// the cheap-path that the historical glTexImage2D-each-call code had
// implicitly (the driver elides storage reallocation when w/h/format
// match — but the GLuint name itself was reused).
void* Pom1HgrPaintHost::uploadTexture(void* tex, const void* rgba,
                                      int w, int h, bool linear)
{
    auto* r = pom1::renderer();
    if (!r) return tex;   // headless / pre-init — keep the old handle (likely nullptr)
    auto* existing = static_cast<pom1::Texture*>(tex);
    if (existing &&
        r->textureWidth(existing)  == w &&
        r->textureHeight(existing) == h) {
        r->updateTexture(existing, static_cast<const uint32_t*>(rgba));
        return existing;
    }
    if (existing) r->destroyTexture(existing);
    const auto filt = linear ? pom1::PomRenderer::Filter::Linear
                             : pom1::PomRenderer::Filter::Nearest;
    return r->createTexture(w, h, filt,
                            static_cast<const uint32_t*>(rgba));
}

void Pom1HgrPaintHost::destroyTexture(void* tex)
{
    if (!tex) return;
    if (auto* r = pom1::renderer())
        r->destroyTexture(static_cast<pom1::Texture*>(tex));
}

ImTextureID Pom1HgrPaintHost::textureToImTexture(void* tex) const
{
    if (!tex) return (ImTextureID)0;
    if (auto* r = pom1::renderer())
        return r->asImTextureID(static_cast<pom1::Texture*>(tex));
    return (ImTextureID)0;
}

bool Pom1HgrPaintHost::savePng(const std::string& path, const uint32_t* rgba,
                               int w, int h, std::string& err)
{
    // rgba is top-down RGBA8 (software-render order), exactly what stbi_write_png
    // expects with stride = w*4. Impl linked from main_imgui.cpp.
    if (stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4) == 0) {
        err = "stbi_write_png failed (directory writable?)";
        return false;
    }
    return true;
}
