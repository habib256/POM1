// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Pom1TmsPaintHost — see Pom1TmsPaintHost.h.

#include "Pom1TmsPaintHost.h"

#include "EmulationController.h"
#include "TmsPaintModel.h"        // tmspaint:: geometry constants
#include "NativeFileDialog.h"     // OS-native file picker for Load/Save/Import
#include "PomRenderer.h"          // shared graphics backend (GL or Metal)
#include "third_party/stb/stb_image_write.h"   // decl only; impl lives in main_imgui.cpp

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

Pom1TmsPaintHost::Pom1TmsPaintHost(EmulationController* emu,
                                   GLFWwindow* const* windowSlot)
    : emu_(emu), windowSlot_(windowSlot), snap_(std::make_unique<TMS9918::Snapshot>())
{
}

bool Pom1TmsPaintHost::pickFilePath(bool forSave, const std::string& title,
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

bool Pom1TmsPaintHost::nativeFilePickerAvailable() const
{
    return pom1::NativeFileDialog::isAvailable();
}

// ── Built-in TMS9918 sprite library (dev/lib/tms9918/sprites_*.asm) ──────────
// The SCROLL-O-SPRITES catalogue ships as ca65 sources: each 16×16 sprite is 32
// `.byte` values under a `; slot NN/MM ... -- name` comment / `xxx_pat:` label
// (left half col 0..7 = 16 B, then right half col 8..15 = 16 B — the native
// $3800+patNum*8 stream). Parsed once and cached. Mirror of
// Pom1HgrPaintHost::devSprites (48 B / row-major there vs 32 B here).
namespace {

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

tmspaint::DevSpriteCategory parseSpriteFile(const std::filesystem::path& file)
{
    tmspaint::DevSpriteCategory cat;
    std::string stem = file.stem().string();               // sprites_creatures
    if (stem.rfind("sprites_", 0) == 0) stem = stem.substr(8);
    cat.name = stem;

    std::ifstream in(file);
    if (!in) return cat;
    tmspaint::DevSprite cur;
    bool haveCur = false;
    std::string pendingName;
    auto flush = [&]() {
        if (haveCur && cur.bytes.size() >= 32) {
            cur.bytes.resize(32);                          // 16×16 native pattern
            cat.sprites.push_back(std::move(cur));
        }
        cur = tmspaint::DevSprite{};
        haveCur = false;
    };
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
        if (t[0] == ';') {
            const size_t dash = t.find("--");
            if (t.find("slot") != std::string::npos && dash != std::string::npos) {
                pendingName = trim(t.substr(dash + 2));
                const size_t paren = pendingName.find('(');
                if (paren != std::string::npos) pendingName = trim(pendingName.substr(0, paren));
            }
            continue;
        }
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

std::vector<tmspaint::DevSpriteCategory> Pom1TmsPaintHost::devSprites()
{
    if (devSpritesLoaded_) return devSpritesCache_;
    devSpritesLoaded_ = true;

    namespace fs = std::filesystem;
    std::error_code ec;
    const char* candidates[] = {
        "dev/lib/tms9918",
        "../dev/lib/tms9918",
        "../../dev/lib/tms9918",
    };
    fs::path dir;
    for (const char* c : candidates)
        if (fs::is_directory(c, ec)) { dir = c; break; }
    if (dir.empty()) return devSpritesCache_;

    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const auto p = e.path();
        const std::string fn = p.filename().string();
        // Catalogue files are "sprites_<cat>.asm" (plural); skip the singular
        // "sprite_helpers.asm" / "sprite_triangle.asm" tooling.
        if (p.extension() == ".asm" && fn.rfind("sprites_", 0) == 0)
            files.push_back(p);
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        auto cat = parseSpriteFile(f);
        if (!cat.sprites.empty()) devSpritesCache_.push_back(std::move(cat));
    }
    return devSpritesCache_;
}

void Pom1TmsPaintHost::pokeVram(uint16_t addr, uint8_t value)
{
    // While batching, defer to one writeTms9918VramBatch() so a bulk edit costs
    // one lock + one framebuffer rebuild + one snapshot publish.
    if (batchDepth_ > 0) { batch_.emplace_back(static_cast<uint16_t>(addr & 0x3FFF), value); return; }
    if (emu_) emu_->writeTms9918Vram(static_cast<uint16_t>(addr & 0x3FFF), value);
}

void Pom1TmsPaintHost::beginBatch()
{
    // Reentrant depth counter (mirrors Pom1HgrPaintHost): a nested begin/endBatch
    // — e.g. Ctrl+Z's applyOps brackets firing while a bulk Line/Rect drag's batch
    // is still open — must NOT clear the queue or flush it early. Only the
    // outermost begin clears and the outermost end flushes.
    if (batchDepth_++ == 0) batch_.clear();
}

void Pom1TmsPaintHost::endBatch()
{
    if (batchDepth_ > 0 && --batchDepth_ == 0) {
        if (emu_) emu_->writeTms9918VramBatch(batch_);
        batch_.clear();
    }
}

void Pom1TmsPaintHost::applyRegisters(const uint8_t regs[8])
{
    if (emu_) emu_->applyTms9918Registers(regs);
}

void Pom1TmsPaintHost::renderVram(const uint8_t* vram16k, const uint8_t regs[8],
                                  uint32_t* outRgba)
{
    // Render the 16 KB VRAM through the real TMS9918 pipeline (the same renderer
    // the live screen uses) into the active 256×192 rect. siliconStrictMode off
    // so the full $3FFF VRAM is addressable regardless of R1 bit 7.
    std::copy(vram16k, vram16k + TMS9918::kVramSize, snap_->vram.begin());
    std::copy(regs, regs + 8, snap_->regs.begin());
    snap_->statusReg = 0;
    snap_->siliconStrictMode = false;
    TMS9918::renderToBuffer(outRgba, *snap_);
}

void Pom1TmsPaintHost::readVram(uint8_t* out16k)
{
    if (emu_) emu_->readTms9918Vram(out16k);
}

bool Pom1TmsPaintHost::liveFramebuffer(uint32_t* outRgba)
{
    if (!emu_) return false;
    emu_->readTms9918Framebuffer(outRgba);
    return true;
}

bool Pom1TmsPaintHost::loadVram(const std::string& path, std::string& err)
{
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = std::string("Cannot open: ") + path; return false; }
    std::vector<uint8_t> buf(TMS9918::kVramSize, 0);
    const size_t n = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (n == 0) { err = "Empty / unreadable VRAM file"; return false; }
    if (!emu_) { err = "No emulator"; return false; }
    std::vector<std::pair<uint16_t, uint8_t>> writes;
    writes.reserve(n);
    for (size_t i = 0; i < n && i < static_cast<size_t>(TMS9918::kVramSize); ++i)
        writes.emplace_back(static_cast<uint16_t>(i), buf[i]);
    emu_->writeTms9918VramBatch(writes);
    return true;
}

bool Pom1TmsPaintHost::saveVram(const std::string& path, std::string& err)
{
    if (!emu_) { err = "No emulator"; return false; }
    std::vector<uint8_t> buf(TMS9918::kVramSize, 0);
    emu_->readTms9918Vram(buf.data());
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { err = std::string("Cannot write: ") + path; return false; }
    const size_t n = std::fwrite(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (n != buf.size()) { err = "Short write"; return false; }
    return true;
}

bool Pom1TmsPaintHost::savePng(const std::string& path, const uint32_t* rgba,
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

// Phase 3 / Metal: routes through PomRenderer so the TMS9918 paint editor
// uses whichever backend the binary was built for. Same-size repeat
// uploads (the canvas at ~60 Hz while painting) sub-update the existing
// texture; only a dimension change destroys-and-recreates. Mirror of
// Pom1HgrPaintHost::uploadTexture.
void* Pom1TmsPaintHost::uploadTexture(void* tex, const void* rgba,
                                      int w, int h, bool linear)
{
    auto* r = pom1::renderer();
    if (!r) return tex;
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

void Pom1TmsPaintHost::destroyTexture(void* tex)
{
    if (!tex) return;
    if (auto* r = pom1::renderer())
        r->destroyTexture(static_cast<pom1::Texture*>(tex));
}

ImTextureID Pom1TmsPaintHost::textureToImTexture(void* tex) const
{
    if (!tex) return (ImTextureID)0;
    if (auto* r = pom1::renderer())
        return r->asImTextureID(static_cast<pom1::Texture*>(tex));
    return (ImTextureID)0;
}
