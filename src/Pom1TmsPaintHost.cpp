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
#include <cstdio>

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
